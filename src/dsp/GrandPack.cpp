#include "GrandPack.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <mutex>
#include <thread>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define HK_HAVE_MMAP 1
#else
#define HK_HAVE_MMAP 0
#endif

namespace hk {

namespace {
inline uint32_t rd32(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline uint32_t rd16(const unsigned char* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8); }

std::mutex& cacheMutex() {
    static std::mutex m;
    return m;
}
std::weak_ptr<GrandPack>& cache() {
    static std::weak_ptr<GrandPack> c;
    return c;
}
}  // namespace

uint64_t GrandPack::checksum(const int16_t* samples, size_t count) {
    uint64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        const uint64_t w = (uint64_t(i) * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) | 1ull;
        sum += uint64_t(uint16_t(samples[i])) * w;
    }
    return sum;
}

bool GrandPack::mapFile(int index, const std::string& path, std::string& error) {
    Map& m = maps_[index];
#if HK_HAVE_MMAP
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) { error = "missing file " + path; return false; }
    struct stat st;
    if (::fstat(fd, &st) != 0 || st.st_size < 44) { ::close(fd); error = "unreadable file " + path; return false; }
    void* p = ::mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (p == MAP_FAILED) { error = "cannot map " + path; return false; }
    m.base = p;
    m.length = size_t(st.st_size);
#else
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error = "missing file " + path; return false; }
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len < 44) { std::fclose(f); error = "unreadable file " + path; return false; }
    m.base = std::malloc(size_t(len));
    m.heap = true;
    m.length = size_t(len);
    const size_t got = m.base ? std::fread(m.base, 1, size_t(len), f) : 0;
    std::fclose(f);
    if (got != size_t(len)) { error = "unreadable file " + path; return false; }
#endif
    // find the format and the samples in the RIFF container
    const unsigned char* b = static_cast<const unsigned char*>(m.base);
    if (std::memcmp(b, "RIFF", 4) != 0 || std::memcmp(b + 8, "WAVE", 4) != 0) { error = "not a WAV file: " + path; return false; }
    size_t pos = 12, dataPos = 0, dataLen = 0;
    bool fmtOk = false;
    while (pos + 8 <= m.length) {
        const uint32_t size = rd32(b + pos + 4);
        const size_t body = pos + 8;
        if (std::memcmp(b + pos, "fmt ", 4) == 0 && body + 16 <= m.length) {
            const uint32_t tag = rd16(b + body), ch = rd16(b + body + 2), rate = rd32(b + body + 4), bits = rd16(b + body + 14);
            fmtOk = (tag == 1 || tag == 0xFFFE) && ch == 2 && rate == 48000 && bits == 16;
        } else if (std::memcmp(b + pos, "data", 4) == 0) {
            dataPos = body;
            dataLen = size;
            if (dataLen == 0 || body + dataLen > m.length) dataLen = m.length - body;
            break;
        }
        pos = body + size + (size & 1);
    }
    if (!fmtOk || dataPos == 0) { error = "unexpected audio format in " + path; return false; }
    if (dataLen / 4 != kGrandFile[index].frames) { error = "wrong length (other pack version?): " + path; return false; }
    m.pcm = reinterpret_cast<const int16_t*>(b + dataPos);
    return true;
}

std::shared_ptr<GrandPack> GrandPack::open(const std::string& dir, std::string& error, const std::atomic<bool>* cancel) {
    auto cancelled = [cancel] { return cancel && cancel->load(std::memory_order_relaxed); };
    // one open at a time, so that two instances starting together share one pack; the wait
    // can be given up
    std::unique_lock<std::mutex> lock(cacheMutex(), std::try_to_lock);
    while (!lock.owns_lock()) {
        if (cancelled()) { error = "cancelled"; return nullptr; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        (void) lock.try_lock();
    }
    if (auto alive = cache().lock())
        if (alive->dir_ == dir) return alive;
    std::shared_ptr<GrandPack> pack(new GrandPack());
    pack->dir_ = dir;
    size_t total = 0;
    for (int i = 0; i < kGrandFiles; ++i) {
        pack->want_[i].store(0);
        pack->headFrames_[i] = kGrandFile[i].frames < kHeadFrames ? kGrandFile[i].frames : kHeadFrames;
        pack->headAt_[i] = total;
        total += size_t(pack->headFrames_[i]) * 2;
    }
    pack->heads_.resize(total);
    for (int i = 0; i < kGrandFiles; ++i) {
        if (cancelled()) { error = "cancelled"; return nullptr; }
        const std::string path = dir + "/" + kGrandFile[i].name + ".wav";
        if (!pack->mapFile(i, path, error)) return nullptr;
        std::memcpy(pack->heads_.data() + pack->headAt_[i], pack->maps_[i].pcm, size_t(pack->headFrames_[i]) * 4);
    }
    pack->helper_ = std::thread([p = pack.get()] { p->helper(); });
    cache() = pack;
    return pack;
}

// Brings requested recordings into memory, a stretch at a time and in turn, so that every
// note that has just started gets its next second or so before any gets the rest.
void GrandPack::helper() {
    struct Job { int file; size_t at, end; };
    std::vector<Job> jobs;
    const size_t kStretch = 256 * 1024;                 // bytes per turn: 1.4 s of sound
    while (!stop_.load(std::memory_order_relaxed)) {
        for (int i = 0; i < kGrandFiles; ++i) {
            if (!want_[i].exchange(0, std::memory_order_relaxed)) continue;
            const size_t from = size_t(headFrames_[i]) * 4, end = size_t(kGrandFile[i].frames) * 4;
            bool known = false;
            for (auto& j : jobs)
                if (j.file == i) { j.at = from; known = true; }      // struck again: start over
            if (!known) jobs.push_back({i, from, end});
        }
        if (jobs.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
            continue;
        }
        for (size_t k = 0; k < jobs.size() && !stop_.load(std::memory_order_relaxed);) {
            Job& j = jobs[k];
            const size_t n = j.end - j.at < kStretch ? j.end - j.at : kStretch;
            const unsigned char* p = reinterpret_cast<const unsigned char*>(maps_[j.file].pcm) + j.at;
#if HK_HAVE_MMAP
            if (!maps_[j.file].heap) {
                // the advice must start on a page boundary
                const size_t page = size_t(::sysconf(_SC_PAGESIZE));
                const size_t lead = (reinterpret_cast<size_t>(p)) & (page - 1);
                ::madvise(const_cast<unsigned char*>(p) - lead, n + lead, MADV_WILLNEED);
            }
#endif
            volatile unsigned char sink = 0;
            for (size_t o = 0; o < n; o += 4096) sink = p[o];
            if (n) sink = p[n - 1];
            (void) sink;
            j.at += n;
            if (j.at >= j.end) jobs.erase(jobs.begin() + long(k));
            else ++k;
        }
    }
}

GrandPack::~GrandPack() {
    stop_.store(true);
    if (helper_.joinable()) helper_.join();
    for (auto& m : maps_) {
        if (!m.base) continue;
#if HK_HAVE_MMAP
        if (!m.heap) { ::munmap(m.base, m.length); continue; }
#endif
        std::free(m.base);
    }
}

int GrandPack::verify(std::atomic<float>* progress, const std::atomic<bool>* cancel) const {
    const int known = verified_.load(std::memory_order_acquire);
    if (known >= 0) {
        if (progress) progress->store(1.0f, std::memory_order_relaxed);
        return known;
    }
    bool ok = true;
    double total = 0, done = 0;
    for (int i = 0; i < kGrandFiles; ++i) total += kGrandFile[i].frames;
    const size_t kChunk = 1u << 18;                  // samples between two looks at the cancel flag
    for (int i = 0; i < kGrandFiles; ++i) {
        const size_t count = size_t(kGrandFile[i].frames) * 2;
        uint64_t sum = 0;
        for (size_t at = 0; at < count; at += kChunk) {
            if (cancel && cancel->load(std::memory_order_relaxed)) return -1;
            const size_t n = count - at < kChunk ? count - at : kChunk;
            const int16_t* d = maps_[i].pcm + at;
            for (size_t j = 0; j < n; ++j) {
                const uint64_t w = (uint64_t(at + j) * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) | 1ull;
                sum += uint64_t(uint16_t(d[j])) * w;
            }
        }
        if (sum != kGrandFile[i].sum) ok = false;
        done += kGrandFile[i].frames;
        if (progress) progress->store(float(done / total), std::memory_order_relaxed);
    }
    verified_.store(ok ? 1 : 0, std::memory_order_release);
    return ok ? 1 : 0;
}

}  // namespace hk

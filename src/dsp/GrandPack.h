// The grand piano's recordings on disk: 641 16-bit stereo WAV files that are
// mapped into memory (not copied), so several instances of the plugin share
// them and the operating system keeps in RAM only what is being played.
//
// The audio thread must not wait for the disk.  Two things see to that: the
// first second of every recording is kept in ordinary memory (about 100 MB),
// and when a note starts a helper thread asks the system for the rest of its
// recording at once and then walks through it, a stretch at a time and in
// turn with the other notes, ahead of the playing position.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "GrandTable.h"

namespace hk {

class GrandPack {
public:
    // Maps every recording found in dir.  Returns null and explains why in error when the
    // pack is absent or is not the one this version of the instrument was built for.
    // Opening the same directory again returns the pack that is already open.  Setting
    // cancel (optional) from another thread makes a slow open give up and return null.
    static std::shared_ptr<GrandPack> open(const std::string& dir, std::string& error,
                                           const std::atomic<bool>* cancel = nullptr);
    ~GrandPack();

    const int16_t* pcm(int file) const { return maps_[file].pcm; }      // interleaved L, R
    uint32_t frames(int file) const { return kGrandFile[file].frames; }
    // The start of the recording, held in memory: the same samples as pcm(), headFrames() of them.
    const int16_t* head(int file) const { return heads_.data() + headAt_[file]; }
    uint32_t headFrames(int file) const { return headFrames_[file]; }
    // Asks for the rest of a recording to be brought into memory.  Safe on the audio thread.
    void prefetch(int file) const {
        if (headFrames_[file] < kGrandFile[file].frames) want_[file].store(1, std::memory_order_relaxed);
    }
    static constexpr uint32_t kHeadFrames = 48000;      // one second
    const std::string& directory() const { return dir_; }

    // Reads every sample once -- which also brings the whole pack into memory -- and
    // compares the checksums with the table.  Meant for a background thread; the pack can
    // be played meanwhile.  progress (optional) runs from 0 to 1; setting cancel stops early.
    // Returns 1 = intact, 0 = damaged, -1 = cancelled.  The result is remembered.
    int verify(std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr) const;

    static uint64_t checksum(const int16_t* samples, size_t count);

private:
    struct Map {
        void* base = nullptr;
        size_t length = 0;
        const int16_t* pcm = nullptr;
        bool heap = false;
    };
    GrandPack() = default;
    GrandPack(const GrandPack&) = delete;
    GrandPack& operator=(const GrandPack&) = delete;
    bool mapFile(int index, const std::string& path, std::string& error);

    void helper();

    Map maps_[kGrandFiles];
    std::string dir_;
    mutable std::atomic<int> verified_{-1};
    std::vector<int16_t> heads_;
    size_t headAt_[kGrandFiles] = {};
    uint32_t headFrames_[kGrandFiles] = {};
    mutable std::atomic<uint8_t> want_[kGrandFiles];
    std::atomic<bool> stop_{false};
    std::thread helper_;
};

}  // namespace hk

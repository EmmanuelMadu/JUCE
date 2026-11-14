// The grand piano's recordings on disk: 641 16-bit stereo WAV files that are
// mapped into memory (not copied), so several instances of the plugin share
// them and the operating system keeps in RAM only what is being played.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

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

    Map maps_[kGrandFiles];
    std::string dir_;
    mutable std::atomic<int> verified_{-1};
};

}  // namespace hk

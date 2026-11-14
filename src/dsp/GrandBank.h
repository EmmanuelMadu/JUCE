// The acoustic grand: recordings of a real piano, played so that they behave
// like the instrument rather than like a set of samples.
//
//   strike   -> loudness follows the key velocity without steps; the recording
//               whose natural loudness is nearest is used, levelled exactly,
//               and a gentle shelf bridges the timbre between two recordings
//   hammer   -> the timbre can be taken from a harder or softer strike than
//               the loudness asks for (felt hardness independent of volume)
//   dampers  -> one per key up to E6; they follow key, sustain pedal (any
//               position: half-pedalling), and sostenuto; a damper that lands
//               stops the highs first; lifting it again catches what is left
//   release  -> the recorded after-ring of the damped strings and the noise
//               of the key coming up; pedal noises scale with pedal speed
//
// Recordings: Salamander Grand Piano V3 (see GrandTable.h).
#pragma once

#include <atomic>
#include <cstdint>

#include "GrandPack.h"
#include "GrandTable.h"

namespace hk {

struct GrandControls {
    double tuneRatio = 1.0;     // A4 / 440 Hz
    double levelExp = 2.0;      // loudness ~ velocity ^ levelExp
    double hammerDb = 0.0;      // strike strength of the timbre relative to the loudness, in dB
    double damper = 1.0;        // multiplies the dampers' stopping time
    double noise = 1.0;         // key-release and pedal noises (linear gain)
    double tails = 1.0;         // recorded after-ring of damped strings (linear gain)
    bool sound = false;         // the grand is switched on
    bool raw = false;           // tests: play the recordings exactly as they are
};

class GrandBank {
public:
    static constexpr int kTick = 16;
    static constexpr int kMaxVoices = 192;
    static constexpr int kNoteLimit = 72;
    static constexpr int kNumKeys = 88;
    static constexpr int kLowest = 21;

    GrandBank();
    void prepare(double sampleRate);
    void reset();
    void setPack(const GrandPack* pack) { pack_.store(pack, std::memory_order_release); }
    bool hasPack() const { return pack_.load(std::memory_order_acquire) != nullptr; }
    void setControls(const GrandControls& c) { ctl_ = c; }

    void noteOn(int note, double velocity);     // always moves the key (and its damper)
    void noteOff(int note);
    void setPedal(double amount);               // sustain pedal, 0..1
    void setSostenuto(double amount);
    void setSoft(double amount);
    void allNotesOff();

    void tick();                                // once every kTick samples
    // Adds the voices to left/right (n <= kTick); exc receives the mono sum of the strings.
    void render(float* left, float* right, float* exc, int n);
    void skip(int n) { now_ += uint64_t(n); }   // time passes while nothing of the grand is rendered

    bool idle() const { return numActive_ == 0; }
    int activeVoices() const { return numActive_; }
    const float* dampers() const { return damp_; }          // per key: 0 lifted .. 1 on the strings
    const float* own(int key) const { return ownUsed_[key] ? own_[key] : nullptr; }
    float keyCents(int key) const;                          // tuning of a key against equal temperament

private:
    enum Kind : uint8_t { kNote, kTail, kNoise };
    struct Voice {
        bool active = false, fading = false, dying = false, tailDone = false, lpOn = false, shelfOn = false;
        Kind kind = kNote;
        int key = -1;
        const int16_t* data = nullptr;      // the whole recording (mapped file)
        const int16_t* head = nullptr;      // its start, held in memory
        int64_t frames = 0, headFrames = 0;
        double pos = 0, rate = 1;
        float base = 0, env = 1, fade = 1, fadeMul = 1;
        float g = 0, dg = 0;
        float sb0 = 1, sb1 = 0, sa1 = 0, szl = 0, szr = 0;
        float lpc = 1, lpl = 0, lpr = 0, lastL = 0, lastR = 0;
        float targetDb = 0, fminHz = 100;
        const GrandLayer* layer = nullptr;
        const GrandNote* note = nullptr;
        uint64_t start = 0;
    };

    Voice* alloc(bool steal);
    void strike(int key, double velocity, const GrandPack* pack);
    void startTail(const Voice& src);
    void startKeyNoise(int key);
    void startPedalNoise(bool down, double speedDb);
    static void attach(Voice& v, const GrandPack* pack, int file);
    void renderVoice(Voice& v, float* left, float* right, float* exc, float* own, int n);
    void updateDampers();
    void compact();
    float fadeFactor(double seconds) const;
    double levelNow(const Voice& v) const;

    std::atomic<const GrandPack*> pack_{nullptr};
    GrandControls ctl_;
    double fs_ = 48000.0;
    Voice voices_[kMaxVoices];
    int active_[kMaxVoices];
    int numActive_ = 0;

    bool down_[kNumKeys], sost_[kNumKeys], struck_[kNumKeys];
    float damp_[kNumKeys], lastVel_[kNumKeys];
    uint64_t strikeTime_[kNumKeys];
    uint64_t now_ = 0, lastPedalTime_ = 0;
    double pedal_ = 0, soft_ = 0, pedalDamp_ = 1;
    bool pedalDown_ = false, sostDown_ = false;
    uint32_t rng_ = 1;

    float own_[kNumKeys][kTick];
    bool ownUsed_[kNumKeys];
    int ownList_[kNumKeys];
    int numOwn_ = 0;
};

}  // namespace hk

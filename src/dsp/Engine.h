// The complete instrument.
//
//   tines:  tine bank -> tone controls -> phaser -> amp -> chorus -> tremolo -+
//                 |                                                           +-> reverb -> output
//                 v (the tines also shake the piano's soundboard)             |
//   grand:  recordings + ringing strings -> tone -> compressor -> width ------+
//
// No dependencies beyond the C++ standard library, so it can be tested
// without a plugin host.
#pragma once

#include <atomic>
#include <vector>

#include "Fx.h"
#include "GrandBank.h"
#include "Params.h"
#include "StringBank.h"
#include "TineBank.h"

namespace hk {

class Engine {
public:
    Engine();
    void prepare(double sampleRate);
    void reset();

    // Parameters are set in plain units (see Params.h); safe to call from any thread.
    void setParam(int id, float value);
    float getParam(int id) const { return params_[id].load(std::memory_order_relaxed); }

    // The grand's recordings.  The pack must stay alive for as long as the engine is used;
    // may be called from any thread.  Until a pack is set the grand is silent.
    void setGrandPack(const GrandPack* pack) { grand_.setPack(pack); }
    bool hasGrandPack() const { return grand_.hasPack(); }
    void setGrandRaw(bool raw) { raw_ = raw; }      // tests: recordings exactly as they are

    // Events take effect at the current position; split blocks at event times for sample accuracy.
    void noteOn(int note, float velocity);      // velocity 0..1
    void noteOff(int note);
    void sustain(float amount);                 // 0..1 (in between: half pedal)
    void sostenuto(float amount);
    void softPedal(float amount);
    void allNotesOff();
    void allSoundOff();

    // Renders n frames.  dry (optional) receives the summed pickup signal before any processing.
    void process(float* left, float* right, int n, double* dry = nullptr);

    int latency() const { return Drive::kLatency; }
    bool idle() const { return bank_.idle() && grand_.idle() && strings_.idle() && tailSamples_ <= 0; }
    int activeKeys() const { return bank_.activeKeys(); }
    int grandVoices() const { return grand_.activeVoices(); }
    int ringingStrings() const { return strings_.ringing(); }

private:
    void controlTick();
    void readParams();
    void grandBus(int m);

    TineBank bank_;
    GrandBank grand_;
    StringBank strings_;
    std::atomic<float> params_[kNumParams];
    double fs_ = 48000.0;
    int tickPos_ = 0;
    long long tailSamples_ = 0;
    bool flushed_ = true;
    bool raw_ = false;

    // smoothed control values
    double sm_[kNumParams];
    double smCoef_ = 0.0;
    bool first_ = true;
    int ampType_ = -1;
    double lastBass_ = 1e9, lastTreble_ = 1e9, lastDrive_ = -1, lastSize_ = -1, lastTone_ = 1e9;

    Biquad bass_, treble_, dcBlock_;
    Biquad cab_[4];
    int cabStages_ = 0;
    double ampTrim_ = 1.0;
    Phaser phaser_;
    Drive drive_;
    Tremolo trem_;
    Chorus chorus_;
    Reverb reverb_;
    double volume_ = 1.0;
    double tineGain_ = 1.0, grandGain_ = 1.0;
    double buf_[TineBank::kTick];

    // grand bus
    float gl_[TineBank::kTick], gr_[TineBank::kTick], exc_[TineBank::kTick], excT_[TineBank::kTick];
    float stretch_[kNumKeys];
    Biquad toneLo_[2], toneHi_[2];
    double compEnv_ = 0.0, compGain_ = 1.0, compStep_ = 0.0, compAtk_ = 0.0, compRel_ = 0.0;
    float delayL_[Drive::kLatency], delayR_[Drive::kLatency];
    int delayPos_ = 0;
    int grandHold_ = 0;            // samples the grand bus still has to run after its last sound
    bool grandBusOn_ = false;
    static constexpr int kTineRest = 12000;     // samples of silence after which the tine chain rests
    int tineRest_ = 0;
    bool tineChainOff_ = false;
};

}  // namespace hk

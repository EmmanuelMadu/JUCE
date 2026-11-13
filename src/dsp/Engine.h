// The complete instrument: tine bank -> tone controls -> phaser -> amp ->
// tremolo -> chorus -> reverb -> output.  No dependencies beyond the C++
// standard library, so it can be tested without a plugin host.
#pragma once

#include <atomic>
#include <vector>

#include "Fx.h"
#include "Params.h"
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

    // Events take effect at the current position; split blocks at event times for sample accuracy.
    void noteOn(int note, float velocity);      // velocity 0..1
    void noteOff(int note);
    void sustain(float amount);                 // 0..1
    void allNotesOff();
    void allSoundOff();

    // Renders n frames.  dry (optional) receives the summed pickup signal before any processing.
    void process(float* left, float* right, int n, double* dry = nullptr);

    int latency() const { return Drive::kLatency; }
    bool idle() const { return bank_.idle() && tailSamples_ <= 0; }
    int activeKeys() const { return bank_.activeKeys(); }

private:
    void controlTick();
    void readParams();

    TineBank bank_;
    std::atomic<float> params_[kNumParams];
    double fs_ = 48000.0;
    int tickPos_ = 0;
    long long tailSamples_ = 0;
    bool flushed_ = true;

    // smoothed control values
    double sm_[kNumParams];
    double smCoef_ = 0.0;
    bool first_ = true;
    int ampType_ = -1;
    double lastBass_ = 1e9, lastTreble_ = 1e9, lastDrive_ = -1, lastSize_ = -1;

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
    double buf_[TineBank::kTick];
};

}  // namespace hk

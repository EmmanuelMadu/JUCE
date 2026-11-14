// Sympathetic resonance: the strings of the grand that are free to move
// (dampers lifted by a held key, by the sustain or the sostenuto pedal, and
// the top strings, which have no dampers) answer whatever reaches the
// soundboard at their own partial frequencies.
//
// Every string is a set of resonators at the partial frequencies measured
// from the recordings (tuning, stretch and string stiffness of each note),
// ringing with the decay times measured from the recordings.  They are driven
// by the grand's own strings -- and by the tines, which is what puts the two
// instruments on one soundboard.
#pragma once

#include "GrandBank.h"
#include "GrandTable.h"

namespace hk {

class StringBank {
public:
    static constexpr int kTick = 16;
    static constexpr int kNumKeys = 88;
    static constexpr int kMaxPartials = 20;

    void prepare(double sampleRate);
    void reset();
    void setTune(double ratio);                             // A4 / 440 Hz
    void setLevels(double fromGrand, double fromTines) { gGrand_ = float(fromGrand); gTine_ = float(fromTines); }
    void tick(const float* dampers, double damperScale);    // once every kTick samples
    // Adds the ringing strings to left/right (n <= kTick).
    void render(const float* excGrand, const float* excTine, const GrandBank& grand, float* left, float* right, int n);
    bool idle() const { return quiet_; }
    int ringing() const { return numRinging_; }

private:
    struct Str {
        int first = 0, count = 0;
        float panL = 1, panR = 1;
        float inGrand = 0, inTine = 0;
        bool ringing = false;
    };
    void configure();

    Str str_[kNumKeys];
    float c_[kNumKeys * kMaxPartials], s_[kNumKeys * kMaxPartials], g_[kNumKeys * kMaxPartials];
    float yr_[kNumKeys * kMaxPartials], yi_[kNumKeys * kMaxPartials];
    double fs_ = 48000.0, tune_ = 1.0;
    float gGrand_ = 0, gTine_ = 0;
    bool quiet_ = true;
    int numRinging_ = 0;
};

}  // namespace hk

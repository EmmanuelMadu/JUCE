// Physical model of an electric piano's sound generators: 88 tines, each with
// its hammer, damper and magnetic pickup.  All per-key constants come from
// measurements of a real instrument (KeyTable).
//
//   hammer  -> force pulse whose duration shortens with speed
//   tine    -> fundamental + second bending mode + mounting "thump" mode
//              (decay grows with swing: aerodynamic drag; slight pitch drop)
//   damper  -> extra decay while the key is up and the pedal is not down
//   pickup  -> output = d/dt Flux(u),  Flux(u) = (1+u^2)^-2 + beta/(1+(u/W)^2)
//              (a sharp pole tip on top of the magnet's broad field),
//              u = tine tip position relative to the pickup axis, in gap units
//
// The pickup is the instrument's only nonlinearity and the source of its
// "bark": a tine that swings across the pickup axis produces a pulse twice
// per cycle.  Every key has its own tine, so re-striking a ringing note adds
// to the motion that is already there, exactly as on the instrument.
#pragma once

#include "KeyTable.h"

namespace hk {

struct TineControls {
    double bark = 1.0;        // pickup closeness: scales offset and swing as seen by the pickup
    double timbre = 1.0;      // multiplies the rest offset u0
    double hammer = 1.0;      // hardness: divides the hammer pulse duration
    double sustain = 1.0;     // multiplies decay times
    double bell = 1.0;        // second bending mode level
    double thump = 1.0;       // mounting thump level
    double damper = 1.0;      // multiplies damper stopping time
    double gamma = 1.3;       // hammer speed = velocity^gamma
    double tuneHz = 440.0;
    double detune = 1.0;      // scales the per-key tuning deviations
};

class TineBank {
public:
    static constexpr int kTick = 16;          // control interval in samples

    void prepare(double sampleRate);
    void reset();
    void setControls(const TineControls& c) { ctl_ = c; }

    void noteOn(int note, double velocity);   // velocity 0..1
    void noteOff(int note);
    void setPedal(double amount);             // 0 = up, 1 = down (values in between: half pedal)
    void allNotesOff();

    // Call tick() every kTick samples (before render), then render() for the samples in between.
    void tick();
    void render(double* out, int n);          // adds nothing: overwrites out[0..n)
    bool idle() const { return numActive_ == 0; }
    int activeKeys() const { return numActive_; }

private:
    struct Key {
        // modal state: displacement y and scaled velocity p = v / omega, in gap units
        double y1 = 0, p1 = 0, y2 = 0, p2 = 0, yT = 0, pT = 0;
        // per-tick coefficients (rotation with decay)
        double c1 = 1, s1 = 0, c2 = 1, s2 = 0, cT = 1, sT = 0;
        // constants for the current note
        double w1 = 0, w2 = 0, wT = 0;        // angular frequencies (rad/sample), undisplaced
        double r2 = 0, rT = 0;                // velocity weights relative to the fundamental
        double u0 = 0, lam = 0, gain = 0, swing = 1;
        // hammer force pulse
        double hAmp = 0, hTau = 1, hK2 = 0, hKT = 0;
        int hPos = 0, hLen = 0;
        // damper
        double damp = 1.0;                    // 0 = lifted, 1 = resting on the tine
        bool down = false;
        bool active = false;
        bool hasMode2 = false;
    };

    void updateKey(Key& k, const KeyParams& kp);
    void configureKey(Key& k, const KeyParams& kp, int index);

    Key keys_[kNumKeys];
    int activeList_[kNumKeys];
    int numActive_ = 0;
    TineControls ctl_;
    double fs_ = 48000.0, T_ = 1.0 / 48000.0;
    double pedal_ = 0.0;
    double engageCoef_ = 0.0, liftCoef_ = 0.0, restCoef_ = 0.0;
};

}  // namespace hk

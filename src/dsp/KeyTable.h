// Per-key physical parameters of the tine model (see tools/calib).
#pragma once

namespace hk {

constexpr int kNumKeys = 88;        // MIDI notes 21..108
constexpr int kLowestNote = 21;

struct KeyParams {
    float cents;        // tuning deviation from equal temperament (cents)
    float a0max;        // tine swing right after a full-velocity strike, in pickup-gap units
    float u0;           // rest offset of the tine tip from the pickup axis (gap units)
    float lam;          // extra linear flux term for this key (normally zero)
    float gain;         // output gain that evens out loudness across the keyboard
    float alpha0;       // decay rate of the fundamental at small swing (1/s)
    float alphaDrag;    // extra decay rate per unit of swing (1/s per gap unit), aerodynamic drag
    float tau;          // time constant of the hammer force pulse at full velocity (s)
    float r2;           // second bending mode: frequency ratio to the fundamental
    float k2;           //   coupling (tip displacement relative to the fundamental for an ideal impulse)
    float alpha2;       //   decay rate (1/s)
    float fThump;       // low-frequency mode of the tine/tonebar assembly on its mounting (Hz)
    float kThump;       //   coupling
    float alphaThump;   //   decay rate (1/s)
    float alphaDamp;    // extra decay rate with the damper resting on the tine (1/s)
    float pitchDrop;    // pitch flattening per unit of swing (cents per gap unit)
};

extern const KeyParams kKeyTable[kNumKeys];

// Broad part of the pickup's flux map (the field of the whole magnet), common to all keys:
// flux(u) = (1 + u^2)^-2 + kFluxBeta / (1 + (u / kFluxWidth)^2)
extern const double kFluxBeta;
extern const double kFluxWidth;

}  // namespace hk

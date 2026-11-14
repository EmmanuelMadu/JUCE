// Parameter list shared by the sound engine, the plugin and the tests.
#pragma once

namespace hk {

enum ParamId {
    kBark = 0,      // pickup closeness
    kTimbre,        // tine offset from the pickup axis (bell <-> mellow)
    kHammer,        // hammer tip hardness
    kSustain,       // decay time of the tines
    kBell,          // level of the tine's second bending mode ("ting")
    kThump,         // level of the mounting thump at the attack
    kDamper,        // how quickly the dampers stop a note
    kDynamics,      // velocity response
    kTune,          // A4 in Hz
    kDetune,        // key-to-key tuning spread
    kAmp,           // 0 direct, 1 suitcase, 2 combo
    kBass,
    kTreble,
    kDrive,
    kTremRate,
    kTremDepth,
    kTremStereo,
    kChorus,
    kChorusRate,
    kPhaser,
    kPhaserRate,
    kReverb,
    kReverbSize,
    kVolume,
    // --- the grand (added after the tine instrument; earlier ids keep their meaning)
    kTinesOn,       // the tine instrument answers the keys
    kGrandOn,       // the grand answers the keys
    kBalance,       // tines <-> grand
    kGrandHammer,   // felt hardness, independent of loudness
    kGrandTone,     // tilt: dark <-> bright
    kGrandRes,      // sympathetic string resonance and after-ring
    kTineRing,      // how much the tines set the grand's strings ringing
    kGrandNoise,    // key and pedal noises
    kGrandWidth,    // stereo width of the grand
    kGrandComp,     // compression
    kNumParams
};

struct ParamInfo {
    const char* id;         // stable identifier (saved in sessions)
    const char* name;       // shown to the user
    float min, max, def;
    const char* unit;
    int steps;              // 0 = continuous, otherwise number of discrete values
    const char* choices;    // for discrete parameters: '|'-separated labels
};

inline const ParamInfo& paramInfo(int id) {
    static const ParamInfo table[kNumParams] = {
        {"bark", "Bark", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"timbre", "Timbre", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"hammer", "Hammer", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"sustain", "Sustain", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"bell", "Bell", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"thump", "Thump", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"damper", "Damper", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"dynamics", "Dynamics", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"tune", "Tune", 415.0f, 466.0f, 440.0f, "Hz", 0, nullptr},
        {"detune", "Detune", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"amp", "Amp", 0.0f, 2.0f, 0.0f, "", 3, "Direct|Suitcase|Combo"},
        {"bass", "Bass", -12.0f, 12.0f, 0.0f, "dB", 0, nullptr},
        {"treble", "Treble", -12.0f, 12.0f, 0.0f, "dB", 0, nullptr},
        {"drive", "Drive", 0.0f, 1.0f, 0.0f, "", 0, nullptr},
        {"tremrate", "Tremolo Rate", 0.5f, 12.0f, 5.0f, "Hz", 0, nullptr},
        {"tremdepth", "Tremolo", 0.0f, 1.0f, 0.0f, "", 0, nullptr},
        {"tremstereo", "Tremolo Stereo", 0.0f, 1.0f, 1.0f, "", 0, nullptr},
        {"chorus", "Chorus", 0.0f, 1.0f, 0.0f, "", 0, nullptr},
        {"chorusrate", "Chorus Rate", 0.1f, 3.0f, 0.6f, "Hz", 0, nullptr},
        {"phaser", "Phaser", 0.0f, 1.0f, 0.0f, "", 0, nullptr},
        {"phaserrate", "Phaser Rate", 0.05f, 6.0f, 0.4f, "Hz", 0, nullptr},
        {"reverb", "Reverb", 0.0f, 1.0f, 0.15f, "", 0, nullptr},
        {"reverbsize", "Reverb Size", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"volume", "Volume", -30.0f, 6.0f, 0.0f, "dB", 0, nullptr},
        {"tineson", "Tines", 0.0f, 1.0f, 1.0f, "", 2, "Off|On"},
        {"grandon", "Grand", 0.0f, 1.0f, 0.0f, "", 2, "Off|On"},
        {"balance", "Balance", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"ghammer", "Grand Hammer", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"gtone", "Grand Tone", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"gres", "Resonance", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"tinering", "Tine Ring", 0.0f, 1.0f, 0.0f, "", 0, nullptr},
        {"gnoise", "Noises", 0.0f, 1.0f, 0.5f, "", 0, nullptr},
        {"gwidth", "Width", 0.0f, 1.5f, 1.0f, "", 0, nullptr},
        {"gcomp", "Comp", 0.0f, 1.0f, 0.0f, "", 0, nullptr},
    };
    return table[id];
}

struct Preset {
    const char* name;
    float v[kNumParams];
};

int numPresets();
int numTinePresets();     // the first programs: the tine instrument on its own
const Preset& preset(int index);

}  // namespace hk

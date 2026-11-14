// Plain C interface to the engine, used by the Python tests and calibration tools.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "Engine.h"

namespace {
// packs opened through this interface stay open for the life of the process
std::shared_ptr<hk::GrandPack>& heldPack() {
    static std::shared_ptr<hk::GrandPack> p;
    return p;
}
}  // namespace

extern "C" {

void* hk_create(double sampleRate) {
    auto* e = new hk::Engine();
    e->prepare(sampleRate);
    return e;
}
void hk_destroy(void* h) { delete static_cast<hk::Engine*>(h); }
void hk_reset(void* h) { static_cast<hk::Engine*>(h)->reset(); }
void hk_set_param(void* h, int id, float v) { static_cast<hk::Engine*>(h)->setParam(id, v); }
float hk_get_param(void* h, int id) { return static_cast<hk::Engine*>(h)->getParam(id); }
void hk_note_on(void* h, int note, float vel) { static_cast<hk::Engine*>(h)->noteOn(note, vel); }
void hk_note_off(void* h, int note) { static_cast<hk::Engine*>(h)->noteOff(note); }
void hk_sustain(void* h, float v) { static_cast<hk::Engine*>(h)->sustain(v); }
void hk_all_notes_off(void* h) { static_cast<hk::Engine*>(h)->allNotesOff(); }
void hk_all_sound_off(void* h) { static_cast<hk::Engine*>(h)->allSoundOff(); }
void hk_process(void* h, float* l, float* r, int n, double* dry) { static_cast<hk::Engine*>(h)->process(l, r, n, dry); }
int hk_latency(void* h) { return static_cast<hk::Engine*>(h)->latency(); }
int hk_idle(void* h) { return static_cast<hk::Engine*>(h)->idle() ? 1 : 0; }
int hk_active_keys(void* h) { return static_cast<hk::Engine*>(h)->activeKeys(); }
int hk_num_params() { return hk::kNumParams; }
const char* hk_param_id(int id) { return hk::paramInfo(id).id; }
float hk_param_min(int id) { return hk::paramInfo(id).min; }
float hk_param_max(int id) { return hk::paramInfo(id).max; }
float hk_param_default(int id) { return hk::paramInfo(id).def; }
int hk_num_presets() { return hk::numPresets(); }
const char* hk_preset_name(int i) { return hk::preset(i).name; }
float hk_preset_value(int i, int id) { return hk::preset(i).v[id]; }

// grand piano
int hk_grand_load(void* h, const char* dir) {              // 1 = loaded, 0 = pack not usable
    std::string err;
    auto pack = hk::GrandPack::open(dir, err);
    if (!pack) { std::fprintf(stderr, "grand pack: %s\n", err.c_str()); return 0; }
    heldPack() = pack;
    static_cast<hk::Engine*>(h)->setGrandPack(pack.get());
    return 1;
}
int hk_grand_verify() { return heldPack() && heldPack()->verify() == 1 ? 1 : 0; }
void hk_grand_raw(void* h, int raw) { static_cast<hk::Engine*>(h)->setGrandRaw(raw != 0); }
void hk_sostenuto(void* h, float v) { static_cast<hk::Engine*>(h)->sostenuto(v); }
void hk_soft(void* h, float v) { static_cast<hk::Engine*>(h)->softPedal(v); }
int hk_grand_voices(void* h) { return static_cast<hk::Engine*>(h)->grandVoices(); }
int hk_ringing_strings(void* h) { return static_cast<hk::Engine*>(h)->ringingStrings(); }
int hk_num_tine_presets() { return hk::numTinePresets(); }
float hk_grand_key_cents(int note) { return hk::kGrandNote[(std::min(std::max(note - 21, 0), 87) + 1) / 3].cents; }
float hk_grand_partial(int note, int n) {
    const hk::GrandNote& g = hk::kGrandNote[(std::min(std::max(note - 21, 0), 87) + 1) / 3];
    return g.partial[std::min(std::max(n, 1), hk::kGrandPartials) - 1] * float(std::pow(2.0, (note - g.root) / 12.0));
}

}  // extern "C"

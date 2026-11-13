// Plain C interface to the engine, used by the Python tests and calibration tools.
#include "Engine.h"

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

}  // extern "C"

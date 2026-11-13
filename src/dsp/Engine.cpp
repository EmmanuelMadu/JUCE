#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace hk {

namespace {
// Level of the pickup sum that maps to full scale: a firm chord sits around -12 dBFS.
constexpr double kOutputTrim = 0.5;

inline double softClip(double x) {
    const double a = std::fabs(x);
    if (a <= 0.8) return x;
    const double y = 0.8 + 0.2 * std::tanh((a - 0.8) / 0.2);
    return x < 0 ? -y : y;
}
}  // namespace

Engine::Engine() {
    for (int i = 0; i < kNumParams; ++i) {
        params_[i].store(paramInfo(i).def);
        sm_[i] = paramInfo(i).def;
    }
}

void Engine::prepare(double sampleRate) {
    fs_ = sampleRate;
    bank_.prepare(sampleRate);
    phaser_.prepare(sampleRate);
    trem_.prepare(sampleRate);
    chorus_.prepare(sampleRate);
    reverb_.prepare(sampleRate);
    smCoef_ = 1.0 - std::exp(-TineBank::kTick / (0.02 * sampleRate));
    dcBlock_.highpass(sampleRate, 12.0, 0.707);
    reset();
}

void Engine::reset() {
    bank_.reset();
    phaser_.reset();
    drive_.reset();
    trem_.reset();
    chorus_.reset();
    reverb_.reset();
    bass_.reset();
    treble_.reset();
    dcBlock_.reset();
    for (auto& b : cab_) b.reset();
    tickPos_ = 0;
    tailSamples_ = 0;
    flushed_ = true;
    first_ = true;
    ampType_ = -1;
    lastBass_ = lastTreble_ = 1e9;
    lastDrive_ = lastSize_ = -1;
}

void Engine::setParam(int id, float value) {
    if (id < 0 || id >= kNumParams) return;
    const ParamInfo& p = paramInfo(id);
    params_[id].store(std::min(p.max, std::max(p.min, value)), std::memory_order_relaxed);
}

void Engine::noteOn(int note, float velocity) {
    if (velocity <= 0.0f) { noteOff(note); return; }
    readParams();
    bank_.noteOn(note, velocity);
    tailSamples_ = static_cast<long long>(14.0 * fs_);      // longest reverb tail, with margin
    flushed_ = false;
}

void Engine::noteOff(int note) { bank_.noteOff(note); }
void Engine::sustain(float amount) { bank_.setPedal(amount); }
void Engine::allNotesOff() { bank_.allNotesOff(); }

void Engine::allSoundOff() {
    const int tp = tickPos_;
    reset();
    tickPos_ = tp;
}

void Engine::readParams() {
    // Model controls act when a hammer is thrown (and once per control tick for decay and damper),
    // so they are read directly, without smoothing.
    auto v = [this](int id) { return static_cast<double>(params_[id].load(std::memory_order_relaxed)); };
    TineControls c;
    c.bark = std::pow(2.0, (v(kBark) - 0.5) * 2.0);
    c.timbre = 0.15 + 1.7 * v(kTimbre);
    c.hammer = std::pow(2.0, (v(kHammer) - 0.5) * 2.5);
    c.sustain = std::pow(2.0, (v(kSustain) - 0.5) * 2.0);
    c.bell = 4.0 * v(kBell) * v(kBell);
    c.thump = 4.0 * v(kThump) * v(kThump);
    c.damper = std::pow(2.0, (v(kDamper) - 0.5) * 3.0);
    c.gamma = 0.7 + 1.2 * v(kDynamics);
    c.tuneHz = v(kTune);
    c.detune = 2.0 * v(kDetune);
    bank_.setControls(c);
}

void Engine::controlTick() {
    for (int i = 0; i < kNumParams; ++i) {
        const double target = params_[i].load(std::memory_order_relaxed);
        if (first_ || paramInfo(i).steps > 0 || i == kTune) sm_[i] = target;
        else sm_[i] += (target - sm_[i]) * smCoef_;
    }
    first_ = false;
    readParams();
    bank_.tick();

    if (std::fabs(sm_[kBass] - lastBass_) > 1e-3) {
        lastBass_ = sm_[kBass];
        bass_.lowShelf(fs_, 140.0, lastBass_);
    }
    if (std::fabs(sm_[kTreble] - lastTreble_) > 1e-3) {
        lastTreble_ = sm_[kTreble];
        treble_.highShelf(fs_, 2200.0, lastTreble_);
    }
    if (std::fabs(sm_[kDrive] - lastDrive_) > 1e-4) {
        lastDrive_ = sm_[kDrive];
        drive_.setDrive(lastDrive_);
    }
    if (std::fabs(sm_[kReverbSize] - lastSize_) > 1e-3) {
        lastSize_ = sm_[kReverbSize];
        reverb_.setSize(lastSize_);
    }
    const int amp = static_cast<int>(sm_[kAmp] + 0.5);
    if (amp != ampType_) {
        ampType_ = amp;
        for (auto& b : cab_) { b.identity(); b.reset(); }
        if (amp == 1) {            // suitcase: four 12-inch speakers in a closed cabinet
            cab_[0].highpass(fs_, 68.0, 0.8);
            cab_[1].peak(fs_, 2300.0, 3.5, 0.9);
            cab_[2].lowpass(fs_, 4600.0, 0.62);
            cab_[3].lowpass(fs_, 5200.0, 0.9);
            cabStages_ = 4;
            ampTrim_ = 1.1;
        } else if (amp == 2) {     // open-back combo: scooped mids, brighter top
            cab_[0].highpass(fs_, 82.0, 0.75);
            cab_[1].peak(fs_, 480.0, -5.5, 0.55);
            cab_[2].peak(fs_, 2900.0, 4.5, 1.0);
            cab_[3].lowpass(fs_, 5600.0, 0.75);
            cabStages_ = 4;
            ampTrim_ = 1.35;
        } else {
            cabStages_ = 0;
            ampTrim_ = 1.0;
        }
    }
    phaser_.update(sm_[kPhaserRate], TineBank::kTick);
    trem_.update(sm_[kTremRate], sm_[kTremDepth], sm_[kTremStereo], TineBank::kTick);
    chorus_.update(sm_[kChorusRate], TineBank::kTick);
    volume_ = std::pow(10.0, sm_[kVolume] / 20.0) * kOutputTrim;
}

void Engine::process(float* left, float* right, int n, double* dry) {
    int done = 0;
    while (done < n) {
        if (tickPos_ == 0) controlTick();
        const int m = std::min(n - done, TineBank::kTick - tickPos_);
        if (tailSamples_ <= 0 && bank_.idle()) {
            // nothing is sounding and every tail has died away: exact silence, effects cleared once
            if (!flushed_) {
                phaser_.reset(); drive_.reset(); chorus_.reset(); reverb_.reset();
                bass_.reset(); treble_.reset(); dcBlock_.reset();
                for (auto& b : cab_) b.reset();
                flushed_ = true;
            }
            std::fill(left + done, left + done + m, 0.0f);
            std::fill(right + done, right + done + m, 0.0f);
            if (dry) std::fill(dry + done, dry + done + m, 0.0);
            done += m;
            tickPos_ = (tickPos_ + m) % TineBank::kTick;
            continue;
        }
        bank_.render(buf_, m);
        if (dry) std::copy(buf_, buf_ + m, dry + done);
        const double ph = sm_[kPhaser], ch = sm_[kChorus], rv = sm_[kReverb];
        for (int i = 0; i < m; ++i) {
            double x = dcBlock_.tick(buf_[i]);
            x = treble_.tick(bass_.tick(x));
            if (ph > 1e-4) x = phaser_.tick(x, ph);
            x = drive_.tick(x) * ampTrim_;
            for (int s = 0; s < cabStages_; ++s) x = cab_[s].tick(x);
            double l, r;
            chorus_.tick(x, ch, l, r);
            l *= trem_.gl;
            r *= trem_.gr;
            double wl, wr;
            reverb_.tick(0.5 * (l + r), wl, wr);
            l += rv * 0.9 * wl;
            r += rv * 0.9 * wr;
            left[done + i] = static_cast<float>(softClip(l * volume_));
            right[done + i] = static_cast<float>(softClip(r * volume_));
        }
        done += m;
        tickPos_ = (tickPos_ + m) % TineBank::kTick;
        if (tailSamples_ > 0 && bank_.idle()) tailSamples_ -= m;
    }
}

// ------------------------------------------------------------------ presets ----
namespace {
#define P(name, bark, timbre, hammer, sustain, bell, thump, damper, dyn, amp, bass, treble, drive, trate, tdepth, tstereo, \
          chorus, crate, phaser, prate, reverb, rsize, vol) \
    {name, {bark, timbre, hammer, sustain, bell, thump, damper, dyn, 440.0f, 0.5f, amp, bass, treble, drive, trate, tdepth, \
            tstereo, chorus, crate, phaser, prate, reverb, rsize, vol}}
const Preset kPresets[] = {
    //                      bark  timb  hamm  sust  bell  thmp  damp  dyn   amp  bass  treb  drive trate tdep  tster chor  crate phas  prate rev   rsize vol
    P("Stage Direct",       0.50f,0.50f,0.50f,0.50f,0.50f,0.50f,0.50f,0.50f,0.f, 0.f,  0.f,  0.00f,5.0f, 0.00f,1.0f, 0.00f,0.6f, 0.00f,0.4f, 0.12f,0.45f,0.5f),
    P("Suitcase Classic",   0.50f,0.52f,0.50f,0.50f,0.50f,0.55f,0.50f,0.50f,1.f, 2.f,  1.f,  0.12f,5.2f, 0.55f,1.0f, 0.00f,0.6f, 0.00f,0.4f, 0.14f,0.45f,-0.5f),
    P("Warm Gospel",        0.42f,0.66f,0.38f,0.58f,0.40f,0.55f,0.50f,0.56f,1.f, 3.f, -2.f,  0.10f,4.2f, 0.30f,1.0f, 0.45f,0.5f, 0.00f,0.4f, 0.26f,0.60f,0.0f),
    P("Bell Tines",         0.55f,0.30f,0.68f,0.55f,0.72f,0.40f,0.50f,0.48f,0.f,-1.f,  4.f,  0.00f,5.0f, 0.00f,1.0f, 0.30f,0.7f, 0.00f,0.4f, 0.22f,0.55f,0.5f),
    P("Bark and Bite",      0.72f,0.45f,0.62f,0.48f,0.55f,0.60f,0.50f,0.42f,2.f, 1.f,  2.f,  0.38f,5.0f, 0.00f,1.0f, 0.00f,0.6f, 0.00f,0.4f, 0.10f,0.40f,-8.0f),
    P("Slow Phase",         0.50f,0.50f,0.48f,0.52f,0.50f,0.50f,0.50f,0.50f,0.f, 1.f,  1.f,  0.05f,5.0f, 0.00f,1.0f, 0.00f,0.6f, 0.75f,0.22f,0.16f,0.50f,3.0f),
    P("Dream Chorus",       0.46f,0.56f,0.44f,0.60f,0.55f,0.45f,0.50f,0.52f,0.f, 1.f,  2.f,  0.00f,5.0f, 0.00f,1.0f, 0.80f,0.45f,0.00f,0.4f, 0.38f,0.72f,0.0f),
    P("Wide Suitcase",      0.52f,0.50f,0.52f,0.50f,0.50f,0.50f,0.50f,0.50f,1.f, 1.f,  2.f,  0.15f,6.4f, 0.85f,1.0f, 0.15f,0.6f, 0.00f,0.4f, 0.18f,0.50f,0.0f),
    P("Mellow Ballad",      0.40f,0.74f,0.30f,0.62f,0.30f,0.50f,0.55f,0.60f,0.f, 2.f, -4.f,  0.00f,3.5f, 0.18f,1.0f, 0.25f,0.4f, 0.00f,0.4f, 0.34f,0.70f,2.0f),
    P("Soul Combo",         0.58f,0.48f,0.55f,0.50f,0.50f,0.60f,0.50f,0.46f,2.f, 2.f,  0.f,  0.22f,5.6f, 0.40f,0.0f, 0.00f,0.6f, 0.00f,0.4f, 0.20f,0.45f,-2.5f),
    P("Dry Funk",           0.62f,0.42f,0.70f,0.42f,0.60f,0.65f,0.40f,0.44f,0.f, 0.f,  3.f,  0.18f,5.0f, 0.00f,1.0f, 0.00f,0.6f, 0.35f,1.6f, 0.04f,0.30f,-2.5f),
    P("Night Drive",        0.50f,0.58f,0.45f,0.56f,0.45f,0.50f,0.50f,0.52f,1.f, 2.f, -1.f,  0.20f,2.6f, 0.35f,1.0f, 0.55f,0.35f,0.30f,0.12f,0.30f,0.65f,-2.0f),
};
#undef P
}  // namespace

int numPresets() { return static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0])); }
const Preset& preset(int index) { return kPresets[std::min(numPresets() - 1, std::max(0, index))]; }

}  // namespace hk

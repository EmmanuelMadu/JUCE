#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace hk {

namespace {
// Level of the pickup sum that maps to full scale: a firm chord sits around -12 dBFS.
constexpr double kOutputTrim = 0.5;
// The recordings peak near full scale for a single hard strike; this puts the grand next to the tines.
constexpr double kGrandTrim = 0.46;
// A string whose partial matches a sustained partial on the soundboard comes back 22 dB below it.
constexpr double kSympGain = 0.08;

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
    for (int k = 0; k < kNumKeys; ++k) stretch_[k] = grand_.keyCents(k);
}

void Engine::prepare(double sampleRate) {
    fs_ = sampleRate;
    bank_.prepare(sampleRate);
    grand_.prepare(sampleRate);
    strings_.prepare(sampleRate);
    compAtk_ = 1.0 - std::exp(-1.0 / (0.008 * sampleRate));
    compRel_ = 1.0 - std::exp(-1.0 / (0.160 * sampleRate));
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
    grand_.reset();
    strings_.reset();
    for (auto& b : toneLo_) b.reset();
    for (auto& b : toneHi_) b.reset();
    compEnv_ = 0.0;
    compGain_ = 1.0;
    compStep_ = 0.0;
    std::fill(delayL_, delayL_ + Drive::kLatency, 0.0f);
    std::fill(delayR_, delayR_ + Drive::kLatency, 0.0f);
    delayPos_ = 0;
    grandHold_ = 0;
    grandBusOn_ = false;
    tineRest_ = 0;
    tineChainOff_ = false;
    lastTone_ = 1e9;
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
    if (params_[kTinesOn].load(std::memory_order_relaxed) > 0.5f) bank_.noteOn(note, velocity);
    grand_.noteOn(note, velocity);          // the key always moves its damper; it sounds if the grand is on
    tailSamples_ = static_cast<long long>(14.0 * fs_);      // longest reverb tail, with margin
    flushed_ = false;
}

void Engine::noteOff(int note) {
    bank_.noteOff(note);
    grand_.noteOff(note);
}
void Engine::sustain(float amount) {
    bank_.setPedal(amount);
    grand_.setPedal(amount);
}
void Engine::sostenuto(float amount) { grand_.setSostenuto(amount); }
void Engine::softPedal(float amount) { grand_.setSoft(amount); }
void Engine::allNotesOff() {
    bank_.allNotesOff();
    grand_.allNotesOff();
}

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
    const bool grandOn = v(kGrandOn) > 0.5;
    // one instrument, one tuning: with the grand in play the tines follow the piano's stretch
    c.stretch = (grandOn || v(kTineRing) > 0.0) ? stretch_ : nullptr;
    bank_.setControls(c);

    GrandControls g;
    g.tuneRatio = c.tuneHz / 440.0;
    g.levelExp = 2.0 * c.gamma / 1.3;
    g.hammerDb = (v(kGrandHammer) - 0.5) * 24.0;            // +-12 dB of strike strength
    g.damper = c.damper;
    const double nz = 2.0 * v(kGrandNoise), rs = 2.0 * v(kGrandRes), ring = v(kTineRing) / 0.75;
    g.noise = nz * nz;
    g.tails = rs;
    g.sound = grandOn;
    g.raw = raw_;
    grand_.setControls(g);
    strings_.setLevels(raw_ ? 0.0 : kSympGain * rs * rs, kSympGain * ring * ring * (kOutputTrim / kGrandTrim));
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
    grand_.tick();
    strings_.setTune(sm_[kTune] / 440.0);
    strings_.tick(grand_.dampers(), std::pow(2.0, (params_[kDamper].load(std::memory_order_relaxed) - 0.5) * 3.0));
    if (std::fabs(sm_[kGrandTone] - lastTone_) > 1e-3) {
        lastTone_ = sm_[kGrandTone];
        const double t = (lastTone_ - 0.5) * 2.0;
        for (int ch = 0; ch < 2; ++ch) {
            toneLo_[ch].lowShelf(fs_, 250.0, -4.5 * t);
            toneHi_[ch].highShelf(fs_, 2500.0, 4.5 * t);
        }
    }
    {   // compressor: one knob lowers the threshold, raises the ratio and makes up the level
        const double a = sm_[kGrandComp];
        double target = 1.0;
        if (a > 1e-4) {
            const double thr = -14.0 - 12.0 * a, slope = 1.0 - 1.0 / (1.0 + 3.0 * a), knee = 6.0;
            const double over = 20.0 * std::log10(std::max(compEnv_, 1e-9)) - thr;
            const double gr = over <= -0.5 * knee ? 0.0
                            : over >= 0.5 * knee ? slope * over
                            : slope * (over + 0.5 * knee) * (over + 0.5 * knee) / (2.0 * knee);
            target = std::pow(10.0, (7.0 * a * a - gr) / 20.0);
        }
        compStep_ = (target - compGain_) / TineBank::kTick;
    }
    {   // balance: each side is at full level up to the middle of the knob
        const double b = sm_[kBalance];
        const double gt = std::min(1.0, 2.0 * (1.0 - b)), gg = std::min(1.0, 2.0 * b);
        tineGain_ = gt * gt;
        grandGain_ = gg * gg / kOutputTrim;
    }

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

// Recordings, ringing strings and the grand's own processing for the next m samples.
// Leaves the result in gl_/gr_, in the same units as the tine path, delayed like it.
void Engine::grandBus(int m) {
    std::fill(gl_, gl_ + m, 0.0f);
    std::fill(gr_, gr_ + m, 0.0f);
    std::fill(exc_, exc_ + m, 0.0f);
    grand_.render(gl_, gr_, exc_, m);
    for (int i = 0; i < m; ++i) excT_[i] = static_cast<float>(buf_[i]);
    strings_.render(exc_, excT_, grand_, gl_, gr_, m);
    const double width = sm_[kGrandWidth];
    for (int i = 0; i < m; ++i) {
        double l = gl_[i] * kGrandTrim, r = gr_[i] * kGrandTrim;
        if (!raw_) {
            l = toneHi_[0].tick(toneLo_[0].tick(l));
            r = toneHi_[1].tick(toneLo_[1].tick(r));
        }
        const double pk = std::max(std::fabs(l), std::fabs(r));
        compEnv_ += (pk > compEnv_ ? compAtk_ : compRel_) * (pk - compEnv_);
        l *= compGain_;
        r *= compGain_;
        compGain_ += compStep_;
        const double mid = 0.5 * (l + r), side = 0.5 * (l - r) * width;
        l = (mid + side) * grandGain_;
        r = (mid - side) * grandGain_;
        const float dl = delayL_[delayPos_], dr = delayR_[delayPos_];
        delayL_[delayPos_] = static_cast<float>(l);
        delayR_[delayPos_] = static_cast<float>(r);
        if (++delayPos_ >= Drive::kLatency) delayPos_ = 0;
        gl_[i] = dl;
        gr_[i] = dr;
    }
}

void Engine::process(float* left, float* right, int n, double* dry) {
    int done = 0;
    while (done < n) {
        if (tickPos_ == 0) controlTick();
        const int m = std::min(n - done, TineBank::kTick - tickPos_);
        const bool grandSounding = !grand_.idle() || !strings_.idle();
        if (tailSamples_ <= 0 && bank_.idle() && !grandSounding) {
            // nothing is sounding and every tail has died away: exact silence, effects cleared once
            if (!flushed_) {
                phaser_.reset(); drive_.reset(); chorus_.reset(); reverb_.reset();
                bass_.reset(); treble_.reset(); dcBlock_.reset();
                for (auto& b : cab_) b.reset();
                for (auto& b : toneLo_) b.reset();
                for (auto& b : toneHi_) b.reset();
                std::fill(delayL_, delayL_ + Drive::kLatency, 0.0f);
                std::fill(delayR_, delayR_ + Drive::kLatency, 0.0f);
                compEnv_ = 0.0;
                grandHold_ = 0;
                grandBusOn_ = false;
                flushed_ = true;
            }
            std::fill(left + done, left + done + m, 0.0f);
            std::fill(right + done, right + done + m, 0.0f);
            if (dry) std::fill(dry + done, dry + done + m, 0.0);
            grand_.skip(m);
            done += m;
            tickPos_ = (tickPos_ + m) % TineBank::kTick;
            continue;
        }
        bank_.render(buf_, m);
        if (dry) std::copy(buf_, buf_ + m, dry + done);

        // The grand's bus runs while its strings sound or can be set ringing by the tines,
        // and a little longer so that its filters and delay empty themselves.
        const bool tinesReach = !bank_.idle() && params_[kTineRing].load(std::memory_order_relaxed) > 0.0f;
        if (grandSounding || tinesReach) {
            grandHold_ = 4096;
            tailSamples_ = static_cast<long long>(14.0 * fs_);
            flushed_ = false;
        }
        const bool bus = grandHold_ > 0;
        if (bus) {
            grandBus(m);
            if (!grandSounding && !tinesReach) grandHold_ -= m;
            grandBusOn_ = true;
        } else {
            grand_.skip(m);
        }
        if (!bus && grandBusOn_) {
            for (auto& b : toneLo_) b.reset();
            for (auto& b : toneHi_) b.reset();
            std::fill(delayL_, delayL_ + Drive::kLatency, 0.0f);
            std::fill(delayR_, delayR_ + Drive::kLatency, 0.0f);
            compEnv_ = 0.0;
            grandBusOn_ = false;
        }

        // With the tines switched off and silent, their amplifier chain has nothing to do.
        if (bank_.idle() && params_[kTinesOn].load(std::memory_order_relaxed) < 0.5f) {
            if (tineRest_ < kTineRest) tineRest_ += m;
        } else {
            tineRest_ = 0;
        }
        const bool tineChain = tineRest_ < kTineRest;
        if (!tineChain && !tineChainOff_) {
            phaser_.reset(); drive_.reset(); chorus_.reset();
            bass_.reset(); treble_.reset(); dcBlock_.reset();
            for (auto& b : cab_) b.reset();
        }
        tineChainOff_ = !tineChain;

        const double ph = sm_[kPhaser], ch = sm_[kChorus], rv = sm_[kReverb];
        for (int i = 0; i < m; ++i) {
            double l = 0.0, r = 0.0;
            if (tineChain) {
                double x = dcBlock_.tick(buf_[i]);
                x = treble_.tick(bass_.tick(x));
                if (ph > 1e-4) x = phaser_.tick(x, ph);
                x = drive_.tick(x) * ampTrim_;
                for (int s = 0; s < cabStages_; ++s) x = cab_[s].tick(x);
                chorus_.tick(x, ch, l, r);
                l *= trem_.gl * tineGain_;
                r *= trem_.gr * tineGain_;
            }
            if (bus) {
                l += gl_[i];
                r += gr_[i];
            }
            double wl, wr;
            reverb_.tick(0.5 * (l + r), wl, wr);
            l += rv * 0.9 * wl;
            r += rv * 0.9 * wr;
            left[done + i] = static_cast<float>(softClip(l * volume_));
            right[done + i] = static_cast<float>(softClip(r * volume_));
        }
        done += m;
        tickPos_ = (tickPos_ + m) % TineBank::kTick;
        if (tailSamples_ > 0 && bank_.idle() && !grandSounding) tailSamples_ -= m;
    }
}

// ------------------------------------------------------------------ presets ----
namespace {
#define P(name, bark, timbre, hammer, sustain, bell, thump, damper, dyn, amp, bass, treble, drive, trate, tdepth, tstereo, \
          chorus, crate, phaser, prate, reverb, rsize, vol) \
    {name, {bark, timbre, hammer, sustain, bell, thump, damper, dyn, 440.0f, 0.5f, amp, bass, treble, drive, trate, tdepth, \
            tstereo, chorus, crate, phaser, prate, reverb, rsize, vol, \
            1.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 1.0f, 0.0f}}
const Preset kTinePresets[] = {
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

// The grand's characters and the two instruments together.  "base" names the tine preset
// whose electric-piano settings the program carries.
struct GrandRow {
    const char* name;
    int base;
    float tines, grand, balance, hammer, tone, res, ring, noise, width, comp, dyn, reverb, rsize, vol;
};
const GrandRow kGrandRows[] = {
    //  name                  base tines grand bal   hamm  tone  res   ring  noise width comp  dyn   rev   rsize vol
    {"Grand Natural",         0,   0.f,  1.f,  0.50f,0.50f,0.50f,0.50f,0.00f,0.50f,1.00f,0.00f,0.50f,0.16f,0.50f,0.0f},
    {"Grand Ballad",          0,   0.f,  1.f,  0.50f,0.38f,0.45f,0.62f,0.00f,0.50f,1.10f,0.00f,0.55f,0.30f,0.68f,-0.5f},
    {"Grand Bright Pop",      0,   0.f,  1.f,  0.50f,0.74f,0.62f,0.45f,0.00f,0.45f,0.90f,0.55f,0.42f,0.14f,0.45f,1.5f},
    {"Grand Mellow",          0,   0.f,  1.f,  0.50f,0.22f,0.38f,0.55f,0.00f,0.50f,1.00f,0.00f,0.55f,0.22f,0.60f,1.0f},
    {"Grand Stage",           0,   0.f,  1.f,  0.50f,0.64f,0.56f,0.50f,0.00f,0.40f,0.70f,0.35f,0.45f,0.12f,0.40f,2.0f},
    {"Grand Cinematic",       0,   0.f,  1.f,  0.50f,0.30f,0.42f,0.80f,0.00f,0.55f,1.30f,0.00f,0.60f,0.45f,0.85f,-2.0f},
    {"Grand Intimate",        0,   0.f,  1.f,  0.50f,0.10f,0.32f,0.50f,0.00f,0.75f,0.80f,0.00f,0.62f,0.10f,0.35f,-0.5f},
    {"Grand and Tines",       0,   1.f,  1.f,  0.50f,0.50f,0.50f,0.50f,0.50f,0.50f,1.00f,0.00f,0.50f,0.20f,0.55f,-2.0f},
    {"Gospel Layer",          2,   1.f,  1.f,  0.46f,0.45f,0.48f,0.55f,0.60f,0.50f,1.00f,0.20f,0.54f,0.26f,0.60f,-0.5f},
    {"Ballad Duo",            8,   1.f,  1.f,  0.55f,0.36f,0.45f,0.62f,0.60f,0.50f,1.10f,0.00f,0.58f,0.32f,0.70f,-2.5f},
    {"Pop Duo",               3,   1.f,  1.f,  0.56f,0.70f,0.60f,0.45f,0.40f,0.45f,0.90f,0.45f,0.44f,0.16f,0.50f,0.5f},
    {"Tines in the Grand",    0,   1.f,  0.f,  0.50f,0.50f,0.50f,0.50f,0.85f,0.50f,1.10f,0.00f,0.50f,0.18f,0.55f,0.0f},
};

const std::vector<Preset>& presetTable() {
    static const std::vector<Preset> table = [] {
        std::vector<Preset> t(std::begin(kTinePresets), std::end(kTinePresets));
        for (const GrandRow& r : kGrandRows) {
            Preset p = kTinePresets[r.base];
            p.name = r.name;
            p.v[kTinesOn] = r.tines;
            p.v[kGrandOn] = r.grand;
            p.v[kBalance] = r.balance;
            p.v[kGrandHammer] = r.hammer;
            p.v[kGrandTone] = r.tone;
            p.v[kGrandRes] = r.res;
            p.v[kTineRing] = r.ring;
            p.v[kGrandNoise] = r.noise;
            p.v[kGrandWidth] = r.width;
            p.v[kGrandComp] = r.comp;
            p.v[kDynamics] = r.dyn;
            p.v[kReverb] = r.reverb;
            p.v[kReverbSize] = r.rsize;
            if (r.tines < 0.5f || r.grand > 0.5f) p.v[kVolume] = r.vol;
            t.push_back(p);
        }
        return t;
    }();
    return table;
}
}  // namespace

int numPresets() { return static_cast<int>(presetTable().size()); }
const Preset& preset(int index) { return presetTable()[static_cast<size_t>(std::min(numPresets() - 1, std::max(0, index)))]; }
int numTinePresets() { return static_cast<int>(sizeof(kTinePresets) / sizeof(kTinePresets[0])); }


}  // namespace hk

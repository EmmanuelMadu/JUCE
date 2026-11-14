#include "GrandBank.h"

#include <algorithm>
#include <cmath>

namespace hk {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kPhases = 256;
constexpr int kTaps = 8;
constexpr double kRelMedian = -13.9;        // typical loudness of the key-release recordings, dB

// Interpolation kernel: 8-point windowed sinc, one row per 1/256 of a sample.
struct SincTable {
    float c[kPhases + 1][2 * kTaps];        // every coefficient twice: left, right
    static double bessel0(double x) {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }
    SincTable() {
        const double beta = 7.0, norm = bessel0(beta);
        for (int p = 0; p <= kPhases; ++p) {
            const double frac = double(p) / kPhases;
            double row[kTaps], sum = 0;
            for (int k = 0; k < kTaps; ++k) {
                const double x = (k - 3) - frac;                     // distance from the tap to the point
                const double s = std::fabs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
                const double u = x / (kTaps / 2);
                const double w = std::fabs(u) >= 1.0 ? 0.0 : bessel0(beta * std::sqrt(1.0 - u * u)) / norm;
                row[k] = s * w;
                sum += row[k];
            }
            for (int k = 0; k < kTaps; ++k) c[p][2 * k] = c[p][2 * k + 1] = float(row[k] / sum);
        }
        for (int k = 0; k < kTaps; ++k) {              // whole-sample positions pass the recording untouched
            c[0][2 * k] = c[0][2 * k + 1] = k == 3 ? 1.0f : 0.0f;
            c[kPhases][2 * k] = c[kPhases][2 * k + 1] = k == 4 ? 1.0f : 0.0f;
        }
    }
};
const SincTable& sinc() {
    static const SincTable t;
    return t;
}

inline double clamp(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Loudness of a recording some time after the strike, dB re its attack.
float envAt(const GrandLayer& ly, float t) {
    if (t <= kGrandEnvTime[0]) return ly.env[0];
    for (int i = 1; i < kGrandEnvPoints; ++i) {
        if (t < kGrandEnvTime[i]) {
            const float u = std::log(t / kGrandEnvTime[i - 1]) / std::log(kGrandEnvTime[i] / kGrandEnvTime[i - 1]);
            return ly.env[i - 1] + u * (ly.env[i] - ly.env[i - 1]);
        }
    }
    return ly.env[kGrandEnvPoints - 1];
}

// Brightness a strike of loudness L would have on this note (between and beyond the recordings).
double brightAt(const GrandNote& gn, double L) {
    const GrandLayer* ly = gn.layer;
    const int last = kGrandLayers - 1;
    const double span = ly[last].level - ly[0].level;
    const double slope = span > 1.0 ? std::max(0.0, double(ly[last].bright - ly[0].bright) / span) : 0.0;
    if (L <= ly[0].level) return ly[0].bright + std::max(-6.0, slope * (L - ly[0].level));
    if (L >= ly[last].level) return ly[last].bright + std::min(6.0, slope * (L - ly[last].level));
    for (int i = 0; i < last; ++i) {
        if (L < ly[i + 1].level) {
            const double w = ly[i + 1].level - ly[i].level;
            const double u = w > 1e-6 ? (L - ly[i].level) / w : 0.0;
            return ly[i].bright + u * (ly[i + 1].bright - ly[i].bright);
        }
    }
    return ly[last].bright;
}

// How fast a damper resting fully on the strings stops them, dB per second.
inline double damperRate(int key) { return 140.0 + (520.0 - 140.0) * std::min(key, 67) / 67.0; }
}  // namespace

GrandBank::GrandBank() {
    sinc();
    reset();
}

void GrandBank::prepare(double sampleRate) {
    fs_ = sampleRate;
    reset();
}

void GrandBank::reset() {
    for (auto& v : voices_) v = Voice();
    numActive_ = 0;
    for (int k = 0; k < kNumKeys; ++k) {
        down_[k] = sost_[k] = struck_[k] = false;
        lastVel_[k] = 0;
        strikeTime_[k] = 0;
        ownUsed_[k] = false;
    }
    numOwn_ = 0;
    now_ = lastPedalTime_ = 0;
    pedal_ = soft_ = 0;
    pedalDown_ = sostDown_ = false;
    rng_ = 1;
    updateDampers();
}

float GrandBank::keyCents(int key) const { return kGrandNote[(std::min(std::max(key, 0), kNumKeys - 1) + 1) / 3].cents; }

void GrandBank::updateDampers() {
    pedalDamp_ = clamp(1.0 - 1.6 * pedal_, 0.0, 1.0);
    for (int k = 0; k < kNumKeys; ++k) {
        if (k + kLowest > kGrandLastDamper) damp_[k] = 0.0f;                 // the top strings have no dampers
        else damp_[k] = (down_[k] || sost_[k]) ? 0.0f : float(pedalDamp_);
    }
}

float GrandBank::fadeFactor(double seconds) const {       // per control tick: 60 dB down in the given time
    return float(std::pow(10.0, -3.0 * kTick / (seconds * fs_)));
}

double GrandBank::levelNow(const Voice& v) const {
    const float t = float(double(now_ - v.start) / fs_);
    return v.targetDb + envAt(*v.layer, t) + 20.0 * std::log10(std::max(double(v.env) * v.fade, 1e-6));
}

GrandBank::Voice* GrandBank::alloc(bool steal) {
    if (numActive_ >= kMaxVoices) {
        if (!steal) return nullptr;
        // full: give up the oldest noise or tail, or failing that the oldest note
        int at = 0;
        for (int i = 0; i < numActive_; ++i)
            if (voices_[active_[i]].kind != kNote) { at = i; break; }
        const int idx = active_[at];
        for (int i = at; i < numActive_ - 1; ++i) active_[i] = active_[i + 1];
        active_[numActive_ - 1] = idx;
        voices_[idx] = Voice();
        voices_[idx].active = true;
        return &voices_[idx];
    }
    for (int i = 0; i < kMaxVoices; ++i) {
        if (!voices_[i].active) {
            voices_[i] = Voice();
            voices_[i].active = true;
            active_[numActive_++] = i;
            return &voices_[i];
        }
    }
    return nullptr;
}

void GrandBank::compact() {
    int n = 0;
    for (int i = 0; i < numActive_; ++i)
        if (voices_[active_[i]].active) active_[n++] = active_[i];
    numActive_ = n;
}

void GrandBank::noteOn(int note, double velocity) {
    const int k = note - kLowest;
    if (k < 0 || k >= kNumKeys) return;
    down_[k] = true;
    if (k + kLowest <= kGrandLastDamper) damp_[k] = 0.0f;
    const GrandPack* pack = pack_.load(std::memory_order_acquire);
    if (!ctl_.sound || !pack) { struck_[k] = false; return; }
    strike(k, velocity, pack);
    struck_[k] = true;
    lastVel_[k] = float(velocity);
    strikeTime_[k] = now_;
}

void GrandBank::noteOff(int note) {
    const int k = note - kLowest;
    if (k < 0 || k >= kNumKeys) return;
    if (!down_[k]) return;
    down_[k] = false;
    if (k + kLowest <= kGrandLastDamper) damp_[k] = sost_[k] ? 0.0f : float(pedalDamp_);
    if (struck_[k]) startKeyNoise(k);
    struck_[k] = false;
}

void GrandBank::setPedal(double amount) {
    const double a = clamp(amount, 0.0, 1.0);
    const double step = std::fabs(a - pedal_);
    const double dt = std::max(1e-3, double(now_ - lastPedalTime_) / fs_);
    // a switch pedal jumps the whole way and tells nothing about the foot: normal noise
    const double speedDb = step >= 0.9 ? 0.0 : clamp(-3.0 + 10.0 * std::log10(std::max(step / dt, 1e-3) / 5.0), -12.0, 3.0);
    pedal_ = a;
    lastPedalTime_ = now_;
    if (!pedalDown_ && a > 0.35) { pedalDown_ = true; startPedalNoise(true, speedDb); }
    else if (pedalDown_ && a < 0.25) { pedalDown_ = false; startPedalNoise(false, speedDb); }
    updateDampers();
}

void GrandBank::setSostenuto(double amount) {
    if (!sostDown_ && amount >= 0.5) {
        sostDown_ = true;
        for (int k = 0; k < kNumKeys; ++k) sost_[k] = down_[k];      // catches the keys held right now
    } else if (sostDown_ && amount < 0.5) {
        sostDown_ = false;
        for (int k = 0; k < kNumKeys; ++k) sost_[k] = false;
    }
    updateDampers();
}

void GrandBank::setSoft(double amount) { soft_ = clamp(amount, 0.0, 1.0); }

void GrandBank::allNotesOff() {
    for (int k = 0; k < kNumKeys; ++k) down_[k] = sost_[k] = struck_[k] = false;
    sostDown_ = false;
    updateDampers();
}

void GrandBank::strike(int k, double velocity, const GrandPack* pack) {
    const GrandNote& gn = kGrandNote[(k + 1) / 3];            // nearest sampled note: at most a semitone away
    const int semis = (k + kLowest) - gn.root;
    const double v = std::max(velocity, 1.0 / 127.0);
    double T = gn.top + 20.0 * ctl_.levelExp * std::log10(v) - 2.5 * soft_;     // loudness asked for
    T = std::max(T, double(gn.top) - 60.0);
    const double Lt = T + ctl_.hammerDb - 5.0 * soft_;                           // strike strength of the timbre
    int j = 0;
    double best = 1e9;
    for (int i = 0; i < kGrandLayers; ++i) {
        const double e = std::fabs(gn.layer[i].level - Lt);
        if (e < best) { best = e; j = i; }
    }
    const GrandLayer& ly = gn.layer[j];
    double shelfDb = 0.0, gainDb = 0.0;
    if (!ctl_.raw) {
        shelfDb = clamp(brightAt(gn, Lt) - ly.bright, -6.0, 6.0);
        const double f = std::pow(10.0, ly.bright / 10.0);
        const double shelfLoud = 10.0 * std::log10(1.0 - f + f * std::pow(10.0, shelfDb / 10.0));
        gainDb = std::min(24.0, T - ly.level - shelfLoud);
    }

    // a key struck again: what its strings were doing gives way to the new strike
    int sounding = 0, quietest = -1;
    double quietLevel = 1e9;
    for (int i = 0; i < numActive_; ++i) {
        Voice& o = voices_[active_[i]];
        if (o.kind != kNote || o.fading) continue;
        if (o.key == k) {
            o.fading = true;
            o.tailDone = true;
            o.fadeMul = fadeFactor(0.45 - 0.30 * k / 87.0);
            continue;
        }
        ++sounding;
        const double lv = levelNow(o);
        if (lv < quietLevel) { quietLevel = lv; quietest = active_[i]; }
    }
    if (sounding >= kNoteLimit && quietest >= 0) {
        voices_[quietest].fading = true;
        voices_[quietest].tailDone = true;
        voices_[quietest].fadeMul = fadeFactor(0.03);
    }

    Voice* nv = alloc(true);
    if (!nv) return;
    Voice& o = *nv;
    const double ratio = std::pow(2.0, semis / 12.0) * ctl_.tuneRatio;
    o.kind = kNote;
    o.key = k;
    o.data = pack->pcm(ly.file);
    o.frames = kGrandFile[ly.file].frames;
    o.rate = ratio * (kGrandRate / fs_);
    o.base = float(kGrandFile[ly.file].scale * std::pow(10.0, gainDb / 20.0));
    o.g = o.base;
    o.targetDb = float(T);
    o.fminHz = float(1.5 * gn.f1 * ratio);
    o.layer = &ly;
    o.note = &gn;
    o.start = now_;
    if (std::fabs(shelfDb) > 0.05) {
        // first-order high shelf, symmetric about the corner
        const double g = std::pow(10.0, shelfDb / 20.0), rg = std::sqrt(g);
        const double kk = std::tan(kPi * std::min(0.45, gn.shelfHz * ratio / fs_));
        const double a = kk / rg, b = kk * rg;
        o.sb0 = float(g * (1.0 + a) / (1.0 + b));
        o.sb1 = float(g * (a - 1.0) / (1.0 + b));
        o.sa1 = float((b - 1.0) / (1.0 + b));
        o.shelfOn = true;
    }
}

void GrandBank::startTail(const Voice& src) {
    if (ctl_.tails <= 0.0 || src.key + kLowest > kGrandLastDamper) return;
    const GrandPack* pack = pack_.load(std::memory_order_acquire);
    if (!pack) return;
    const int ri = (src.key + 1) / 3;
    if (ri >= kGrandTailRoots) return;
    // the tails were recorded after hard staccato strikes: scale with what the strings are doing now
    const double Tt = kGrandTail[0][ri].level - 4.0 + (levelNow(src) - src.note->layer[kGrandLayers - 1].level);
    if (Tt < -96.0) return;
    int s = 0;
    if (Tt < kGrandTail[1][ri].level + 2.0) s = 1;
    if (Tt < kGrandTail[2][ri].level + 2.0) s = 2;
    const GrandTail& tl = kGrandTail[s][ri];
    const double rate = src.rate;
    const int key = src.key;
    Voice* nv = alloc(false);
    if (!nv) return;
    nv->kind = kTail;
    nv->key = key;
    nv->data = pack->pcm(tl.file);
    nv->frames = kGrandFile[tl.file].frames;
    nv->rate = rate;
    nv->base = float(kGrandFile[tl.file].scale * std::pow(10.0, std::min(12.0, Tt - tl.level) / 20.0) * ctl_.tails);
    nv->g = nv->base;
    nv->start = now_;
}

void GrandBank::startKeyNoise(int k) {
    if (ctl_.noise <= 0.0) return;
    const GrandPack* pack = pack_.load(std::memory_order_acquire);
    if (!pack) return;
    const double vel = lastVel_[k], held = double(now_ - strikeTime_[k]) / fs_;
    double dbv = -37.0 + 20.0 * std::log10(1.0 - 0.82 * (1.0 - vel * vel)) - std::min(24.0, 2.0 * held);
    if (pedalDamp_ < 0.5) dbv -= 6.0;                       // dampers held off by the pedal: only the key returns
    dbv += kRelMedian - kGrandRelLevel[k];
    Voice* nv = alloc(false);
    if (!nv) return;
    const int file = kGrandRel[k];
    nv->kind = kNoise;
    nv->key = k;
    nv->data = pack->pcm(file);
    nv->frames = kGrandFile[file].frames;
    nv->rate = kGrandRate / fs_;
    nv->base = float(kGrandFile[file].scale * std::pow(10.0, dbv / 20.0) * ctl_.noise);
    nv->g = nv->base;
    nv->start = now_;
}

void GrandBank::startPedalNoise(bool down, double speedDb) {
    if (!ctl_.sound || ctl_.noise <= 0.0) return;
    const GrandPack* pack = pack_.load(std::memory_order_acquire);
    if (!pack) return;
    rng_ = rng_ * 1664525u + 1013904223u;
    const int file = kGrandPedal[(down ? 0 : 2) + int((rng_ >> 16) & 1u)];
    Voice* nv = alloc(false);
    if (!nv) return;
    nv->kind = kNoise;
    nv->key = -1;
    nv->data = pack->pcm(file);
    nv->frames = kGrandFile[file].frames;
    nv->rate = kGrandRate / fs_;
    nv->base = float(kGrandFile[file].scale * std::pow(10.0, ((down ? -20.0 : -19.0) + speedDb) / 20.0) * ctl_.noise);
    nv->g = nv->base;
    nv->start = now_;
}

void GrandBank::tick() {
    const double dt = kTick / fs_;
    bool removed = false;
    const int count = numActive_;                 // voices started during this tick are not touched
    for (int i = 0; i < count; ++i) {
        Voice& v = voices_[active_[i]];
        if (v.dying) { v.active = false; removed = true; continue; }
        if (v.kind == kNote) {
            const float d = damp_[v.key];
            if (d > 0.0f) {
                const double rate = damperRate(v.key) * d * d / ctl_.damper;
                v.env *= float(std::exp(-rate * dt * 0.115129254649702));
                if (!v.lpOn) { v.lpOn = true; v.lpl = v.lastL; v.lpr = v.lastR; }
                if (d > 0.5f && !v.tailDone) { v.tailDone = true; startTail(v); }
            } else if (!v.fading) {
                v.tailDone = false;               // lifted again: what is left rings on and can be damped anew
            }
            if (v.lpOn) {
                // a damper takes the highs first
                const double fc = std::max(double(v.fminHz), 18000.0 * std::pow(double(v.env), 1.5));
                v.lpc = fc >= 16000.0 ? 1.0f : float(1.0 - std::exp(-2.0 * kPi * fc / fs_));
            }
        }
        if (v.fading) v.fade *= v.fadeMul;
        const float level = v.env * v.fade;
        if (level < 1e-4f) {
            v.dying = true;
            v.dg = -v.g * (1.0f / kTick);
        } else {
            v.dg = (v.base * level - v.g) * (1.0f / kTick);
        }
    }
    if (removed) compact();
}

void GrandBank::renderVoice(Voice& v, float* left, float* right, float* exc, float* own, int n) {
    const float (*tab)[2 * kTaps] = sinc().c;
    const int16_t* d = v.data;
    const int64_t frames = v.frames;
    double pos = v.pos;
    const double rate = v.rate;
    float g = v.g;
    const float dg = v.dg;
    float szl = v.szl, szr = v.szr, lpl = v.lpl, lpr = v.lpr, xl = v.lastL, xr = v.lastR;
    const bool shelf = v.shelfOn, lp = v.lpOn;
    const float sb0 = v.sb0, sb1 = v.sb1, sa1 = v.sa1, lpc = v.lpc;
    for (int i = 0; i < n; ++i) {
        const int64_t ip = int64_t(pos);
        if (ip >= frames) { v.active = false; break; }
        const float ph = float(pos - double(ip)) * kPhases;
        const int pi = std::min(int(ph), kPhases - 1);
        const float t = ph - float(pi);
        const float* c0 = tab[pi];
        const float* c1 = tab[pi + 1];
        // eight points of the recording around the position, weighted by the kernel; written
        // so that the compiler can use vector instructions, with a fixed order of the sums
        const int16_t* q;
        int16_t edge[2 * kTaps];
        if (ip >= 3 && ip + 4 < frames) {
            q = d + 2 * (ip - 3);
        } else {
            for (int k = 0; k < kTaps; ++k) {
                const int64_t j = ip - 3 + k;
                const bool in = j >= 0 && j < frames;
                edge[2 * k] = in ? d[2 * j] : int16_t(0);
                edge[2 * k + 1] = in ? d[2 * j + 1] : int16_t(0);
            }
            q = edge;
        }
        float pr[2 * kTaps], acc[4];
        for (int j = 0; j < 2 * kTaps; ++j) pr[j] = (c0[j] + t * (c1[j] - c0[j])) * float(q[j]);
        for (int j = 0; j < 4; ++j) acc[j] = (pr[j] + pr[4 + j]) + (pr[8 + j] + pr[12 + j]);
        float sl = acc[0] + acc[2], sr = acc[1] + acc[3];
        pos += rate;
        if (shelf) {
            const float yl = sb0 * sl + szl, yr = sb0 * sr + szr;
            szl = sb1 * sl - sa1 * yl;
            szr = sb1 * sr - sa1 * yr;
            sl = yl;
            sr = yr;
        }
        xl = sl;
        xr = sr;
        if (lp) {
            lpl += lpc * (sl - lpl);
            lpr += lpc * (sr - lpr);
            sl = lpl;
            sr = lpr;
        }
        const float ol = sl * g, orr = sr * g;
        g += dg;
        left[i] += ol;
        right[i] += orr;
        if (exc) {
            const float m = 0.5f * (ol + orr);
            exc[i] += m;
            own[i] += m;
        }
    }
    v.pos = pos;
    v.g = g;
    v.szl = szl; v.szr = szr; v.lpl = lpl; v.lpr = lpr;
    v.lastL = xl; v.lastR = xr;
}

void GrandBank::render(float* left, float* right, float* exc, int n) {
    for (int i = 0; i < numOwn_; ++i) ownUsed_[ownList_[i]] = false;
    numOwn_ = 0;
    bool ended = false;
    for (int i = 0; i < numActive_; ++i) {
        Voice& v = voices_[active_[i]];
        float* own = nullptr;
        if (v.kind == kNote) {
            if (!ownUsed_[v.key]) {
                ownUsed_[v.key] = true;
                ownList_[numOwn_++] = v.key;
                std::fill(own_[v.key], own_[v.key] + kTick, 0.0f);
            }
            own = own_[v.key];
        }
        renderVoice(v, left, right, v.kind == kNote ? exc : nullptr, own, n);
        if (!v.active) ended = true;
    }
    if (ended) compact();
    now_ += uint64_t(n);
}

}  // namespace hk

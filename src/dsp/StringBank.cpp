#include "StringBank.h"

#include <algorithm>
#include <cmath>

namespace hk {

namespace {
constexpr double kPi = 3.14159265358979323846;

// Decay of a freely ringing string by register, dB per second (from the recordings).
double baseDecay(int key) {
    static const double kKey[] = {0, 12, 24, 36, 48, 60, 72, 84, 87};
    static const double kRate[] = {2.5, 2.8, 3.8, 6.5, 13.0, 22.0, 30.0, 45.0, 55.0};
    for (int i = 1; i < 9; ++i)
        if (key <= kKey[i]) {
            const double u = (key - kKey[i - 1]) / (kKey[i] - kKey[i - 1]);
            return kRate[i - 1] + u * (kRate[i] - kRate[i - 1]);
        }
    return kRate[8];
}
inline double damperRate(int key) { return 140.0 + (520.0 - 140.0) * std::min(key, 67) / 67.0; }
}  // namespace

void StringBank::prepare(double sampleRate) {
    fs_ = sampleRate;
    configure();
    reset();
}

void StringBank::reset() {
    std::fill(yr_, yr_ + kNumKeys * kMaxPartials, 0.0f);
    std::fill(yi_, yi_ + kNumKeys * kMaxPartials, 0.0f);
    for (auto& s : str_) s.ringing = false;
    quiet_ = true;
    numRinging_ = 0;
}

void StringBank::setTune(double ratio) {
    if (std::fabs(ratio - tune_) < 1e-9) return;
    tune_ = ratio;
    configure();
}

void StringBank::configure() {
    const double fmax = std::min(6000.0, 0.42 * fs_);
    for (int k = 0; k < kNumKeys; ++k) {
        const GrandNote& gn = kGrandNote[(k + 1) / 3];
        const int semis = (k + 21) - gn.root;
        const double shift = std::pow(2.0, semis / 12.0) * tune_;
        Str& st = str_[k];
        st.first = k * kMaxPartials;
        int n = 0;
        for (; n < kMaxPartials; ++n) {
            const int p = n + 1;
            const double f = gn.partial[n] * shift;          // where this partial really is
            if (f > fmax && n > 0) break;
            const double rate = baseDecay(k) * (0.6 + 0.4 * std::sqrt(double(p)));       // dB/s
            const double alpha = rate * 0.115129254649702;                             // 1/s
            const double r = std::exp(-alpha / fs_), w = 2.0 * kPi * std::min(f, 0.45 * fs_) / fs_;
            const int j = st.first + n;
            c_[j] = float(r * std::cos(w));
            s_[j] = float(r * std::sin(w));
            // unit input gain = a matched, sustained partial comes back at the same level
            g_[j] = float(2.0 * alpha / fs_ / (1.0 + (f / 4000.0) * (f / 4000.0)));
        }
        st.count = (n + 3) & ~3;                                                         // whole groups of four
        for (int j = st.first + n; j < st.first + st.count; ++j) c_[j] = s_[j] = g_[j] = 0.0f;
        const double angle = (0.30 + 0.40 * k / 87.0) * 0.5 * kPi;                       // bass a little left
        st.panL = float(std::sqrt(2.0) * std::cos(angle));
        st.panR = float(std::sqrt(2.0) * std::sin(angle));
    }
}

void StringBank::tick(const float* dampers, double damperScale) {
    const double dt = kTick / fs_;
    int ringing = 0;
    for (int k = 0; k < kNumKeys; ++k) {
        Str& st = str_[k];
        const float d = dampers[k];
        const float lift = 1.0f - d;
        st.inGrand = (k + 21 <= kGrandLastDamper) ? gGrand_ * lift * lift : 0.0f;        // the top strings are in the recordings
        st.inTine = gTine_ * lift * lift;
        if (!st.ringing) continue;
        float* yr = yr_ + st.first;
        float* yi = yi_ + st.first;
        if (d > 0.0f) {
            const float m = float(std::exp(-damperRate(k) * d * d * dt * 0.115129254649702 / damperScale));
            for (int j = 0; j < st.count; ++j) { yr[j] *= m; yi[j] *= m; }
        }
        float e = 0.0f;
        for (int j = 0; j < st.count; ++j) e += yr[j] * yr[j] + yi[j] * yi[j];
        if (e < 1e-14f) {
            for (int j = 0; j < st.count; ++j) yr[j] = yi[j] = 0.0f;
            st.ringing = false;
        } else {
            ++ringing;
        }
    }
    numRinging_ = ringing;
}

void StringBank::render(const float* excGrand, const float* excTine, const GrandBank& grand, float* left, float* right, int n) {
    bool anyG = false, anyT = false;
    for (int i = 0; i < n; ++i) {
        anyG = anyG || excGrand[i] != 0.0f;
        anyT = anyT || excTine[i] != 0.0f;
    }
    bool any = false;
    for (int k = 0; k < kNumKeys; ++k) {
        Str& st = str_[k];
        const bool driven = (anyG && st.inGrand > 0.0f) || (anyT && st.inTine > 0.0f);
        if (!driven && !st.ringing) continue;
        float x[kTick];
        const float* own = grand.own(k);
        const float a = st.inGrand, b = st.inTine;
        if (own) for (int i = 0; i < n; ++i) x[i] = a * (excGrand[i] - own[i]) + b * excTine[i];
        else     for (int i = 0; i < n; ++i) x[i] = a * excGrand[i] + b * excTine[i];
        // separate arrays: telling the compiler so lets it work on four resonators at once
        float* __restrict yr = yr_ + st.first;
        float* __restrict yi = yi_ + st.first;
        const float* __restrict c = c_ + st.first;
        const float* __restrict s = s_ + st.first;
        const float* __restrict g = g_ + st.first;
        const int cnt = st.count;
        const float pl = st.panL, pr = st.panR;
        // Four resonators at a time, each group through the whole stretch with its state in
        // registers: z <- z * r e^(jw) + g * input, output = real part.
        float out[kTick] = {};
        for (int j = 0; j < cnt; j += 4) {
            float re[4], im[4], cc[4], ss[4], gg[4];
            for (int q = 0; q < 4; ++q) {
                re[q] = yr[j + q]; im[q] = yi[j + q];
                cc[q] = c[j + q]; ss[q] = s[j + q]; gg[q] = g[j + q];
            }
            for (int i = 0; i < n; ++i) {
                const float in = x[i];
                float nr[4], ni[4];
                for (int q = 0; q < 4; ++q) {
                    nr[q] = cc[q] * re[q] - ss[q] * im[q] + gg[q] * in;
                    ni[q] = ss[q] * re[q] + cc[q] * im[q];
                }
                for (int q = 0; q < 4; ++q) { re[q] = nr[q]; im[q] = ni[q]; }
                out[i] += (nr[0] + nr[1]) + (nr[2] + nr[3]);
            }
            for (int q = 0; q < 4; ++q) { yr[j + q] = re[q]; yi[j + q] = im[q]; }
        }
        for (int i = 0; i < n; ++i) {
            left[i] += pl * out[i];
            right[i] += pr * out[i];
        }
        st.ringing = true;
        any = true;
    }
    quiet_ = !any;
}

}  // namespace hk

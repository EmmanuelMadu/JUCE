// Small signal-processing blocks used after the pickups: filters, phaser,
// oversampled drive, tremolo, chorus and reverb.  Header-only, no dependencies.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace hk {

constexpr double kPi = 3.14159265358979323846264338327950288;

// ---------------------------------------------------------------- biquad ----
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline double tick(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void set(double B0, double B1, double B2, double A0, double A1, double A2) {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    void identity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    void lowShelf(double fs, double f, double dB, double slope = 0.8) {
        const double A = std::pow(10.0, dB / 40.0), w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
        const double al = s / 2 * std::sqrt((A + 1 / A) * (1 / slope - 1) + 2), r = 2 * std::sqrt(A) * al;
        set(A * ((A + 1) - (A - 1) * c + r), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - r),
            (A + 1) + (A - 1) * c + r, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - r);
    }
    void highShelf(double fs, double f, double dB, double slope = 0.8) {
        const double A = std::pow(10.0, dB / 40.0), w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
        const double al = s / 2 * std::sqrt((A + 1 / A) * (1 / slope - 1) + 2), r = 2 * std::sqrt(A) * al;
        set(A * ((A + 1) + (A - 1) * c + r), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - r),
            (A + 1) - (A - 1) * c + r, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - r);
    }
    void peak(double fs, double f, double dB, double q) {
        const double A = std::pow(10.0, dB / 40.0), w = 2 * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowpass(double fs, double f, double q) {
        const double w = 2 * kPi * std::min(f, 0.45 * fs) / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass(double fs, double f, double q) {
        const double w = 2 * kPi * f / fs, c = std::cos(w), al = std::sin(w) / (2 * q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
};

// ---------------------------------------------------------------- phaser ----
// Four first-order all-pass stages swept by an LFO, with feedback: the classic
// two-notch pedal sound.
struct Phaser {
    double fs = 48000, phase = 0, a = 0, fb = 0, x1[4] = {0, 0, 0, 0}, y1[4] = {0, 0, 0, 0};
    void prepare(double sampleRate) { fs = sampleRate; reset(); }
    void reset() { phase = 0; fb = 0; for (int i = 0; i < 4; ++i) x1[i] = y1[i] = 0; update(0.4, 16); }
    void update(double rateHz, int samples) {            // call once per control tick
        phase += rateHz * samples / fs;
        phase -= std::floor(phase);
        const double lfo = 0.5 - 0.5 * std::cos(2 * kPi * phase);          // 0..1
        const double fc = 220.0 * std::pow(8.5, lfo);                       // 220 Hz .. 1.9 kHz
        const double t = std::tan(kPi * std::min(fc, 0.4 * fs) / fs);
        a = (t - 1) / (t + 1);
    }
    inline double tick(double x, double amount) {
        double s = x + 0.5 * amount * fb;
        for (int i = 0; i < 4; ++i) {
            const double y = a * s + x1[i] - a * y1[i];
            x1[i] = s;
            y1[i] = y;
            s = y;
        }
        fb = s;
        return x * (1.0 - 0.5 * amount) + s * 0.5 * amount;
    }
};

// ----------------------------------------------------- 2x oversampled drive ----
// Linear-phase half-band filters (31 taps) around a tube-like soft clipper.
// Total delay: exactly 15 samples at the base rate, whatever the drive setting.
struct Drive {
    static constexpr int kTaps = 31, kHalf = 15, kLatency = 15;
    double h[kTaps];
    double xh[16] = {0}, vh[32] = {0};
    int xi = 0, vi = 0;
    double g = 0.25, bias = 0, makeup = 4, tb = 0;

    Drive() {
        // Kaiser-windowed half-band low-pass
        const double beta = 8.0;
        auto i0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        for (int n = 0; n < kTaps; ++n) {
            const int k = n - kHalf;
            const double w = i0(beta * std::sqrt(1.0 - (double(k) / kHalf) * (double(k) / kHalf))) / i0(beta);
            h[n] = (k == 0) ? 0.5 : ((k % 2) ? std::sin(kPi * k / 2) / (kPi * k) * w : 0.0);
        }
        // exact unity gain at DC
        double sum = 0;
        for (double v : h) sum += v;
        for (int n = 0; n < kTaps; ++n) if (n != kHalf) h[n] *= 0.5 / (sum - 0.5);
        setDrive(0.0);
    }
    void reset() { std::fill(xh, xh + 16, 0.0); std::fill(vh, vh + 32, 0.0); xi = vi = 0; }
    void setDrive(double d) {                           // d in 0..1
        g = 0.25 * std::pow(10.0, d * 36.0 / 20.0);
        bias = 0.15 * d;
        tb = std::tanh(bias);
        makeup = 4.0 * std::pow(0.125, d);
    }
    inline double shape(double x) const { return (std::tanh(g * x + bias) - tb) * makeup; }
    inline double tick(double x) {
        xi = (xi + 1) & 15;
        xh[xi] = x;
        // interpolated sample (odd taps), then the pass-through sample
        double e = 0;
        for (int j = 0; j < 16; ++j) e += h[2 * j] * xh[(xi - j) & 15];
        e *= 2.0;
        const double o = xh[(xi - 7) & 15];
        vi = (vi + 1) & 31;
        vh[vi] = shape(e);
        double y = 0.5 * vh[(vi - kHalf) & 31];
        for (int j = 0; j < 16; ++j) y += h[2 * j] * vh[(vi - 2 * j) & 31];
        vi = (vi + 1) & 31;
        vh[vi] = shape(o);
        return y;
    }
};

// --------------------------------------------------------------- tremolo ----
// Level modulation with a rounded-square LFO; with full "stereo" the two
// channels move in opposition, as in a suitcase piano's vibrato.
struct Tremolo {
    double fs = 48000, phase = 0, gl = 1, gr = 1;
    void prepare(double sampleRate) { fs = sampleRate; reset(); }
    void reset() { phase = 0; gl = gr = 1; }
    void update(double rateHz, double depth, double stereo, int samples) {
        phase += rateHz * samples / fs;
        phase -= std::floor(phase);
        const double s = std::tanh(2.2 * std::sin(2 * kPi * phase)) / std::tanh(2.2);
        gl = 1.0 - depth * 0.5 * (1.0 + s);
        gr = 1.0 - depth * 0.5 * (1.0 + s * (1.0 - 2.0 * stereo));
    }
};

// ---------------------------------------------------------------- chorus ----
struct Chorus {
    std::vector<float> buf;
    int mask = 0, w = 0;
    double fs = 48000, phase = 0, dl = 0, dr = 0, lp = 0;
    void prepare(double sampleRate) {
        fs = sampleRate;
        int n = 1;
        while (n < int(0.04 * fs)) n <<= 1;
        buf.assign(n, 0.0f);
        mask = n - 1;
        reset();
    }
    void reset() { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; phase = 0; lp = 0; update(0.6, 16); }
    void update(double rateHz, int samples) {
        phase += rateHz * samples / fs;
        phase -= std::floor(phase);
        dl = (0.0095 + 0.0032 * std::sin(2 * kPi * phase)) * fs;
        dr = (0.0095 + 0.0032 * std::sin(2 * kPi * phase + 0.5 * kPi)) * fs;
    }
    inline double tap(double d) const {
        const int i = int(d);
        const double f = d - i;
        return buf[(w - i) & mask] * (1.0 - f) + buf[(w - i - 1) & mask] * f;
    }
    inline void tick(double x, double amount, double& l, double& r) {
        lp += 0.35 * (x - lp);                       // the delayed copy is slightly darker
        w = (w + 1) & mask;
        buf[w] = float(lp);
        const double norm = 1.0 / (1.0 + 0.45 * amount);
        l = (x + amount * tap(dl)) * norm;
        r = (x + amount * tap(dr)) * norm;
    }
};

// ---------------------------------------------------------------- reverb ----
// Eight-line feedback delay network with input diffusion and high damping.
struct Reverb {
    static constexpr int kLines = 8;
    std::vector<float> line[kLines], ap[2], pre;
    int len[kLines], pos[kLines], apLen[2], apPos[2], preLen = 1, prePos = 0;
    double fbk[kLines], damp[kLines], dampCoef = 0.3, fs = 48000;
    void prepare(double sampleRate) {
        fs = sampleRate;
        static const int base[kLines] = {1423, 1637, 1877, 2099, 2371, 2633, 2917, 3181};
        for (int i = 0; i < kLines; ++i) {
            len[i] = std::max(8, int(base[i] * fs / 48000.0 + 0.5)) | 1;
            line[i].assign(len[i], 0.0f);
        }
        static const int apBase[2] = {241, 83};
        for (int i = 0; i < 2; ++i) {
            apLen[i] = std::max(4, int(apBase[i] * fs / 48000.0 + 0.5));
            ap[i].assign(apLen[i], 0.0f);
        }
        preLen = std::max(1, int(0.012 * fs));
        pre.assign(preLen, 0.0f);
        reset();
        setSize(0.5);
    }
    void reset() {
        for (int i = 0; i < kLines; ++i) { std::fill(line[i].begin(), line[i].end(), 0.0f); pos[i] = 0; damp[i] = 0; }
        for (int i = 0; i < 2; ++i) { std::fill(ap[i].begin(), ap[i].end(), 0.0f); apPos[i] = 0; }
        std::fill(pre.begin(), pre.end(), 0.0f);
        prePos = 0;
    }
    void setSize(double size) {                         // 0..1 -> decay time 0.5 .. 4.5 s
        const double t60 = 0.5 * std::pow(9.0, size);
        for (int i = 0; i < kLines; ++i) fbk[i] = std::pow(10.0, -3.0 * len[i] / (t60 * fs));
        dampCoef = 1.0 - std::exp(-2 * kPi * 4200.0 / fs);
    }
    inline void tick(double in, double& l, double& r) {
        double x = pre[prePos];
        pre[prePos] = float(in);
        if (++prePos >= preLen) prePos = 0;
        for (int i = 0; i < 2; ++i) {                    // diffusion all-passes
            const double d = ap[i][apPos[i]];
            const double v = x + 0.6 * d;
            ap[i][apPos[i]] = float(v);
            if (++apPos[i] >= apLen[i]) apPos[i] = 0;
            x = d - 0.6 * v;
        }
        double o[kLines], sum = 0;
        for (int i = 0; i < kLines; ++i) {
            damp[i] += dampCoef * (line[i][pos[i]] - damp[i]);
            o[i] = damp[i] * fbk[i];
            sum += o[i];
        }
        sum *= 2.0 / kLines;
        l = r = 0;
        for (int i = 0; i < kLines; ++i) {
            double v = o[i] - sum + x;                   // Householder feedback
            if (std::fabs(v) < 1e-20) v = 0;
            line[i][pos[i]] = float(v);
            if (++pos[i] >= len[i]) pos[i] = 0;
            if (i & 1) r += (i & 2) ? o[i] : -o[i];
            else l += (i & 2) ? o[i] : -o[i];
        }
        l *= 0.5;
        r *= 0.5;
    }
};

}  // namespace hk

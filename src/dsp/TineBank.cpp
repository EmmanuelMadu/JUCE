#include "TineBank.h"

#include <algorithm>
#include <cmath>

namespace hk {

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kSilence = 2.0e-7;        // swing (gap units) below which a tine is put to rest
}  // namespace

void TineBank::prepare(double sampleRate) {
    fs_ = sampleRate;
    T_ = 1.0 / sampleRate;
    engageCoef_ = 1.0 - std::exp(-kTick * T_ / 0.012);
    liftCoef_ = 1.0 - std::exp(-kTick * T_ / 0.004);
    restCoef_ = 1.0 - std::exp(-kTick * T_ / 0.008);
    for (int i = 0; i < kNumKeys; ++i) {           // smooth run of the tuning table (+-6 keys)
        double sum = 0;
        int n = 0;
        for (int j = std::max(0, i - 6); j <= std::min(kNumKeys - 1, i + 6); ++j) { sum += kKeyTable[j].cents; ++n; }
        trend_[i] = sum / n;
    }
    reset();
}

void TineBank::reset() {
    for (auto& k : keys_) k = Key();
    numActive_ = 0;
    pedal_ = 0.0;
}

void TineBank::configureKey(Key& k, const KeyParams& kp, int index) {
    const int note = index + kLowestNote;
    // on its own the instrument keeps its measured tuning; next to the grand it follows the
    // piano's stretch and keeps only its key-to-key scatter
    const double cents = ctl_.stretch ? ctl_.stretch[index] + (kp.cents - trend_[index]) * ctl_.detune
                                      : kp.cents * ctl_.detune;
    const double f1 = ctl_.tuneHz * std::pow(2.0, (note - 69) / 12.0 + cents / 1200.0);
    k.w1 = kTwoPi * f1 * T_;
    k.w2 = k.w1 * kp.r2;
    k.wT = kTwoPi * kp.fThump * T_;
    // modes above ~0.42 fs cannot be represented; the highest keys lose their second mode
    k.hasMode2 = (k.w2 < 0.42 * kTwoPi);
    k.r2 = kp.r2;
    k.rT = k.wT / k.w1;
    k.swing = ctl_.bark;
    k.u0 = kp.u0 * ctl_.timbre * ctl_.bark;
    k.lam = kp.lam;
    // a closer pickup also sees a stronger field; keep part of that level change
    k.gain = kp.gain * std::sqrt(ctl_.bark);
    k.c2 = std::cos(k.w2);
    k.s2 = std::sin(k.w2);
    k.cT = std::cos(k.wT);
    k.sT = std::sin(k.wT);
}

void TineBank::updateKey(Key& k, const KeyParams& kp) {
    // damper position follows key and pedal with a short felt-compression time
    const double target = k.down ? 0.0 : std::max(0.0, 1.0 - 1.6 * pedal_);
    const double before = k.damp;
    k.damp += (target - k.damp) * ((target > k.damp) ? engageCoef_ : liftCoef_);
    const double a1 = std::sqrt(k.y1 * k.y1 + k.p1 * k.p1);
    if (before < 0.02 && k.damp >= 0.02 && a1 > 1e-3) {
        // the felt lands on a moving tine: a small push on the whole assembly
        k.pT += 0.03 * ctl_.thump * std::min(a1, 3.0);
    }
    const double dampRate = kp.alphaDamp / ctl_.damper * k.damp;
    const double alpha1 = (kp.alpha0 + kp.alphaDrag * a1) / ctl_.sustain + dampRate;
    const double g1 = std::exp(-alpha1 * T_);
    // slight flattening at large swing
    const double w = k.w1 * (1.0 - kp.pitchDrop * a1 * (std::log(2.0) / 1200.0));
    k.c1 = g1 * std::cos(w);
    k.s1 = g1 * std::sin(w);
    if (k.hasMode2) {
        const double g2 = std::exp(-(kp.alpha2 + 4.0 * dampRate) * T_);
        k.c2 = g2 * std::cos(k.w2);
        k.s2 = g2 * std::sin(k.w2);
    }
    const double gT = std::exp(-(kp.alphaThump + 0.5 * dampRate) * T_);
    k.cT = gT * std::cos(k.wT);
    k.sT = gT * std::sin(k.wT);
}

void TineBank::noteOn(int note, double velocity) {
    const int i = note - kLowestNote;
    if (i < 0 || i >= kNumKeys) return;
    Key& k = keys_[i];
    const KeyParams& kp = kKeyTable[i];
    if (!k.active) {
        const double dampWas = k.damp;
        k = Key();
        k.damp = dampWas;
        k.active = true;
        activeList_[numActive_++] = i;
    }
    configureKey(k, kp, i);
    k.down = true;
    const double v = std::min(1.0, std::max(0.0, velocity));
    const double speed = std::pow(v, ctl_.gamma);                 // relative hammer speed
    // contact shortens as the tip is driven harder; a harder tip shortens it further
    double tau = kp.tau * std::pow(std::max(speed, 0.02), -0.4) / ctl_.hammer;
    // a contact much longer than the note's period is not physical for a struck tine
    tau = std::min(tau, 0.8 / (k.w1 * fs_));
    tau = std::min(0.004, std::max(1.5 * T_, tau));
    k.hTau = tau * fs_;                                            // in samples
    k.hLen = std::max(2, static_cast<int>(std::ceil(9.0 * k.hTau)));
    k.hPos = 0;
    // pulse w(t) = (t/tau) e^(1 - t/tau), normalised to unit sum; its spectrum is 1 / (1 + (w tau)^2)
    double sum = 0.0;
    for (int n = 0; n < k.hLen; ++n) {
        const double x = (n + 0.5) / k.hTau;
        sum += x * std::exp(1.0 - x);
    }
    const double wt = k.w1 * k.hTau;
    const double h1 = 1.0 / (1.0 + wt * wt);
    // A tine that is already moving takes less from the hammer when it moves with it and more when it
    // moves against it (the blow depends on their relative speed); a tine already swinging far beyond
    // its normal range takes little more.
    double rel = std::min(1.5, std::max(0.2, 1.0 - k.p1 / (2.5 * kp.a0max)));
    if (k.y1 * k.y1 + k.p1 * k.p1 > 6.25 * kp.a0max * kp.a0max) rel *= 0.25;
    // the calibrated swing is what the fundamental reaches from rest, whatever the pulse length
    k.hAmp = kp.a0max * speed * rel / (h1 * sum);
    k.hK2 = k.hasMode2 ? kp.k2 * ctl_.bell : 0.0;
    k.hKT = kp.kThump * ctl_.thump;
    updateKey(k, kp);
}

void TineBank::noteOff(int note) {
    const int i = note - kLowestNote;
    if (i < 0 || i >= kNumKeys) return;
    keys_[i].down = false;
}

void TineBank::setPedal(double amount) { pedal_ = std::min(1.0, std::max(0.0, amount)); }

void TineBank::allNotesOff() {
    for (auto& k : keys_) k.down = false;
    pedal_ = 0.0;
}

void TineBank::tick() {
    int n = 0;
    for (int j = 0; j < numActive_; ++j) {
        const int i = activeList_[j];
        Key& k = keys_[i];
        const double e = k.y1 * k.y1 + k.p1 * k.p1 + k.y2 * k.y2 + k.p2 * k.p2 + k.yT * k.yT + k.pT * k.pT;
        if (k.hPos >= k.hLen && e < kSilence * kSilence) {
            const double damp = k.damp;
            const bool down = k.down;
            k = Key();
            k.damp = damp;
            k.down = down;
            continue;                                              // dropped from the active list
        }
        updateKey(k, kKeyTable[i]);
        activeList_[n++] = i;
    }
    numActive_ = n;
    // dampers of resting keys still follow the pedal, so a later strike starts from the right state
    for (auto& k : keys_) {
        if (!k.active) {
            const double target = k.down ? 0.0 : std::max(0.0, 1.0 - 1.6 * pedal_);
            k.damp += (target - k.damp) * restCoef_;
        }
    }
}

void TineBank::render(double* out, int n) {
    std::fill(out, out + n, 0.0);
    for (int j = 0; j < numActive_; ++j) {
        Key& k = keys_[activeList_[j]];
        double y1 = k.y1, p1 = k.p1, y2 = k.y2, p2 = k.p2, yT = k.yT, pT = k.pT;
        const double c1 = k.c1, s1 = k.s1, c2 = k.c2, s2 = k.s2, cT = k.cT, sT = k.sT;
        const double u0 = k.u0, lam = k.lam, sw = k.swing, r2 = k.r2, rT = k.rT, gain = k.gain * sw;
        const bool m2 = k.hasMode2;
        const double invW2 = 1.0 / (kFluxWidth * kFluxWidth), bb = 2.0 * kFluxBeta * invW2;
        for (int i = 0; i < n; ++i) {
            if (k.hPos < k.hLen) {
                const double x = (k.hPos + 0.5) / k.hTau;
                const double f = k.hAmp * x * std::exp(1.0 - x);
                p1 += f;
                p2 += k.hK2 * f;
                pT += k.hKT * f;
                ++k.hPos;
            }
            double t = c1 * y1 + s1 * p1;
            p1 = c1 * p1 - s1 * y1;
            y1 = t;
            double disp = y1, vel = p1;
            if (m2) {
                t = c2 * y2 + s2 * p2;
                p2 = c2 * p2 - s2 * y2;
                y2 = t;
                disp += y2;
                vel += r2 * p2;
            }
            t = cT * yT + sT * pT;
            pT = cT * pT - sT * yT;
            yT = t;
            disp += yT;
            vel += rT * pT;
            const double u = u0 + sw * disp;
            const double d = 1.0 + u * u, e = 1.0 + u * u * invW2;
            out[i] += gain * (lam - 4.0 * u / (d * d * d) - bb * u / (e * e)) * vel;
        }
        k.y1 = y1; k.p1 = p1; k.y2 = y2; k.p2 = p2; k.yT = yT; k.pT = pT;
    }
}

}  // namespace hk

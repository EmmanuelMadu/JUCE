#!/usr/bin/env python3
"""Turn the fit results into the per-key table used by the sound engine.

Reads the measurements and the pickup fit, derives for every reference note
  * tine swing at full velocity, decay law, tuning deviation,
  * the level, frequency ratio and decay of the second bending mode,
then interpolates across all 88 keys (with a small, fixed key-to-key spread on
the keys that were not measured, as on a real instrument) and writes
src/dsp/KeyTable.cpp.

Usage: make_tables.py <meas.pkl> <fit.pkl> <KeyTable.cpp>
"""
import pickle
import sys

import numpy as np

GAMMA = 1.3            # hammer speed = (velocity/127)^GAMMA (must match Engine)
REF_VEL = 90


def hammer_tau(midi):
    """Time constant (s) of the hammer force pulse at full velocity."""
    return 0.00025 * 2.0 ** (-(midi - 55) / 25.0)


def decay_law(t, a):
    """Fit da/dt = -a (alpha0 + alphad * a); returns alpha0, alphad, a(0), rms dB error."""
    best = None
    la = np.log(a)
    for a0 in np.geomspace(0.05, 2.0, 60):            # alpha0, 1/s
        for ad in np.concatenate([[0.0], np.geomspace(0.005, 2.0, 50)]):
            # closed form with unknown initial value A: a(t) = a0 A e / (a0 + ad A (1 - e)), e = exp(-a0 t)
            e = np.exp(-a0 * t)
            # choose A by matching the log-mean (few fixed-point steps)
            A = a[0]
            for _ in range(8):
                model = a0 * A * e / (a0 + ad * A * (1 - e))
                A *= np.exp(np.mean(la - np.log(model)))
            model = a0 * A * e / (a0 + ad * A * (1 - e))
            err = np.sqrt(np.mean((la - np.log(model)) ** 2))
            if best is None or err < best[3]:
                best = (a0, ad, A, err)
    return best[0], best[1], best[2], best[3] * 8.686


def main():
    meas = pickle.load(open(sys.argv[1], "rb"))
    fit = pickle.load(open(sys.argv[2], "rb"))
    by = {}
    for r in meas:
        by.setdefault(r["name"], []).append(r)
    notes = []
    print("note midi   u0    lam   a0(soft..hard at t=0)                 alpha0 alphaD fit dB | cents | mode2: ratio decay(1/s) k2")
    for f in fit["fits"]:
        name, midi = f["name"], f["midi"]
        # decay law: all takes share alpha0, alphad; fit on pooled trajectories (each take its own start value)
        ts, as_, splits = [], [], []
        for tk in f["takes"]:
            ok = (tk["a"] > 0.012) & (tk["t"] > 0.04)
            ts.append(tk["t"][ok]); as_.append(tk["a"][ok]); splits.append(ok.sum())
        best = None
        for a0 in np.geomspace(0.08, 2.0, 50):
            for ad in np.concatenate([[0.0], np.geomspace(0.01, 1.5, 40)]):
                tot, starts = 0.0, []
                for t, a in zip(ts, as_):
                    if len(t) < 4:
                        starts.append(a[0] if len(a) else 0.0)
                        continue
                    e = np.exp(-a0 * t)
                    A = a[0]
                    for _ in range(6):
                        model = a0 * A * e / (a0 + ad * A * (1 - e))
                        A *= np.exp(np.mean(np.log(a) - np.log(model)))
                    model = a0 * A * e / (a0 + ad * A * (1 - e))
                    tot += np.sum((np.log(a) - np.log(model)) ** 2)
                    starts.append(A)
                if best is None or tot < best[0]:
                    best = (tot, a0, ad, starts)
        tot, alpha0, alphad, starts = best
        err_db = np.sqrt(tot / max(1, sum(splits))) * 8.686
        cents = float(np.median([r["cents"] for r in by[name]]))
        # second bending mode: inharmonic partials between 6.2 and 7.6 x f0 with a real decay.  The lists also hold
        # mixing products (mode +- harmonics), so keep the ratio that recurs across the takes.
        cand = []
        for r in by[name]:
            for p in r["partials"]:
                if 6.2 <= p["ratio"] <= 7.6 and np.isfinite(p["decay_db_s"]) and p["decay_db_s"] > 8:
                    i = int(np.argmin(np.abs(r["t"] - 0.08)))
                    l1 = 20 * np.log10(max(r["amps"][i, 0], 1e-9))
                    tk = [t for t in f["takes"] if t["layer"] == r["layer"]][0]
                    cand.append((p["ratio"], p["decay_db_s"] / 8.686, p["peak_db"] - l1, starts[f["takes"].index(tk)]))
        if cand:
            bins = np.round(np.array([c[0] for c in cand]) * 10) / 10
            vals, counts = np.unique(bins, return_counts=True)
            # among ratios seen at least twice, take the one nearest the expected trend (about 7.3 for
            # the lower keys, about 6.4 at the top); mixing products sit a whole number away from it
            expected = float(np.interp(midi, [76, 84], [7.3, 6.45]))
            seen = vals[counts >= 2] if np.any(counts >= 2) else vals
            top = seen[np.argmin(np.abs(seen - expected))]
            cand = [c for c in cand if abs(c[0] - top) <= 0.08]
            if len(cand) < 2:
                cand = []
        notes.append(dict(name=name, midi=midi, u0=f["takes"][0]["u0"], lam=f["lam"], a0=starts, alpha0=alpha0, alphad=alphad,
                          cents=cents, mode2=cand, f0=f["f0"]))
        m2 = (f"{np.median([c[0] for c in cand]):.2f} {np.median([c[1] for c in cand]):5.1f}" if cand else "   -     -")
        print(f"{name:>3} {midi:3d}  {f['takes'][0]['u0']:4.2f} {f['lam']:+5.2f}  " + " ".join(f"{s:5.2f}" for s in starts).ljust(36)
              + f"  {alpha0:5.3f} {alphad:5.3f}  {err_db:4.2f}   | {cents:+5.1f} | {m2} ({len(cand)})")

    # ---- reference values at the measured keys (outliers tempered, see comments) ----
    mid = np.array([n["midi"] for n in notes], float)
    u0 = np.array([n["u0"] for n in notes])
    lam = np.array([n["lam"] for n in notes])
    a0max = np.array([max(n["a0"]) for n in notes])
    alpha0 = np.array([n["alpha0"] for n in notes])
    alphad = np.array([n["alphad"] for n in notes])
    cents = np.array([n["cents"] for n in notes])
    # the three highest notes are too weakly nonlinear for (u0, swing) to be told apart: use the trend
    weak = mid >= 81
    u0[weak] = 0.36
    beta, wb = float(fit.get("beta", 0.0)), float(fit.get("wb", 3.0))
    if beta > 0.0:
        lam = np.zeros(len(lam))            # the broad-field term of the flux map replaces the per-note linear term
    else:
        lam = np.clip(lam, -0.5, 0.1)
        lam[weak] = -0.05
    trend = 4.2 * 2.0 ** (-(mid - 40) / 9.0)
    a0max[weak] = trend[weak] * 1.2
    cents = cents - np.median(cents)                                   # keep deviations, not the overall sharpness
    cents = np.clip(cents, -6, 6)
    cents[0] = 0.0                                                     # lowest note: takes disagree by 27 cents

    keys = np.arange(21, 109)
    rng = np.random.default_rng(1977)

    def interp(vals, log=False, spread=0.0, lo=None, hi=None):
        v = np.log(vals) if log else vals
        out = np.interp(keys, mid, v)
        # continue the end trends gently beyond the measured range
        for side, k_edge, sl in ((keys < mid[0], mid[0], (v[1] - v[0]) / (mid[1] - mid[0])),
                                 (keys > mid[-1], mid[-1], (v[-1] - v[-2]) / (mid[-1] - mid[-2]))):
            out[side] = (v[0] if k_edge == mid[0] else v[-1]) + 0.5 * sl * (keys[side] - k_edge)
        measured = np.isin(keys, mid)
        noise = rng.normal(0.0, 1.0, len(keys)) * spread
        noise[measured] = 0.0
        out = out + noise if log else out * (1 + noise)
        out = np.exp(out) if log else out
        if lo is not None or hi is not None:
            out = np.clip(out, lo, hi)
        return out

    K = dict(
        cents=np.where(np.isin(keys, mid), np.interp(keys, mid, cents), rng.normal(0, 1.6, len(keys)).clip(-4, 4)),
        a0max=interp(a0max, log=True, spread=0.06, lo=0.02, hi=9.0),
        u0=interp(u0, spread=0.12, lo=0.15, hi=2.2),
        lam=interp(lam, lo=-0.5, hi=0.1),
        alpha0=interp(alpha0, log=True, spread=0.08, lo=0.1, hi=40.0),
        alphad=interp(np.maximum(alphad, 0.02), log=True, spread=0.08, lo=0.0, hi=3.0),
    )
    # second bending mode
    m2 = [(n["midi"], np.median([c[0] for c in n["mode2"]]), np.median([c[1] for c in n["mode2"]])) for n in notes if n["mode2"]]
    m2 = np.array(m2)
    K["r2"] = np.interp(keys, m2[:, 0], m2[:, 1])
    K["alpha2"] = np.exp(np.interp(keys, m2[:, 0], np.log(m2[:, 2])))
    K["r2"][keys < m2[0, 0]] = 7.3
    K["alpha2"][keys < m2[0, 0]] = max(8.0, m2[0, 2])
    tau = hammer_tau(keys)
    corr = {}
    if len(sys.argv) > 5:
        corr = {int(k): float(v) for k, v in __import__("json").load(open(sys.argv[5])).items()}
    k2s = []
    for n in notes:
        for ratio, dec, rel_db, a0 in n["mode2"]:
            vrel = min(1.0, a0 / max(n["a0"]))
            w1 = 2 * np.pi * n["f0"]
            t = min(hammer_tau(n["midi"]) * max(vrel, 0.02) ** -0.4, 0.8 / w1)        # as in TineBank::noteOn
            h1, h2 = 1 / (1 + (w1 * t) ** 2), 1 / (1 + (w1 * ratio * t) ** 2)
            k2s.append((n["midi"], 10 ** (rel_db / 20) / ratio * h1 / h2))
    k2s = np.array(k2s)
    k2_by = {int(m): float(np.median(k2s[k2s[:, 0] == m, 1])) * corr.get(int(m), 1.0) for m in np.unique(k2s[:, 0])}
    km = np.array(sorted(k2_by))
    K["k2"] = np.clip(np.exp(np.interp(keys, km, np.log([k2_by[m] for m in km]))), 0.001, 0.8)
    K["k2"][keys < km[0]] = k2_by[int(km[0])] * 0.7
    print("second mode at measured keys: " + ", ".join(f"{int(m)}: r {np.interp(m, m2[:, 0], m2[:, 1]):.2f} k {k2_by[int(m)]:.4f}" for m in km))
    K["tau"] = tau
    K["fthump"] = 78.0 * (1 + 0.18 * np.sin(keys * 1.7) + 0.08 * rng.normal(0, 1, len(keys)))
    K["kthump"] = np.full(len(keys), 0.02)
    K["alphathump"] = np.full(len(keys), 6.9 / 0.2)
    K["alphadamp"] = 6.9 / np.interp(keys, [21, 45, 69, 108], [0.40, 0.22, 0.13, 0.07])
    K["pitchdrop"] = np.interp(keys, [21, 60, 84, 108], [1.2, 1.0, 0.4, 0.0])

    # output gain: equal RMS at a reference velocity, computed with the same pickup law as the engine
    th = 2 * np.pi * np.arange(2048) / 2048
    gains = []
    for i in range(len(keys)):
        a = K["a0max"][i] * (REF_VEL / 127.0) ** GAMMA
        u = K["u0"][i] + a * np.sin(th)
        s = (K["lam"][i] - 4 * u / (1 + u * u) ** 3 - 2 * beta / wb ** 2 * u / (1 + (u / wb) ** 2) ** 2) * a * np.cos(th)
        gains.append(0.1 / np.sqrt(np.mean(s ** 2)))
    K["gain"] = np.array(gains)

    with open(sys.argv[3], "w") as fh:
        fh.write("// Generated by tools/calib/make_tables.py from measurements of reference recordings.\n")
        fh.write("// Do not edit by hand; re-run the calibration instead.\n")
        fh.write('#include "KeyTable.h"\n\nnamespace hk {\n\n')
        fh.write(f"// Flux map of the pickup: (1 + u^2)^-2 + beta / (1 + (u / width)^2), u in gap units.\n")
        fh.write(f"const double kFluxBeta = {beta:.4f};\nconst double kFluxWidth = {wb:.4f};\n\n")
        fh.write("const KeyParams kKeyTable[kNumKeys] = {\n")
        fh.write("//   cents   a0max     u0     lam    gain  alpha0  alphaD     tau(s)     r2      k2  alpha2  fThump  kThump  aThump   aDamp  pitchDrop\n")
        for i, k in enumerate(keys):
            fh.write(f"    {{ {K['cents'][i]:+6.2f}f, {K['a0max'][i]:7.4f}f, {K['u0'][i]:6.3f}f, {K['lam'][i]:+6.3f}f, {K['gain'][i]:7.4f}f, "
                     f"{K['alpha0'][i]:6.3f}f, {K['alphad'][i]:6.3f}f, {K['tau'][i]:.3e}f, {K['r2'][i]:6.3f}f, {K['k2'][i]:7.4f}f, "
                     f"{K['alpha2'][i]:6.2f}f, {K['fthump'][i]:6.1f}f, {K['kthump'][i]:6.3f}f, {K['alphathump'][i]:6.1f}f, "
                     f"{K['alphadamp'][i]:6.1f}f, {K['pitchdrop'][i]:4.2f}f }},   // {k}\n")
        fh.write("};\n\n}  // namespace hk\n")
    print(f"wrote {sys.argv[3]}")
    if len(sys.argv) > 4:        # per-take start swings, for compare.py
        info = {n["name"]: dict(midi=n["midi"], **({"r2": float(np.median([c[0] for c in n["mode2"]]))} if n["mode2"] else {}), a0={tk["layer"]: a for tk, a in zip(next(f for f in fit["fits"] if f["name"] == n["name"])["takes"], n["a0"])},
                                a0max=float(K["a0max"][n["midi"] - 21])) for n in notes}
        pickle.dump(dict(gamma=GAMMA, notes=info), open(sys.argv[4], "wb"))
    for name, m in (("A0", 21), ("C2", 36), ("C3", 48), ("C4", 60), ("C5", 72), ("C6", 84), ("C7", 96), ("C8", 108)):
        i = m - 21
        print(f"  {name}: a0max {K['a0max'][i]:5.2f}  u0 {K['u0'][i]:4.2f}  lam {K['lam'][i]:+5.2f}  alpha0 {K['alpha0'][i]:5.2f}/s (T60 {6.9 / K['alpha0'][i]:4.1f} s)  "
              f"alphaD {K['alphad'][i]:4.2f}  r2 {K['r2'][i]:4.2f} k2 {K['k2'][i]:6.4f} a2 {K['alpha2'][i]:5.1f}/s  gain {K['gain'][i]:5.2f}")


if __name__ == "__main__":
    main()

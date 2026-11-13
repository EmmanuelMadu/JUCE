#!/usr/bin/env python3
"""Recover the recording chain's phase response from the harmonic phases of all notes.

For every note and harmonic the pickup model allows only two phases (a sign).
What is left over, dth(k, n) = T(n f_k) - n T(f_k), depends only on the chain's
phase curve T(f).  Solving for one smooth T(f) across all notes, and converting
it to a level curve with the minimum-phase (Bode) relation, gives the EQ the
recordings were made with -- independently of the pickup model.

Usage: chain_phase.py <meas.pkl> <fit.pkl> <out.npz>
"""
import os
import pickle
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fit_complex as fc  # noqa: E402


def main():
    meas = pickle.load(open(sys.argv[1], "rb"))
    fit = pickle.load(open(sys.argv[2], "rb"))
    notes = {}
    for r in meas:
        notes.setdefault(r["name"], []).append(r)
    rows = []
    for f in fit["fits"]:
        if f["f0"] > 1000:
            continue
        data = [fc.take_data(r) for r in notes[f["name"]]]
        nh = max(d["nh"] for d in data)
        for n in range(2, nh + 1):
            zs = []
            for d in data:
                if n > d["nh"]:
                    continue
                L, floor = d["L"], d["floor"]
                good = (L[:, n - 1] > floor[n - 1] + 14) & (L[:, n - 1] > L.max(axis=1) - 40)
                good &= (L[:, 0] > L.max(axis=1) - 25) & (L[:, 0] > floor[0] + 14)
                z = np.exp(2j * d["psi"][good, n - 1]) * (-1.0 if n % 2 == 0 else 1.0)
                zs.append(z)
            z = np.concatenate(zs)
            if len(z) < 6:
                continue
            m = z.mean()
            pred = f["c"] - n * f["d1"]
            dth = np.angle(m * np.exp(-2j * pred)) / 2 + pred         # branch nearest the per-note fit
            rows.append((f["f0"], n, dth, abs(m) * min(len(z), 40) / 40.0))
    rows = np.array(rows)
    knots = np.arange(np.log2(40.0), np.log2(13000.0) + 1e-9, 1.0 / 3.0)
    K = len(knots)

    def hat(fr):
        pos = np.clip((np.log2(fr) - knots[0]) * 3.0, 0, K - 1 - 1e-9)
        i0 = pos.astype(int)
        w = pos - i0
        B = np.zeros((len(fr), K))
        B[np.arange(len(fr)), i0] = 1 - w
        B[np.arange(len(fr)), i0 + 1] = w
        return B

    A = hat(rows[:, 0] * rows[:, 1]) - rows[:, 1:2] * hat(rows[:, 0])
    w = rows[:, 3]
    D = np.zeros((K - 2, K))
    for j in range(K - 2):
        D[j, j:j + 3] = [1, -2, 1]
    anchor = np.zeros((1, K))
    anchor[0, 0] = 1.0
    for lam in (3.0,):
        Aw = np.vstack([A * w[:, None], lam * D, 3.0 * anchor])
        yw = np.concatenate([rows[:, 2] * w, np.zeros(K - 2), [0.0]])
        th = np.linalg.lstsq(Aw, yw, rcond=None)[0]
    res = np.degrees(rows[:, 2] - A @ th)
    slope = th / (np.pi / 2) * 6.0206                      # dB per octave
    E = np.concatenate([[0.0], np.cumsum(0.5 * (slope[1:] + slope[:-1]) / 3.0)])
    E -= np.interp(np.log2(200.0), knots, E)
    np.savez(sys.argv[3], x=knots, db=E, ph=th)
    print(f"{len(rows)} (note, harmonic) phase values; residual after one global curve: {np.sqrt(np.average(res ** 2, weights=w)):.1f} deg rms "
          f"(per-note two-parameter fits left {np.sqrt(np.average(np.degrees(rows[:, 2] - np.array([f['c'] - n * f['d1'] for f in fit['fits'] for n in [0]])[0]) ** 2)) if False else 0:.0f})")
    print("  Hz:        " + " ".join(f"{2 ** k:6.0f}" for k in knots))
    print("  phase deg: " + " ".join(f"{np.degrees(t):+6.1f}" for t in th))
    print("  level dB:  " + " ".join(f"{e:+6.1f}" for e in E))


if __name__ == "__main__":
    main()

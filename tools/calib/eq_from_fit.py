#!/usr/bin/env python3
"""Estimate the recording chain's level-versus-frequency curve from fit residuals.

Every harmonic of every recording gives one sample of (frequency, measured
level minus pickup-model level).  If one smooth curve E(f), plus one gain per
recording, explains those samples for all notes at once, that curve is the
EQ the recordings were made with.

Usage: eq_from_fit.py <fit.pkl> <eq_out.npz>
"""
import pickle
import sys

import numpy as np


def main():
    fit = pickle.load(open(sys.argv[1], "rb"))
    xs, ys, ks = [], [], []
    k = 0
    for f in fit["fits"]:
        for tk in f["takes"]:
            n = np.arange(1, tk["nh"] + 1)
            fi, ni = np.where(tk["ok"])
            xs.append(np.log2(tk["f0"] * n[ni]))
            ys.append(tk["resid"][fi, ni])
            ks.append(np.full(len(fi), k))
            k += 1
    x, y, kk = np.concatenate(xs), np.concatenate(ys), np.concatenate(ks)
    knots = np.arange(np.log2(40.0), np.log2(14000.0) + 1e-9, 1.0 / 3.0)
    K = len(knots)
    # hat-function basis
    pos = np.clip((x - knots[0]) / (knots[1] - knots[0]), 0, K - 1 - 1e-9)
    i0 = pos.astype(int)
    w1 = pos - i0
    rows = np.arange(len(x))
    A = np.zeros((len(x), K + k))
    A[rows, i0] = 1 - w1
    A[rows, i0 + 1] = w1
    A[rows, K + kk] = 1.0
    # smoothness of the curve and a gauge (take offsets average zero)
    D = np.zeros((K - 2, K + k))
    for j in range(K - 2):
        D[j, j:j + 3] = [1, -2, 1]
    lam = 0.15 * np.sqrt(len(x) / K)
    gauge = np.zeros((1, K + k))
    gauge[0, K:] = 1.0
    w = np.ones(len(x))
    for _ in range(8):
        Aw = np.vstack([A * w[:, None], lam * D, 100.0 * gauge])
        yw = np.concatenate([y * w, np.zeros(K - 2), [0.0]])
        sol = np.linalg.lstsq(Aw, yw, rcond=None)[0]
        res = y - A @ sol
        w = np.sqrt(np.where(np.abs(res) < 5.0, 1.0, 5.0 / np.maximum(np.abs(res), 1e-9)))
    E = sol[:K]
    ref = np.interp(np.log2(200.0), knots, E)
    E = E - ref
    counts = np.array([np.sum(np.abs(x - kn) < 1 / 6) for kn in knots])
    np.savez(sys.argv[2], x=knots, db=E)
    before = y - np.array([np.median(y[kk == j]) for j in range(k)])[kk]
    print(f"{len(x)} samples from {k} recordings; residual {np.sqrt(np.mean(np.clip(before, -30, 30) ** 2)):.2f} dB rms before, "
          f"{np.sqrt(np.mean(np.clip(res, -30, 30) ** 2)):.2f} dB after the curve; median |error| {np.median(np.abs(res)):.2f} dB")
    print("  Hz:  " + " ".join(f"{2 ** kn:6.0f}" for kn in knots))
    print("  dB:  " + " ".join(f"{e:+6.1f}" for e in E))
    print("  n:   " + " ".join(f"{c:6d}" for c in counts))


if __name__ == "__main__":
    main()

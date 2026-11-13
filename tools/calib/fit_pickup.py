#!/usr/bin/env python3
"""Fit the pickup model to the measured harmonic envelopes, one note at a time.

For each note, all dynamic layers share one pickup voicing (u0, q) and one
smooth level-versus-harmonic curve (the recording's EQ).  Each recording has
its own gain (the files are peak-normalised) and every analysis frame has its
own tine swing a.  The fit therefore says how far the tine swings, in units of
the pickup gap, at every moment of every recording.

Usage: fit_pickup.py <meas.pkl> <out.pkl> [--eq <eq.npz>] [note ...]
"""
import os
import pickle
import sys
from multiprocessing import Pool

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pickup import A_GRID, harmonic_table  # noqa: E402

U0_GRID = np.array([0.0, 0.02, 0.04, 0.06, 0.08, 0.10, 0.13, 0.16, 0.20, 0.25, 0.30, 0.35, 0.40,
                    0.50, 0.60, 0.70, 0.80, 0.90, 1.00, 1.20, 1.40, 1.60])
Q_GRID = [0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0]
CLIP = 12.0          # dB; robust cap on a single harmonic's error
MAXH = 24
EQ_FIXED = None      # optional global EQ: (log2 f knots, dB values)


def frames_for(rec, nper=70):
    t, A = rec["t"], rec["amps"]
    tot = np.sqrt((A[:, :8] ** 2).sum(axis=1))
    last = len(t) - 1
    below = np.where(tot < tot.max() * 10 ** (-45 / 20))[0]
    below = below[below > np.argmax(tot)]
    if len(below):
        last = below[0]
    t0 = max(0.03, 0.75 * rec["win"])
    if t[last] <= t0:
        return np.array([], int)
    want = np.geomspace(t0, t[last], nper)
    return np.unique(np.searchsorted(t, want).clip(0, last))


def prepare(recs):
    nh = min(MAXH, min(r["amps"].shape[1] for r in recs))
    L, lay, tt, fl = [], [], [], []
    for r in recs:
        idx = frames_for(r)
        A = 20 * np.log10(np.maximum(r["amps"][:, :nh], 1e-9))
        L.append(A[idx])
        lay.append(np.full(len(idx), r["layer"]))
        tt.append(r["t"][idx])
        f = np.maximum(np.percentile(A, 3, axis=0) + 8.0, A.max() - 85.0)
        fl.append(np.tile(f, (len(idx), 1)))
    return np.vstack(L), np.concatenate(lay), np.concatenate(tt), np.vstack(fl), nh


def basis(nh, f0):
    x = np.log2(np.arange(1, nh + 1))
    if EQ_FIXED is not None:
        return np.ones((nh, 1)), np.interp(np.log2(f0) + x, EQ_FIXED[0], EQ_FIXED[1])
    xs = x / max(x[-1], 1.0)
    return np.stack([np.ones(nh), xs, xs ** 2, xs ** 3], axis=1), np.zeros(nh)


def solve(L, floor, lay, table, B, fixed, start=None, iters=10):
    layers = sorted(set(lay), reverse=True)          # softest first
    F, N = L.shape
    Lc = np.maximum(L, floor)
    valid = L > floor
    ng = len(layers) - 1
    # design matrix for offsets: layer gains (softest = 0) + EQ basis
    G = np.zeros((F, ng))
    for j, layer in enumerate(layers[1:]):
        G[lay == layer, j] = 1.0
    if start is None:
        g = np.zeros(ng)
        c = np.zeros(B.shape[1])
        c[0] = np.median(Lc.max(axis=0) - table.max(axis=0) - fixed)
    else:
        g, c = start
    cost = np.inf
    ia = np.zeros(F, int)
    for _ in range(iters):
        off = (G @ g)[:, None] + (B @ c)[None, :] + fixed[None, :]                 # F x N
        tot = np.empty((F, table.shape[0]))
        for k in range(table.shape[0]):               # loop over amplitudes keeps memory small
            M = np.maximum(table[k][None, :] + off, floor)
            tot[:, k] = (np.clip(Lc - M, -CLIP, CLIP) ** 2).sum(axis=1)
        ia = tot.argmin(axis=1)
        cost = tot[np.arange(F), ia].sum()
        pred = table[ia, :]
        R = L - pred - fixed[None, :]
        ok = valid & (pred + off > floor - 6)
        fi, ni = np.where(ok)
        if len(fi) < 20:
            break
        X = np.hstack([G[fi], B[ni]])
        y = R[fi, ni]
        w = np.ones(len(y))
        sol = np.concatenate([g, c])
        for _ in range(3):
            sol = np.linalg.lstsq(X * w[:, None], y * w, rcond=None)[0]
            res = y - X @ sol
            w = np.where(np.abs(res) < 6.0, 1.0, 6.0 / np.maximum(np.abs(res), 1e-9)) ** 0.5
        g, c = sol[:ng], sol[ng:]
    return np.sqrt(cost / L.size), ia, g, c


def fit_note(args):
    name, recs, eq = args
    global EQ_FIXED
    EQ_FIXED = eq
    L, lay, tt, floor, nh = prepare(recs)
    f0 = float(np.mean([r["f0"] for r in recs]))
    B, fixed = basis(nh, f0)
    best, per_q = None, {}
    for q in Q_GRID:
        start = None
        for u0 in U0_GRID:
            table = harmonic_table(u0, q, nh)
            rms, ia, g, c = solve(L, floor, lay, table, B, fixed, start=start, iters=6 if start else 14)
            start = (g, c)
            if q not in per_q or rms < per_q[q][0]:
                per_q[q] = (rms, u0)
            if best is None or rms < best["rms"]:
                best = dict(rms=rms, u0=u0, q=q, ia=ia, g=g, c=c)
    # polish the winner
    table = harmonic_table(best["u0"], best["q"], nh)
    rms, ia, g, c = solve(L, floor, lay, table, B, fixed, start=(best["g"], best["c"]), iters=12)
    layers = sorted(set(lay), reverse=True)
    gains = dict(zip(layers, np.concatenate([[0.0], g])))
    return dict(name=name, midi=recs[0]["midi"], f0=f0, nh=nh, u0=best["u0"], q=best["q"], rms=rms,
                gains=gains, eq=B @ c + fixed, c=c, layer=lay, t=tt, a=A_GRID[ia], L=L, floor=floor,
                per_q=per_q)


def main():
    args = sys.argv[1:]
    eq = None
    if "--eq" in args:
        i = args.index("--eq")
        d = np.load(args[i + 1])
        eq = (d["x"], d["db"])
        del args[i:i + 2]
    meas = pickle.load(open(args[0], "rb"))
    want = set(args[2:])
    notes = {}
    for r in meas:
        if want and r["name"] not in want:
            continue
        notes.setdefault(r["name"], []).append(r)
    jobs = [(k, v, eq) for k, v in sorted(notes.items(), key=lambda kv: kv[1][0]["midi"])]
    with Pool(2) as pool:
        fits = pool.map(fit_note, jobs)
    pickle.dump(fits, open(args[1], "wb"))
    print("note     f0    u0    q   rms |  rms by q " + " ".join(f"{q:4.2f}" for q in Q_GRID)
          + " | file gains dB (soft..hard)   | swing a at 50 ms (soft..hard)    | a at 1 s")
    for f in fits:
        pq = " ".join(f"{f['per_q'][q][0]:4.1f}" for q in Q_GRID)
        layers = sorted(set(f["layer"]), reverse=True)
        gs = " ".join(f"{f['gains'][layer]:+5.1f}" for layer in layers)
        a50, a1 = [], []
        for layer in layers:
            m = f["layer"] == layer
            t, a = f["t"][m], f["a"][m]
            a50.append(f"{a[np.argmin(np.abs(t - 0.05))]:5.2f}")
            a1.append(f"{a[np.argmin(np.abs(t - 1.0))]:5.2f}")
        print(f"{f['name']:>3} {f['f0']:7.1f}  {f['u0']:4.2f} {f['q']:4.2f} {f['rms']:4.1f} |           {pq} | "
              f"{gs:<29} | {' '.join(a50):<29} | {' '.join(a1)}")


if __name__ == "__main__":
    main()

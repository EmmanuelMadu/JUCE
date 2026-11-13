#!/usr/bin/env python3
"""Fit the pickup model to the measured harmonics, using levels and signs.

Model: the tine tip moves as u(t) = u0 + a sin(wt) (in pickup-gap units), the
flux is Phi(u) = (1 + u^2)^-q and the output is dPhi/dt, seen through a
recording chain with a smooth magnitude E(f).

Because the flux depends on position only, the phase of harmonic n relative
to the fundamental can take just two values (a sign), and the signs flip as
the swing decays.  The recording chain rotates those phases a little; that
rotation is measured first, per note, without reference to the pickup model.
Then levels and signs are fitted together, which pins down where the pickup
axis sits inside the swing (u0 / a) far better than levels alone.

Usage: fit_complex.py <meas.pkl> <out.pkl> [--q 2.0] [--eq eq.npz] [--per-take] [note ...]
"""
import os
import pickle
import sys
from multiprocessing import Pool

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

A_GRID = np.geomspace(0.01, 30.0, 200)
U0_GRID = np.geomspace(0.04, 4.0, 70)
MAXH = 24
HUBER = 6.0           # dB; level errors beyond this count linearly
SIGN_COST = 14.0      # dB-equivalent cost of a wrong sign
M = 4096
_TABLES = {}
BETA, W_BROAD = 0.0, 3.0      # broad-field part of the flux map: BETA / (1 + (u / W_BROAD)^2)


def tables(u0, q, nh, pol):
    """Level (dB) and relative phase of the output harmonics for every swing in A_GRID."""
    key = (round(float(u0), 6), q, nh, pol, BETA, W_BROAD)
    if key not in _TABLES:
        th = 2 * np.pi * np.arange(M) / M
        u = u0 + A_GRID[:, None] * np.sin(th)[None, :]
        C = np.fft.rfft((1.0 + u * u) ** (-q) + BETA / (1.0 + (u / W_BROAD) ** 2), axis=1) / M
        n = np.arange(1, nh + 1)
        B = 2 * np.real(C[:, 1:nh + 1] * np.exp(1j * n * np.pi / 2)[None, :])
        lev = 20 * np.log10(np.maximum(np.abs(B) * n[None, :], 1e-12))
        neg = B < 0
        psi = np.pi / 2 - n[None, :] * np.pi / 2 + np.pi * neg - n[None, :] * np.pi * neg[:, :1]
        if pol < 0:
            psi = psi + (1 - n[None, :]) * np.pi
        _TABLES[key] = (lev, psi)
    return _TABLES[key]


def take_data(rec, nper=48):
    t, A = rec["t"], rec["amps"]
    nh = min(MAXH, A.shape[1])
    tot = np.sqrt((A[:, :8].astype(float) ** 2).sum(axis=1))
    last = len(t) - 1
    below = np.where(tot < tot.max() * 10 ** (-45 / 20))[0]
    below = below[below > np.argmax(tot)]
    if len(below):
        last = below[0]
    t0 = max(0.03, 0.75 * rec["win"])
    idx = np.unique(np.searchsorted(t, np.geomspace(t0, max(t[last], t0 * 1.5), nper)).clip(0, last))
    L = 20 * np.log10(np.maximum(A[idx, :nh].astype(float), 1e-9))
    ph = rec["phase"][idx, :nh].astype(float)
    n = np.arange(1, nh + 1)
    psi = ph - n[None, :] * ph[:, :1]
    Lall = 20 * np.log10(np.maximum(A[:, :nh].astype(float), 1e-9))
    floor = np.maximum(np.percentile(Lall, 3, axis=0) + 8.0, Lall.max() - 85.0)
    return dict(t=t[idx], L=L, psi=psi, floor=floor, nh=nh, f0=rec["f0"], layer=rec["layer"])


def chain_phase(data):
    """Phase rotation of the recording chain for one note: dth_n = c - n * d1 (n >= 2).

    Uses only the fact that corrected phases must be 0/180 deg (odd n) or +-90 deg (even n).
    """
    zs, ns = [], []
    for d in data:
        L, floor = d["L"], d["floor"]
        n = np.arange(1, d["nh"] + 1)
        good = (L > floor[None, :] + 14) & (L > L.max(axis=1, keepdims=True) - 40)
        good &= ((L[:, :1] > L.max(axis=1, keepdims=True) - 25) & (L[:, :1] > floor[0] + 14))
        good[:, 0] = False
        z = np.exp(2j * d["psi"]) * np.where(n % 2 == 0, -1.0, 1.0)[None, :]
        zs.append(z[good])
        ns.append(np.broadcast_to(n[None, :], L.shape)[good])
    z, n = np.concatenate(zs), np.concatenate(ns)
    if len(z) < 10:
        return 0.0, 0.0, 0.0
    best = (-1.0, 0.0, 0.0)
    for d1 in np.radians(np.arange(-40.0, 40.01, 0.5)):
        s = np.sum(z * np.exp(2j * n * d1))            # = sum exp(2i(psi' - (c - n d1))) * exp(2ic)
        if abs(s) > best[0]:
            best = (abs(s), np.angle(s) / 2, d1)
    return best[1], best[2], best[0] / len(z)


def frame_costs(d, lev, psi, gain, e, dth):
    L, floor = d["L"], d["floor"]
    Lc = np.maximum(L, floor[None, :])[:, None, :]
    valid = (L > floor[None, :] + 3)[:, None, :]
    ok_ref = ((L[:, 0] > L.max(axis=1) - 30) & (L[:, 0] > floor[0] + 10))[:, None, None]
    m = np.maximum(lev[None, :, :] + (e + gain)[None, None, :], floor[None, None, :])
    dl = np.abs(Lc - m)
    dl2 = np.where(dl <= HUBER, dl ** 2, 2 * HUBER * dl - HUBER ** 2)
    if BETA == 0.0:
        dl2[:, :, 0] = 0.0      # without the broad-field term the fundamental cannot be fitted: see fit_lambda()
    dp = np.angle(np.exp(1j * (d["psi"][:, None, :] - psi[None, :, :] - dth[None, None, :])))
    both = valid & (m > floor[None, None, :] + 3) & ok_ref
    return (dl2 + np.where(both, (SIGN_COST * dp / np.pi) ** 2, 0.0)).sum(axis=2)


def fit_take(d, u0, q, pol, e, dth, gain0=None):
    lev, psi = tables(u0, q, d["nh"], pol)
    best = None
    if gain0 is not None:
        starts, iters = [gain0], 3
    else:
        starts, iters = list(np.max(d["L"]) - np.max(lev + e[None, :]) + np.array([-10, 0, 10, 20, 32])), 5
    for g in starts:
        cost, ia = np.inf, None
        for _ in range(iters):
            tot = frame_costs(d, lev, psi, g, e, dth)
            ia = tot.argmin(axis=1)
            cost = tot[np.arange(len(ia)), ia].sum()
            pred = lev[ia] + e[None, :]
            ok = (d["L"] > d["floor"][None, :] + 3) & (pred + g > d["floor"][None, :] - 3)
            if BETA == 0.0:
                ok[:, 0] = False
            if ok.sum() < 8:
                break
            g_new = np.median((d["L"] - pred)[ok])
            if abs(g_new - g) < 0.05:
                break
            g = g_new
        if best is None or cost < best[0]:
            best = (cost, g, ia)
    return best


def scan_take(d, q, pol, e, dth):
    res = [None] * len(U0_GRID)
    for order in (range(len(U0_GRID)), range(len(U0_GRID) - 1, -1, -1)):
        g = None
        for i in order:
            r = fit_take(d, U0_GRID[i], q, pol, e, dth, gain0=g)
            g = r[1]
            if res[i] is None or r[0] < res[i][0]:
                res[i] = r
    for i in (0, len(U0_GRID) // 3, 2 * len(U0_GRID) // 3, len(U0_GRID) - 1):
        r = fit_take(d, U0_GRID[i], q, pol, e, dth)
        if r[0] < res[i][0]:
            res[i] = r
    return res


def quality(d, u0, q, pol, e, dth, g, ia):
    """Plain statistics of a fit: level error (dB rms) and share of wrong signs."""
    lev, psi = tables(u0, q, d["nh"], pol)
    pred = lev[ia] + e[None, :] + g
    ok = (d["L"] > d["floor"][None, :] + 6) & (pred > d["floor"][None, :] + 6) & (d["L"] > d["L"].max(axis=1, keepdims=True) - 45)
    if BETA == 0.0:
        ok[:, 0] = False
    err = (d["L"] - pred)[ok]
    dp = np.angle(np.exp(1j * (d["psi"] - psi[ia] - dth[None, :])))[ok]
    return err, np.abs(dp) > np.pi / 2, ok


def fit_note(args):
    name, recs, q, eq, per_take, beta, wb = args
    global BETA, W_BROAD
    BETA, W_BROAD = beta, wb
    data = sorted((take_data(r) for r in recs), key=lambda d: -d["layer"])
    f0 = float(np.mean([d["f0"] for d in data]))
    nh = max(d["nh"] for d in data)
    n = np.arange(1, nh + 1)
    c, d1, coh = chain_phase(data)
    dth_full = c - n * d1
    dth_full[0] = 0.0
    e_full = np.interp(np.log2(f0 * n), eq[0], eq[1]) if eq is not None else np.zeros(nh)
    out = None
    for pol in (1, -1):
        scans = [scan_take(d, q, pol, e_full[:d["nh"]], dth_full[:d["nh"]]) for d in data]
        if per_take:
            pick = [int(np.argmin([r[0] for r in sc])) for sc in scans]
        else:
            tot = np.sum([[r[0] for r in sc] for sc in scans], axis=0)
            pick = [int(np.argmin(tot))] * len(data)
        total = sum(sc[i][0] for sc, i in zip(scans, pick))
        if out is None or total < out["cost"]:
            takes, errs, wrong = [], [], []
            for d, sc, i in zip(data, scans, pick):
                cost, g, ia = sc[i]
                err, bad, ok = quality(d, U0_GRID[i], q, pol, e_full[:d["nh"]], dth_full[:d["nh"]], g, ia)
                errs.append(err)
                wrong.append(bad)
                lev, _ = tables(U0_GRID[i], q, d["nh"], pol)
                takes.append(dict(layer=d["layer"], u0=U0_GRID[i], gain=g, t=d["t"], a=A_GRID[ia],
                                  lev_rms=float(np.sqrt(np.mean(err ** 2))) if len(err) else 0.0,
                                  wrong=float(np.mean(bad)) if len(bad) else 0.0,
                                  resid=(d["L"] - lev[ia] - g), ok=ok, f0=d["f0"], nh=d["nh"]))
            errs, wrong = np.concatenate(errs), np.concatenate(wrong)
            out = dict(name=name, midi=recs[0]["midi"], f0=f0, q=q, pol=pol, cost=total, takes=takes,
                       c=c, d1=d1, coh=coh, lev_rms=float(np.sqrt(np.mean(errs ** 2))), wrong=float(np.mean(wrong)))
    fit_lambda(out, data, e_full, q)
    return out


LAM_GRID = np.concatenate([-np.geomspace(4.0, 0.01, 60), [0.0], np.geomspace(0.01, 4.0, 60)])


def b1_table(u0, q):
    th = 2 * np.pi * np.arange(M) / M
    u = u0 + A_GRID[:, None] * np.sin(th)[None, :]
    C = np.fft.rfft((1.0 + u * u) ** (-q) + BETA / (1.0 + (u / W_BROAD) ** 2), axis=1) / M
    return 2 * np.real(C[:, 1] * 1j)


def fit_lambda(out, data, e_full, q):
    """Flux = bump + lam * u.  The linear term only adds to the fundamental: B1 -> B1 + lam * a."""
    best = None
    rows = []
    for d, tk in zip(data, out["takes"]):
        ia = np.searchsorted(A_GRID, tk["a"]).clip(0, len(A_GRID) - 1)
        b1 = b1_table(tk["u0"], q)[ia]
        ok = d["L"][:, 0] > d["floor"][0] + 10
        rows.append((d["L"][ok, 0] - tk["gain"] - e_full[0], b1[ok], tk["a"][ok]))
    for lam in LAM_GRID:
        err = np.concatenate([y - 20 * np.log10(np.maximum(np.abs(b + lam * a), 1e-9)) for y, b, a in rows])
        c = np.sum(np.where(np.abs(err) < 6, err ** 2, 12 * np.abs(err) - 36))
        if best is None or c < best[0]:
            best = (c, lam, float(np.sqrt(np.mean(np.clip(err, -30, 30) ** 2))))
    out["lam"], out["h1_rms"] = best[1], best[2]
    err0 = np.concatenate([y - 20 * np.log10(np.maximum(np.abs(b), 1e-9)) for y, b, a in rows])
    out["h1_rms_nolam"] = float(np.sqrt(np.mean(np.clip(err0, -30, 30) ** 2)))


def main():
    args = sys.argv[1:]
    q, eq, per_take, beta, wb = 2.0, None, False, 0.0, 3.0
    if "--broad" in args:
        i = args.index("--broad"); beta, wb = float(args[i + 1]), float(args[i + 2]); del args[i:i + 3]
    if "--q" in args:
        i = args.index("--q"); q = float(args[i + 1]); del args[i:i + 2]
    if "--eq" in args:
        i = args.index("--eq"); c = np.load(args[i + 1]); eq = (c["x"], c["db"]); del args[i:i + 2]
    if "--per-take" in args:
        args.remove("--per-take"); per_take = True
    meas = pickle.load(open(args[0], "rb"))
    want = set(args[2:])
    notes = {}
    for r in meas:
        if not want or r["name"] in want:
            notes.setdefault(r["name"], []).append(r)
    jobs = [(k, v, q, eq, per_take, beta, wb) for k, v in sorted(notes.items(), key=lambda kv: kv[1][0]["midi"])]
    with Pool(2) as pool:
        fits = pool.map(fit_note, jobs)
    pickle.dump(dict(q=q, eq=eq, fits=fits, beta=beta, wb=wb), open(args[1], "wb"))
    lr = np.sqrt(np.mean([f["lev_rms"] ** 2 for f in fits]))
    print(f"q = {q}; broad term beta {beta} width {wb}; per-take voicing: {per_take}; level error {lr:.2f} dB rms; wrong signs {100 * np.mean([f['wrong'] for f in fits]):.1f} %")
    print("note     f0 pol chain phase c/d1 (coh) | level rms (h2+), wrong signs | lam, h1 rms with/without | per take, soft..hard: u0 / gain dB / swing a at 50 ms > 1 s > 4 s / level rms")
    for f in fits:
        parts = []
        for tk in f["takes"]:
            t, a = tk["t"], tk["a"]
            pick = lambda tt: a[np.argmin(np.abs(t - tt))]
            parts.append(f"{tk['u0']:4.2f}/{tk['gain']:+5.1f}/{pick(0.05):4.2f}>{pick(1.0):4.2f}>{pick(4.0):4.2f}/{tk['lev_rms']:3.1f}")
        print(f"{f['name']:>3} {f['f0']:7.1f} {f['pol']:+d}  {np.degrees(f['c']):+5.0f}/{np.degrees(f['d1']):+5.1f} ({f['coh']:.2f}) | {f['lev_rms']:4.1f} dB {100 * f['wrong']:4.1f} % | {f['lam']:+5.2f} {f['h1_rms']:4.1f}/{f['h1_rms_nolam']:4.1f} | " + "  ".join(parts))


if __name__ == "__main__":
    main()

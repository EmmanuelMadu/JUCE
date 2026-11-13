#!/usr/bin/env python3
"""Compare the engine with the reference recordings, note by note and layer by layer.

Each reference take is replayed on the model at the velocity that gives the same
initial tine swing; harmonic levels over time are then measured on both in the
same way.  Reported: level error of the harmonics that matter (within 30 dB of
the strongest at that moment), and the fundamental's decay.

Usage: compare.py <meas.pkl> <notes.pkl> [note ...]
"""
import os
import pickle
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tests"))
import hk  # noqa: E402
from analyze import demod, estimate_f0  # noqa: E402

TIMES = [0.05, 0.12, 0.3, 0.6, 1.2, 2.5, 5.0]
NH = 16


def levels(x, fs, f0, on, times):
    periods = 4 * max(1, int(np.ceil(0.012 * f0 / 4)))
    win = int(round(periods * fs / f0))
    out = np.full((len(times), NH), -200.0)
    for n in range(1, NH + 1):
        if n * f0 > 0.45 * fs:
            break
        a, t = demod(x, fs, n * f0, win, max(1, win // 4), start=on)
        t = t - on / fs
        for i, tt in enumerate(times):
            if len(t) and tt <= t[-1]:
                out[i, n - 1] = 20 * np.log10(max(abs(a[np.argmin(np.abs(t - tt))]), 1e-10))
    return out


def main():
    meas = pickle.load(open(sys.argv[1], "rb"))
    info = pickle.load(open(sys.argv[2], "rb"))
    want = set(sys.argv[3:])
    fs = 44100
    rows = []
    print("note layer  vel   significant-harmonic error (dB rms) at t = " + " ".join(f"{t:g}s" for t in TIMES) + "  | all | fundamental drop 0.6->5 s: recording / model (dB)")
    for r in sorted(meas, key=lambda r: (r["midi"], -r["layer"])):
        if want and r["name"] not in want:
            continue
        ni = info["notes"][r["name"]]
        if r["layer"] not in ni["a0"]:
            continue
        vel = min(1.0, (ni["a0"][r["layer"]] / ni["a0max"]) ** (1.0 / info["gamma"]))
        # recording: levels at the reference times
        rec = np.full((len(TIMES), NH), -200.0)
        for i, tt in enumerate(TIMES):
            if tt <= r["t"][-1]:
                j = int(np.argmin(np.abs(r["t"] - tt)))
                k = min(NH, r["amps"].shape[1])
                rec[i, :k] = 20 * np.log10(np.maximum(r["amps"][j, :k], 1e-10))
        s = hk.Synth(fs)
        s.set("tune", 440.0)
        s.note_on(r["midi"], vel)
        _, _, d = s.render(int(fs * 6.5), block=512, dry=True)
        on = int(np.argmax(np.abs(d) > 0.02 * np.abs(d).max()))
        f0 = estimate_f0(d, fs, 440.0 * 2 ** ((r["midi"] - 69) / 12), on)
        mod = levels(d, fs, f0, on, TIMES)
        ok = (rec > rec.max(axis=1, keepdims=True) - 30) & (rec > -150) & (mod > -150)
        gain = np.median((rec - mod)[ok]) if ok.any() else 0.0
        err = np.where(ok, rec - mod - gain, np.nan)
        per_t = [np.sqrt(np.nanmean(e ** 2)) if np.isfinite(e).any() else np.nan for e in err]
        tot = np.sqrt(np.nanmean(err ** 2))
        drop_r = rec[3, 0] - rec[6, 0] if rec[6, 0] > -150 else np.nan
        drop_m = mod[3, 0] - mod[6, 0]
        rows.append((r["name"], r["layer"], tot))
        print(f"{r['name']:>3}  {r['layer']}    {vel:4.2f}  " + " ".join(f"{e:5.1f}" for e in per_t) + f"  | {tot:4.1f} | {drop_r:5.1f} / {drop_m:5.1f}")
    tots = np.array([x[2] for x in rows])
    print(f"overall: median {np.nanmedian(tots):.1f} dB, mean {np.nanmean(tots):.1f} dB over {len(rows)} takes")


if __name__ == "__main__":
    main()

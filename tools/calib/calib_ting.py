#!/usr/bin/env python3
"""Closed-loop check of the second bending mode ("ting"): level relative to the
fundamental, recording against engine, per note and dynamic.  Writes per-note
correction factors for make_tables.py.

Usage: calib_ting.py <meas.pkl> <notes.pkl> <corrections.json>
"""
import json
import os
import pickle
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tests"))
import hk  # noqa: E402
from analyze import demod  # noqa: E402


def ting(x, fs, f0, on, ratios):
    w2 = max(int(round(8 * fs / f0)), int(0.008 * fs))
    a1, t1 = demod(x, fs, f0, w2, max(1, w2 // 8), start=on, stop=on + int(0.5 * fs))
    ref = 20 * np.log10(abs(a1[np.argmin(np.abs(t1 - on / fs - 0.08))]) + 1e-12)
    best = -200.0
    for r in ratios:
        if abs(r - round(r)) < 0.1:
            continue
        a, _ = demod(x, fs, r * f0, w2, max(1, w2 // 8), start=on, stop=on + int(0.5 * fs))
        best = max(best, 20 * np.log10(np.abs(a).max() + 1e-12))
    return best - ref


def main():
    meas = pickle.load(open(sys.argv[1], "rb"))
    info = pickle.load(open(sys.argv[2], "rb"))
    old = json.load(open(sys.argv[3])) if os.path.exists(sys.argv[3]) else {}
    fs = 44100
    corr = {}
    print("ting level re fundamental, recording / model (dB), soft..hard")
    for name, ni in info["notes"].items():
        if "r2" not in ni:
            continue
        diffs, cells = [], []
        for r in sorted([r for r in meas if r["name"] == name], key=lambda r: -r["layer"]):
            ps = [p for p in r["partials"] if abs(p["ratio"] - ni["r2"]) < 0.1 and np.isfinite(p["decay_db_s"]) and p["decay_db_s"] > 8]
            if not ps or r["layer"] not in ni["a0"]:
                continue
            i = int(np.argmin(np.abs(r["t"] - 0.08)))
            rec = max(p["peak_db"] for p in ps) - 20 * np.log10(r["amps"][i, 0])
            vel = min(1.0, (ni["a0"][r["layer"]] / ni["a0max"]) ** (1 / info["gamma"]))
            s = hk.Synth(fs)
            s.note_on(r["midi"], vel)
            d = s.render(int(fs * 0.6), dry=True)[2]
            on = int(np.argmax(np.abs(d) > 0.02 * np.abs(d).max()))
            seg = d[on + int(0.05 * fs):on + int(0.45 * fs)]
            S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 19))
            fr = np.fft.rfftfreq(1 << 19, 1 / fs)
            lo, hi = np.searchsorted(fr, [r["f0"] * 0.97, r["f0"] * 1.03])
            f0m = fr[lo + np.argmax(S[lo:hi])]
            mod = ting(d, fs, f0m, on, np.arange(ni["r2"] - 0.12, ni["r2"] + 0.121, 0.01))
            diffs.append(rec - mod)
            cells.append(f"{rec:5.1f}/{mod:5.1f}")
        if diffs:
            step = float(np.clip(np.median(diffs), -20, 20))
            corr[str(ni["midi"])] = old.get(str(ni["midi"]), 1.0) * 10 ** (step / 20)
            print(f"  {name:>3} (x{ni['r2']:.2f}): " + "  ".join(cells) + f"   median difference {np.median(diffs):+.1f} dB")
    json.dump(corr, open(sys.argv[3], "w"), indent=1)


if __name__ == "__main__":
    main()

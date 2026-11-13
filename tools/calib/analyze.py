#!/usr/bin/env python3
"""Measure reference electric-piano recordings.

For every recording (one note at one dynamic) this extracts
  * the fundamental frequency (and its drift over time),
  * the level of every harmonic over time (the harmonics are produced by the
    pickup, so they are exact multiples of the fundamental),
  * inharmonic partials of the attack (the higher bending modes of the tine).

Usage: analyze.py <folder with A_<midi>__<note>_<layer>.flac files> <out.pkl>

The recordings themselves are never copied; only measurements are stored.
"""
import os
import pickle
import re
import subprocess
import sys
from multiprocessing import Pool

import numpy as np

NAME = re.compile(r"A_(\d+)__([A-G]#?\d)_(\d)\.flac$")
MAX_HARM = 32


def load(path):
    fs = int(subprocess.run(
        ["ffprobe", "-hide_banner", "-loglevel", "error", "-show_entries", "stream=sample_rate",
         "-of", "default=nw=1:nk=1", path], capture_output=True, text=True).stdout.strip())
    raw = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", path,
                          "-f", "f64le", "-ac", "1", "-"], capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float64), fs


def demod(x, fs, f, win, hop, start=0, stop=None):
    """Complex amplitude of the component at frequency f, Hann-windowed frames."""
    stop = len(x) if stop is None else min(stop, len(x))
    n = np.arange(start, stop)
    z = x[start:stop] * np.exp(-2j * np.pi * f * n / fs)
    w = np.hanning(win + 2)[1:-1]
    nfr = max(0, (len(z) - win) // hop + 1)
    if nfr == 0:
        return np.zeros(0, complex), np.zeros(0)
    idx = np.arange(win)[None, :] + hop * np.arange(nfr)[:, None]
    amp = (z[idx] * w).sum(axis=1) * (2.0 / w.sum())
    t = (start + hop * np.arange(nfr) + win / 2.0) / fs
    return amp, t


def estimate_f0(x, fs, fnom, on):
    length = (len(x) - on) / fs
    t0, t1 = 0.25, min(1.75, max(0.6, length * 0.5))
    seg = x[on + int(t0 * fs): on + int(t1 * fs)]
    n = len(seg)
    w = np.hanning(n)
    t = np.arange(n) / fs
    best = (-1.0, fnom)
    for cents in np.arange(-70.0, 70.01, 1.0):
        f = fnom * 2 ** (cents / 1200)
        s = sum(abs(np.sum(seg * w * np.exp(-2j * np.pi * k * f * t)))
                for k in range(1, 5) if k * f < 0.45 * fs)
        if s > best[0]:
            best = (s, f)
    f = best[1]
    # refine from the phase slope of the strongest low harmonic
    for _ in range(3):
        win = int(round(4 * fs / f))
        best_k, best_a, best_df = 1, -1.0, 0.0
        for k in (1, 2, 3):
            if k * f > 0.45 * fs:
                continue
            a, tt = demod(seg, fs, k * f, win, max(1, win // 4))
            if len(a) < 8:
                continue
            ph = np.unwrap(np.angle(a))
            wgt = np.abs(a)
            slope = np.polyfit(tt, ph, 1, w=wgt)[0]
            if np.mean(np.abs(a)) > best_a:
                best_k, best_a, best_df = k, np.mean(np.abs(a)), slope / (2 * np.pi) / k
        f += best_df
    return f


def analyse(path):
    m = NAME.search(os.path.basename(path))
    midi, name, layer = int(m.group(1)), m.group(2), int(m.group(3))
    x, fs = load(path)
    peak = np.abs(x).max()
    on = int(np.argmax(np.abs(x) > 0.02 * peak))
    on = max(0, on - int(0.0005 * fs))
    fnom = 440.0 * 2 ** ((midi - 69) / 12)
    f0 = estimate_f0(x, fs, fnom, on)

    # harmonic envelopes: window = whole number of periods (nulls on the neighbours)
    periods = 4 * max(1, int(np.ceil(0.012 * f0 / 4)))
    win = int(round(periods * fs / f0))
    hop = max(1, win // 4)
    nh = int(min(MAX_HARM, np.floor(0.46 * fs / f0)))
    amps = []
    for k in range(1, nh + 1):
        a, t = demod(x, fs, k * f0, win, hop, start=on)
        amps.append(a)
    amps = np.array(amps).T            # frames x harmonics (complex)
    t = t - on / fs
    ph1 = np.unwrap(np.angle(amps[:, 0]))
    finst = f0 + np.gradient(ph1, t) / (2 * np.pi) if len(t) > 3 else np.full(len(t), f0)

    # noise floor per harmonic: quietest stretch of the file, measured the same way
    tail = np.abs(amps[int(len(t) * 0.9):, :])
    floor = np.median(tail, axis=0) if len(tail) else np.zeros(nh)

    # inharmonic attack partials
    wl = int(max(0.10, 8.0 / f0) * fs)
    seg = x[on: on + wl]
    w = np.hanning(len(seg))
    nfft = 1 << 18
    spec = np.abs(np.fft.rfft(seg * w, nfft)) * 2 / w.sum()
    fr = np.fft.rfftfreq(nfft, 1 / fs)
    partials = []
    lo = np.searchsorted(fr, 1.5 * f0)
    hi = np.searchsorted(fr, min(0.46 * fs, 40 * f0))
    s = spec[lo:hi]
    if len(s) > 3:
        pk = np.where((s[1:-1] > s[:-2]) & (s[1:-1] >= s[2:]) & (s[1:-1] > spec.max() * 10 ** (-70 / 20)))[0] + 1
        cand = sorted(((s[i], fr[lo + i]) for i in pk), reverse=True)
        for a, f in cand:
            r = f / f0
            if abs(r - round(r)) < 0.13:
                continue                 # a pickup harmonic, not a bending mode
            if any(abs(f - p["freq"]) < 0.2 * f0 for p in partials):
                continue
            w2 = int(round(8 * fs / f0)) if f0 > 60 else int(round(4 * fs / f0))
            w2 = max(w2, int(0.008 * fs))
            pa, pt = demod(x, fs, f, w2, max(1, w2 // 8), start=on, stop=on + int(1.2 * fs))
            pt = pt - on / fs
            mag = np.abs(pa)
            if len(mag) < 6:
                continue
            i0 = int(np.argmax(mag))
            sel = np.arange(i0, len(mag))
            sel = sel[mag[sel] > mag[i0] * 10 ** (-30 / 20)]
            if len(sel) >= 4 and sel[-1] - sel[0] == len(sel) - 1:
                slope, icpt = np.polyfit(pt[sel], 20 * np.log10(mag[sel]), 1)
            else:
                slope, icpt = float("nan"), float("nan")
            partials.append(dict(freq=float(f), ratio=float(r), level_db=float(20 * np.log10(a)),
                                 peak_db=float(20 * np.log10(mag[i0])), t_peak=float(pt[i0]),
                                 decay_db_s=float(-slope), level0_db=float(icpt)))
            if len(partials) >= 8:
                break

    return dict(file=os.path.basename(path), midi=midi, name=name, layer=layer, fs=fs, f0=f0,
                cents=1200 * np.log2(f0 / fnom), peak=peak, onset=on, length=(len(x) - on) / fs,
                win=win / fs, t=t, amps=np.abs(amps).astype(np.float32),
                phase=np.angle(amps).astype(np.float32), finst=finst.astype(np.float32),
                floor=floor.astype(np.float32), partials=partials)


def main():
    folder, out = sys.argv[1], sys.argv[2]
    files = sorted(os.path.join(folder, f) for f in os.listdir(folder) if NAME.search(f))
    with Pool(2) as pool:
        res = pool.map(analyse, files)
    with open(out, "wb") as fh:
        pickle.dump(res, fh)
    print(f"{len(res)} recordings measured -> {out}")
    print("note layer   f0(Hz)  cents  harm  len(s)  inharmonic partials (ratio:level dB:decay dB/s)")
    for r in sorted(res, key=lambda r: (r["midi"], -r["layer"])):
        ps = " ".join(f"{p['ratio']:.2f}:{p['peak_db']:.0f}:{p['decay_db_s']:.0f}" for p in r["partials"][:4])
        print(f"{r['name']:>3} {r['layer']}  {r['f0']:9.3f} {r['cents']:+6.1f}  {r['amps'].shape[1]:3d}  {r['length']:5.1f}  {ps}")


if __name__ == "__main__":
    main()

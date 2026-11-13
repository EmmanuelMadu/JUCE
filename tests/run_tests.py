#!/usr/bin/env python3
"""Engine tests.  Usage: run_tests.py [test name ...]   (needs build/test/libhk.so)"""
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hk  # noqa: E402

FS = 48000
results = []


def check(name, ok, detail=""):
    results.append((name, bool(ok)))
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f"  ({detail})" if detail else ""))


def f0_of(x, fs, guess, t0=0.5, t1=1.5):
    seg = x[int(t0 * fs):int(t1 * fs)]
    w = np.hanning(len(seg))
    n = 1 << 21
    spec = np.abs(np.fft.rfft(seg * w, n))
    fr = np.fft.rfftfreq(n, 1 / fs)
    lo, hi = np.searchsorted(fr, [guess * 0.96, guess * 1.04])
    k = lo + int(np.argmax(spec[lo:hi]))
    a, b, c = np.log(spec[k - 1] + 1e-30), np.log(spec[k] + 1e-30), np.log(spec[k + 1] + 1e-30)
    return fr[k] + 0.5 * (a - c) / (a - 2 * b + c) * (fr[1] - fr[0])


def harmonics(x, fs, f0, t0, t1, nh=6):
    seg = x[int(t0 * fs):int(t1 * fs)]
    w = np.hanning(len(seg))
    t = np.arange(len(seg)) / fs
    return np.array([20 * np.log10(abs(np.sum(seg * w * np.exp(-2j * np.pi * n * f0 * t))) / (w.sum() / 2) + 1e-15) for n in range(1, nh + 1)])


def play(events, total, fs=FS, block=512, setup=None, dry=False):
    """events: list of (sample, kind, a, b); kind in on/off/pedal/param.  Blocks are split at event times."""
    s = hk.Synth(fs)
    if setup:
        setup(s)
    events = sorted(events, key=lambda e: e[0])
    outs, pos, i = [], 0, 0
    while pos < total:
        while i < len(events) and events[i][0] <= pos:
            _, kind, a, b = events[i]
            if kind == "on": s.note_on(a, b)
            elif kind == "off": s.note_off(a)
            elif kind == "pedal": s.sustain(a)
            elif kind == "param": s.set(a, b)
            i += 1
        nxt = events[i][0] if i < len(events) else total
        m = min(block, nxt - pos, total - pos)
        outs.append(s.render(m, dry=dry))
        pos += m
    return [np.concatenate([o[j] for o in outs]) for j in range(3 if dry else 2)]


def t_tuning():
    print("tuning of all 88 keys (detune off, soft strike)")
    worst = 0.0
    for note in range(21, 109):
        s = hk.Synth(FS)
        s.set("detune", 0.0)
        s.note_on(note, 0.15)
        _, _, d = s.render(int(FS * 1.2), dry=True)
        target = 440.0 * 2 ** ((note - 69) / 12)
        cents = 1200 * np.log2(f0_of(d, FS, target, 0.25, 1.15) / target)
        worst = max(worst, abs(cents))
    check("every key within 2.5 cents of equal temperament", worst < 2.5, f"worst {worst:.2f} cents")
    s = hk.Synth(FS); s.set("detune", 0.0); s.set("tune", 432.0); s.note_on(69, 0.15)
    _, _, d = s.render(int(FS * 1.2), dry=True)
    f = f0_of(d, FS, 432.0, 0.25, 1.15)
    check("tune control moves A4", abs(1200 * np.log2(f / 432.0)) < 1.0, f"{f:.2f} Hz")


def t_blocks():
    print("block-size independence")
    rng = np.random.default_rng(3)
    ev = []
    for k in range(60):
        t = int(rng.integers(0, FS * 3))
        note = int(rng.integers(30, 96))
        ev.append((t, "on", note, float(rng.uniform(0.1, 1.0))))
        ev.append((t + int(rng.integers(200, FS)), "off", note, 0))
    ev += [(int(FS * 0.7), "pedal", 1.0, 0), (int(FS * 2.2), "pedal", 0.0, 0), (int(FS * 1.1), "param", "tremdepth", 0.6),
           (int(FS * 1.5), "param", "chorus", 0.5), (int(FS * 1.9), "param", "amp", 1.0), (int(FS * 2.5), "param", "drive", 0.5)]
    ref = play(ev, FS * 4, block=4096)
    same = True
    for b in (1, 7, 64, 480, 1000):
        out = play(ev, FS * 4, block=b)
        same &= np.array_equal(out[0], ref[0]) and np.array_equal(out[1], ref[1])
    check("output identical for block sizes 1, 7, 64, 480, 1000, 4096", same)


def t_rates():
    print("sample-rate consistency")
    res = {}
    for fs in (44100, 48000, 88200, 96000):
        s = hk.Synth(fs); s.set("detune", 0.0); s.note_on(55, 0.9)
        _, _, d = s.render(int(fs * 2.2), dry=True)
        f0 = f0_of(d, fs, 196.0, 0.3, 1.3)
        res[fs] = (f0, harmonics(d, fs, f0, 0.25, 0.45), harmonics(d, fs, f0, 1.6, 2.0)[0])
    ref = res[48000]
    dev = max(np.max(np.abs(res[fs][1] - ref[1])) for fs in res)
    cents = max(abs(1200 * np.log2(res[fs][0] / ref[0])) for fs in res)
    dec = max(abs((res[fs][1][0] - res[fs][2]) - (ref[1][0] - ref[2])) for fs in res)
    check("harmonic levels agree across 44.1/48/88.2/96 kHz", dev < 0.6, f"max deviation {dev:.2f} dB")
    check("pitch agrees across rates", cents < 0.3, f"{cents:.3f} cents")
    check("decay agrees across rates", dec < 0.3, f"{dec:.2f} dB")


def t_dynamics():
    print("velocity response")
    ok, spans = True, []
    for note in (33, 45, 57, 69, 81, 93):
        rms = []
        for v in np.linspace(0.05, 1.0, 12):
            s = hk.Synth(FS); s.note_on(note, v)
            _, _, d = s.render(int(FS * 0.5), dry=True)
            rms.append(np.sqrt(np.mean(d ** 2)))
        ok &= bool(np.all(np.diff(rms) > 0))
        spans.append(20 * np.log10(rms[-1] / rms[0]))
    check("level rises monotonically with velocity on every tested key", ok)
    check("dynamic range between velocity 0.05 and 1.0 is 20-45 dB", all(20 < s < 45 for s in spans), " ".join(f"{s:.0f}" for s in spans))
    # brightness: harder strikes are richer in harmonics
    s1 = hk.Synth(FS); s1.note_on(48, 0.25); d1 = s1.render(int(FS * 0.6), dry=True)[2]
    s2 = hk.Synth(FS); s2.note_on(48, 1.0); d2 = s2.render(int(FS * 0.6), dry=True)[2]
    h1, h2 = harmonics(d1, FS, f0_of(d1, FS, 130.8, 0.1, 0.55), 0.1, 0.5, 8), harmonics(d2, FS, f0_of(d2, FS, 130.8, 0.1, 0.55), 0.1, 0.5, 8)
    check("hard strikes are brighter (bark)", (h2[2:].max() - h2[0]) > (h1[2:].max() - h1[0]) + 10,
          f"upper harmonics re fundamental: soft {h1[2:].max() - h1[0]:.0f} dB, hard {h2[2:].max() - h2[0]:.0f} dB")


def t_damper():
    print("dampers and pedal")
    def level(out, t0, t1):
        return 20 * np.log10(np.sqrt(np.mean(out[int(t0 * FS):int(t1 * FS)] ** 2)) + 1e-12)
    held = play([(0, "on", 57, 0.8)], FS * 3, dry=True)[2]
    rel = play([(0, "on", 57, 0.8), (FS, "off", 57, 0)], FS * 3, dry=True)[2]
    ped = play([(0, "on", 57, 0.8), (FS // 2, "pedal", 1.0, 0), (FS, "off", 57, 0)], FS * 3, dry=True)[2]
    pedup = play([(0, "on", 57, 0.8), (FS // 2, "pedal", 1.0, 0), (FS, "off", 57, 0), (2 * FS, "pedal", 0.0, 0)], FS * 3, dry=True)[2]
    half = play([(0, "on", 57, 0.8), (FS // 2, "pedal", 0.45, 0), (FS, "off", 57, 0)], FS * 3, dry=True)[2]
    check("released note is stopped by its damper", level(rel, 1.6, 1.8) < level(held, 1.6, 1.8) - 45, f"{level(rel, 1.6, 1.8) - level(held, 1.6, 1.8):.0f} dB after 0.6 s")
    check("pedal keeps the note ringing", abs(level(ped, 2.0, 2.5) - level(held, 2.0, 2.5)) < 0.5)
    check("lifting the pedal stops it", level(pedup, 2.6, 2.9) < level(held, 2.6, 2.9) - 45)
    check("half pedal damps more slowly than a full damper", level(rel, 1.3, 1.5) + 6 < level(half, 1.3, 1.5) < level(ped, 1.3, 1.5) - 3,
          f"full {level(rel, 1.3, 1.5):.0f}, half {level(half, 1.3, 1.5):.0f}, none {level(ped, 1.3, 1.5):.0f} dB")
    s = hk.Synth(FS); s.note_on(60, 0.9); s.render(FS // 2); s.note_off(60)
    for _ in range(80):
        s.render(FS // 2)
        if s.idle: break
    l, r = s.render(4800)
    check("engine goes idle and exactly silent after a note ends", s.idle and not np.any(l) and not np.any(r))


def t_restrike():
    print("repeated strikes")
    ev = [(int(i * 0.06 * FS), "on", 45, 1.0) for i in range(60)]
    d = play(ev, int(FS * 5), dry=True)[2]
    one = play([(0, "on", 45, 1.0)], FS, dry=True)[2]
    check("60 fast full-force strikes on one key stay bounded", np.all(np.isfinite(d)) and np.abs(d).max() < 4 * np.abs(one).max(),
          f"peak {np.abs(d).max() / np.abs(one).max():.2f} x a single strike")
    a = play([(0, "on", 57, 0.7), (int(0.2000 * FS), "on", 57, 0.7)], FS, dry=True)[2]
    b = play([(0, "on", 57, 0.7), (int(0.2023 * FS), "on", 57, 0.7)], FS, dry=True)[2]
    ra, rb = np.sqrt(np.mean(a[int(0.5 * FS):] ** 2)), np.sqrt(np.mean(b[int(0.5 * FS):] ** 2))
    check("a second strike interacts with the tine's motion (result depends on timing)", abs(20 * np.log10(ra / rb)) > 0.5, f"{20 * np.log10(ra / rb):+.1f} dB for half a period's difference")


def t_stress():
    print("stress: random notes, pedal and parameter changes")
    rng = np.random.default_rng(11)
    s = hk.Synth(FS)
    peak, bad = 0.0, False
    t0 = time.time()
    for blk in range(1500):
        for _ in range(rng.integers(0, 4)):
            n = int(rng.integers(15, 115))
            if rng.random() < 0.6: s.note_on(n, float(rng.random()))
            else: s.note_off(n)
        if rng.random() < 0.05: s.sustain(float(rng.random()))
        if rng.random() < 0.2:
            i = int(rng.integers(0, hk.NUM_PARAMS))
            lo, hi, _ = hk.PARAM_RANGE[i]
            s.set(i, float(rng.uniform(lo, hi)))
        l, r = s.render(int(rng.integers(1, 700)))
        bad |= not (np.all(np.isfinite(l)) and np.all(np.isfinite(r)))
        peak = max(peak, float(np.abs(l).max(initial=0)), float(np.abs(r).max(initial=0)))
    check("no NaN or infinity", not bad)
    check("output never exceeds full scale", peak <= 1.0, f"peak {peak:.3f}")
    s.all_notes_off(); s.sustain(0.0)
    for _ in range(60):
        s.render(FS)
        if s.idle: break
    check("returns to idle after all notes off", s.idle)


def t_presets():
    print("presets")
    levels = []
    for i, name in enumerate(hk.PRESET_NAMES):
        s = hk.Synth(FS); s.load_preset(i)
        for n in (48, 55, 60, 64, 67): s.note_on(n, 0.8)
        l, r = s.render(FS * 2)
        pk = max(np.abs(l).max(), np.abs(r).max())
        rms = np.sqrt(np.mean(l[FS // 10:FS] ** 2 + r[FS // 10:FS] ** 2) / 2)
        levels.append((name, 20 * np.log10(pk), 20 * np.log10(rms)))
    print("     " + "; ".join(f"{n}: peak {p:.1f}, rms {r:.1f}" for n, p, r in levels))
    check("no preset clips on a firm five-note chord", all(p < -1.0 for _, p, _ in levels))
    r = [x[2] for x in levels]
    check("preset loudness within a 7 dB window", max(r) - min(r) < 7.0, f"{min(r):.1f} .. {max(r):.1f} dB")


def t_latency():
    print("latency and effects sanity")
    s = hk.Synth(FS)
    for name, v in (("reverb", 0.0), ("chorus", 0.0), ("phaser", 0.0), ("tremdepth", 0.0), ("drive", 0.0), ("amp", 0.0)):
        s.set(name, v)
    s.note_on(93, 0.9)          # a high note: the 12 Hz DC blocker's phase lead is then far below one sample
    l, r, d = s.render(FS, dry=True)
    lag = int(np.argmax(np.correlate(l[:8000].astype(float), d[:7900], "valid")))
    check("reported latency matches the signal path", lag == s.latency, f"measured {lag}, reported {s.latency}")
    check("direct path is mono-compatible and symmetric", np.allclose(l, r))
    s2 = hk.Synth(FS); s2.set("tremdepth", 1.0); s2.set("tremstereo", 1.0); s2.set("reverb", 0.0); s2.note_on(69, 0.5)
    l2, r2 = s2.render(FS)
    env_l = np.abs(l2[FS // 4:]).reshape(-1, 480).max(axis=1); env_r = np.abs(r2[FS // 4:]).reshape(-1, 480).max(axis=1)
    check("stereo tremolo moves the two channels in opposition", np.corrcoef(env_l, env_r)[0, 1] < -0.5, f"correlation {np.corrcoef(env_l, env_r)[0, 1]:.2f}")


def t_alias():
    print("aliasing with heavy drive")
    s = hk.Synth(FS); s.set("drive", 1.0); s.set("reverb", 0.0); s.set("detune", 0.0); s.note_on(96, 1.0)
    l, _ = s.render(FS)
    seg = l[2000:2000 + 32768].astype(float) * np.hanning(32768)
    spec = 20 * np.log10(np.abs(np.fft.rfft(seg)) + 1e-12); spec -= spec.max()
    fr = np.fft.rfftfreq(32768, 1 / FS)
    f0 = fr[np.argmax(spec)]
    mask = np.ones(len(fr), bool)
    for k in range(1, 12):
        mask &= np.abs(fr - k * f0) > 60
    mask &= fr > 100
    worst = spec[mask].max()
    check("non-harmonic (aliased) components at least 55 dB below the note", worst < -55, f"{worst:.0f} dB")


def t_speed():
    print("speed")
    s = hk.Synth(FS); s.load_preset(2); s.sustain(1.0)
    for n in range(36, 84, 2): s.note_on(n, 0.8)
    t0 = time.time(); s.render(FS * 10, block=256); dt = time.time() - t0
    check("24 ringing notes with effects render faster than 15x real time", 10 / dt > 15, f"{10 / dt:.0f}x real time, {s.active_keys} keys active")


TESTS = dict(tuning=t_tuning, blocks=t_blocks, rates=t_rates, dynamics=t_dynamics, damper=t_damper, restrike=t_restrike,
             stress=t_stress, presets=t_presets, latency=t_latency, alias=t_alias, speed=t_speed)

if __name__ == "__main__":
    for name in (sys.argv[1:] or TESTS):
        TESTS[name]()
    failed = [n for n, ok in results if not ok]
    print(f"\n{len(results) - len(failed)} of {len(results)} checks passed" + (f"; FAILED: {failed}" if failed else ""))
    sys.exit(1 if failed else 0)

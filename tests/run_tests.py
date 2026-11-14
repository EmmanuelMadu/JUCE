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
            elif kind == "sost": s.sostenuto(a)
            elif kind == "soft": s.soft(a)
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
    for i, name in enumerate(hk.PRESET_NAMES[:hk.NUM_TINE_PRESETS]):
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


# ============================================================ the grand ====
GRAND = None          # set by have_grand()


def have_grand():
    global GRAND
    if GRAND is None:
        GRAND = bool(hk.GRAND_DIR) and hk.Synth(FS).load_grand()
        if hk.GRAND_DIR and not GRAND:
            check("the recordings in HK_GRAND_DIR can be loaded", False, hk.GRAND_DIR)
        elif not GRAND:
            print("  (grand piano recordings not found: set HK_GRAND_DIR to run these tests)")
    return GRAND


def grand_setup(**params):
    """Setup function: grand only, no reverb, no noises unless asked."""
    base = dict(tineson=0, grandon=1, reverb=0, gnoise=0)
    base.update(params)

    def setup(s):
        s.load_grand()
        for k, v in base.items():
            s.set(k, v)
    return setup


def rms_db(x, t0, t1, fs=FS):
    seg = np.asarray(x[int(t0 * fs):int(t1 * fs)], dtype=np.float64)
    return 10 * np.log10(np.mean(seg ** 2) + 1e-30)


def loud(l, r, t0=0.0, t1=0.2, fs=FS):
    """K-weighted loudness of a stretch of stereo signal, dB."""
    from scipy.signal import bilinear, lfilter
    z = np.stack([l, r], axis=1).astype(np.float64)
    if fs == 48000:
        b1, a1 = [1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585]
    else:       # high shelf +4 dB around 1.68 kHz, the same curve at another rate
        w, g = 2 * np.pi * 1681.97, 10 ** (3.99984 / 20)
        b1, a1 = bilinear([g, w * np.sqrt(g) / 0.7071752, w * w], [1.0, w / 0.7071752, w * w], fs)
    z = lfilter(b1, a1, z, axis=0)
    return -0.691 + 10 * np.log10((z[int(t0 * fs):int(t1 * fs)] ** 2).sum(axis=1).mean() + 1e-30)


def bright_db(x, fc=2000.0, fs=FS, t0=0.0, t1=0.3):
    from scipy.signal import lfilter
    k = np.tan(np.pi * fc / fs)
    seg = np.asarray(x[int(t0 * fs):int(t1 * fs)], dtype=np.float64)
    h = lfilter(np.array([1.0, -1.0]) / (1 + k), [1.0, (k - 1) / (k + 1)], seg)
    return 10 * np.log10(np.mean(h ** 2) / (np.mean(seg ** 2) + 1e-30) + 1e-30)


def read_pack(name, frames=None):
    """Samples of one recording of the pack as float (16-bit units)."""
    import wave
    with wave.open(os.path.join(hk.GRAND_DIR, name + ".wav"), "rb") as w:
        n = w.getnframes() if frames is None else min(frames, w.getnframes())
        return np.frombuffer(w.readframes(n), dtype="<i2").reshape(-1, 2).astype(np.float64)


def t_grand_pack():
    print("grand: recordings")
    if not have_grand(): return
    t0 = time.time()
    ok = bool(hk._lib.hk_grand_verify())
    check("all 641 recordings present and bit-identical to the pack this version was built for", ok, f"checked in {time.time() - t0:.1f} s")
    worst = -999.0
    s = hk.Synth(FS); s.load_grand(); s.grand_raw(True)
    for k, v in dict(tineson=0, grandon=1, reverb=0, gres=0, gnoise=0).items(): s.set(k, v)
    for key, vel in ((60, 1.0), (21, 0.6), (108, 0.9), (81, 0.3), (45, 0.45)):
        s.reset(); s.note_on(key, vel)
        out = s.render(FS, 256)[0][s.latency:].astype(np.float64)
        best = 0.0
        for layer in range(1, 17):
            ref = read_pack("n%03d_v%02d" % (key, layer), FS - s.latency)[:, 0]
            g = np.dot(out, ref) / np.dot(ref, ref)
            best = min(best, 10 * np.log10(np.mean((out - g * ref) ** 2) / np.mean(out ** 2) + 1e-30))
        worst = max(worst, best)
    check("played at its own pitch a recording comes out sample for sample", worst < -120, f"largest residual {worst:.0f} dB")
    # a key between two sampled notes: compare with a long reference interpolation of the recording
    s.reset(); s.note_on(61, 1.0)
    out = s.render(FS // 2, 256)[0][s.latency:].astype(np.float64)
    best = 0.0
    rate = 2 ** (1 / 12)
    pos = np.arange(len(out)) * rate
    base = np.floor(pos).astype(int)
    frac = pos - base
    for layer in range(1, 17):
        x = np.concatenate([np.zeros(40), read_pack("n060_v%02d" % layer, FS)[:, 0]])
        ref = np.zeros(len(out))
        for k in range(-31, 33):
            d = k - frac
            ref += x[base + k + 40] * np.sinc(d) * np.where(np.abs(d) < 32, np.i0(9.0 * np.sqrt(np.clip(1 - (d / 32) ** 2, 0, 1))) / np.i0(9.0), 0)
        g = np.dot(out, ref) / np.dot(ref, ref)
        best = min(best, 10 * np.log10(np.mean((out - g * ref) ** 2) / np.mean(out ** 2) + 1e-30))
    check("a key a semitone from its recording: error of the pitch shift below -60 dB", best < -60, f"{best:.0f} dB")


def t_grand_tuning():
    print("grand: tuning")
    if not have_grand(): return
    worst = 0.0
    for key in (21, 22, 36, 47, 59, 60, 61, 69, 76, 84, 96):
        out = play([(0, "on", key, 0.7)], FS * 2, setup=grand_setup(gres=0))
        # unison strings beat and shift single partials by a cent or two: the better of two partials counts
        dev = min(abs(1200 * np.log2(f0_of(out[0], FS, hk.grand_partial(key, n), 0.15, 1.8) / hk.grand_partial(key, n))) for n in (1, 2))
        worst = max(worst, dev)
    check("every string where the piano's own tuning has it (stretch included)", worst < 1.5, f"largest deviation {worst:.2f} cents")
    lo, hi = 1200 * np.log2(hk.grand_partial(21) / 27.5), 1200 * np.log2(hk.grand_partial(105) / 3520.0)
    check("the stretch of a real grand: flat in the bass, sharp in the treble", lo < -10 and hi > 20, f"A0 {lo:+.0f} cents, A7 {hi:+.0f} cents")
    out = play([(0, "on", 69, 0.7)], FS * 2, setup=grand_setup(gres=0, tune=432.0))
    f = f0_of(out[0], FS, 432.0, 0.2, 1.8)
    check("tune control moves the grand", abs(1200 * np.log2(f / (hk.grand_partial(69) * 432.0 / 440.0))) < 1.0, f"{f:.2f} Hz")
    f = []
    for on in (0, 1):
        out = play([(0, "on", 33, 0.7)], FS * 3, setup=lambda s, on=on: (s.load_grand(), s.set("grandon", on), s.set("balance", 0.0), s.set("reverb", 0)))
        f.append(f0_of(out[0], FS, 55.0, 0.5, 2.5))
    d = 1200 * np.log2(f[1] / f[0])
    want = 1200 * np.log2(hk.grand_partial(33) / 55.0)
    check("next to the grand the tines follow the piano's stretch", abs(d - want) < 2.5 and abs(d) > 3, f"A1 moves {d:+.1f} cents")


def t_grand_dynamics():
    print("grand: touch")
    if not have_grand(): return
    s = hk.Synth(FS); grand_setup(gres=0)(s)
    worst_step, worst_law, mono = 0.0, 0.0, True
    for key in (33, 60, 61, 88, 100):
        lv = []
        vels = list(range(4, 128, 3))
        for v in vels:
            s.reset(); s.note_on(key, v / 127.0)
            l, r = s.render(FS // 4, 512)
            lv.append(loud(l[s.latency:], r[s.latency:]))
        lv = np.array(lv); law = 40 * np.log10(np.array(vels) / 127.0)
        d = np.diff(lv) - np.diff(law)
        mono &= bool(np.all(np.diff(lv) > 0))
        worst_step = max(worst_step, float(np.abs(d).max()))
        worst_law = max(worst_law, float(np.abs((lv - lv[-1]) - (law - law[-1])).max()))
    check("loudness rises with every step of velocity", mono)
    check("no steps between the 16 recorded strengths", worst_step < 0.25, f"largest irregularity {worst_step:.2f} dB")
    check("loudness follows one smooth law over the whole range", worst_law < 0.5, f"largest deviation {worst_law:.2f} dB")
    # timbre: brighter with velocity, and no jump where the recording changes
    br = []
    for v in range(20, 128):
        s.reset(); s.note_on(48, v / 127.0)
        br.append(bright_db(s.render(FS // 3, 512)[0][s.latency:], 1500.0))
    br = np.array(br)
    check("harder strikes are brighter", br[-1] - br[0] > 1.5, f"{br[-1] - br[0]:.1f} dB more above 1.5 kHz from soft to hard")
    check("timbre moves without jumps", np.abs(np.diff(br)).max() < 0.6, f"largest step between neighbouring velocities {np.abs(np.diff(br)).max():.2f} dB")
    # hammer hardness independent of loudness
    lv, bb = [], []
    for h in (0.0, 0.25, 0.5, 0.75, 1.0):
        s.reset(); s.set("ghammer", h); s.note_on(48, 0.6)
        l, r = s.render(FS // 3, 512)
        lv.append(loud(l[s.latency:], r[s.latency:])); bb.append(bright_db(l[s.latency:], 1500.0))
    s.set("ghammer", 0.5)
    check("hammer knob leaves the loudness alone", max(lv) - min(lv) < 1.0, f"spread {max(lv) - min(lv):.2f} dB")
    check("hammer knob changes the felt: soft to hard", all(np.diff(bb) > 0) and bb[-1] - bb[0] > 2.5, " ".join(f"{b:.1f}" for b in bb) + " dB above 1.5 kHz")
    for fs in (44100, 96000):
        f, lv = [], []
        for rate in (FS, fs):
            out = play([(0, "on", 61, 0.7)], rate * 2, fs=rate, setup=grand_setup(gres=0))
            f.append(f0_of(out[0], rate, 277.18, 0.2, 1.8)); lv.append(loud(out[0][15:], out[1][15:], 0, 0.2, rate))
        check(f"same pitch and loudness at {fs} Hz", abs(1200 * np.log2(f[1] / f[0])) < 0.3 and abs(lv[1] - lv[0]) < 0.3,
              f"{1200 * np.log2(f[1] / f[0]):+.2f} cents, {lv[1] - lv[0]:+.2f} dB")


def t_grand_pedals():
    print("grand: dampers and pedals")
    if not have_grand(): return
    T = FS * 3
    off = int(0.5 * FS)
    su = grand_setup(gres=0)
    held = play([(0, "on", 60, 0.8)], T, setup=su)[0]
    rel = play([(0, "on", 60, 0.8), (off, "off", 60, 0)], T, setup=su)[0]
    ped = play([(0, "on", 60, 0.8), (off - 2000, "pedal", 1.0, 0), (off, "off", 60, 0)], T, setup=su)[0]
    half = play([(0, "on", 60, 0.8), (off - 2000, "pedal", 0.38, 0), (off, "off", 60, 0)], T, setup=su)[0]
    catch = play([(0, "on", 60, 0.8), (off, "off", 60, 0), (off + int(0.06 * FS), "pedal", 1.0, 0)], T, setup=su)[0]
    d = rms_db(rel, 1.0, 1.2) - rms_db(held, 1.0, 1.2)
    check("a released key is stopped by its damper", d < -40, f"{d:.0f} dB half a second after release")
    check("the sustain pedal keeps it ringing", abs(rms_db(ped, 1.5, 2.5) - rms_db(held, 1.5, 2.5)) < 0.3)
    h = rms_db(half, 0.8, 1.0) - rms_db(held, 0.8, 1.0)
    check("half pedal: the dampers brush the strings", -40 < h < -3, f"{h:.0f} dB after 0.3 s (free 0, fully damped {rms_db(rel, 0.8, 1.0) - rms_db(held, 0.8, 1.0):.0f})")
    c1, c2 = rms_db(catch, 1.0, 1.5) - rms_db(held, 1.0, 1.5), rms_db(catch, 2.0, 2.5) - rms_db(held, 2.0, 2.5)
    check("release and catch: the pedal saves what the damper had not yet stopped", -40 < c1 < -3 and abs(c1 - c2) < 2.0, f"{c1:.0f} dB below the held note, and staying there")
    # the damper takes the highs first
    from scipy.signal import butter, sosfilt
    sos = butter(4, 1500.0, "highpass", fs=FS, output="sos")
    hi_share = lambda x: rms_db(sosfilt(sos, x), 1.0, 1.5) - rms_db(x, 1.0, 1.5)
    check("a brushed string is left duller", hi_share(catch) < hi_share(held) - 4, f"partials above 1.5 kHz {hi_share(catch) - hi_share(held):.0f} dB weaker")
    lo = play([(0, "on", 30, 0.8), (off, "off", 30, 0)], T, setup=su)[0]
    hi = play([(0, "on", 84, 0.8), (off, "off", 84, 0)], T, setup=su)[0]
    def stop_time(x):
        e = np.array([rms_db(x, 0.5 + 0.01 * i, 0.52 + 0.01 * i) for i in range(100)])
        return 0.01 * int(np.argmax(e < e[0] - 30))
    check("bass dampers take longer than treble dampers", stop_time(lo) > 1.5 * stop_time(hi) > 0, f"{stop_time(lo) * 1000:.0f} ms against {stop_time(hi) * 1000:.0f} ms to fall 30 dB")
    top = play([(0, "on", 96, 0.8), (off, "off", 96, 0)], T, setup=su)[0]
    top_held = play([(0, "on", 96, 0.8)], T, setup=su)[0]
    check("the top strings have no dampers and ring on", abs(rms_db(top, 0.8, 1.2) - rms_db(top_held, 0.8, 1.2)) < 0.1)
    sost = play([(0, "on", 60, 0.8), (int(0.2 * FS), "sost", 1.0, 0), (int(0.3 * FS), "on", 64, 0.8), (off, "off", 60, 0), (off, "off", 64, 0)], T, setup=su)[0]
    both = play([(0, "on", 60, 0.8), (int(0.3 * FS), "on", 64, 0.8)], T, setup=su)[0]
    c4 = lambda x: harmonics(x, FS, 261.47, 1.2, 1.7, 1)[0]
    e4 = lambda x: harmonics(x, FS, 329.21, 1.2, 1.7, 1)[0]
    check("sostenuto holds the key that was down and no other", abs(c4(sost) - c4(both)) < 1.5 and e4(sost) < e4(both) - 30,
          f"C4 {c4(sost) - c4(both):+.1f} dB, E4 {e4(sost) - e4(both):+.0f} dB")
    soft = play([(0, "soft", 1.0, 0), (0, "on", 60, 0.7)], FS, setup=su)
    norm = play([(0, "on", 60, 0.7)], FS, setup=su)
    dl, db_ = loud(soft[0], soft[1]) - loud(norm[0], norm[1]), bright_db(soft[0], 1500.0) - bright_db(norm[0], 1500.0)
    check("soft pedal: quieter and mellower", -4 < dl < -1.5 and db_ < -0.3, f"{dl:.1f} dB, {db_:.1f} dB above 1.5 kHz")


def t_grand_release():
    print("grand: release tails and noises")
    if not have_grand(): return
    T, off = FS * 3, int(0.3 * FS)
    ev = [(0, "on", 48, 0.9), (off, "off", 48, 0)]
    dry = play(ev, T, setup=grand_setup(gres=0))[0]
    tail = play(ev, T, setup=grand_setup(gres=0.5))[0]
    d = rms_db(tail - dry, 0.5, 1.5)
    n = rms_db(play([(0, "on", 48, 0.9)], T, setup=grand_setup(gres=0))[0], 0.1, 0.3)
    check("after-ring of the damped strings follows a staccato note", -60 < d - n < -25, f"{d - n:.0f} dB below the note")
    soft = play([(0, "on", 48, 0.25), (off, "off", 48, 0)], T, setup=grand_setup(gres=0.5))[0] - play([(0, "on", 48, 0.25), (off, "off", 48, 0)], T, setup=grand_setup(gres=0))[0]
    check("the after-ring scales with the strike", rms_db(soft, 0.5, 1.5) < d - 12, f"soft strike: {rms_db(soft, 0.5, 1.5) - d:.0f} dB")
    late = [(0, "on", 48, 0.9), (2 * FS, "off", 48, 0)]
    dl = rms_db(play(late, T, setup=grand_setup(gres=0.5))[0] - play(late, T, setup=grand_setup(gres=0))[0], 2.2, 2.9)
    check("and with how far the note had died away", dl < d - 6, f"released after 2 s: {dl - d:.0f} dB")
    kn = play(ev, T, setup=grand_setup(gres=0, gnoise=0.5))[0] - dry
    check("the key coming up is heard", rms_db(kn, 0.3, 0.5) > -90 and rms_db(kn, 0.0, 0.29) < -200, f"{rms_db(kn, 0.3, 0.5) - n:.0f} dB below the note")
    fast = play([(1000, "pedal", 1.0, 0)], FS, setup=grand_setup(gnoise=0.5))[0]
    ramp = [(1000 + 480 * i, "pedal", i / 40.0, 0) for i in range(41)]          # 0.4 s for the whole travel
    slow = play(ramp, FS, setup=grand_setup(gnoise=0.5))[0]
    ramp2 = [(1000 + 48 * i, "pedal", i / 40.0, 0) for i in range(41)]          # 40 ms
    quick = play(ramp2, FS, setup=grand_setup(gnoise=0.5))[0]
    pk = lambda x: 20 * np.log10(np.abs(x).max() + 1e-30)
    check("pedal noise, louder for a faster foot", pk(slow) > -100 and pk(quick) > pk(slow) + 6 and abs(pk(fast) - pk(quick)) < 8,
          f"slow {pk(slow):.0f}, quick {pk(quick):.0f}, switch {pk(fast):.0f} dBFS")
    silent = play([(1000, "pedal", 1.0, 0), (20000, "pedal", 0.0, 0)], FS, setup=grand_setup(gnoise=0))[0]
    check("noises off: silence", not np.any(silent))


def t_grand_resonance():
    print("grand: sympathetic resonance")
    if not have_grand(): return
    T = FS * 5
    chord = [(100, "pedal", 1.0, 0)] + [(200, "on", n, 0.8) for n in (60, 64, 67)] + [(int(0.3 * FS), "off", n, 0) for n in (60, 64, 67)]
    d0 = play(chord, T, setup=grand_setup(gres=0))[0]
    d1 = play(chord, T, setup=grand_setup(gres=0.5))[0]
    r = d1 - d0
    a, b = rms_db(r, 0.4, 1.0) - rms_db(d0, 0.4, 1.0), rms_db(r, 2.0, 4.0) - rms_db(d0, 2.0, 4.0)
    check("pedal down: the free strings join in", -30 < a < -8, f"{a:.0f} dB below the chord at first")
    check("and bloom as the chord dies away", b > a + 2, f"{b:.0f} dB below it after 2 s")
    seg = r[FS:3 * FS] * np.hanning(2 * FS)
    sp = np.abs(np.fft.rfft(seg)); fr = np.fft.rfftfreq(len(seg), 1 / FS)
    part = np.array([hk.grand_partial(n, k) for n in (60, 64, 67) for k in range(1, 13)])
    near = np.min(np.abs(fr[1:, None] - part[None, :]) / fr[1:, None], axis=1) < 0.006
    share = np.sum(sp[1:][near] ** 2) / np.sum(sp[1:] ** 2)
    check("they ring at partials of the notes played", share > 0.9, f"{100 * share:.0f} % of the resonance within 0.6 % of a partial of the chord")
    up = chord + [(2 * FS, "pedal", 0.0, 0)]
    ru = play(up, T, setup=grand_setup(gres=0.5))[0] - play(up, T, setup=grand_setup(gres=0))[0]
    check("pedal up: the dampers silence them", rms_db(ru, 3.0, 4.0) < rms_db(r, 3.0, 4.0) - 30, f"{rms_db(ru, 3.0, 4.0) - rms_db(r, 3.0, 4.0):.0f} dB")
    # a held key answers a note an octave above (no pedal)
    ev = [(0, "on", 48, 0.2), (2 * FS, "on", 60, 0.9), (int(2.25 * FS), "off", 60, 0)]
    h = play(ev, T, setup=grand_setup(gres=0.5))[0] - play(ev, T, setup=grand_setup(gres=0))[0]
    ev2 = [(0, "on", 49, 0.2), (2 * FS, "on", 60, 0.9), (int(2.25 * FS), "off", 60, 0)]       # C#3 has no partial at C4
    h2 = play(ev2, T, setup=grand_setup(gres=0.5))[0] - play(ev2, T, setup=grand_setup(gres=0))[0]
    c = lambda x: harmonics(x, FS, 261.3, 3.0, 4.5, 1)[0]
    check("a held C3 picks up a staccato C4; a held C#3 does not", c(h) > c(h2) + 12, f"{c(h):.0f} dB against {c(h2):.0f} dB at C4's pitch")
    # one soundboard: the tines set the strings ringing
    def tines(ring, pedal=1.0, grand=0):
        def setup(s):
            s.load_grand()
            for k, v in dict(grandon=grand, reverb=0, tinering=ring, gnoise=0).items(): s.set(k, v)
        return play([(100, "pedal", pedal, 0), (200, "on", 69, 0.8), (int(0.6 * FS), "off", 69, 0)], T, setup=setup)
    base, ring = tines(1e-4)[0], tines(0.75)[0]
    st = ring - base
    lv = rms_db(st, 1.0, 2.0) - rms_db(base, 1.0, 2.0)
    check("tines only, pedal down: the piano's strings answer the tines", -25 < lv < -5, f"{lv:.0f} dB below the tine")
    seg = st[FS:3 * FS] * np.hanning(2 * FS)
    sp = np.abs(np.fft.rfft(seg)); fr = np.fft.rfftfreq(len(seg), 1 / FS)
    f = fr[np.argmax(sp)]
    check("at the pitch of the note", abs(1200 * np.log2(f / hk.grand_partial(69))) < 8, f"{f:.1f} Hz")
    slow = rms_db(st, 3.5, 4.5) - rms_db(st, 1.0, 2.0)
    check("with the decay of piano strings", -30 < slow < -6, f"{slow:.0f} dB in 2.5 s")
    nop = tines(0.75, pedal=0.0)[0] - tines(1e-4, pedal=0.0)[0]
    check("no pedal, key up: nothing rings on", rms_db(nop, 1.5, 2.5) < rms_db(st, 1.5, 2.5) - 40)
    off = tines(0.0)[0]
    s = hk.Synth(FS); s.set("reverb", 0); s.sustain(1.0); s.render(100); s.note_on(69, 0.8); a_ = s.render(int(0.6 * FS) - 200 + 100, 512)[0]
    check("tine ring at zero leaves the tine instrument exactly as it was", np.array_equal(off[200:200 + 5000], play([(100, "pedal", 1.0, 0), (200, "on", 69, 0.8)], T, setup=lambda s: s.set("reverb", 0))[0][200:200 + 5000]))


def t_grand_engine():
    print("grand: engine")
    if not have_grand(): return
    rng = np.random.default_rng(5)
    ev = []
    for k in range(50):
        t = int(rng.integers(0, FS * 3))
        note = int(rng.integers(24, 104))
        ev.append((t, "on", note, float(rng.uniform(0.05, 1.0))))
        ev.append((t + int(rng.integers(200, FS)), "off", note, 0))
    ev += [(int(FS * 0.5), "pedal", 1.0, 0), (int(FS * 1.4), "pedal", 0.45, 0), (int(FS * 1.6), "pedal", 0.0, 0), (int(FS * 2.0), "pedal", 0.8, 0),
           (int(FS * 0.9), "sost", 1.0, 0), (int(FS * 2.4), "sost", 0.0, 0), (int(FS * 1.2), "soft", 0.7, 0),
           (int(FS * 1.1), "param", "ghammer", 0.8), (int(FS * 1.7), "param", "gcomp", 0.6), (int(FS * 2.1), "param", "gwidth", 0.4),
           (int(FS * 2.6), "param", "balance", 0.7), (int(FS * 0.3), "param", "gtone", 0.7)]
    def setup(s):
        s.load_grand()
        for k, v in dict(grandon=1, tinering=0.6, gnoise=0.6).items(): s.set(k, v)
    ref = play(ev, FS * 4, block=4096, setup=setup)
    same = True
    for b in (1, 7, 64, 480, 1000):
        out = play(ev, FS * 4, block=b, setup=setup)
        same &= np.array_equal(out[0], ref[0]) and np.array_equal(out[1], ref[1])
    check("both instruments, pedals and resonance: identical for block sizes 1, 7, 64, 480, 1000, 4096", same)

    s = hk.Synth(FS); s.load_grand()
    peak, bad, voices = 0.0, False, 0
    for blk in range(1500):
        for _ in range(rng.integers(0, 5)):
            n = int(rng.integers(15, 115))
            if rng.random() < 0.65: s.note_on(n, float(rng.random()))
            else: s.note_off(n)
        if rng.random() < 0.08: s.sustain(float(rng.random()))
        if rng.random() < 0.02: s.sostenuto(float(rng.random()))
        if rng.random() < 0.02: s.soft(float(rng.random()))
        if rng.random() < 0.2:
            i = int(rng.integers(0, hk.NUM_PARAMS))
            lo, hi, _ = hk.PARAM_RANGE[i]
            s.set(i, float(rng.uniform(lo, hi)))
        if rng.random() < 0.3: s.set("grandon", 1)
        l, r = s.render(int(rng.integers(1, 700)))
        bad |= not (np.all(np.isfinite(l)) and np.all(np.isfinite(r)))
        peak = max(peak, float(np.abs(l).max(initial=0)), float(np.abs(r).max(initial=0)))
        voices = max(voices, s.grand_voices)
    check("stress: no NaN or infinity, output within full scale", not bad and peak <= 1.0, f"peak {peak:.3f}, up to {voices} voices")
    s.all_notes_off(); s.sustain(0.0); s.sostenuto(0.0)
    for _ in range(90):
        l, r = s.render(FS)
        if s.idle: break
    l, r = s.render(FS)
    check("returns to idle and exact silence", s.idle and not np.any(l) and not np.any(r), f"{s.grand_voices} voices, {s.ringing_strings} strings")

    s = hk.Synth(FS)                                    # no recordings attached
    s.set("grandon", 1); s.set("tineson", 0); s.note_on(60, 0.8)
    l, r = s.render(FS)
    t = hk.Synth(FS); t.note_on(60, 0.8)
    u = hk.Synth(FS); u.set("grandon", 1); u.set("balance", 0.5); u.note_on(60, 0.8)
    check("without the recordings the grand is silent and the tines play on", not np.any(l) and np.abs(u.render(FS)[0]).max() > 0.5 * np.abs(t.render(FS)[0]).max())
    a = hk.Synth(FS); a.load_grand(); a.set("tineson", 0); a.set("grandon", 1); a.set("balance", 0.0); a.note_on(60, 0.8)
    b = hk.Synth(FS); b.load_grand(); b.set("tineson", 0); b.set("grandon", 1); b.note_on(60, 0.8)
    c = hk.Synth(FS); c.load_grand(); c.set("grandon", 1); c.set("balance", 0.0); c.set("reverb", 0); c.note_on(60, 0.8)
    d = hk.Synth(FS); d.set("reverb", 0); d.set("tinering", 1e-4); d.note_on(60, 0.8)
    check("balance acts only when both instruments are on", np.array_equal(a.render(FS)[0], b.render(FS)[0])
          and np.allclose(c.render(FS)[0], d.render(FS)[0], atol=2e-4))

    levels = []
    for i, name in enumerate(hk.PRESET_NAMES):
        if i < hk.NUM_TINE_PRESETS: continue
        s = hk.Synth(FS); s.load_grand(); s.load_preset(i)
        for n in (48, 55, 60, 64, 67): s.note_on(n, 0.8)
        l, r = s.render(FS * 2, 512)
        levels.append((name, 20 * np.log10(max(np.abs(l).max(), np.abs(r).max())), loud(l, r, 0.1, 1.0), s.get("grandon"), s.get("tineson")))
    print("     " + "; ".join(f"{n}: peak {p:.1f}, loudness {k:.1f}" for n, p, k, _, _ in levels))
    check("no grand or duo preset clips on a firm five-note chord", all(p < -1.0 for _, p, _, _, _ in levels))
    g = [k for _, _, k, on, tn in levels if on > 0.5 and tn < 0.5]
    check("the grand's characters sit within 3 dB of each other", max(g) - min(g) < 3.0, f"{min(g):.1f} .. {max(g):.1f} dB")

    for name, notes, res in (("40 notes, pedal down, strings ringing", 40, 0.5), ("64 notes", 64, 0.5)):
        s = hk.Synth(FS); s.load_grand(); s.load_preset(hk.PRESET_NAMES.index("Grand Natural")); s.set("gres", res); s.sustain(1.0)
        for n in rng.choice(np.arange(28, 100), notes, replace=False): s.note_on(int(n), 0.7)
        s.render(FS // 2, 256)
        t0 = time.time(); s.render(FS * 4, 256); dt = time.time() - t0
        last = (4 / dt, s.grand_voices, s.ringing_strings)
        print(f"     {name}: {4 / dt:.1f}x real time ({s.grand_voices} voices, {s.ringing_strings} strings)")
    check("64 notes with every string ringing render faster than 4x real time", last[0] > 4, f"{last[0]:.1f}x")



TESTS = dict(tuning=t_tuning, blocks=t_blocks, rates=t_rates, dynamics=t_dynamics, damper=t_damper, restrike=t_restrike,
             stress=t_stress, presets=t_presets, latency=t_latency, alias=t_alias, speed=t_speed,
             grand_pack=t_grand_pack, grand_tuning=t_grand_tuning, grand_dynamics=t_grand_dynamics, grand_pedals=t_grand_pedals,
             grand_release=t_grand_release, grand_resonance=t_grand_resonance, grand_engine=t_grand_engine)

if __name__ == "__main__":
    for name in (sys.argv[1:] or TESTS):
        TESTS[name]()
    failed = [n for n, ok in results if not ok]
    print(f"\n{len(results) - len(failed)} of {len(results)} checks passed" + (f"; FAILED: {failed}" if failed else ""))
    sys.exit(1 if failed else 0)

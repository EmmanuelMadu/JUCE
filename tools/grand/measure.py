#!/usr/bin/env python3
"""Measure the Salamander Grand Piano V3 recordings.

  python3 -I measure.py <SalamanderGrandPiano dir> <out.pkl>

For every recording: length, peak, onset, noise, attack level, brightness,
decay envelope and the point where the note has sunk into the noise.  For
every sampled note: tuning, string stiffness (inharmonicity B) and the decay
rate of the lower partials.  Only measurements are written, no audio.
"""
import os, sys, re, pickle
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import soundfile as sf
from multiprocessing import Pool

NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
ENV_T = [0.05, 0.1, 0.2, 0.4, 0.8, 1.6, 3.2, 6.4, 12.8, 25.6]


def name_to_midi(nm):
    m = re.match(r'^([A-G]#?)(-?\d)$', nm)
    return NOTE_NAMES.index(m.group(1)) + 12 * (int(m.group(2)) + 1)


def midi_to_name(m):
    return NOTE_NAMES[m % 12] + str(m // 12 - 1)


def load(path):
    x, sr = sf.read(path, dtype='int32', always_2d=True)
    return x.astype(np.float64) / 2147483648.0, sr


def moving_rms(p, w):
    """p = instantaneous power, w = window in samples -> RMS, same length."""
    c = np.concatenate(([0.0], np.cumsum(p)))
    n = len(p)
    i1 = np.minimum(np.arange(n) + w, n)
    i0 = np.arange(n)
    return np.sqrt(np.maximum(c[i1] - c[i0], 0.0) / np.maximum(i1 - i0, 1))


def onepole_hp(x, sr, fc):
    # first-order high-pass (bilinear), applied per channel
    from scipy.signal import lfilter
    k = np.tan(np.pi * fc / sr)
    b = np.array([1.0, -1.0]) / (1.0 + k)
    a = np.array([1.0, (k - 1.0) / (k + 1.0)])
    return lfilter(b, a, x, axis=0)


def basic(path):
    x, sr = load(path)
    n = len(x)
    out = {'frames': n, 'sr': sr, 'ch': x.shape[1]}
    out['peak'] = float(np.abs(x).max())
    out['dc'] = [float(v) for v in x.mean(axis=0)]
    p = (x ** 2).mean(axis=1)
    e1 = moving_rms(p, int(0.001 * sr))
    pk = e1.max()
    ipk = int(e1.argmax())
    out['envpeak'] = float(pk)
    out['t_envpeak'] = ipk / sr
    # noise before the note (first 5 ms)
    npre = float(np.sqrt(p[:int(0.005 * sr)].mean()))
    out['noise_pre'] = npre
    out['noise_tail'] = float(np.sqrt(p[-int(0.05 * sr):].mean()))
    # onset: first crossing of -20 dB re envelope peak, then walk back
    thr = pk * 10 ** (-20 / 20)
    i20 = int(np.argmax(e1 > thr))
    floor = max(npre * 4.0, pk * 10 ** (-50 / 20))
    j = i20
    while j > 0 and e1[j] > floor:
        j -= 1
    out['onset'] = j
    out['i20'] = i20
    # levels over the first 300 ms after the onset
    a, b = j, min(n, j + int(0.3 * sr))
    out['rms300'] = float(np.sqrt(p[a:b].mean()))
    out['rmsL'] = float(np.sqrt((x[a:b, 0] ** 2).mean()))
    out['rmsR'] = float(np.sqrt((x[a:b, -1] ** 2).mean()))
    e10 = moving_rms(p, int(0.01 * sr))
    out['env10peak'] = float(e10.max())
    for fc in (1000.0, 2000.0, 4000.0):
        h = onepole_hp(x[a:b], sr, fc)
        out['hp%d' % int(fc)] = float(np.sqrt((h ** 2).mean()))
    # decay envelope
    env = []
    for t in ENV_T:
        i0, i1 = j + int(0.8 * t * sr), j + int(1.25 * t * sr)
        env.append(float(np.sqrt(p[i0:i1].mean())) if i1 <= n else float('nan'))
    out['env'] = env
    # where the note sinks into the noise
    e50 = moving_rms(p, int(0.05 * sr))
    out['env50peak'] = float(e50.max())
    for db in (50, 60, 66, 72):
        lim = e50.max() * 10 ** (-db / 20)
        idx = np.nonzero(e50 > lim)[0]
        out['end%d' % db] = int(idx[-1]) if len(idx) else 0
    return out


def find_peak(mag, freqs, f_lo, f_hi):
    i0 = int(np.searchsorted(freqs, f_lo))
    i1 = int(np.searchsorted(freqs, f_hi))
    if i1 <= i0 + 2:
        return None
    k = i0 + int(np.argmax(mag[i0:i1]))
    if k <= 0 or k >= len(mag) - 1:
        return None
    a, b, c = np.log(mag[k - 1] + 1e-30), np.log(mag[k] + 1e-30), np.log(mag[k + 1] + 1e-30)
    d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
    d = float(np.clip(d, -0.5, 0.5))
    df = freqs[1] - freqs[0]
    return freqs[k] + d * df, float(np.exp(b - 0.25 * (a - c) * d))


def fit_fb(ns, fs, w):
    ns, fs, w = np.asarray(ns, float), np.asarray(fs, float), np.asarray(w, float)
    y = (fs / ns) ** 2
    A = np.stack([np.ones_like(ns), ns ** 2], axis=1)
    W = np.sqrt(w)[:, None]
    coef, *_ = np.linalg.lstsq(A * W, y * W[:, 0], rcond=None)
    F2 = coef[0]
    B = coef[1] / F2
    return float(np.sqrt(F2)), float(B)


def partial_analysis(path, midi):
    x, sr = load(path)
    b = basic(path)
    m = x.mean(axis=1)
    fet = 440.0 * 2 ** ((midi - 69) / 12.0)
    dur = float(np.clip(300.0 / fet, 0.5, 4.0))
    i0 = b['onset'] + int(0.12 * sr)
    seg = m[i0:i0 + int(dur * sr)]
    if len(seg) < int(0.2 * sr):
        seg = m[b['onset']:]
    win = np.hanning(len(seg))
    nfft = 1 << int(np.ceil(np.log2(len(seg) * 8)))
    mag = np.abs(np.fft.rfft(seg * win, nfft)) / win.sum() * 2
    freqs = np.fft.rfftfreq(nfft, 1 / sr)
    top = mag.max()
    found = {}
    # stage 1: low partials near their equal-tempered places
    for n in range(1, 7):
        if n * fet * 1.04 > 0.45 * sr:
            break
        r = find_peak(mag, freqs, n * fet * 0.965, n * fet * 1.045)
        if r and r[1] > top * 10 ** (-45 / 20):
            found[n] = r
    if len(found) >= 2:
        ns = sorted(found)
        F, B = fit_fb(ns, [found[k][0] for k in ns], [found[k][1] ** 0.5 for k in ns])
    else:
        n1 = sorted(found)[0] if found else 1
        F = found[n1][0] / n1 if found else fet
        B = 0.0
    B = max(B, 0.0)
    nmax = int(min(48, (0.42 * sr) / fet))
    miss = 0
    for n in range(7, nmax + 1):
        fp = n * F * np.sqrt(1 + B * n * n)
        if fp > 0.45 * sr or fp > 11000:
            break
        r = find_peak(mag, freqs, fp - 0.3 * F, fp + 0.3 * F)
        if r and r[1] > top * 10 ** (-60 / 20):
            found[n] = r
            miss = 0
            ns = sorted(found)
            F, B = fit_fb(ns, [found[k][0] for k in ns], [found[k][1] ** 0.5 for k in ns])
            B = max(B, 0.0)
        else:
            miss += 1
            if miss > 4:
                break
    ns = sorted(found)
    # robust refit: drop outliers > 0.4 % from the fitted line
    if len(ns) >= 3:
        for _ in range(3):
            pred = np.array([k * F * np.sqrt(1 + B * k * k) for k in ns])
            act = np.array([found[k][0] for k in ns])
            ok = np.abs(act / pred - 1) < 0.004
            if ok.sum() >= 3 and ok.sum() < len(ns):
                ns = [k for k, o in zip(ns, ok) if o]
                F, B = fit_fb(ns, [found[k][0] for k in ns], [found[k][1] ** 0.5 for k in ns])
                B = max(B, 0.0)
            else:
                break
    f1 = F * np.sqrt(1 + B)
    res = {'F': F, 'B': B, 'f1': f1, 'cents': 1200 * np.log2(f1 / fet), 'npart': len(ns),
           'partials': {k: found[k] for k in ns}, 'dur': dur}
    # decay of the lower partials: short-time levels
    wlen = int(np.clip(40.0 / fet, 0.08, 0.6) * sr)
    hop = wlen // 2
    w = np.hanning(wlen)
    nf = 1 << int(np.ceil(np.log2(wlen * 4)))
    fr = np.fft.rfftfreq(nf, 1 / sr)
    start = b['onset'] + int(0.05 * sr)
    tmax = min(len(m) - wlen, start + int(8.0 * sr))
    times, levels = [], {k: [] for k in ns[:16]}
    for s in range(start, tmax, hop):
        sp = np.abs(np.fft.rfft(m[s:s + wlen] * w, nf)) / w.sum() * 2
        times.append((s - b['onset'] + wlen / 2) / sr)
        for k in levels:
            fk = found[k][0]
            j0, j1 = int(np.searchsorted(fr, fk - 0.25 * F)), int(np.searchsorted(fr, fk + 0.25 * F))
            levels[k].append(float(sp[j0:max(j1, j0 + 1)].max()))
    res['dec_t'] = times
    res['dec_lv'] = levels
    return res


def spectrum_peaks(path, t0, dur, nkeep=40):
    """Strongest spectral peaks of a recording segment (for the release-ring files)."""
    x, sr = load(path)
    b = basic(path)
    m = x.mean(axis=1)
    i0 = b['onset'] + int(t0 * sr)
    seg = m[i0:i0 + int(dur * sr)]
    if len(seg) < 256:
        return []
    w = np.hanning(len(seg))
    nfft = 1 << int(np.ceil(np.log2(len(seg) * 4)))
    mag = np.abs(np.fft.rfft(seg * w, nfft)) / w.sum() * 2
    fr = np.fft.rfftfreq(nfft, 1 / sr)
    pk = (mag[1:-1] > mag[:-2]) & (mag[1:-1] >= mag[2:])
    idx = np.nonzero(pk)[0] + 1
    idx = idx[np.argsort(mag[idx])[::-1][:nkeep]]
    return sorted((float(fr[i]), float(mag[i])) for i in idx)


def work(job):
    kind, path, extra = job
    try:
        if kind == 'basic':
            return (kind, os.path.basename(path), basic(path))
        if kind == 'part':
            return (kind, os.path.basename(path), partial_analysis(path, extra))
        if kind == 'spec':
            return (kind, os.path.basename(path), spectrum_peaks(path, *extra))
    except Exception as e:  # keep going, report
        return ('error', os.path.basename(path), repr(e))


def main():
    root, outp = sys.argv[1], sys.argv[2]
    sdir = os.path.join(root, 'Samples')
    files = sorted(f for f in os.listdir(sdir) if f.endswith('.flac'))
    jobs = [('basic', os.path.join(sdir, f), None) for f in files]
    roots = [21 + 3 * i for i in range(30)]
    for mnote in roots:
        for lay in (8, 12, 15):
            jobs.append(('part', os.path.join(sdir, '%sv%d.flac' % (midi_to_name(mnote), lay)), mnote))
    for f in files:
        if f.startswith('harm'):
            jobs.append(('spec', os.path.join(sdir, f), (0.05, 0.6)))
    res = {'basic': {}, 'part': {}, 'spec': {}, 'error': {}}
    with Pool(2) as pool:
        for i, r in enumerate(pool.imap_unordered(work, jobs, chunksize=4)):
            res[r[0]][r[1]] = r[2]
    # offsets from the SFZ data files
    offs = {}
    for lay in range(1, 17):
        txt = open(os.path.join(root, 'Data', 'vel_%02d.txt' % lay)).read()
        for mm in re.finditer(r'\$OFF(\d+)\s+(\d+)', txt):
            offs[(21 + 3 * (int(mm.group(1)) - 1), lay)] = int(mm.group(2))
    res['sfz_off'] = offs
    with open(outp, 'wb') as f:
        pickle.dump(res, f)
    print('files', len(files), 'basic', len(res['basic']), 'part', len(res['part']),
          'spec', len(res['spec']), 'errors', len(res['error']))
    for k, v in list(res['error'].items())[:10]:
        print('ERR', k, v)


if __name__ == '__main__':
    main()

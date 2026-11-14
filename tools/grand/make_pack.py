#!/usr/bin/env python3
"""Prepare the grand piano sample pack from the Salamander Grand Piano V3.

  make_pack.py analyse <Salamander dir> <analysis.pkl>
  make_pack.py plan    <analysis.pkl> <recipe.json>
  make_pack.py build   <Salamander dir> <recipe.json> <out dir> [--wav <dir>] [--check]

"analyse" and "plan" decide how every recording is trimmed and scaled; the
result is recipe.json, which is kept in the repository.  "build" applies the
recipe with exact arithmetic only (no library maths), so the pack comes out
bit-identical on every machine; --check compares it with the checksums in the
recipe.
"""
import os, sys, re, json, pickle
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import soundfile as sf

NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
ENV_T = [0.05, 0.1, 0.2, 0.4, 0.8, 1.6, 3.2, 6.4, 12.8, 25.6]
ROOTS = [21 + 3 * i for i in range(30)]
HARM_ROOTS = [21 + 3 * i for i in range(23)]
SOURCE_COMMIT = '3382bf9496bba2486f5ab0de55a264d1dfc38404'
PEAK_TARGET = 0.97


def midi_to_name(m):
    return NOTE_NAMES[m % 12] + str(m // 12 - 1)


def bright_corner(midi):
    f = 440.0 * 2 ** ((midi - 69) / 12.0)
    return float(min(max(4.0 * f, 1500.0), 6000.0))


def file_list():
    """(kind, source name, pack name, key/root, layer or variant)"""
    out = []
    for m in ROOTS:
        for k in range(1, 17):
            out.append(('note', '%sv%d' % (midi_to_name(m), k), 'n%03d_v%02d' % (m, k), m, k))
    for st, tag in (('L', 0), ('S', 1), ('V3', 2)):
        for m in HARM_ROOTS:
            out.append(('harm', 'harm%s%s' % (st, midi_to_name(m)), 'tail%d_%03d' % (tag, m), m, tag))
    for k in range(1, 89):
        out.append(('rel', 'rel%d' % k, 'rel_%03d' % (20 + k), 20 + k, 0))
    for i, nm in enumerate(('pedalD1', 'pedalD2', 'pedalU1', 'pedalU2')):
        out.append(('pedal', nm, nm.replace('pedalD', 'pedal_down').replace('pedalU', 'pedal_up'), 0, i))
    return out


def load24(path):
    x, sr = sf.read(path, dtype='int32', always_2d=True)
    assert sr == 48000 and x.shape[1] == 2, path
    return x >> 8          # 24-bit integers


def moving_rms(p, w):
    c = np.concatenate(([0.0], np.cumsum(p)))
    n = len(p)
    i0 = np.arange(n)
    i1 = np.minimum(i0 + w, n)
    return np.sqrt(np.maximum(c[i1] - c[i0], 0.0) / np.maximum(i1 - i0, 1))


def kweight(x, sr=48000):
    from scipy.signal import lfilter
    y = lfilter([1.53512485958697, -2.69169618940638, 1.19839281085285],
                [1.0, -1.69065929318241, 0.73248077421585], x, axis=0)
    return lfilter([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621], y, axis=0)


def hp1(x, sr, fc):
    from scipy.signal import lfilter
    k = np.tan(np.pi * fc / sr)
    return lfilter(np.array([1.0, -1.0]) / (1.0 + k), [1.0, (k - 1.0) / (k + 1.0)], x, axis=0)


def analyse_one(job):
    kind, src, dst, key, sub, root = job
    x = load24(os.path.join(root, 'Samples', src + '.flac')).astype(np.float64) / 8388608.0
    sr = 48000
    n = len(x)
    p = (x ** 2).mean(axis=1)
    e1 = moving_rms(p, 48)
    pk = e1.max()
    crit = {'note': -40.0, 'harm': -40.0, 'rel': -30.0, 'pedal': -40.0}[kind]
    lead = {'note': 48, 'harm': 48, 'rel': 48, 'pedal': 96}[kind]
    i_on = int(np.argmax(e1 > pk * 10 ** (crit / 20)))
    start = max(0, i_on - lead)
    i20 = int(np.argmax(e1 > pk * 10 ** (-20 / 20)))
    out = {'kind': kind, 'src': src, 'dst': dst, 'key': key, 'sub': sub, 'frames_src': n,
           'peak': float(np.abs(x).max()), 'start': start, 't20': (i20 - start) / sr,
           't_peak': (int(e1.argmax()) - start) / sr}
    a, b = start, min(n, start + int(0.3 * sr))
    out['rms300'] = float(np.sqrt(p[a:b].mean()))
    out['rmsL'] = float(np.sqrt((x[a:b, 0] ** 2).mean()))
    out['rmsR'] = float(np.sqrt((x[a:b, 1] ** 2).mean()))
    kw = kweight(x[max(0, start - 2400):min(n, start + int(1.05 * sr))])
    off = start - max(0, start - 2400)
    kp = (kw ** 2).sum(axis=1)
    out['lk200'] = float(-0.691 + 10 * np.log10(kp[off:off + int(0.2 * sr)].mean() + 1e-20))
    out['lk400'] = float(-0.691 + 10 * np.log10(kp[off:off + int(0.4 * sr)].mean() + 1e-20))
    out['lk1000'] = float(-0.691 + 10 * np.log10(kp[off:off + int(1.0 * sr)].mean() + 1e-20))
    if kind == 'note':
        h = hp1(x[a:b], sr, bright_corner(key))
        out['bright'] = float(10 * np.log10((h ** 2).mean() / (x[a:b] ** 2).mean()))
        env = []
        for t in ENV_T:
            i0, i1 = start + int(0.8 * t * sr), start + int(1.25 * t * sr)
            env.append(float(np.sqrt(p[i0:i1].mean())) if i1 <= n else float('nan'))
        out['env'] = env
    e50 = moving_rms(p, 2400)
    out['tail_rms'] = float(e50[max(0, n - 4800)])
    out['e50peak'] = float(e50.max())
    return out


def cmd_analyse(root, outp):
    from multiprocessing import Pool
    jobs = [f + (root,) for f in file_list()]
    with Pool(2) as pool:
        res = pool.map(analyse_one, jobs, chunksize=8)
    with open(outp, 'wb') as f:
        pickle.dump(res, f)
    print('analysed', len(res))


# --------------------------------------------------------------------------
def smoothstep(n):
    """n-point fade 0..1 made of exact arithmetic only (3t^2 - 2t^3)."""
    t = (np.arange(n, dtype=np.float64) + 0.5) / n
    return t * t * (3.0 - 2.0 * t)


def dither(n, seed):
    """Deterministic triangular dither in LSB units, exact integer maths."""
    i = np.arange(2 * n, dtype=np.uint64) + np.uint64(seed) * np.uint64(0x1000003)
    z = (i + np.uint64(0x9E3779B97F4A7C15)) & np.uint64(0xFFFFFFFFFFFFFFFF)
    z = (z ^ (z >> np.uint64(30))) * np.uint64(0xBF58476D1CE4E5B9)
    z = (z ^ (z >> np.uint64(27))) * np.uint64(0x94D049BB133111EB)
    z = z ^ (z >> np.uint64(31))
    u = (z >> np.uint64(40)).astype(np.float64) / 16777216.0      # 24 random bits, [0,1)
    return (u[:n] - u[n:]).reshape(-1, 1)                          # triangular in (-1,1)


def checksum(pcm):
    """Order-sensitive 64-bit checksum of int16 samples (same in C++)."""
    v = pcm.reshape(-1).astype(np.uint16).astype(np.uint64)
    i = np.arange(len(v), dtype=np.uint64)
    w = (i * np.uint64(0x9E3779B97F4A7C15) + np.uint64(0x632BE59BD9B4E019)) | np.uint64(1)
    return int((v * w).sum(dtype=np.uint64))


def render(root, e):
    x = load24(os.path.join(root, 'Samples', e['src'] + '.flac'))[e['start']:e['end']].astype(np.float64)
    y = x * e['gain']                               # 24-bit units -> 16-bit units
    fi, fo = e['fadein'], e['fadeout']
    if fi:
        y[:fi] *= smoothstep(fi)[:, None]
    if fo:
        y[-fo:] *= smoothstep(fo)[::-1][:, None]
    d = dither(len(y), e['seed'])
    d2 = dither(len(y), e['seed'] + 7919)
    y[:, 0:1] += d
    y[:, 1:2] += d2
    q = np.floor(y + 0.5)
    q = np.clip(q, -32768, 32767).astype(np.int16)
    return q


def build_one(job):
    root, e, outdir, wavdir = job
    q = render(root, e)
    sf.write(os.path.join(outdir, e['dst'] + '.flac'), q, 48000, subtype='PCM_16')
    if wavdir:
        sf.write(os.path.join(wavdir, e['dst'] + '.wav'), q, 48000, subtype='PCM_16')
    return e['dst'], len(q), checksum(q), int(np.abs(q.astype(np.int32)).max())


def cmd_build(root, recipe_path, outdir, wavdir, check):
    from multiprocessing import Pool
    rec = json.load(open(recipe_path))
    os.makedirs(outdir, exist_ok=True)
    if wavdir:
        os.makedirs(wavdir, exist_ok=True)
    jobs = [(root, e, outdir, wavdir) for e in rec['files']]
    with Pool(2) as pool:
        res = pool.map(build_one, jobs, chunksize=4)
    sums = {r[0]: '%016x' % r[2] for r in res}
    bad = 0
    if check:
        for e in rec['files']:
            if e.get('sum') != sums[e['dst']]:
                bad += 1
                print('CHECKSUM MISMATCH', e['dst'], e.get('sum'), sums[e['dst']])
        print('checked %d files, %d mismatches' % (len(res), bad))
    else:
        for e in rec['files']:
            e['sum'] = sums[e['dst']]
        json.dump(rec, open(recipe_path, 'w'), indent=0, separators=(',', ':'))
        print('built %d files; checksums written to the recipe' % len(res))
    tot = sum(r[1] for r in res)
    print('total frames %d = %.1f s, PCM16 %.2f GB, max peak %d' % (tot, tot / 48000, tot * 4 / 1e9, max(r[3] for r in res)))
    return 1 if bad else 0


def cmd_plan(pkl, recipe_path):
    A = pickle.load(open(pkl, 'rb'))
    files = []
    for i, a in enumerate(A):
        n = a['frames_src']
        start = a['start']
        end = n
        fadein = {'note': 48, 'harm': 96, 'rel': 24, 'pedal': 96}[a['kind']]
        fadeout = min(4800 if a['kind'] != 'rel' else 960, (end - start) // 4)
        # gain: 24-bit integer units -> 16-bit units with the peak at PEAK_TARGET
        gain = PEAK_TARGET * 32767.0 / (a['peak'] * 8388608.0)
        files.append({'src': a['src'], 'dst': a['dst'], 'kind': a['kind'], 'key': a['key'], 'sub': a['sub'],
                      'start': int(start), 'end': int(end), 'fadein': int(fadein), 'fadeout': int(fadeout),
                      'gain': float(gain), 'seed': i + 1})
    rec = {'version': 1, 'source': 'https://github.com/sfzinstruments/SalamanderGrandPiano',
           'source_commit': SOURCE_COMMIT, 'rate': 48000, 'files': files}
    json.dump(rec, open(recipe_path, 'w'), indent=0, separators=(',', ':'))
    print('planned', len(files))


if __name__ == '__main__':
    c = sys.argv[1]
    if c == 'analyse':
        cmd_analyse(sys.argv[2], sys.argv[3])
    elif c == 'plan':
        cmd_plan(sys.argv[2], sys.argv[3])
    elif c == 'build':
        wav = sys.argv[sys.argv.index('--wav') + 1] if '--wav' in sys.argv else None
        sys.exit(cmd_build(sys.argv[2], sys.argv[3], sys.argv[4], wav, '--check' in sys.argv))

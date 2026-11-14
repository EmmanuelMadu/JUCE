#!/usr/bin/env python3
import os, sys, pickle
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from measure import midi_to_name, ENV_T
R = pickle.load(open(sys.argv[1], 'rb'))
B = R['basic']
db = lambda v: 20 * np.log10(max(v, 1e-12))
roots = [21 + 3 * i for i in range(30)]
print('== natural attack level (dB FS rms, first 300 ms) per note (rows) and layer (cols 1..16)')
L = np.zeros((30, 16))
for i, m in enumerate(roots):
    for k in range(16):
        L[i, k] = db(B['%sv%d.flac' % (midi_to_name(m), k + 1)]['rms300'])
    print('%-4s' % midi_to_name(m), ' '.join('%5.1f' % v for v in L[i]), '| range %.1f' % (L[i, 15] - L[i, 0]),
          'nonmono', int((np.diff(L[i]) < 0).sum()))
print('mean step per layer:', ' '.join('%4.1f' % v for v in np.diff(L, axis=1).mean(axis=0)))
print('== brightness hp2000/rms (dB)')
for i, m in enumerate(roots):
    if i % 3 == 0:
        print('%-4s' % midi_to_name(m), ' '.join('%5.1f' % (db(B['%sv%d.flac' % (midi_to_name(m), k + 1)]['hp2000']) - L[i, k]) for k in range(16)))
print('== onset (ms), sfz offset (ms), time -20dB (ms rel onset), envpeak (ms rel onset): layers 1,4,8,12,16')
for i, m in enumerate(roots):
    if i % 3 == 0:
        s = []
        for k in (1, 4, 8, 12, 16):
            b = B['%sv%d.flac' % (midi_to_name(m), k)]
            s.append('%5.1f/%5.1f/%4.1f/%5.1f' % (b['onset'] / 48, R['sfz_off'][(m, k)] / 48, (b['i20'] - b['onset']) / 48, b['t_envpeak'] * 1000 - b['onset'] / 48))
        print('%-4s' % midi_to_name(m), '  '.join(s))
print('== duration s / end60 s / end66 s / noise_pre dBFS / noise_tail dBFS / peak dBFS : layers 1, 8, 16')
tot = {50: 0, 60: 0, 66: 0, 72: 0, 'full': 0}
for i, m in enumerate(roots):
    for k in range(1, 17):
        b = B['%sv%d.flac' % (midi_to_name(m), k)]
        for d in (50, 60, 66, 72):
            tot[d] += b['end%d' % d] - b['onset']
        tot['full'] += b['frames']
    if i % 3 == 0:
        s = []
        for k in (1, 8, 16):
            b = B['%sv%d.flac' % (midi_to_name(m), k)]
            s.append('%5.1f %5.1f %5.1f %6.1f %6.1f %6.1f' % (b['frames'] / 48000, b['end60'] / 48000, b['end66'] / 48000, db(b['noise_pre']), db(b['noise_tail']), db(b['peak'])))
        print('%-4s' % midi_to_name(m), ' | '.join(s))
print('total note seconds: full %.0f  to-50dB %.0f  to-60dB %.0f  to-66dB %.0f  to-72dB %.0f' % tuple(tot[k] / 48000 for k in ('full', 50, 60, 66, 72)))
print('PCM16 stereo GB: full %.2f, -60: %.2f, -66: %.2f' % (tot['full'] * 4 / 1e9, tot[60] * 4 / 1e9, tot[66] * 4 / 1e9))
print('== decay envelope (dB re attack) layer 12 at', ENV_T)
for i, m in enumerate(roots):
    if i % 3 == 0:
        b = B['%sv12.flac' % midi_to_name(m)]
        print('%-4s' % midi_to_name(m), ' '.join('%6.1f' % (db(v) - db(b['rms300'])) if v == v else '   -- ' for v in b['env']))
print('== tuning: cents (first partial vs ET), B, partials used: layers 8/12/15')
for i, m in enumerate(roots):
    s = []
    for k in (8, 12, 15):
        p = R['part']['%sv%d.flac' % (midi_to_name(m), k)]
        s.append('%+6.1f %.2e %2d' % (p['cents'], p['B'], p['npart']))
    print('%-4s' % midi_to_name(m), ' | '.join(s))

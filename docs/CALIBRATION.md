# How the model was calibrated

The reference is the 65 mono recordings of the jRhodes3d set (15 keys of a
1977 Mark I Stage 73, three to five dynamics each). The scripts are in
`tools/calib/`; the recordings are not stored here.

## What was measured

`analyze.py` extracts, for every recording, the fundamental frequency and the
level and phase of every harmonic over time, plus inharmonic partials of the
attack.

Three observations fix the structure of the model:

1. **The pickup output is the time derivative of a function of tine position.**
   Rebuilding the flux from the measured harmonics and plotting it against the
   tine's position gives the same curve on the upswing and the downswing.
   Equivalently, the phase of each harmonic relative to the fundamental only
   takes two values, and those signs flip exactly where harmonic levels pass
   through nulls as the note decays.
2. **The flux curve is a sharp bump on a broad one**: `(1+u^2)^-2 +
   3 / (1 + (u/4)^2)`, with `u` the tine tip's distance from the pickup axis
   in units of the pickup gap. The broad part is needed to explain why the
   fundamental of hard bass notes grows as the note decays.
3. **Decay speeds up with swing** in proportion to the swing, as aerodynamic
   drag would; one pair of constants per note reproduces the swing over time
   of all its dynamics to about 1 dB.

`fit_complex.py` then finds, per note, the pickup voicing (rest offset of the
tine) and the tine swing at every moment of every recording, using harmonic
levels and signs together. `make_tables.py` turns that into the 88-key table,
interpolating between measured keys with a small fixed key-to-key spread.

## How close the engine gets

`compare.py` replays every reference take on the finished engine at the
velocity that gives the same initial swing, and measures both the same way.
"Level error" is the rms difference, over time, of the harmonics within 30 dB
of the strongest one at that moment.

| Note | Level error per dynamic layer, soft to hard (dB rms) | Fundamental's drop from 0.6 s to 5 s: recording / model (dB) |
|---|---|---|
| F1 | 2.8, 9.9, 7.2, 6.1, 9.1 | -0.1 / 2.7 |
| B1 | 5.6, 7.1, 8.3, 6.0, 7.3 | 2.8 / 5.0 |
| E2 | 3.9, 3.5, 4.8, 4.4, 7.2 | 0.5 / 2.8 |
| A2 | 2.2, 6.9, 4.4, 4.7, 5.9 | 4.7 / 4.7 |
| D3 | 2.1, 3.8, 5.4, 3.3, 1.9 | 8.9 / 8.4 |
| G3 | 1.7, 2.0, 5.9, 3.9, 4.4 | 10.9 / 12.4 |
| B3 | 2.6, 4.0, 4.3, 6.2, 7.2 | 15.5 / 17.2 |
| D4 | 6.9, 4.5, 3.4, 3.3, 3.7 | 14.6 / 13.6 |
| F4 | 0.3, 1.6, 1.1, 1.3, 1.8 | 17.7 / 17.1 |
| B4 | 3.3, 2.5, 1.7, 1.5 | 21.2 / 20.9 |
| E5 | 6.8, 3.7, 2.3, 3.3 | 49.9 / 42.2 |
| A5 | 3.1, 0.5, 3.2 | 27.7 / 27.4 |
| D6 | 1.5, 0.8, 0.8 | 30.6 / 33.0 |
| G6 | 1.6, 1.2, 0.7 | note ends before 5 s |
| C7 | 7.8, 7.5, 8.8 | note ends before 5 s |

Over all 65 takes the median error is 3.7 dB (mean 4.1 dB); 35 % of takes are
within 3 dB and 68 % within 5 dB. The middle and upper registers are closest;
the lowest octave is the weakest.

## Known limits of the calibration

* The recordings were made with the owner's EQ (treble boost, low-mid scoop).
  A power-law tilt in that EQ cannot be told apart from pickup sharpness by
  levels alone, so the fit treats the recording chain as flat. The model's
  unprocessed tone is therefore as bright as those recordings.
* Takes of the same note do not always agree with each other (different
  voicing between sessions, and one note's takes differ in pitch by 27 cents),
  so some of the error is a limit of the reference, not of the model.
* The three highest sampled notes are too clean for pickup offset and swing
  to be separated; they use the trend of the notes below.
* Everything here is measurement. Nobody has listened to the result yet.

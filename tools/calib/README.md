# Calibration tools

These scripts turn recordings of a real tine piano into `src/dsp/KeyTable.cpp`.
They need Python 3 with NumPy, `ffmpeg`/`ffprobe`, and the engine built as a
shared library (`cmake --build build --target hk`, then `HK_LIB=build/libhk.so`).

The reference used so far is the mono folder of
<https://github.com/sfzinstruments/jlearman.jRhodes3d> (not included here).

```sh
S=/path/to/jRhodes3d-mono          # reference recordings
W=/tmp/calib                       # scratch folder for measurements

python3 analyze.py     $S $W/meas.pkl                     # 1. measure every recording
python3 fit_complex.py $W/meas.pkl $W/fit.pkl --broad 3 4 # 2. pickup voicing and tine swing per note
python3 make_tables.py $W/meas.pkl $W/fit.pkl ../../src/dsp/KeyTable.cpp $W/notes.pkl ting_corrections.json
                                                          # 3. write the 88-key table
python3 compare.py     $W/meas.pkl $W/notes.pkl           # 4. engine against recordings
python3 calib_ting.py  $W/meas.pkl $W/notes.pkl ting_corrections.json
                                                          # 5. (optional) re-level the attack "ting";
                                                          #    then repeat step 3 and rebuild
```

`eq_from_fit.py` and `chain_phase.py` are diagnostics for the recording chain
(EQ and phase); `pickup.py` and `fit_pickup.py` are the earlier levels-only fit,
kept for reference. See `docs/CALIBRATION.md` for what the fit found.

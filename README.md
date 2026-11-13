# Hearth Keys

A physically modelled tine electric piano (Rhodes-style) as a VST3 / Audio Unit
instrument and standalone app, built with JUCE.

**Status: first playable stage (v0.1).** The electric piano model, its effects
and the interface are in place and tested on Linux. The Mac build is produced
by the GitHub workflow in `.github/workflows/build.yml`.

## What makes the sound

Nothing is sampled. Each of the 88 keys has its own simulated sound generator:

| Part | Model |
|---|---|
| Hammer | A force pulse whose duration shortens the harder the key is struck. |
| Tine | The fundamental, the second bending mode (the short "ting") and the low thump of the tine/tonebar assembly on its mounting. Decay speeds up at large swing (air drag) and pitch drops slightly. |
| Damper | Extra damping while the key is up and the sustain pedal is not down, including half-pedalling. |
| Pickup | Output = rate of change of the magnetic flux through the coil, where the flux depends on where the tine tip is relative to the pickup: a sharp pole tip on top of the magnet's broad field. |

The pickup is the only nonlinear element and it is where the instrument's
"bark" comes from: a tine that swings across the pickup's axis produces a pulse
twice per cycle, so harder playing adds harmonics continuously rather than in
sampled steps. Because every key keeps its own tine state, re-striking a
ringing note interacts with the motion that is already there.

After the pickups: bass and treble controls, a four-stage phaser, an
oversampled drive stage with two cabinet voicings (suitcase and combo),
stereo tremolo, chorus and reverb.

## Where the numbers come from

The per-key constants in `src/dsp/KeyTable.cpp` were fitted to measurements of
recordings of a real 1977 Mark I Stage 73 (see `docs/CALIBRATION.md`): how far
each tine swings at five dynamics, how its harmonics rise and fall as it
decays, how fast it decays, how it is tuned, and how its pickup is voiced. The
recordings themselves are not part of this project and are not needed to build
or run it.

## Building

Requirements: CMake 3.22+, a C++17 compiler, and on Linux the usual JUCE
dependencies (ALSA, X11, FreeType, etc.). JUCE 9.0.5 is downloaded
automatically.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel --target HearthKeys_VST3 HearthKeys_Standalone
```

On macOS add `HearthKeys_AU` to the targets and, for a universal binary,
`"-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"` to the first command. Built plug-ins
are in `build/HearthKeys_artefacts/Release/`. See `docs/INSTALL.txt` for
installing a downloaded build on a Mac.

To use an existing JUCE checkout instead of downloading:
`-DHK_JUCE_PATH=/path/to/JUCE`.

## Tests

The sound engine (`src/dsp`) has no dependencies and is tested on its own:

```sh
cmake --build build --target hk          # the engine as a shared library
HK_LIB=build/libhk.so python3 tests/run_tests.py     # needs numpy
```

The tests cover tuning of all 88 keys, bit-identical output for any block
size, agreement across sample rates, velocity response, dampers and pedal,
repeated strikes, a randomised stress run, preset levels, latency, aliasing
and speed. `hk_snapshot` renders the interface to a PNG without a screen.

## Layout

```
src/dsp/       sound engine (TineBank, effects, Engine, generated KeyTable)
src/plugin/    JUCE plug-in wrapper and interface
tools/calib/   measurement and fitting scripts that produce KeyTable.cpp
tools/         snapshot tool
tests/         engine tests
```

## Names and identifiers

"Hearth Keys", the maker name "Homemade" and the plug-in codes (`Hmde` /
`HrKy`) are placeholders set in `CMakeLists.txt`. Change them before saving
projects with the plug-in: DAWs identify it by those codes.

## Licence

No licence has been chosen for this project's own code yet. Third-party
components and credits are listed in `NOTICE.md`.

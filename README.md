# Hearth Keys

Two keyboards in one instrument, as a VST3 / Audio Unit plug-in and standalone
app built with JUCE:

- **Tines**: a physically modelled tine electric piano (Rhodes-style);
- **Grand**: an acoustic grand built on recordings of a real piano, with the
  behaviour of the instrument modelled around them.

They can be played separately or together, and they share one soundboard:
the tines set the piano's strings ringing.

**Status: v0.2.** Both instruments, the effects and the interface are in
place and tested. The Mac build is produced, installed and tested by the
GitHub workflow in `.github/workflows/build.yml`.

## Installing on a Mac

Paste this line into Terminal (Applications > Utilities) and press Return. It
downloads the latest build, installs the plug-ins for the current user and
fetches the grand's recordings (about 270 MB, once):

```sh
cd ~/Downloads && curl -L -o HearthKeys-macOS.zip https://github.com/EmmanuelMadu/JUCE/releases/download/latest/HearthKeys-macOS.zip && rm -rf "Hearth Keys" && ditto -x -k HearthKeys-macOS.zip . && bash "Hearth Keys/install_mac.sh"
```

Then restart Logic: AU Instruments > Homemade > Hearth Keys. Details are in
`docs/INSTALL.txt`.

## The tines

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

## The grand

The grand plays recordings of a Yamaha C5 (the Salamander Grand Piano V3 by
Alexander Holm: 30 notes in minor thirds, 16 strike strengths each). What is
added around them:

| Part | How |
|---|---|
| Touch | Loudness follows the key velocity on one smooth curve. The recording whose natural loudness is nearest is used and levelled exactly, so there are no steps between the 16 strengths; a gentle shelf bridges the timbre between two neighbours. |
| Hammer | The timbre can be taken from a harder or softer strike than the loudness asks for: felt hardness independent of volume, not an equaliser. |
| Dampers | One per key up to E6 (the top strings have none, as on the piano). They follow the key, the sustain pedal at any position (half-pedalling), and the sostenuto pedal. A damper that lands takes the highs first; lifting it again catches what is left. Bass dampers are slower than treble dampers. |
| Release | The recorded after-ring of the damped strings, scaled to what the strings were doing, and the noise of the key coming up. Pedal noises scale with how fast the pedal moves. |
| Strings | Sympathetic resonance is modelled: every string that is free to move (held key, pedal, the undamped top) is a set of resonators at its partial frequencies as measured from the recordings, ringing with the measured decay times, driven by whatever reaches the soundboard. |
| Soft pedal | Softer strike and lower level. |

**One soundboard.** The same strings are also driven by the tines. With the
grand in play the tines are tuned to the piano's stretch, so a tine note meets
the string of its own key exactly; hold the pedal and the piano answers
underneath the electric piano. "Tine Ring" sets how much.

The recordings are not in this repository. `tools/grand/make_pack.py` rebuilds
the sample pack bit for bit from the published source (the workflow does this
once and publishes it as a release); the plug-in checks every sample of the
installed pack against checksums compiled into it. Without the pack the tines
work and the grand is silent.

## Where the numbers come from

`src/dsp/GrandTable.cpp` holds what was measured from the piano recordings:
loudness and brightness of every strike, decay, tuning of every sampled note,
and the frequencies of its first twenty partials (`tools/grand`).

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
HK_LIB=build/libhk.so python3 tests/run_tests.py     # needs numpy and scipy
```

For the tines the tests cover tuning of all 88 keys, bit-identical output for
any block size, agreement across sample rates, velocity response, dampers and
pedal, repeated strikes, a randomised stress run, preset levels, latency,
aliasing and speed.

For the grand (set `HK_GRAND_DIR` to an installed pack; skipped otherwise):
every recording against its checksum, sample-exact playback, the pitch shift
between sampled notes, tuning, stepless loudness and timbre, the hammer
control, dampers, half pedal, release-and-catch, sostenuto, soft pedal,
release tails and noises, sympathetic resonance (pedal and held keys), the
tines driving the strings, block-size independence, a stress run and speed.

`hk_snapshot` renders the interface to a PNG without a screen.

## Layout

```
src/dsp/       sound engine: TineBank (tines), GrandBank + GrandPack (grand),
               StringBank (ringing strings), effects, Engine, generated tables
src/plugin/    JUCE plug-in wrapper and interface
tools/calib/   measurement and fitting scripts that produce KeyTable.cpp
tools/grand/   measurement of the piano recordings, the pack recipe and builder
tools/         snapshot tool
scripts/       Mac installer (plug-ins and recordings)
tests/         engine tests
```

## Names and identifiers

"Hearth Keys", the maker name "Homemade" and the plug-in codes (`Hmde` /
`HrKy`) are placeholders set in `CMakeLists.txt`. Change them before saving
projects with the plug-in: DAWs identify it by those codes.

## Licence

No licence has been chosen for this project's own code yet. Third-party
components and credits are listed in `NOTICE.md`.

# The grand: what is measured and what is chosen

## Source

Salamander Grand Piano V3 (Alexander Holm, CC BY 3.0): a Yamaha C5 recorded
with two spaced microphones about 12 cm above the strings, 48 kHz / 24 bit.
30 notes in minor thirds from A0, 16 strike strengths each; 88 key-release
noises; release tails in three strengths for the 23 sampled notes that have
dampers; four pedal noises. 641 files, 6800 seconds.

## The pack (`tools/grand/make_pack.py`, `recipe.json`)

Each file is trimmed to 1 ms before the point where its level first reaches
-40 dB of its peak (the recordings start with 5 to 30 ms of action noise,
which would be felt as latency), given a 1 ms fade in and a short fade out,
scaled so that its peak is at 97 % of full scale, and reduced to 16 bits with
triangular dither. The scale factor is kept in `GrandTable.cpp`, so quiet
recordings lose nothing: each one is stored 96 dB above its own noise floor
and played back at its original level. The build uses exact arithmetic only
and is bit-identical on every machine; the plug-in verifies the installed
files against checksums. 1.31 GB as PCM, 276 MB to download.

The installed files are mapped into memory, not loaded: the system keeps in
RAM what is being played. So that the audio thread never waits for the disk,
the first second of every recording is held in ordinary memory (about
100 MB), and a helper thread brings in the rest of a recording when its note
starts, ahead of the playing position.

## Measured from the recordings (`measure.py`, `make_table.py`)

- **Loudness of every strike**: K-weighted level of the first 200 ms. The 16
  strengths of a note span 14 to 42 dB (23 dB on average), about 1 dB apart
  in the middle of the range. Within a note they are ordered by measured
  loudness (a few neighbours had come out in the wrong order).
- **Brightness of every strike**: share of the energy above a corner at four
  times the note's frequency (1.5 to 6 kHz).
- **Decay**: level at ten times from 50 ms to 26 s after the strike.
- **Tuning**: A0 is 25 cents flat, the middle is within 3 cents, A7 is 38
  cents sharp: the stretch of a real grand, kept as it is.
- **Partials**: the first twenty partial frequencies of every sampled note
  where at least two strike strengths agree within 3 cents (365 of 600); the
  rest follow a stiff-string fit (B from 7.4e-5 at F#2 to about 1.8e-2 at C8).
- **Decay of single partials**: 0.7 to 4 dB/s in the bass, 5 to 15 dB/s in
  the middle, 20 to 60 dB/s at the top, rising with the partial number.

## How they are played

- **Touch.** A key velocity asks for a loudness: the smoothed top level of
  its note minus 40 dB per decade of velocity at the default Dynamics. The
  recording whose natural loudness is nearest is played with exactly the gain
  that gives the loudness asked for. A first-order shelf at the brightness
  corner moves the timbre to where a strike of that loudness would have been
  between its two neighbours (at most 6 dB). Measured result: loudness within
  0.3 dB of the curve at every velocity, no step larger than 0.2 dB.
- **Hammer.** The recording is chosen for a strike up to 12 dB harder or
  softer than the loudness asked for; the gain still gives the loudness asked
  for.
- **Pitch.** Every key uses the nearest sampled note, at most a semitone
  away, through an 8-point windowed-sinc interpolator (error below -80 dB
  against a 64-point reference; exact at the sampled pitch).
- **Dampers.** Keys up to E6. A resting damper removes 140 dB/s in the bass
  to 520 dB/s at the top; a partly lifted one (half pedal) the square of its
  share of that. A low-pass that closes as the damper works takes the highs
  first. Nothing is reset when the damper lifts again.
- **Release tails.** The recorded tails are after-ring of the damped strings
  following hard staccato notes. They are played 4 dB down, as in the
  original mapping, scaled by where the note's loudness stands when its
  damper lands (strike strength and decay), using the soft and softest sets
  as that level falls.
- **Key and pedal noises.** Levels of the original mapping (-37 dB, -20 dB),
  key noise falling 2 dB per second the note was held; pedal noise from -12
  to +3 dB with pedal speed for a continuous pedal.

## Chosen, not measured

The recordings contain no notes played with the pedal down, so the level of
the **sympathetic resonance** could not be measured. The resonators have the
measured frequencies and decay times; their coupling is set so that a string
whose partial matches a sustained partial on the soundboard reaches 22 dB
below it (Resonance at 50 %). With that, a chord played with the pedal down
has its free strings 17 dB below it after half a second and 12 dB below it as
it dies away, and a C3 held without pedal answers a staccato C4 about 40 dB
below the strike: the order of magnitude known from pianos, not a
calibration. The damper rates, the low-pass of a landing damper and the soft
pedal (2.5 dB and a 5 dB softer strike) are likewise set by judgement.

The top strings (above E6) have no dampers and ring in every recording, so
they are not added a second time for the grand's own notes; they do answer
the tines.

## One soundboard

The tine signal is fed to the same strings, scaled so that at Tine Ring 75 %
a tine note drives them as much as a piano note of the same loudness would.
With the grand in play the tines take the piano's tuning curve (keeping their
own key-to-key scatter), so that the fundamental of a tine sits on the first
partial of the string of its key.

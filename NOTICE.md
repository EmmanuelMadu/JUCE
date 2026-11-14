# Third-party components and credits

## JUCE

The plug-in is built with the [JUCE framework](https://juce.com), which is
dual-licensed under the AGPLv3 and the commercial JUCE licence. JUCE is not
included in this repository; it is downloaded at build time. Whoever builds
and distributes the plug-in is responsible for complying with one of those
licences (see <https://juce.com/get-juce/>).

## Grand piano recordings

The grand plays a processed copy of the **Salamander Grand Piano V3** by
**Alexander Holm** (a Yamaha C5), <https://archive.org/details/SalamanderGrandPianoV3>,
licensed under [Creative Commons Attribution 3.0](https://creativecommons.org/licenses/by/3.0/).
The FLAC edition at <https://github.com/sfzinstruments/SalamanderGrandPiano>
was used as the source. Changes: silence before each sound trimmed, short
fades, every file scaled to full level, 24 to 16 bits with dither, files
renamed (`tools/grand/make_pack.py` and `recipe.json` describe them exactly).
The recordings are distributed as a separate download ("grand-pack-v1"), not
in this repository, together with the licence text and this credit; the
plug-in names the source on its GRAND page.

Yamaha is a trademark of its owner and is named only to say which instrument
was recorded.

## Reference recordings used for calibration

The physical parameters of the tine model were fitted to measurements of the
**jRhodes3d** sample set by Jeff Learman (a 1977 Rhodes Mark I Stage 73),
<https://github.com/sfzinstruments/jlearman.jRhodes3d>.

Those samples are licensed CC BY-NC 4.0 for use as a sample set (the author
offers commercial licences on request) and CC0 for making music. **No audio
from that set is included here or in the built plug-in**; only numbers derived
from measuring it (swing amplitudes, decay rates, tuning deviations, pickup
voicing) are. If you intend to sell a product based on this project, ask the
author about commercial terms or recalibrate against recordings you are free
to use; this note is not legal advice.

Rhodes is a trademark of its owner; this project is not affiliated with or
endorsed by any instrument maker. Product names are used only to describe the
kind of instrument being modelled.

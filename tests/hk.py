"""ctypes wrapper around the engine's C interface (build/test/libhk.so)."""
import ctypes
import os

import numpy as np

_here = os.path.dirname(os.path.abspath(__file__))
_lib = ctypes.CDLL(os.environ.get("HK_LIB", os.path.join(_here, "..", "build", "test", "libhk.so")))
_lib.hk_create.restype = ctypes.c_void_p
_lib.hk_create.argtypes = [ctypes.c_double]
for name, args in (("hk_destroy", []), ("hk_reset", []), ("hk_set_param", [ctypes.c_int, ctypes.c_float]),
                   ("hk_note_on", [ctypes.c_int, ctypes.c_float]), ("hk_note_off", [ctypes.c_int]),
                   ("hk_sustain", [ctypes.c_float]), ("hk_all_notes_off", []), ("hk_all_sound_off", [])):
    getattr(_lib, name).argtypes = [ctypes.c_void_p] + args
    getattr(_lib, name).restype = None
_lib.hk_process.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
_lib.hk_get_param.argtypes = [ctypes.c_void_p, ctypes.c_int]
_lib.hk_get_param.restype = ctypes.c_float
for name in ("hk_latency", "hk_idle", "hk_active_keys"):
    getattr(_lib, name).argtypes = [ctypes.c_void_p]
    getattr(_lib, name).restype = ctypes.c_int
_lib.hk_param_id.restype = ctypes.c_char_p
_lib.hk_param_id.argtypes = [ctypes.c_int]
for name in ("hk_param_min", "hk_param_max", "hk_param_default"):
    getattr(_lib, name).restype = ctypes.c_float
    getattr(_lib, name).argtypes = [ctypes.c_int]
_lib.hk_preset_name.restype = ctypes.c_char_p
_lib.hk_preset_name.argtypes = [ctypes.c_int]
_lib.hk_preset_value.restype = ctypes.c_float
_lib.hk_preset_value.argtypes = [ctypes.c_int, ctypes.c_int]

HAS_GRAND = hasattr(_lib, "hk_grand_load")
NUM_TINE_PRESETS = None
if HAS_GRAND:
    _lib.hk_grand_load.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _lib.hk_grand_load.restype = ctypes.c_int
    _lib.hk_grand_verify.restype = ctypes.c_int
    _lib.hk_grand_raw.argtypes = [ctypes.c_void_p, ctypes.c_int]
    _lib.hk_grand_raw.restype = None
    for name in ("hk_sostenuto", "hk_soft"):
        getattr(_lib, name).argtypes = [ctypes.c_void_p, ctypes.c_float]
        getattr(_lib, name).restype = None
    for name in ("hk_grand_voices", "hk_ringing_strings"):
        getattr(_lib, name).argtypes = [ctypes.c_void_p]
        getattr(_lib, name).restype = ctypes.c_int
    NUM_TINE_PRESETS = _lib.hk_num_tine_presets()
    _lib.hk_grand_key_cents.argtypes = [ctypes.c_int]
    _lib.hk_grand_key_cents.restype = ctypes.c_float
    _lib.hk_grand_partial.argtypes = [ctypes.c_int, ctypes.c_int]
    _lib.hk_grand_partial.restype = ctypes.c_float
GRAND_DIR = os.environ.get("HK_GRAND_DIR", "")

NUM_PARAMS = _lib.hk_num_params()
PARAM_IDS = [_lib.hk_param_id(i).decode() for i in range(NUM_PARAMS)]
P = {name: i for i, name in enumerate(PARAM_IDS)}
PARAM_RANGE = [(_lib.hk_param_min(i), _lib.hk_param_max(i), _lib.hk_param_default(i)) for i in range(NUM_PARAMS)]
NUM_PRESETS = _lib.hk_num_presets()
PRESET_NAMES = [_lib.hk_preset_name(i).decode() for i in range(NUM_PRESETS)]


def grand_partial(note, n=1):
    """Frequency (Hz, A = 440) of a partial of a key of the grand, from the engine's table."""
    return float(_lib.hk_grand_partial(int(note), int(n)))


def preset_values(i):
    return [_lib.hk_preset_value(i, j) for j in range(NUM_PARAMS)]


class Synth:
    def __init__(self, fs=48000.0):
        self.fs = fs
        self.h = _lib.hk_create(fs)

    def __del__(self):
        if getattr(self, "h", None):
            _lib.hk_destroy(self.h)
            self.h = None

    def set(self, name, value):
        _lib.hk_set_param(self.h, P[name] if isinstance(name, str) else name, float(value))

    def get(self, name):
        return _lib.hk_get_param(self.h, P[name] if isinstance(name, str) else name)

    def load_preset(self, i):
        for j, v in enumerate(preset_values(i)):
            _lib.hk_set_param(self.h, j, v)

    def note_on(self, note, vel=0.8):
        _lib.hk_note_on(self.h, int(note), float(vel))

    def note_off(self, note):
        _lib.hk_note_off(self.h, int(note))

    def sustain(self, v):
        _lib.hk_sustain(self.h, float(v))

    def sostenuto(self, v):
        _lib.hk_sostenuto(self.h, float(v))

    def soft(self, v):
        _lib.hk_soft(self.h, float(v))

    def load_grand(self, directory=None):
        """Attach the grand piano recordings; returns False if they are not there."""
        d = directory or GRAND_DIR
        return bool(d) and bool(_lib.hk_grand_load(self.h, d.encode()))

    def grand_raw(self, on=True):
        _lib.hk_grand_raw(self.h, 1 if on else 0)

    @property
    def grand_voices(self):
        return _lib.hk_grand_voices(self.h)

    @property
    def ringing_strings(self):
        return _lib.hk_ringing_strings(self.h)

    def all_notes_off(self):
        _lib.hk_all_notes_off(self.h)

    def all_sound_off(self):
        _lib.hk_all_sound_off(self.h)

    def reset(self):
        _lib.hk_reset(self.h)

    def render(self, n, block=None, dry=False):
        """Render n frames; returns (left, right) or (left, right, dry)."""
        left = np.zeros(n, np.float32)
        right = np.zeros(n, np.float32)
        d = np.zeros(n, np.float64) if dry else None
        block = block or n
        pos = 0
        while pos < n:
            m = min(block, n - pos)
            _lib.hk_process(self.h, left[pos:].ctypes.data, right[pos:].ctypes.data, m,
                            d[pos:].ctypes.data if dry else None)
            pos += m
        return (left, right, d) if dry else (left, right)

    @property
    def latency(self):
        return _lib.hk_latency(self.h)

    @property
    def idle(self):
        return bool(_lib.hk_idle(self.h))

    @property
    def active_keys(self):
        return _lib.hk_active_keys(self.h)

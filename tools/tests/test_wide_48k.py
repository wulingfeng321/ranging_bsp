"""WIDE 48 kHz waveform/template and scan-window boundary smoke test."""
import ctypes as c
import sys
import wave
from pathlib import Path
from audio_paths import wide_audio
import numpy as np

lib = c.CDLL(sys.argv[1])
lib.RangeDsp_Find.argtypes = [c.POINTER(c.c_int16), c.c_uint, c.c_uint,
                            c.POINTER(c.c_float), c.POINTER(c.c_uint32)]
root = Path(__file__).resolve().parents[2]
with wave.open(str(wide_audio(48000))) as w:
    assert w.getframerate() == 48000 and w.getnframes() == 744000
    audio = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(int)
assert not np.any(audio[504000:])
signature = audio[:4992]
for offset in list(range(0, 768, 17)) + [190, 191, 192, 193, 766, 767]:
    pcm = np.zeros(6144, dtype=int)
    pcm[offset:offset + len(signature)] = np.rint(signature*.3).astype(int)
    x = (c.c_int16*6144)(*pcm)
    pos, q = c.c_float(), c.c_uint32()
    found = False
    for start in range(0, 768, 192):
        if lib.RangeDsp_Find(x, start, start+192, c.byref(pos), c.byref(q)):
            assert abs(pos.value-offset)<.6, (offset, pos.value)
            found = True
            break
    assert found, offset
print('PASS: WIDE 48 kHz template, 52 alignments including slice/end boundaries, WAV duration and silence')

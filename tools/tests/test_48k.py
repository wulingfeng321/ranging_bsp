"""Actual 48 kHz LEGACY/JOINT2 C DSP, fractional arrivals and full playback.
Usage: python tools/tests/test_48k.py path/to/joint48.dll
"""
import ctypes as c
import sys
import wave
from pathlib import Path
from audio_paths import standard_audio
import numpy as np
from dsp_types import DspPeaks, Peaks

lib = c.CDLL(sys.argv[1])
assert lib.Test_SampleRate() == 48000
lib.RangeDsp_Find.argtypes = [c.POINTER(c.c_int16), c.c_uint, c.c_uint,
                            c.POINTER(c.c_float), c.POINTER(c.c_uint32)]
lib.RangeDsp_Candidates.argtypes = [c.POINTER(c.c_int16), c.c_float, c.POINTER(DspPeaks)]
lib.Test_Pair.argtypes = [c.POINTER(Peaks), c.POINTER(Peaks), c.c_int64,
                        c.POINTER(c.c_int64), c.POINTER(c.c_uint32), c.POINTER(c.c_uint32)]
FS, WINDOW, PULSE, STEP, ADVANCE, SLICE = 48000, 6912, 1536, 1920, 768, 192
NS = 1e9 / FS
DELAY = 378546
root = Path(__file__).resolve().parents[2]
with wave.open(str(standard_audio(48000))) as wav:
    assert (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) == (FS, 1, 2)
    audio = np.frombuffer(wav.readframes(wav.getnframes()), dtype='<i2').astype(int)
assert len(audio) == FS * 31 // 2
assert not np.any(audio[FS * 21 // 2:])
signature = audio[:5376]


def detect(values):
    x = (c.c_int16 * WINDOW)(*values)
    pos, quality = c.c_float(), c.c_uint32()
    for scan in range(0, ADVANCE, SLICE):
        if lib.RangeDsp_Find(x, scan, scan + SLICE, c.byref(pos), c.byref(quality)):
            dsp, peaks = DspPeaks(), Peaks()
            lib.RangeDsp_Candidates(x, pos, c.byref(dsp))
            peaks.count, peaks.overflow = dsp.count, dsp.overflow
            for i in range(dsp.count):
                peaks.peak[i].quality = dsp.peak[i].quality
                for p in range(3):
                    peaks.peak[i].offset[p] = round((dsp.peak[i].position[p] - pos.value - p * STEP) * NS)
            return pos.value, peaks
    return None


def pair(a, b):
    delta, quality, spread = c.c_int64(), c.c_uint32(), c.c_uint32()
    status = lib.Test_Pair(c.byref(a[1]), c.byref(b[1]), round((b[0] - a[0]) * NS),
                          c.byref(delta), c.byref(quality), c.byref(spread))
    return status, delta.value


def fractional(at):
    values = np.zeros(WINDOW)
    for pulse in range(3):
        x = np.arange(WINDOW) - at - pulse * STEP
        active = (x >= 0) & (x <= PULSE - 1)
        env = np.ones(WINDOW)
        rising, falling = x < 48, x >= PULSE - 48
        env[rising] = .5 * (1 - np.cos(2 * np.pi * x[rising] / 95))
        env[falling] = .5 * (1 - np.cos(2 * np.pi * (x[falling] - (PULSE - 96)) / 95))
        f0, f1 = (6000, 2000) if pulse == 1 else (2000, 6000)
        t = x / FS
        values[active] += (7000 * env * np.cos(2 * np.pi * (f0 * t + (f1 - f0) * t*t / .064)))[active]
    return np.rint(values).astype(int)


rng = np.random.default_rng(97)
for offset in range(ADVANCE):
    values = rng.integers(-30, 31, WINDOW)
    values[offset:offset + len(signature)] += np.rint(signature * .25).astype(int)
    got = detect(values)
    assert got and got[1].count and abs(got[0] - offset) < .6, (offset, got)
print('PASS: 768 DMA/window alignments, full-rate templates and candidate extraction')

worst = 0
for phase in np.arange(100) / 100:
    a = detect(fractional(300 + phase))
    for direction in (1, -1):
        b = detect(fractional(300 + phase + direction * DELAY / NS))
        assert a and b
        status, delta = pair(a, b)
        assert status == 1, (phase, direction, status)
        error = abs(delta - direction * DELAY)
        worst = max(worst, error)
        assert error < 3000, (phase, direction, error)
print(f'PASS: 200 fractional-delay cases in both directions; worst error {worst} ns ({worst*343420/1e9:.3f} mm)')

for delay in (6, 12, 24, 48):
    a = np.zeros(WINDOW, dtype=int)
    a[300:300 + len(signature)] = np.rint(signature * .2).astype(int)
    b = a.copy()
    b[300 + delay:300 + delay + PULSE] += np.rint(signature[:PULSE] * .3).astype(int)
    left, right = detect(a), detect(b)
    assert left
    if right:
        status, delta = pair(left, right)
        assert status != 1 or abs(delta) < 30000, (delay, status, delta)
print('PASS: single-pulse echo cannot produce a confident large time shift')
assert detect(np.zeros(WINDOW, dtype=int)) is None
assert detect(rng.integers(-1000, 1001, WINDOW)) is None
tone = np.rint(12000 * np.sin(np.arange(WINDOW) * 2*np.pi*4000/FS)).astype(int)
assert detect(tone) is None
print('PASS: silence, noise and continuous tone rejected')

for offset in (0, 17, 383, 766):
    stream = np.concatenate((np.zeros(offset, dtype=int), audio, np.zeros(WINDOW, dtype=int)))
    hits = []
    for base in range(0, len(stream) - WINDOW + 1, ADVANCE):
        # Same refractory decision per slice as AudioProcess, not a whole-window shortcut.
        if hits and base + ADVANCE <= hits[-1] + FS // 4:
            continue
        got = detect(stream[base:base + WINDOW])
        if got and (not hits or base + got[0] > hits[-1] + FS // 4):
            assert got[1].count
            hits.append(base + got[0])
    expected = [offset + g*4*FS + p*FS//2 for g in range(3) for p in range(5)]
    assert len(hits) == 15, (offset, hits)
    assert max(abs(a-b) for a,b in zip(hits, expected)) < .6
print('PASS: complete 15.5 s WAV, 15 signatures at four offsets; tail silence gives no events')

"""Actual WIDE C detector: python test_range_wide.py path/to/wide.dll."""
import ctypes as c
import math
from pathlib import Path
from audio_paths import GENERATED
import random
import struct
import sys
import wave

ROOT = Path(__file__).resolve().parents[2]
lib = c.CDLL(sys.argv[1])
lib.RangeDsp_Find.argtypes = [c.POINTER(c.c_int16), c.c_uint, c.c_uint,
                            c.POINTER(c.c_float), c.POINTER(c.c_uint32)]

def read(name):
    with wave.open(str(GENERATED / name)) as w:
        assert (w.getframerate(), w.getnchannels(), w.getsampwidth()) == (16000, 1, 2)
        return list(struct.unpack('<' + 'h'*w.getnframes(), w.readframes(w.getnframes())))

audio = read('wide_repeat_16000hz_15p5s.wav')
signature = audio[:1664]
rng = random.Random(42)

def find(values):
    x = (c.c_int16*2048)(*values)
    pos, q = c.c_float(), c.c_uint32()
    for k in range(0, 256, 64):
        if lib.RangeDsp_Find(x, k, k+64, c.byref(pos), c.byref(q)):
            return pos.value, q.value
    return None

def scene(offset, direct=0.3, echoes=(), noise=20):
    values = [rng.randint(-noise, noise) for _ in range(2048)]
    for delay, gain in [(0, direct), *echoes]:
        for i, v in enumerate(signature):
            if offset+delay+i < 2048:
                values[offset+delay+i] += round(v*gain)
    assert max(map(abs, values)) < 32768
    return values

for offset in range(256):
    got = find(scene(offset))
    assert got and abs(got[0]-offset) < 0.6, (offset, got)
for gain in [0.005, 0.05, -0.3, 0.7]:
    got = find(scene(63, gain, noise=5))
    assert got and abs(got[0]-63) < 0.6, (gain, got)
print('PASS: all 256 offsets, weak signal and polarity')

# Fractional arrival generated from the continuous waveform, not by rounding
# the delay or linearly interpolating the integer PCM template.
for offset in [17.1,17.25,17.5,17.75,17.9,63.5,127.5,254.5]:
    values=[]
    for sample in range(2048):
        v=0.0
        for pulse in range(3):
            at=sample-offset-pulse*576
            if 0<=at<=511:
                edge=min(at,511-at)
                envelope=0.5-0.5*math.cos(math.pi*edge/64) if edge<64 else 1.0
                f0,f1=(6500,1500) if pulse==1 else (1500,6500)
                t=at/16000
                v+=0.3*0.75*32767*envelope*math.cos(2*math.pi*(f0*t+(f1-f0)*t*t/(2*512/16000)))
        values.append(round(v))
    got=find(values)
    assert got and abs(got[0]-offset)<0.6,(offset,got)
print('PASS: continuous-waveform fractional arrivals (<0.6 sample error)')

# Stronger delayed reflection, including peaks across 64/256-sample boundaries.
for offset in [0, 1, 17, 60, 63, 64, 125, 127, 250, 255]:
    for delay in [8, 16, 32, 48, 80]:
        for echo in [0.15, 0.45, -0.45]:
            got = find(scene(offset, 0.3, [(delay, echo)]))
            assert got and abs(got[0]-offset) < 0.8, (offset, delay, echo, got)
print('PASS: resolved synthetic reflections, including stronger/inverted echoes')

assert find([0]*2048) is None
for _ in range(30):
    assert find([rng.randint(-5000, 5000) for _ in range(2048)]) is None
for freq in [1500, 2000, 3000, 4000, 6000, 6500]:
    assert find([round(10000*math.sin(i*2*math.pi*freq/16000)) for i in range(2048)]) is None
assert find(signature[:512]+[0]*(2048-512)) is None
assert find(read('standard_group_16000hz_2p5s.wav')[:2048]) is None
wrong = signature[:]
wrong[576:1088] = signature[:512]
assert find(wrong+[0]*(2048-len(wrong))) is None
print('PASS: silence, noise, tones, partial/wrong/legacy signatures rejected')

expected = [g*64000+i*8000 for g in range(3) for i in range(5)]
for offset in [0, 17, 63, 127, 254]:
    stream = [0]*offset + audio + [0]*2048
    hits = []
    for base in range(0, len(stream)-2047, 256):
        x = (c.c_int16*2048)(*stream[base:base+2048])
        pos, q = c.c_float(), c.c_uint32()
        for scan in range(0,256,64):
            if hits and base+scan <= int(hits[-1])+4000:
                continue
            if lib.RangeDsp_Find(x,scan,scan+64,c.byref(pos),c.byref(q)):
                at = base+pos.value
                if not hits or int(at)>int(hits[-1])+4000:
                    hits.append(at)
    assert len(hits)==15 and all(abs(v-(s+offset))<0.6 for v,s in zip(hits,expected)), (offset,hits)
print('PASS: entire 10.5 second WAV, 15 events, five stream alignments')

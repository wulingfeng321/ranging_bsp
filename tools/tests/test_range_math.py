"""Exercise actual C detector/sync/geometry via a host shared library.
Usage: python test_range_math.py path/to/range_math.dll
"""
import ctypes as c
import math, random, struct, sys, wave
from pathlib import Path
lib=c.CDLL(sys.argv[1])
class Sync(c.Structure):
    _fields_=[('x',c.c_double*16),('y',c.c_double*16),('origin',c.c_double),
              ('offset',c.c_double),('slope',c.c_double),('count',c.c_uint32),
              ('next',c.c_uint32),('bestRtt',c.c_uint32),('residual',c.c_uint32),
              ('uncertainty',c.c_uint32),('locked',c.c_uint8)]
lib.RangeSync_Reset.argtypes=[c.POINTER(Sync)]
lib.RangeSync_Add.argtypes=[c.POINTER(Sync)]+[c.c_uint64]*4
lib.RangeSync_Master.argtypes=[c.POINTER(Sync),c.c_uint64]
lib.RangeSync_Master.restype=c.c_uint64
lib.RangeDsp_Find.argtypes=[c.POINTER(c.c_int16),c.c_uint,c.c_uint,c.POINTER(c.c_float),c.POINTER(c.c_uint32)]
lib.RangeDsp_Distance.argtypes=[c.c_int64,c.c_int32,c.POINTER(c.c_uint32),c.POINTER(c.c_int32)]
root=Path(__file__).resolve().parents[2]
with wave.open(str(root/'tools/test_audio_chirp.wav'),'rb') as w:
    samples=struct.unpack('<'+'h'*w.getnframes(),w.readframes(w.getnframes()))
sig=samples[:1792]
rng=random.Random(123)
def find(values):
    x=(c.c_int16*2048)(*values); pos=c.c_float(); q=c.c_uint32()
    for k in range(0,256,64):
        if lib.RangeDsp_Find(x,k,k+64,c.byref(pos),c.byref(q)):
            return pos.value,q.value
    return None
for offset in [0,1,17,63,64,127,128,199,254]:
    for gain in [0.2,0.7,-0.5]:
        values=[rng.randint(-60,60) for _ in range(2048)]
        for i,v in enumerate(sig): values[offset+i]+=int(v*gain)
        got=find(values)
        assert got is not None and abs(got[0]-offset)<0.6 and got[1]>900,(offset,gain,got)
assert find([0]*2048) is None
assert find([rng.randint(-1000,1000) for _ in range(2048)]) is None
assert find([int(12000*math.sin(i*2*math.pi*4000/16000)) for i in range(2048)]) is None
wrong=list(sig); wrong[640:1152]=sig[:512]
assert find(wrong+[0]*256) is None
assert find(list(sig[:512])+[0]*(2048-512)) is None

# Bring-up profile accepts a clean weak signature (~51 PCM RMS), below the
# original 64 RMS floor, while the rejection cases above must still pass.
weak=[0]*2048
for i,v in enumerate(sig): weak[100+i]=int(v*0.0025)
got=find(weak)
assert got is not None and abs(got[0]-100)<0.6,got

# Distortion confined to the first pulse must not select its +/-2-sample
# sidelobe when the other two pulses agree on the true signature arrival.
for delay,echo_gain in [(2,1.0),(4,1.0),(4,1.5),(8,2.0)]:
    values=[0]*2048
    for i,v in enumerate(sig): values[100+i]=int(v*0.2)
    for i,v in enumerate(sig[:512]):
        values[100+i+delay]+=int(v*0.2*echo_gain)
    got=find(values)
    assert got is not None and abs(got[0]-100)<0.6,(delay,echo_gain,got)

# B clock is 700 ms ahead and runs 40 ppm fast; A remains monotonic.
s=Sync(); lib.RangeSync_Reset(c.byref(s))
def bclock(a): return round(a*1.00004+700_000_000)
for i in range(24):
    t=10_000_000_000+i*100_000_000
    assert lib.RangeSync_Add(c.byref(s),bclock(t),t+12000,t+52000,bclock(t+64000))
assert s.locked and abs(s.slope-0.00004)<1e-8
test=12_450_000_000
assert abs(lib.RangeSync_Master(c.byref(s),bclock(test))-test)<20
count=s.count
assert not lib.RangeSync_Add(c.byref(s),bclock(test),test+12000,test+52000,bclock(test+5_000_000))
assert s.count==count
lib.RangeSync_Reset(c.byref(s)); assert not s.locked

mm=c.c_uint32(); side=c.c_int32()
for dt,sgn in [(291205,1),(-291205,-1)]:
    assert lib.RangeDsp_Distance(dt,200,c.byref(mm),c.byref(side))
    assert mm.value==100 and side.value==sgn,(mm.value,side.value)
assert lib.RangeDsp_Distance(0,200,c.byref(mm),c.byref(side)) and mm.value==0 and side.value==0
assert not lib.RangeDsp_Distance(20_000_000,200,c.byref(mm),c.byref(side))
assert not lib.RangeDsp_Distance(1000,1000,c.byref(mm),c.byref(side))
print('PASS: actual WAV offsets/gains/polarity; silence/noise/tone/wrong signature rejection; clock offset/drift/outlier; geometry')

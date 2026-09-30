"""python test_joint_peaks.py joint.dll; actual C DSP + pairing + wire codec."""
import ctypes as c
import math
import random
import struct
import sys
import wave
from pathlib import Path
from audio_paths import standard_audio
lib=c.CDLL(sys.argv[1])
assert lib.Test_SampleRate()==16000, "This regression suite requires a 16 kHz host library; use test_48k.py for 48 kHz"
class DspPeak(c.Structure):
    _fields_=[('position',c.c_float*3),('quality',c.c_uint32)]
class DspPeaks(c.Structure):
    _fields_=[('peak',DspPeak*3),('count',c.c_uint32),('overflow',c.c_uint32)]
class Peak(c.Structure):
    _fields_=[('offset',c.c_int32*3),('quality',c.c_uint32)]
class Peaks(c.Structure):
    _fields_=[('peak',Peak*3),('count',c.c_uint32),('overflow',c.c_uint32)]
lib.RangeDsp_Find.argtypes=[c.POINTER(c.c_int16),c.c_uint,c.c_uint,c.POINTER(c.c_float),c.POINTER(c.c_uint32)]
lib.RangeDsp_Candidates.argtypes=[c.POINTER(c.c_int16),c.c_float,c.POINTER(DspPeaks)]
lib.Test_Pair.argtypes=[c.POINTER(Peaks),c.POINTER(Peaks),c.c_int64,c.POINTER(c.c_int64),c.POINTER(c.c_uint32),c.POINTER(c.c_uint32)]
lib.Test_Pack.argtypes=[c.POINTER(Peak)]; lib.Test_Pack.restype=c.c_uint64
lib.Test_Unpack.argtypes=[c.c_uint64,c.POINTER(Peak)]
def pair(a,b,base):
    delta,q,span=c.c_int64(),c.c_uint32(),c.c_uint32()
    status=lib.Test_Pair(c.byref(a),c.byref(b),base,c.byref(delta),c.byref(q),c.byref(span))
    return status,delta.value,q.value,span.value
def peaks(*entries):
    result=Peaks(); result.count=len(entries)
    for i,(offset,q) in enumerate(entries): result.peak[i]=Peak((c.c_int32*3)(*offset),q)
    return result
a=peaks(([0,0,0],950))
b=peaks(([0,0,0],950),([145000]*3,930))
assert pair(a,b,378546)[0]==2 # 13 vs 18 cm is ambiguous, never pick 18 by default.
b=peaks(([0,90000,0],990),([-145594]*3,900))
assert pair(a,b,524140)==(1,378546,900,0) # Strong inconsistent peak rejected.
assert pair(a,peaks(([0,50000,0],950)),378546)[0]==0
assert pair(a,peaks(([0,0,0],950)),-378546)[1]==-378546
b.overflow=1; assert pair(a,b,524140)[0]==2
for offsets in [[-1000125,123456,20000],[-8191750,0,8191750]]:
    original=Peak((c.c_int32*3)(*offsets),900); decoded=Peak()
    assert lib.Test_Unpack(lib.Test_Pack(c.byref(original)),c.byref(decoded))
    assert all(abs(x-y)<=125 for x,y in zip(offsets,decoded.offset))
assert not lib.Test_Unpack(0,c.byref(Peak()))
print('PASS: unique/negative delay, 13-vs-18 ambiguity, pulse inconsistency, overflow, wire codec')
root=Path(__file__).resolve().parents[2]
with wave.open(str(standard_audio(16000))) as w:
    audio=list(struct.unpack('<'+'h'*w.getnframes(),w.readframes(w.getnframes())))
sig=audio[:1792]; rng=random.Random(97)
def detect(values):
    x=(c.c_int16*2304)(*values); pos=c.c_float(); q=c.c_uint32()
    for scan in range(0,256,64):
        if lib.RangeDsp_Find(x,scan,scan+64,c.byref(pos),c.byref(q)):
            dsp=DspPeaks(); lib.RangeDsp_Candidates(x,pos,c.byref(dsp))
            result=Peaks(); result.count=dsp.count; result.overflow=dsp.overflow
            for i in range(dsp.count):
                result.peak[i].quality=dsp.peak[i].quality
                for p in range(3): result.peak[i].offset[p]=round((dsp.peak[i].position[p]-pos.value-p*640)*62500)
            return pos.value,result
    return None
for offset in range(256):
    first=[rng.randint(-30,30) for _ in range(2304)]
    second=[rng.randint(-30,30) for _ in range(2304)]
    for i,v in enumerate(sig): first[offset+i]+=round(v*.25); second[offset+i]+=round(v*.4)
    left,right=detect(first),detect(second)
    assert left and right and left[1].count and right[1].count,offset
    matched=pair(left[1],right[1],378546+round((right[0]-left[0])*62500))
    assert matched[0]==1 and abs(matched[1]-378546)<3000,(offset,matched)
print('PASS: actual WAV DSP -> 3 pulse offsets -> joint 130mm pairing, all 256 alignments')
for delay in [2,4,8,16]:
    first=[0]*2304; second=[0]*2304
    for i,v in enumerate(sig): first[100+i]=round(v*.2); second[100+i]=round(v*.2)
    for i,v in enumerate(sig[:512]): second[100+i+delay]+=round(v*.3)
    left,right=detect(first),detect(second)
    if right:
        matched=pair(left[1],right[1],378546+round((right[0]-left[0])*62500))
        assert matched[0]!=1 or abs(matched[1]-378546)<30000,(delay,matched)
print('PASS: one-pulse echoes produce correct delay or rejection, not a confident shifted distance')
def fractional_recording(offset,pulse_offsets=(0,0,0)):
    values=[]
    for sample in range(2304):
        v=0.0
        for pulse in range(3):
            at=sample-offset-pulse*640-pulse_offsets[pulse]
            if 0<=at<=511:
                envelope=1.0
                if at<16: envelope=0.5*(1-math.cos(2*math.pi*at/31))
                elif at>=496: envelope=0.5*(1-math.cos(2*math.pi*(at-480)/31))
                f0,f1=(6000,2000) if pulse==1 else (2000,6000)
                t=at/16000
                v+=7000*envelope*math.cos(2*math.pi*(f0*t+(f1-f0)*t*t/(2*512/16000)))
        values.append(round(v))
    return values
accepted=0
for phase in [0,0.1,0.25,0.5,0.75,0.9]:
    left=detect(fractional_recording(100+phase))
    right=detect(fractional_recording(100+phase+378546/62500))
    if left and right:
        matched=pair(left[1],right[1],round((right[0]-left[0])*62500))
        if matched[0]==1:
            accepted+=1
            assert abs(matched[1]-378546)<30000,(phase,matched)
assert accepted>=3,accepted
print('PASS: physically delayed fractional recordings, accepted estimates within 30us;',accepted,'of 6 accepted')
for offsets in [(0,0.2,0),(0,0.2,-0.2)]:
    got=detect(fractional_recording(100.4,offsets))
    assert got and got[1].count,(offsets,got)
    # Same small pulse-dependent phase distortion on both boards cancels in
    # B-A. JOINT1 rejected these before it could compare the boards at all.
    matched=pair(got[1],got[1],378546)
    assert matched[0]==1 and matched[1]==378546,(offsets,matched)
print('PASS: pulse maxima straddling adjacent samples retained and jointly matched')
for offset in [0,17,127,254]:
    stream=[0]*offset+audio+[0]*2304; hits=[]
    for base in range(0,len(stream)-2303,256):
        if hits and base<=hits[-1]+4000: continue
        got=detect(stream[base:base+2304])
        if got:
            assert got[1].count,(base,got)
            hits.append(base+got[0])
    assert len(hits)==15,(offset,hits)
print('PASS: 15 candidate events from standard repeat WAV at four alignments')

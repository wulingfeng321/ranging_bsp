"""Actual bounded C detector: recorded data, alignment, noise and MAC budgets."""
import ctypes as c
import json
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from replay_capture import DspPeaks, templates, load, mapper, baseline, pair, batch

lib=c.CDLL(sys.argv[1]);lib.Test_MacCount.argtypes=[c.c_int];lib.Test_MacCount.restype=c.c_uint64
lib.RangeDsp_Find.argtypes=[c.POINTER(c.c_int16),c.c_uint,c.c_uint,c.POINTER(c.c_float),c.POINTER(c.c_uint32)]
lib.RangeDsp_Candidates.argtypes=[c.POINTER(c.c_int16),c.c_float,c.POINTER(DspPeaks)]
tpl=templates();rng=np.random.default_rng(29)
max_macs=0
def detect_window(x):
 global max_macs
 lib.RangeDsp_Reset();p=x.ctypes.data_as(c.POINTER(c.c_int16));pos,q=c.c_float(),c.c_uint32()
 for scan in range(0,768,192):
  for turn in range(100):
   lib.Test_MacCount(1);result=lib.RangeDsp_Find(p,scan,scan+192,c.byref(pos),c.byref(q))
   mac=lib.Test_MacCount(0);max_macs=max(max_macs,mac)
   assert mac<=85000,mac
   if result>=0:break
  else:raise AssertionError('pending did not finish')
  if result:
   peaks=DspPeaks();lib.RangeDsp_Candidates(p,pos,c.byref(peaks));assert peaks.count==1
   return pos.value
 return None

for offset in range(24,768):
 x=rng.integers(-20,21,6912,dtype=np.int16)
 for pulse in range(3):
  at=offset+pulse*1920;x[at:at+1536]+=np.rint(tpl[pulse%2]*.15).astype(np.int16)
 got=detect_window(x);assert got is not None and abs(got-offset)<.6,(offset,got)
print('PASS: 744 scan/window alignments; <=85000 MACs per call',flush=True)
for polarity in [1,-1]:
 for lag in [61,106,240]:
  x=rng.integers(-20,21,6912,dtype=np.int16)
  for p in range(3):
   at=300+p*1920;x[at:at+1536]+=np.rint(polarity*.15*tpl[p%2]).astype(np.int16)
   x[at+lag:at+lag+1536]+=np.rint(polarity*.18*tpl[p%2]).astype(np.int16)
  got=detect_window(x);assert got is not None and abs(got-300)<2,(polarity,lag,got)
for x in [np.zeros(6912,dtype=np.int16),rng.normal(0,1500,6912).astype(np.int16),
          np.rint(6000*np.sin(2*np.pi*4000*np.arange(6912)/48000)).astype(np.int16)]:
 assert detect_window(x) is None
print('PASS: echoes, reversed polarity, silence/noise/tone; max MACs',max_macs,flush=True)
if len(sys.argv)>2:
 root=Path(sys.argv[2]);allrows=[]
 for n in range(1,17):
  maps={};found={};r={'round':n,'boards':{}}
  for b in 'AB':
   m,x,a,logs=load(root/f'R{n:06d}',b);_,maps[b],_=mapper(m,a,logs)
   lib.Test_MacCount(1);ev,d=baseline(lib,x);macs=lib.Test_MacCount(0)
   found[b]=[e['candidate_peaks'][0] for e in ev if e['candidate_peaks']]
   assert len(found[b])==15,(n,b,len(found[b]))
   r['boards'][b]={'events':ev,'diagnostics':d,'macs':macs}
  ps=pair(found['A'],found['B'],maps);r['pairs']=ps;r['batch']=batch(ps)
  expected=json.loads((root/'offline_v1_complete'/f'R{n:06d}.json').read_text())['modes']['early65']
  assert r['batch']==expected['batch'],(n,r['batch'],expected['batch'])
  assert all(abs(p['mm']-e['mm'])<.1 for p,e in zip(ps,expected['pairs'])),n
  allrows.append(r);print('PASS capture',n,r['batch'],flush=True)
 (root/'early_c_v1.json').write_text(json.dumps(allrows,indent=2))

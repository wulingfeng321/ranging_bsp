"""Count actual C correlation MACs over playback; not an ARM timing estimate."""
import ctypes as c
import json
import sys
import wave
from pathlib import Path
import numpy as np
from analyze_joint_detector import DspPeaks

root = Path(__file__).resolve().parents[2]
report = []
for rate in (16000, 48000):
    scale = rate//16000
    size, advance, step = 2304*scale, 256*scale, 64*scale
    suffix = '_48k' if scale==3 else ''
    lib = c.CDLL(str(Path(sys.argv[1]) / f'joint{rate}.dll'))
    assert lib.Test_SampleRate()==rate
    lib.RangeDsp_Find.argtypes = [c.POINTER(c.c_int16), c.c_uint, c.c_uint,
                                c.POINTER(c.c_float), c.POINTER(c.c_uint32)]
    lib.RangeDsp_Candidates.argtypes = [c.POINTER(c.c_int16), c.c_float, c.POINTER(DspPeaks)]
    lib.Test_MacCount.argtypes = [c.c_int]
    lib.Test_MacCount.restype = c.c_uint64
    with wave.open(str(root / f'tools/test_audio_chirp{suffix}_repeat.wav')) as w:
        audio = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(int)
    stream = np.concatenate((np.zeros(127*scale, dtype=int), audio, np.zeros(size, dtype=int)))
    pos, q, peaks = c.c_float(), c.c_uint32(), DspPeaks()
    last, hits, maximum = None, 0, 0
    lib.Test_MacCount(1)
    for base in range(0, len(stream)-size+1, advance):
        pcm = (c.c_int16*size)(*stream[base:base+size])
        for scan in range(0, advance, step):
            if last is not None and base+scan<=last+rate//4:
                continue
            before = lib.Test_MacCount(0)
            if lib.RangeDsp_Find(pcm,scan,scan+step,c.byref(pos),c.byref(q)):
                if last is None or base+int(pos.value)>last+rate//4:
                    lib.RangeDsp_Candidates(pcm,pos,c.byref(peaks))
                    last=base+int(pos.value)
                    hits+=1
            maximum=max(maximum,lib.Test_MacCount(0)-before)
    assert hits==15, (rate,hits)
    report.append({'sample_rate':rate,'events':hits,'correlation_macs':lib.Test_MacCount(0),
                   'max_macs_one_slice_with_candidates':maximum})
print(json.dumps(report,indent=2))
print(f"48k/16k total correlation work ratio: {report[1]['correlation_macs']/report[0]['correlation_macs']:.3f}")

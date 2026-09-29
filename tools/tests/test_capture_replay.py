"""Signal-level checks for the offline experiment (not MCU qualification)."""
import sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from replay_capture import templates, scores, arrivals, select, pair, FS, STEP, PULSE

tpl = templates()
rng = np.random.default_rng(290926)


def scene(delay=0, echo=None, polarity=1, pulses=3):
    x = rng.normal(0, 20, FS).astype(float)
    for p in range(pulses):
        start = 4000 + delay + p*STEP
        x[start:start+PULSE] += polarity*.16*tpl[p % 2]
        if echo:
            lag, gain = echo
            x[start+lag:start+lag+PULSE] += polarity*.16*gain*tpl[p % 2]
    return np.rint(x).astype(np.int16)


def detect(x):
    return select(arrivals(scores(x, tpl)), 'early65', .65)


for delay in (0, 177, 261):
    for polarity in (1, -1):
        for echo in (None, (61, .7), (106, 1.2), (240, 1.2)):
            out = detect(scene(delay, echo, polarity))
            assert len(out) == 1, (delay, polarity, echo, len(out))
            assert abs(out[0]['pos'] - (4000+delay)) < 2, (delay, polarity, echo, out)
print('PASS: clean/polarity-inverted and 1.27/2.21/5 ms delayed echoes')

for values in (np.zeros(FS, dtype=np.int16),
               rng.normal(0, 1500, FS).astype(np.int16),
               np.rint(6000*np.sin(2*np.pi*4000*np.arange(FS)/FS)).astype(np.int16),
               scene(pulses=1), scene(pulses=2)):
    assert not detect(values), 'Noise/tone/incomplete signature generated an event'
print('PASS: silence, noise, 4 kHz tone and incomplete signatures rejected')

maps = {'A': lambda p: np.array(p)*1e9/FS, 'B': lambda p: np.array(p)*1e9/FS}
a, b = detect(scene(208, (106, 1.2))), detect(scene())
ab, ba = pair(a, b, maps), pair(b, a, maps)
assert len(ab) == len(ba) == 1 and ab[0]['accepted'] and ba[0]['accepted']
assert ab[0]['delta_ns'] < 0 < ba[0]['delta_ns']
assert abs(ab[0]['mm']-208*346450/FS) < 10
bad = [dict(b[0], pulse_pos=[b[0]['pulse_pos'][0], b[0]['pulse_pos'][1]+8, b[0]['pulse_pos'][2]])]
assert not pair(a, bad, maps)[0]['accepted']
print('PASS: either source side and inconsistent pulse delays rejected')

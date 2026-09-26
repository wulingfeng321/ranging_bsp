"""Characterize the actual LEGACY/JOINT2 C detector, without hardware claims.

Usage: python tools/tests/analyze_joint_detector.py path/to/joint.dll
Build range_dsp.c + peak_pair_host.c with APP_RANGE_AUDIO_PROFILE=1 and
APP_RANGE_JOINT_PEAKS=1. Export RangeDsp_Find, RangeDsp_Candidates, Test_Pair.
NumPy/SciPy are also used by the existing audio generation tools.
"""
import argparse
import ctypes as c
import json
from collections import Counter

import numpy as np
from scipy.signal import butter, sosfilt


class DspPeak(c.Structure):
    _fields_ = [('position', c.c_float * 3), ('quality', c.c_uint32)]


class DspPeaks(c.Structure):
    _fields_ = [('peak', DspPeak * 3), ('count', c.c_uint32), ('overflow', c.c_uint32)]


class Peak(c.Structure):
    _fields_ = [('offset', c.c_int32 * 3), ('quality', c.c_uint32)]


class Peaks(c.Structure):
    _fields_ = [('peak', Peak * 3), ('count', c.c_uint32), ('overflow', c.c_uint32)]


SAMPLE_NS = 62500
DELAY_NS = 378546  # 130 mm at the firmware's 20 C sound speed.
STATUS = {0: 'inconsistent', 1: 'accepted', 2: 'ambiguous'}


def recording(offset):
    """Sample the generated legacy chirp at a fractional arrival time."""
    samples = np.arange(2304, dtype=float)
    values = np.zeros(2304)
    for pulse in range(3):
        at = samples - offset - pulse * 640
        active = (at >= 0) & (at <= 511)
        envelope = np.ones(2304)
        rising = at < 16
        falling = at >= 496
        envelope[rising] = .5 * (1 - np.cos(2 * np.pi * at[rising] / 31))
        envelope[falling] = .5 * (1 - np.cos(2 * np.pi * (at[falling] - 480) / 31))
        f0, f1 = (6000, 2000) if pulse == 1 else (2000, 6000)
        t = at / 16000
        values[active] += (7000 * envelope * np.cos(
            2 * np.pi * (f0 * t + (f1 - f0) * t * t / (2 * .032))))[active]
    return np.rint(values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library')
    args = parser.parse_args()
    lib = c.CDLL(args.library)
    lib.RangeDsp_Find.argtypes = [c.POINTER(c.c_int16), c.c_uint, c.c_uint,
                                 c.POINTER(c.c_float), c.POINTER(c.c_uint32)]
    lib.RangeDsp_Candidates.argtypes = [c.POINTER(c.c_int16), c.c_float, c.POINTER(DspPeaks)]
    lib.Test_Pair.argtypes = [c.POINTER(Peaks), c.POINTER(Peaks), c.c_int64,
                             c.POINTER(c.c_int64), c.POINTER(c.c_uint32), c.POINTER(c.c_uint32)]

    def detect(values):
        pcm = (c.c_int16 * 2304)(*np.rint(values).astype(int))
        position, quality = c.c_float(), c.c_uint32()
        for start in range(0, 256, 64):
            if not lib.RangeDsp_Find(pcm, start, start + 64, c.byref(position), c.byref(quality)):
                continue
            dsp = DspPeaks()
            lib.RangeDsp_Candidates(pcm, position, c.byref(dsp))
            peaks = Peaks()
            peaks.count, peaks.overflow = dsp.count, dsp.overflow
            for i in range(dsp.count):
                peaks.peak[i].quality = dsp.peak[i].quality
                for p in range(3):
                    peaks.peak[i].offset[p] = round(
                        (dsp.peak[i].position[p] - position.value - p * 640) * SAMPLE_NS)
            return position.value, peaks
        return None

    def compare(a, b):
        if a is None or b is None:
            return {'status': 'detection_failed'}
        delta, quality, spread = c.c_int64(), c.c_uint32(), c.c_uint32()
        base = round((b[0] - a[0]) * SAMPLE_NS)
        status = lib.Test_Pair(c.byref(a[1]), c.byref(b[1]), base,
                               c.byref(delta), c.byref(quality), c.byref(spread))
        # Swapping the identical observed peak sets must preserve the decision.
        reverse_delta, reverse_quality, reverse_spread = c.c_int64(), c.c_uint32(), c.c_uint32()
        reverse = lib.Test_Pair(c.byref(b[1]), c.byref(a[1]), -base,
                                c.byref(reverse_delta), c.byref(reverse_quality), c.byref(reverse_spread))
        assert status == reverse
        if status == 1:
            assert delta.value == -reverse_delta.value
        return {'status': STATUS[status], 'delta_ns': delta.value,
                'quality': quality.value, 'spread_ns': spread.value}

    bandpass = butter(3, [3000, 5000], btype='bandpass', fs=16000, output='sos')
    report = {'assumption': 'same synthetic frequency response on both boards; no added echoes',
              'sweeps': []}
    for filtered in (False, True):
        def receive(at):
            values = recording(at)
            return detect(sosfilt(bandpass, values) if filtered else values)

        for direction in (1, -1):
            counts, worst = Counter(), None
            for phase_index in range(100):
                phase = phase_index / 100
                at = 100 + phase
                a = receive(at)
                b = receive(at + direction * DELAY_NS / SAMPLE_NS)
                result = compare(a, b)
                counts[result['status']] += 1
                if result['status'] == 'accepted':
                    error = result['delta_ns'] - direction * DELAY_NS
                    if worst is None or abs(error) > abs(worst['error_ns']):
                        worst = {'phase': phase, 'error_ns': error,
                                 'error_mm_at_20C': round(error * 343420 / 1e9, 3)}
            report['sweeps'].append({'bandpass_3_to_5_kHz': filtered,
                                     'source_side': 'A' if direction == 1 else 'B',
                                     'counts': dict(counts), 'worst_accepted_error': worst})

        # Keep a deterministic example separate from phase-averaged acceptance.
        at = 100.47
        a = receive(at)
        report['filtered_example' if filtered else 'unfiltered_example'] = {
            'phase': .47,
            'A_side': compare(a, receive(at + DELAY_NS / SAMPLE_NS)),
            'B_side': compare(a, receive(at - DELAY_NS / SAMPLE_NS))}
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

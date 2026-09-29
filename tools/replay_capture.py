"""Offline capture experiment; never modifies firmware or uses distance to detect.

Input: verified R000001..R000016 captures. The baseline calls actual C DSP.
Experimental detection scans complete PCM, then groups/refines arrival peaks.
Recorded clock fits are used only to map independent board detections to time.
Distances/round labels enter evaluation only. R000012 has no distance label.
This is not a cycle-accurate MCU scheduler or a complete network replay.
"""
import argparse
import ctypes as ct
import hashlib
import json
from pathlib import Path
import re
import time

import numpy as np
from scipy.ndimage import maximum_filter1d
from scipy.signal import correlate, find_peaks

from decode_capture import parse, fnv1a, SCHEMAS

FS, PULSE, STEP, SIGNATURE = 48000, 1536, 1920, 5376
WINDOW, ADVANCE, SLICE = 6912, 768, 192
ROOT = Path(__file__).resolve().parents[1]


class DspPeak(ct.Structure):
    _fields_ = [('position', ct.c_float * 3), ('quality', ct.c_uint32)]


class DspPeaks(ct.Structure):
    _fields_ = [('peak', DspPeak * 3), ('count', ct.c_uint32), ('overflow', ct.c_uint32)]


class Diag(ct.Structure):
    _fields_ = [('coarse', ct.c_uint32), ('fail', ct.c_uint32 * 3),
                ('gap', ct.c_uint32), ('signatures', ct.c_uint32),
                ('none', ct.c_uint32), ('overflow', ct.c_uint32),
                ('coarseMax', ct.c_float), ('pulseMax', ct.c_float * 3),
                ('gapMin', ct.c_float), ('haveGap', ct.c_uint8)]


def templates():
    source = (ROOT / 'Core/Inc/range_template_48k.h').read_text()
    return [np.array([int(v) for v in re.search(name + r'\[1536\] = \{(.*?)\}',
             source, re.S)[1].split(',') if v.strip()], dtype=float)
            for name in ('rangeUp', 'rangeDown')]


def load(folder, board):
    data = (folder / (board + '.RNG')).read_bytes()
    marker = (folder / 'COMPLETE.TXT').read_text()
    size, checksum = re.search(board + r' bytes=(\d+) fnv1a=([\da-fA-F]+)', marker).groups()
    assert len(data) == int(size) and fnv1a(data) == int(checksum, 16)
    meta, pcm, anchors, logs = parse(data)
    assert meta['rate'] == FS and meta['profile'] == 1
    rows = {k: [dict(zip(SCHEMAS[k], row[1:])) for row in logs if row[0] == k]
            for k in SCHEMAS}
    return meta, np.frombuffer(pcm, dtype='<i2').reshape(-1, 2)[:, 0].copy(), np.array(anchors), rows


def mapper(meta, anchors, rows):
    # Fit the same local sample clock to all retained anchors. IRQ jitter remains
    # a limitation; absolute intercept uncertainty is measured in the report.
    frame = anchors[:, 0].astype(float) - float(anchors[0, 0] - anchors[0, 3])
    t0 = float(anchors[0, 1])
    period, intercept = np.polyfit(frame, anchors[:, 1].astype(float) - t0, 1)
    clocks = rows['clock']
    events = {r['event']: int(r['local_ns']) for r in rows['event']}
    known = np.array([events[r['event']] for r in clocks], dtype=float)

    def local(pos):
        return t0 + intercept + np.asarray(pos) * period

    def master(pos):
        ns = local(pos)
        if meta['board'] == 'A':
            return ns
        r = clocks[int(np.argmin(abs(known - float(np.mean(ns)))))]
        return ns - float(r['offset']) - (ns - float(r['origin'])) * float(r['slope'])
    residual = anchors[:, 1].astype(float) - local(frame)
    return local, master, {'period_ns': float(period), 'anchor_residual_max_ns': float(abs(residual).max())}


def baseline(lib, x):
    lib.RangeDsp_Find.argtypes = [ct.POINTER(ct.c_int16), ct.c_uint, ct.c_uint,
                                ct.POINTER(ct.c_float), ct.POINTER(ct.c_uint32)]
    lib.RangeDsp_Candidates.argtypes = [ct.POINTER(ct.c_int16), ct.c_float, ct.POINTER(DspPeaks)]
    diag = Diag.in_dll(lib, 'rangeDspDiagnostics')
    ct.memset(ct.addressof(diag), 0, ct.sizeof(diag))
    last = None
    out = []
    for base in range(0, len(x) - WINDOW + 1, ADVANCE):
        if hasattr(lib, 'RangeDsp_Reset'):
            lib.RangeDsp_Reset()
        ptr = x[base:].ctypes.data_as(ct.POINTER(ct.c_int16))
        for scan in range(0, ADVANCE, SLICE):
            if last is not None and base + scan <= last + FS // 4:
                continue
            pos, q = ct.c_float(), ct.c_uint32()
            status = lib.RangeDsp_Find(ptr, scan, scan + SLICE, ct.byref(pos), ct.byref(q))
            pending = 0
            while status < 0:
                pending += 1
                assert pending < 1000, 'Unbounded DSP pending state'
                status = lib.RangeDsp_Find(ptr, scan, scan + SLICE, ct.byref(pos), ct.byref(q))
            if not status:
                continue
            at = base + pos.value
            if last is not None and int(at) <= last + FS // 4:
                continue
            last = int(at)
            peaks = DspPeaks()
            lib.RangeDsp_Candidates(ptr, pos, ct.byref(peaks))
            out.append({'pos': at, 'q': q.value, 'candidate_count': peaks.count,
                        'overflow': peaks.overflow,
                        'candidate_peaks': [{'pos': float(np.mean([base+peaks.peak[i].position[p]-p*STEP for p in range(3)])),
                                             'pulse_pos': [base+peaks.peak[i].position[p]-p*STEP for p in range(3)],
                                             'q': peaks.peak[i].quality/1000}
                                            for i in range(peaks.count)]})
    return out, {'coarse': diag.coarse, 'pulse_fail': list(diag.fail), 'gap': diag.gap,
                 'signatures': diag.signatures, 'no_candidates': diag.none}


def scores(x, tpls):
    v = x.astype(float)
    sums = np.r_[0., np.cumsum(v)]
    energy = np.r_[0., np.cumsum(v * v)]
    variance = energy[PULSE:] - energy[:-PULSE] - (sums[PULSE:] - sums[:-PULSE]) ** 2 / PULSE
    result = []
    for tpl in tpls:
        dot = correlate(v, tpl, mode='valid', method='fft')
        s = dot * dot / np.maximum(variance * (tpl @ tpl), 1.)
        s[variance < PULSE * 32 ** 2] = 0
        result.append(s)
    n = len(x) - SIGNATURE + 1
    return np.array([result[0][:n], result[1][STEP:STEP+n], result[0][2*STEP:2*STEP+n]])


def arrivals(s, min_quality=.3):
    # Full-rate absolute quality gate with independent +/-3-sample pulse peaks.
    # No gap-energy veto: its effect is evaluated separately against baseline.
    joint = s.sum(axis=0)
    weak = maximum_filter1d(s, size=7, axis=1, mode='constant').min(axis=0)
    seeds, _ = find_peaks(joint)
    seeds = seeds[weak[seeds] >= min_quality ** 2]
    raw = []
    for k in seeds:
        if k < 4 or k >= s.shape[1]-4:
            continue
        pos, quality = [], []
        for p in range(3):
            j = k-3 + int(np.argmax(s[p, k-3:k+4]))
            l, v, r = s[p, j-1:j+2]
            if v < min_quality**2 or v < l or v <= r:
                break
            den = l - 2*v + r
            shift = .5*(l-r)/den if den < -1e-5 else 0.
            if abs(shift) > .5:
                shift = 0.
            pos.append(float(j + shift))
            quality.append(float(np.sqrt(v)))
        if len(pos) != 3 or max(pos)-min(pos) > 3:
            continue
        raw.append({'pos': float(np.mean(pos)), 'pulse_pos': pos, 'q': min(quality)})
    # Merge carrier sidelobes into a local arrival group by retaining its best
    # three-pulse quality. Distinct echoes outside +/-0.4 ms remain candidates.
    selected = []
    for r in sorted(raw, key=lambda r: -r['q']):
        if all(abs(r['pos']-p['pos']) > .0004*FS for p in selected):
            selected.append(r)
    selected.sort(key=lambda r: r['pos'])
    # Fixed 12 ms event search after the first qualifying arrival. Independent
    # of source side, board spacing, known signature timestamps and logs.
    groups = []
    at = 0
    while at < len(selected):
        start = selected[at]['pos']
        group = []
        while at < len(selected) and selected[at]['pos'] <= start+.012*FS:
            group.append(selected[at]); at += 1
        groups.append(group)
        while at < len(selected) and selected[at]['pos'] < start+.25*FS:
            at += 1
    return groups


def select(groups, mode, relative=.75):
    out = []
    for group in groups:
        best = max(r['q'] for r in group)
        if mode == 'strongest':
            chosen = max(group, key=lambda r: r['q'])
        elif mode == 'weighted':
            first = group[0]['pos']
            # 10% score penalty per millisecond, quality floor still applies.
            chosen = max(group, key=lambda r: r['q'] / (1+.1*(r['pos']-first)/48))
        else:
            chosen = next(r for r in group if r['q'] >= relative*best)
        out.append(chosen)
    return out


def pair(a, b, maps):
    out, used = [], set()
    for aa in a:
        ta = maps['A'](aa['pulse_pos'])
        choices = [(abs(float(np.mean(maps['B'](bb['pulse_pos'])-ta))), i, bb)
                   for i, bb in enumerate(b) if i not in used]
        if not choices:
            continue
        diff, i, bb = min(choices, key=lambda r: r[0])
        if diff > 20000000:
            continue
        used.add(i)
        ds = maps['B'](bb['pulse_pos'])-ta
        span = float(np.ptp(ds))
        out.append({'delta_ns': float(np.mean(ds)), 'mm': abs(float(np.mean(ds)))*346450/1e9,
                    'span_ns': span, 'accepted': span <= 40000,
                    'a_pos': aa['pos'], 'b_pos': bb['pos']})
    return out


def batch(pairs):
    # Same 6 samples, 60%, strict majority and 40 mm interval as firmware.
    # Range limit is applied AFTER cluster selection.
    values = sorted(int(np.floor(p['mm']+.5)) * (-1 if p['delta_ns'] < 0 else 1)
                    for p in pairs if p['accepted'])
    if len(values) < 6:
        return {'state': 'too_few', 'n': len(values)}
    clusters = [list(v for v in values[i:] if v-x <= 40) for i, x in enumerate(values)]
    group = min(clusters, key=lambda g: (-len(g), g[-1]-g[0]))
    n, used = len(values), len(group)
    center = abs(int(np.median(group)))
    state = ('unstable' if used < 6 or used*2 <= n or used*100 < n*60 else
             ('ok' if 100 <= center <= 2000 else 'out_of_range'))
    return {'state': state, 'n': n, 'used': used, 'mm': center, 'span': group[-1]-group[0]}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root', type=Path)
    ap.add_argument('--dll', type=Path, required=True)
    ap.add_argument('--rounds', nargs='+', type=int, default=list(range(1, 17)))
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--ablation-dir', type=Path, help='Optional DLLs no_gap, half_coarse, both')
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    lib = ct.CDLL(str(args.dll.resolve()))
    tpls = templates()
    results = []
    provenance = {'experiment': 'offline arrival groups v1',
                  'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  'dsp_sha256': hashlib.sha256((ROOT/'Core/Src/range_dsp.c').read_bytes()).hexdigest(),
                  'dll_sha256': hashlib.sha256(args.dll.read_bytes()).hexdigest(),
                  'parameters': {'min_quality': .3, 'min_rms': 32, 'nms_ms': .4,
                                 'search_ms': 12, 'refractory_ms': 250,
                                 'pulse_spread_samples': 3, 'pair_spread_ns': 40000},
                  'limitations': ['offline FFT, not a firmware implementation',
                                  'uses recorded clock fits, not network simulation',
                                  'known distances used for evaluation only',
                                  '16-round data already inspected; not a blind trial']}
    (args.output/'provenance.json').write_text(json.dumps(provenance, indent=2), encoding='utf-8')
    for number in args.rounds:
        started = time.perf_counter()
        folder = args.root / f'R{number:06d}'
        maps, groups, info = {}, {}, {}
        for board in 'AB':
            meta, x, anchors, rows = load(folder, board)
            local, maps[board], timing = mapper(meta, anchors, rows)
            found, diag = baseline(lib, x)
            logged = np.array([int(e['local_ns']) for e in rows['event']], dtype=float)
            differences = [float(np.min(abs(logged-local(r['pos'])))) for r in found] if len(logged) else []
            groups[board] = arrivals(scores(x, tpls))
            info[board] = {'baseline': found, 'baseline_diag': diag, 'logged_events': len(logged),
                           'nearest_logged_error_ns': differences, 'timing': timing,
                           'experimental_groups': groups[board]}
            if args.ablation_dir:
                info[board]['ablations'] = {}
                for name in ('no_gap', 'half_coarse', 'both'):
                    alt = ct.CDLL(str((args.ablation_dir/(name+'.dll')).resolve()))
                    ev, counters = baseline(alt, x)
                    info[board]['ablations'][name] = {'events': len(ev), 'diag': counters}
        modes = {}
        for mode, ratio in [('strongest', .75), ('weighted', .75), ('early65', .65), ('early75', .75), ('early85', .85)]:
            ps = pair(select(groups['A'], mode, ratio), select(groups['B'], mode, ratio), maps)
            modes[mode] = {'pairs': ps, 'batch': batch(ps)}
        truth = 1000 if number <= 3 else (1500 if number in [4,5,6,7,8,9] else
                (1700 if number in [10,11,16] else (1900 if number in [13,14,15] else None)))
        result = {'round': number, 'truth_mm': truth, 'boards': info, 'modes': modes,
                  'elapsed_s': time.perf_counter()-started}
        results.append(result)
        (args.output / f'R{number:06d}.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(number, 'baseline', [(b,len(info[b]['baseline']),info[b]['logged_events']) for b in 'AB'],
              'experimental', [(b,len(groups[b])) for b in 'AB'],
              'batches', {k:v['batch'] for k,v in modes.items()}, flush=True)
    (args.output/'summary.json').write_text(json.dumps(results, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()

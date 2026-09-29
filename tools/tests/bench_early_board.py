"""Explicit hardware benchmark; resets the ranging round after each window.

Requires pyocd, libusb-package, numpy/scipy and fresh ELF symbol dumps in TEMP.
No audio is played and no SD file is written. Run only on connected test boards.
"""
import json
import os
from pathlib import Path
import re
import sys
import time
import numpy as np
from pyocd.core.helpers import ConnectHelper
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from replay_capture import load

root = Path(sys.argv[1])
rows = json.loads((root / 'early_c_v1.json').read_text())
results = []
for board, uid in [('A', '0675FF514966504867230834'), ('B', '0667FF485153826687134138')]:
    symbols = (Path(os.environ['TEMP']) / f'range-early-{board}-symbols.txt').read_text()
    def address(name):
        line = next(s for s in symbols.splitlines() if ': ' + name + ' ' in s)
        return int(re.search(r'0x[0-9a-fA-F]+', line)[0], 16)
    bench, status = address('rangeDspBench'), address('appRangeStatus')
    with ConnectHelper.session_with_chosen_probe(unique_id=uid, target_override='cortex_m',
            connect_mode='attach', auto_unlock=False, frequency=4000000) as session:
        target = session.target
        for n, event in [(13, 0), (14, 14), (15, 0)]:
            _, pcm, _, _ = load(root / f'R{n:06d}', board)
            pos = rows[n-1]['boards'][board]['events'][event]['pos']
            base = int(pos) - 300
            data = pcm[base:base+6912].astype('<i2').tobytes()
            target.write_memory_block32(0xC0600000, np.frombuffer(data, dtype='<u4').tolist())
            target.write32(bench+4, 0)
            target.write32(bench, 0x42534D31)
            target.flush()
            start = time.monotonic()
            while target.read32(bench+4) != 2:
                if time.monotonic()-start > 10:
                    raise RuntimeError('Benchmark did not finish')
                time.sleep(.02)
            values = target.read_memory_block32(bench, 8)
            result = dict(zip(['request','status','calls','max_us','total_us','found','quality','position_q16'], values))
            result.update(board=board, round=n, event=event, expected_position=pos-base)
            assert result['found'] == 1, result
            assert abs(result['position_q16']/65536-(pos-base)) < 2, result
            print(result, flush=True)
            results.append(result)
        time.sleep(5)
        print(board, 'drops/gaps/overruns', target.read32(status+8), target.read32(status+64), target.read32(status+68), flush=True)
(root / 'early_c_hardware_benchmark.json').write_text(json.dumps(results, indent=2))

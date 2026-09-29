"""Offline exploratory comparison on R1..R19; no firmware changes.
Known distances label evaluation only. R12 remains unlabeled.
"""
import argparse
import json
from pathlib import Path
from replay_capture import load, mapper, scores, templates, arrivals, select, pair, batch

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('old_root',type=Path)
    ap.add_argument('new_root',type=Path)
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();result=[]
    for n in range(1,20):
        root=args.old_root if n<=16 else args.new_root
        maps={};groups={}
        for board in 'AB':
            meta,x,anchors,rows=load(root/f'R{n:06d}',board)
            _,maps[board],_=mapper(meta,anchors,rows)
            s=scores(x,templates())
            groups[board]={q:arrivals(s,q) for q in (.3,.24)}
        modes={}
        for name,q,ratio in [('current',.3,.65),('earliest_only',.3,0),
                             ('lower_floor_only',.24,.65),('lower_floor_earlier',.24,.55)]:
            chosen={b:select(groups[b][q],'early',ratio) for b in 'AB'}
            ps=pair(chosen['A'],chosen['B'],maps)
            modes[name]=dict(events={b:len(chosen[b]) for b in 'AB'},batch=batch(ps),pairs=ps)
        truth=1000 if n<=3 else 1500 if n<=9 else 1700 if n in (10,11,16) else None if n==12 else 1900
        result.append(dict(round=n,truth_mm=truth,modes=modes))
        print(n,{name:data['batch'] for name,data in modes.items()},flush=True)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2),encoding='utf-8')

if __name__=='__main__':main()

"""Diagnose recorded early1 trials. Known distance is used ONLY to inspect a
small expected-arrival region, never to generate a deployable detector result.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from scipy.signal import find_peaks
from replay_capture import load, mapper, scores, templates, arrivals, select

def local_candidates(s, lo, hi):
    seeds, _ = find_peaks(s.sum(axis=0)[lo:hi])
    out = []
    for k in seeds+lo:
        if k < 4 or k+4 >= s.shape[1]:
            continue
        positions, qualities = [], []
        for p in range(3):
            at = k-3+int(np.argmax(s[p,k-3:k+4]))
            l,c,r = s[p,at-1:at+2]
            if c < l or c <= r:
                break
            den=l-2*c+r
            shift=.5*(l-r)/den if den < -1e-5 else 0
            if abs(shift)>.5:shift=0
            positions.append(float(at+shift));qualities.append(float(np.sqrt(c)))
        if len(positions)==3 and np.ptp(positions)<=3:
            out.append(dict(pos=float(np.mean(positions)),q=min(qualities),pulse_q=qualities,
                            spread=float(np.ptp(positions))))
    return sorted(out,key=lambda v:-v['q'])

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root',type=Path)
    ap.add_argument('--rounds',type=int,nargs='+',required=True)
    ap.add_argument('--distance-mm',type=float,required=True)
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    report=[];plots=[]
    for n in args.rounds:
        boards={};ss={};maps={};local={};allgroups={}
        for b in 'AB':
            meta,x,anchors,rows=load(args.root/f'R{n:06d}',b)
            local[b],maps[b],fit=mapper(meta,anchors,rows)
            ss[b]=scores(x,templates());allgroups[b]=arrivals(ss[b])
            boards[b]=dict(meta=meta,fit=fit,last_status=rows['status'][-1],last_dsp=rows['dsp'][-1],
              anchor_epochs=np.unique(anchors[:,2]).tolist(),max_anchor_step_ns=int(np.diff(anchors[:,1]).max()),
              clipped_samples=int(np.sum(abs(x.astype(np.int32))>=32767)),
              logged_events=len(rows['event']),pairs=rows['pair'])
        # B is near source in these user-described tests. Estimate A direct
        # arrival from B's accepted peak plus known geometric delay (diagnostic).
        selected_b=select(allgroups['B'],'early65',.65)
        observations=[]
        for i,bpeak in enumerate(selected_b):
            expected=(float(maps['B'](bpeak['pos']))+args.distance_mm/346450*1e9-float(local['A'](0)))/(float(local['A'](1))-float(local['A'](0)))
            k=int(round(expected));lo=max(4,k-19);hi=min(ss['A'].shape[1]-4,k+20)
            peaks=local_candidates(ss['A'],lo,hi)
            region=local_candidates(ss['A'],max(4,k-48),min(ss['A'].shape[1]-4,k+300))
            strongest=region[0] if region else None
            best=peaks[0] if peaks else None
            observations.append(dict(event=i+1,expected_a_sample=expected,
                direct_region_best=best,region_strongest=strongest,
                direct_relative=(best['q']/strongest['q'] if best and strongest else None),
                pulse_max_in_direct_region=np.sqrt(ss['A'][:,lo:hi].max(axis=1)).tolist()))
            if i==0:
                left=max(0,k-48);right=min(ss['A'].shape[1],k+240)
                plots.append((n,(np.arange(left,right)-expected)/48,np.sqrt(ss['A'][:,left:right]),best,strongest,expected))
        entry=dict(round=n,truth_mm=args.distance_mm,boards=boards,expected_region_observations=observations)
        report.append(entry)
        qs=[r['direct_region_best']['q'] for r in observations if r['direct_region_best']]
        ratios=[r['direct_relative'] for r in observations if r['direct_relative'] is not None]
        print(n,'groups',len(allgroups['A']),len(allgroups['B']), 'direct_q_range',min(qs) if qs else None,max(qs) if qs else None,
              'relative_range',min(ratios) if ratios else None,max(ratios) if ratios else None,
              'direct_pass_030',sum(q>=.3 for q in qs), 'of',len(observations),flush=True)
        print('first',observations[0],flush=True)
    (args.output/'diagnostics.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axes=plt.subplots(len(plots),1,figsize=(10,3*len(plots)),squeeze=False)
    for ax,(n,t,q,best,strongest,expected) in zip(axes[:,0],plots):
        for p in range(3):ax.plot(t,q[p],label=f'Pulse {p+1}',lw=1)
        ax.axhline(.3,color='black',ls='--',label='Quality gate 0.30')
        ax.axvspan(-19/48,19/48,color='green',alpha=.08,label='Expected direct-path region')
        ax.set(title=f'R{n:06d}: A board, first signature',xlabel='Delay relative to expected direct arrival (ms)',ylabel='Normalized correlation')
        ax.set_ylim(0,.8);ax.grid(alpha=.2);ax.legend(fontsize=8,ncol=3)
    fig.tight_layout();fig.savefig(args.output/'correlation.png',dpi=160);plt.close(fig)

if __name__=='__main__':main()

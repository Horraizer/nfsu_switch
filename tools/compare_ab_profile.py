#!/usr/bin/env python3
"""Compare sealed AB logs. Refuse unmatched routes or incomplete GPU reports."""
import argparse
import csv
import io
import json
import math
import re
from pathlib import Path

def read(path):
    text=path.read_text(encoding="utf-8",errors="replace")
    results=re.findall(r"^AB_RESULT (.*)$",text,re.M)
    routes=re.findall(r"^AB_ROUTE (.*)$",text,re.M)
    if len(results)!=1 or len(routes)!=1: raise ValueError("Need one complete AB run per log")
    result=dict(re.findall(r"(\w+)=(\S*)",results[0]))
    route=dict(re.findall(r"(\w+)=(\S*)",routes[0]))
    if result.get("valid")!="1" or result.get("cancelled")!="0": raise ValueError("Invalid or cancelled run")
    tables={}
    for kind in ("CPU","GPU"):
        header=re.search(r"^AB_"+kind+r"_COLUMNS (.*)$",text,re.M)
        if not header: raise ValueError("Missing columns")
        lines=re.findall(r"^AB_"+kind+r" (.*)$",text,re.M)
        rows=list(csv.DictReader(io.StringIO(header[1]+"\n"+"\n".join(lines))))
        table={}
        for row in rows:
            if None in row or any(v is None for v in row.values()):raise ValueError("Truncated row")
            row={k:float(v) for k,v in row.items()}
            if not all(math.isfinite(v) for v in row.values()):raise ValueError("Nonfinite row")
            n=int(row["frame"])
            if n in table:raise ValueError("Duplicate frame")
            table[n]=row
        tables[kind]=table
    if set(tables['CPU'])!=set(range(1,int(result['frames'])+1)):raise ValueError("Missing CPU frames")
    active={n for n,r in tables['CPU'].items() if r['active']==1}
    if not active or set(tables['GPU'])!=active:raise ValueError("Missing/extra GPU frames")
    for n in active:
        if tables['GPU'][n]['valid']!=1 or tables['GPU'][n]['tick']!=tables['CPU'][n]['tick']:
            raise ValueError("Invalid or unpaired GPU report")
    clocks=re.findall(r"^RUN_STATS build=.*CPU_HZ=(\d+) CPU_OK=(\d+) GPU_HZ=(\d+) GPU_OK=(\d+) EMC_HZ=(\d+) EMC_OK=(\d+)",text,re.M)
    if len(clocks)!=1 or any(clocks[0][i]!='1' for i in (1,3,5)):raise ValueError("Missing valid clocks")
    return dict(result=result,route=route,tables=tables,active=active,clocks=clocks[0])

def stats(values):
    v=sorted(values)
    return dict(mean=sum(v)/len(v),p50=v[math.ceil(.50*len(v))-1],
        p95=v[math.ceil(.95*len(v))-1],p99=v[math.ceil(.99*len(v))-1],maximum=v[-1])

def compare(a,b):
    if a['route']!=b['route'] or a['clocks']!=b['clocks'] or a['active']!=b['active']:
        raise ValueError("Route, clocks or driving windows differ")
    if b['result']['mode']!='replay':raise ValueError("B must verify a replay")
    for n,r in a['tables']['CPU'].items():
        other=b['tables']['CPU'].get(n,{})
        if any(r[k]!=other.get(k) for k in ('tick','ticks','active')):raise ValueError("Frame schedule differs")
    result={'valid':True,'route':a['route'],'clocks':a['clocks'],'paired_frames':len(a['active']),
        'limitation':'Paired timing differences require the build manifests for attribution. Detailed diagnostic barriers perturb GPU scheduling; presentation return is not display scanout.',
        'metrics':{}}
    for table in ('CPU','GPU'):
        for key in next(iter(a['tables'][table].values())):
            if not key.endswith('_ms') or key not in next(iter(b['tables'][table].values())):continue
            av=[a['tables'][table][n][key] for n in sorted(a['active'])]
            bv=[b['tables'][table][n][key] for n in sorted(a['active'])]
            result['metrics'][table+'.'+key]={'A':stats(av),'B':stats(bv),
                'paired_delta_B_minus_A':stats([y-x for x,y in zip(av,bv)])}
    result['worst_gpu_delta_frames']=sorted([
        dict(frame=n,tick=a['tables']['CPU'][n]['tick'],
            A_ms=a['tables']['GPU'][n]['total_ms'],B_ms=b['tables']['GPU'][n]['total_ms'],
            delta_ms=b['tables']['GPU'][n]['total_ms']-a['tables']['GPU'][n]['total_ms'])
        for n in a['active']],key=lambda r:r['delta_ms'],reverse=True)[:20]
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('a',type=Path);parser.add_argument('b',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    result=compare(read(args.a),read(args.b))
    args.output.write_text(json.dumps(result,indent=2,ensure_ascii=True)+'\n',encoding='ascii')
    print(json.dumps({'paired_frames':result['paired_frames'],'gpu':result['metrics']['GPU.total_ms']},indent=2))

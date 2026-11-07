"""Original45B150/45B900 control flow with explicit query-result services.

This verifies selection/copy/order, not the substituted45B050 geometry and
collision producer. Every service hook is recorded; no whole-planner claim.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from physics256_native import Machine,bits
from world_body_trace import Image
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for k in ('root','output','evidence'):ap.add_argument('--'+k,type=Path,required=True)
    a=ap.parse_args();data=(a.root/'speed.exe').read_bytes();sha=hashlib.sha256(data).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    cases=[]
    # target ids are stable synthetic world-node addresses in both consumers.
    ids=[0x1140000,0x1141000,0x1142000]
    def add(name,current=(1,0,0.,False),prior=True,available=True,neighbors=None,rows=None,backup=True):
        cases.append(dict(name=name,current=current,prior=prior,available=available,neighbors=neighbors or [],rows=rows or [],backup=backup))
    def row(kind,target,score,gap,flag=False,obstacle=0):return (kind,target,flag,score,gap,obstacle)
    for prior in (False,True):
      add('ordinary-'+str(prior),prior=prior,rows=[row(1,0,20.,99999.)])
      add('no-candidate-'+str(prior),prior=prior)
      add('retained-blocked-'+str(prior),prior=prior,rows=[row(1,0,20.,.3)])
      for policy in range(4):
        neighbors=[(ids[0],True,policy,1.),(ids[1],True,1,1.)]
        for delta in (-.10001,-.1,0.,.1,.10001):
          add('sides-%s-%d-%s'%(prior,policy,delta),prior=prior,neighbors=neighbors,
              rows=[row(1,0,20.,.3),row(2,ids[0],21.,8.),row(3,ids[0],21.+delta,8.),row(2,ids[1],20.,8.)])
    for kind in (2,3):
      for matching in (False,True):
       for avail in (False,True):
        add('retained-backup-%d-%s-%s'%(kind,matching,avail),current=(kind,ids[0],0.,False),backup=avail,
            neighbors=[(ids[0],True,1,-1.)],rows=[row(kind,ids[0],20.,.8,obstacle=ids[0] if matching else ids[1]),row(1,0,-1.,99999.)])
      add('opposing-'+str(kind),current=(kind,ids[0],0.,False),neighbors=[(ids[0],True,kind,-1.)],
          rows=[row(kind,ids[0],-1.,99999.),row(kind,ids[0],22.,9.,True)])
    add('unavailable-retained',current=(2,ids[0],0.,False),available=False,rows=[row(1,0,20.,99999.)])
    add('ineligible',neighbors=[(ids[0],False,1,-1.)],rows=[row(1,0,20.,.2),row(2,ids[0],30.,8.)])
    add('offset-retained',current=(1,0,1.,False),rows=[row(1,0,20.,.2)])
    lines=['//45B150/45B900. Explicit query-result hooks; selection-only. '+sha];receipts=[]
    for c in cases:
        m=Machine(Image(data));m.allowed=[(0x45b150,0x45b851),(0x45b860,0x45b897),(0x45b8a0,0x45b9e8),
            (0x458f10,0x458f41),(0x459064,0x459096),(0x44a000,0x44a051),(0x45cfb0,0x45cfeb)]
        ctx,desc,path,side,actor=0x1100000,0x1120000,0x1121000,0x1122000,0x1130000
        m.put(ctx,actor);m.put(ctx+4,len(c['neighbors']));m.put(actor+0x70,1.,'f');m.put(actor+0x74,0.,'f')
        def putdesc(at,d):
            m.put(at,d[0]);m.put(at+4,d[1]);m.put(at+8,d[2],'f');m.put(at+12,int(d[3]),'B')
        def getdesc(at):return (m.get(at),m.get(at+4),m.get(at+8,'f'),bool(m.get(at+12,'B')))
        putdesc(desc,c['current']);calls=[]
        for i,(ident,eligible,policy,dot) in enumerate(c['neighbors']):
            m.put(ctx+8+i*0x38+0x34,ident);obj=ident+0x300;body=ident+0x400
            m.put(ident+0x1d4,obj);m.put(obj+0x1c,body);m.put(body+0x90,dot,'f')
        def admission(x):
            d=getdesc(x.reg('ecx'));is_current=x.reg('ecx')==desc
            allowed=c['available'] if is_current else (c['backup'] and d[0] in (1,2,3))
            x.ret(value=int(allowed))
        def eligible(x):
            at=x.get(x.reg('esp')+4);i=(at-ctx-8)//0x38
            x.ret(value=int(c['neighbors'][i][1]))
        def policy(x):
            at=x.get(x.reg('esp')+8);i=(at-ctx-8)//0x38
            x.ret(8,value=c['neighbors'][i][2])
        def find(x):
            target=x.reg('edi');answer=0
            for i,n in enumerate(c['neighbors']):
                if n[0]==target:answer=ctx+8+i*0x38;break
            x.ret(value=answer)
        def evaluation(x):
            sp=x.reg('esp');d=getdesc(x.get(sp+4));side_admitted=bool(x.get(sp+8));score,gap,obstacle=-1.,99999.,0
            for r in c['rows']:
                if (d[0],d[1],d[3])==r[:3]:score,gap,obstacle=r[3:];break
            calls.append(dict(descriptor=d,side=side_admitted,score=score,gap=gap,obstacle=obstacle))
            x.put(x.get(sp+12),score,'f');x.put(x.get(sp+16),obstacle);x.put(x.get(sp+20),gap,'f')
            x.put(x.reg('edi'),1,'B');x.put(x.reg('edi')+16,float(len(calls)),'f')
            x.ret(0x1c,value=int(score> -1 and gap>4.))
        def fallback(x):
            out=x.get(x.reg('esp')+4);x.put(out,1,'B');x.put(out+16,-99.,'f');x.ret()
        m.hook(0x45ad30,'supplied_category_admission',callback=admission)
        m.hook(0x456780,'supplied_neighbor_eligibility',callback=eligible)
        m.hook(0x45c0d0,'supplied_side_policy',callback=policy)
        m.hook(0x57b520,'supplied_neighbor_lookup',callback=find)
        m.hook(0x45b050,'supplied_geometry_fitness_collision',callback=evaluation)
        m.hook(0x45c370,'fallback_path_capture',callback=fallback)
        m.hook(0x45bfb0,'final_publication_not_under_test',pop=12)
        m.run(0x45b150,(desc,path,ctx,bits(4.),side,int(c['prior'])))
        expected=dict(descriptor=getdesc(desc),side=bool(m.get(side,'B')),found=bool(m.reg('eax')&255),marker=m.get(path+16,'f'))
        def descriptor(d):return '{%d,%du,%s,%s}'%(d[0],d[1],literal(d[2]),str(d[3]).lower())
        neighbors='{'+','.join('{%du,%s,%du,%s}'%(n[0],str(n[1]).lower(),n[2],literal(n[3])) for n in c['neighbors'])+'}'
        rows='{'+','.join('{%d,%du,%s,%s,%s,%du}'%(r[0],r[1],str(r[2]).lower(),literal(r[3]),literal(r[4]),r[5]) for r in c['rows'])+'}'
        lines+=['// '+c['name'],'{%s,%s,%s,%s,%du,%s,%du,%s,%s,%s,%s,%s,%du},'%(descriptor(c['current']),str(c['prior']).lower(),
            str(c['available']).lower(),str(c['backup']).lower(),len(c['neighbors']),neighbors,len(c['rows']),rows,
            descriptor(expected['descriptor']),str(expected['side']).lower(),str(expected['found']).lower(),literal(expected['marker']),len(calls))]
        receipts.append(dict(input=c,result=expected,evaluations=calls,hooks=m.events,visited=sorted(m.visited)))
    a.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    a.evidence.write_text(json.dumps(dict(scope=__doc__,sha256=sha,cases=receipts),indent=2)+'\n',encoding='ascii')
    print('Original candidate selection:',len(cases),'cases; explicit query service hooks recorded')
if __name__=='__main__':main()

"""Authenticated original44BD10/44C010 on supplied single-player race snapshots.
No semantic code replacement. Captured initialized tables are explicit inputs;
this witness does not claim integrated original scheduler/physical trajectories.
"""
import argparse,hashlib,json
import capstone
from pathlib import Path
from world_body_trace import Image
from controller293_scene import PairedController,HUMAN,REF
from controller293_strategy import CONTROLLER,ACTION
from controller293_planner import inputs
from physics256_native import ACTOR
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for k in ('root','output','evidence'):ap.add_argument('--'+k,type=Path,required=True)
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes()
    digest=hashlib.sha256(data).hexdigest()
    if digest!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported retail')
    pair=PairedController(Image(data),inputs(args.root),event_rides=True);m=pair.m
    first_event=len(m.events)
    image=Image(data);dis=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    for a,b in ((0x44bd10,0x44c00b),(0x44c010,0x44c245),(0x4175c0,0x4175f2),
                (0x583ea0,0x583f18),(0x583f20,0x583fe7),(0x401c40,0x401c59)):
        m.instructions.update({i.address:i.size for i in dis.disasm(image.read(a,b-a),a)})
    cfg=m.get(ACTOR+0x14);status=m.get(0x73619c);own=m.get(ACTOR+0x1c);other=m.get(HUMAN+0x1c)
    def floats(at,n):return [m.get(at+4*j,'f') for j in range(n)]
    cases=[]
    # distance/progress, velocity delta, rank, action eligibility, update cadence,
    # difficulty, history, wrapping and disabled-comparison paths.
    specs=[(.30,.31,30.,25.,1,0,1.,0,2000,True,True,False,240000),
        (.30,.20,30.,25.,0,1,.5,1,2000,True,True,False,240000),
        (.30,.31,5.,40.,2,0,1.3,2,2000,True,True,False,240000),
        (.30,.30,70.,10.,0,3,.2,2,2000,True,True,False,240000),
        (.30,.30,10.,70.,3,0,1.5,0,2000,True,True,False,240000),
        (.99,.01,25.,25.,1,0,1.,1,2000,True,True,True,240000),
        (.01,.99,25.,25.,0,1,1.,1,2000,True,True,True,240000)]
    base=specs[0]
    for age in (1199,1200,1201):specs.append(base[:8]+(age,)+base[9:])
    for column,value in ((9,False),(10,False),(12,0)):
        s=list(base);s[column]=value;specs.append(tuple(s))
    selections=[(s,2,False,1,1.) for s in specs]
    # Original44C010 selects the alternate bank by traffic density2/3,
    # not by race type. Exercise both banks, event lengths, ranks and history.
    length=m.get(0x7372b0,'f')
    for traffic in range(4):
        for laps,point_to_point in ((2,False),(4,False),(4,True)):
            for difficulty in (0,3,6,9):
                for gap in (-500.,0.,240.):
                    history=.7 if gap<0 else 1.2 if gap>0 else 1.
                    srank,trank=(3,0) if gap<0 else (0,3) if gap>0 else (1,2)
                    spec=(.3+gap/length,.3,30.,25.,srank,trank,history,difficulty,2000,True,True,False,240000)
                    selections.append((spec,laps,point_to_point,traffic,.9 if traffic<2 else 1.1))
    for difficulty in range(10):
        grace=m.get(0x77a044+difficulty*0x54,'f')
        for ticks in (int(grace*4000)-1,int(grace*4000),int(grace*4000)+1):
            spec=list(base);spec[7]=difficulty;spec[12]=ticks
            selections.append((tuple(spec),2,False,1,1.))
    for spec,laps,point_to_point,traffic,reference_weight in selections:
        sp,tp,ss,ts,srank,trank,history,difficulty,age,allows,compare,wrap,race_ticks=spec
        m.put(0x78a31c,laps);m.put(0x78a318,int(point_to_point));m.put(0x78a300,traffic)
        m.put(0x78a404,reference_weight,'f')
        tick=500000;previous=tick-age
        m.put(cfg+0x735,difficulty,'b');m.put(own+0x14,sp,'f');m.put(other+0x14,tp,'f')
        m.put(own+0xa,srank,'b');m.put(other+0xa,trank,'b')
        m.put(ACTOR+0x3d4,ss,'f');m.put(ACTOR+0x3d0,ss,'f');m.put(HUMAN+0x3d4,ts,'f')
        m.put(ACTOR+0x18,0);m.put(CONTROLLER+0x46c,1,'B');m.put(CONTROLLER+0x3f8,ACTION)
        m.put(ACTION+0x1f2,int(allows),'B');m.put(0x73ad34,tick);m.put(status+0x14,race_ticks)
        m.put(CONTROLLER+0x420,previous);m.put(CONTROLLER+0x470,0);m.put(CONTROLLER+0x4b8,.875,'f');m.put(ACTOR+0x404,1.125,'f')
        m.put(0x78a400,int(compare));m.put(0x78a368,int(wrap));m.put(0x7361c4,1);m.put(0x7361bc,REF)
        m.put(m.get(REF+0xa8)+0xc,history,'f')
        # Reconstruct original event-table selection, never substitute a curve.
        length=0
        if m.get(0x78a318)==0:
            for j in (1,0):
                if m.get(0x78a31c)>=m.get(0x70077c+4*j):length=j;break
        mode=1 if m.get(0x78a300) in (2,3) else 0
        row=(mode+length*2)*0x58
        low=[floats(0x779c78+row+8*j,2) for j in range(11)]
        high=[floats(0x779dd8+row+8*j,2) for j in range(11)]
        tuning=[low,high,floats(0x779f38,16),floats(0x779f78,16),floats(0x779fd0+8*trank,2),floats(0x779fb0+8*trank,2),
            m.get(0x77a49c,'f'),m.get(0x77a044+difficulty*0x54,'f'),m.get(0x77a4a0,'f'),m.get(0x77a010+difficulty*0x54,'f')]
        weight=m.get(0x78a404+4*m.get(REF+0x10),'f')
        inp=[tick,1,race_ticks,difficulty,srank,trank,1,True,True,False,False,allows,False,compare,wrap,
             m.get(own+0x14,'f'),m.get(other+0x14,'f'),m.get(0x7372b0,'f'),ss,ts,weight,history]
        m.run(0x44bd10,eax=CONTROLLER,budget=3000000)
        target=m.get(CONTROLLER+0x470)
        if target not in (0,REF):raise ValueError('unexpected reference')
        expected=[m.get(CONTROLLER+0x420),int(target==REF),m.get(CONTROLLER+0x4b8),m.get(ACTOR+0x404)]
        cases.append(dict(input=inp,tuning=tuning,before=[previous,0,.875,1.125],expected=expected,selection=[laps,point_to_point,traffic]))
    def lit(v):
        if isinstance(v,bool):return str(v).lower()
        if isinstance(v,float):return literal(v)
        if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
        return str(v)
    # std::array of struct requires the extra aggregate brace around elements.
    def tuning(c):
        t=c['tuning'];return '{{'+lit(t[0])+'},{'+lit(t[1])+'},'+','.join(lit(v) for v in t[2:])+'}'
    lines=['// Original44BD10 outputs; scale outputs are uint32 float bits.','const CatchupCase catchups[]={']
    lines += ['{'+lit(c['input'])+','+tuning(c)+','+lit(c['before'])+','+lit(c['expected'])+','+','.join(lit(v) for v in c['selection'])+'},' for c in cases]
    lines+=['};']
    args.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=digest,fpu_control_word='007F',
        query_hook_events=m.events[first_event:],cases=cases),indent=2)+'\n',encoding='ascii')
    print('Original catchup:',len(cases),'cases, selected',sum(c['expected'][1] for c in cases))
if __name__=='__main__':main()

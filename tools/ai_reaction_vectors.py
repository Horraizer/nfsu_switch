"""Original44C2E0/429100/429190 on declared actor/neighbor snapshots.
Only audio start/stop/status are explicit external services, recorded per case.
"""
import argparse,hashlib,json,struct
from pathlib import Path
import capstone
from world_body_trace import Image
from controller293_scene import PairedController,HUMAN
from controller293_planner import inputs,CTX
from controller293_strategy import CONTROLLER
from controller293_native import EXEC_RANGES
from physics256_native import ACTOR
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for k in ('root','output','evidence'):ap.add_argument('--'+k,type=Path,required=True)
    args=ap.parse_args();raw=(args.root/'speed.exe').read_bytes();sha=hashlib.sha256(raw).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    im=Image(raw);pair=PairedController(im,inputs(args.root),event_rides=True);m=pair.m
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    for a,b in list(EXEC_RANGES)+[(0x44c2e0,0x44c548),(0x429100,0x429182),(0x429190,0x429266),
        (0x4216f0,0x421740),(0x456780,0x45685c),(0x57b520,0x57b550),(0x57a6b0,0x57a700)]:
        m.instructions.update({i.address:i.size for i in md.disasm(im.read(a,b-a),a)})
    node=m.get(0x73b07c);othernode=0
    while node!=0x73b07c:
        ref=m.get(node+0x1d4)
        if ref==m.get(HUMAN+0x82c):othernode=node
        node=m.get(node)
    if not othernode:raise ValueError('missing target node')
    cfg=m.get(ACTOR+0x14);othercfg=m.get(HUMAN+0x14);body=0x119a000;world=m.get(0x7361f8);m.put(HUMAN+0xc,body)
    m.put(CONTROLLER+0x3e0,CTX);m.put(CTX,ACTOR);m.put(CTX+0x388,0,'b')
    m.put(CTX+8+12,2.,'f');m.put(CTX+8+16,100.,'f')
    entry=CTX+64;m.put(entry+12,2.,'f');m.put(entry+52,othernode)
    m.put(ACTOR+0x24,3)
    def state():return [m.get(ACTOR+0x844),*[m.get(ACTOR+x,'f') for x in (0x848,0x850,0x84c)],*[bool(m.get(ACTOR+x,'B')) for x in (0x83e,0x83f,0x840)]]
    def setstate(v):
        m.put(ACTOR+0x844,v[0])
        for x,n in zip((0x848,0x850,0x84c),v[1:4]):m.put(ACTOR+x,n,'f')
        for x,n in zip((0x83e,0x83f,0x840),v[4:]):m.put(ACTOR+x,int(n),'B')
    cases=[];start=len(m.events)
    def run_case(values,before,seed):
        tick,relative,count,selector,selfrole,role,disabled,racing,eligible=values
        setstate(before);m.put(0x73ad34,tick);m.put(world+0x2d0,seed);m.put(othercfg+4,role)
        m.put(cfg+4,selfrole);m.put(ACTOR+0x24,selector)
        m.put(body+0xc54,int(disabled));m.put(m.get(0x7361f0),9 if racing else 4);m.put(CTX+4,count)
        m.put(entry+4,relative,'f');m.put(entry+16,110. if eligible else 90.,'f')
        m.run(0x44c2e0,(CONTROLLER,CTX),eax=othernode,budget=3000000)
        cases.append([values,before,seed,state(),m.get(world+0x2d0)])
    for role in (1,2,3):
      for relative in (-4.,0.,50.,51.):
       for seed in (1,0x60000000,0x7ffffffe,0xffffffff):
        for busy in (False,True):
         tick=500000;before=[tick if busy else 0,.2 if busy else 0.,.1 if busy else 0.,.3 if busy else 0.,False,False,False]
         run_case([tick,relative,2,3,2,role,False,True,True],before,seed)
    for selector in (2,3,4):
     for eligible in (False,True):
      for disabled in (False,True):
       for racing in (False,True):
        for role in (1,2):
         run_case([500000,0.,2,selector,2,role,disabled,racing,eligible],[0,0.,0.,0.,False,False,False],12345)
    for count in (5,6):
     for age in (2399,2400,2401):
      for selfrole in (2,3):
       run_case([10000+age,-4.,count,3,selfrole,2,False,True,True],[10000,.2,.1,.3,True,True,True],12345)
    # No timing is inferred from the producer: exercise the actual post-physics
    # consumer independently at every boundary with declared audio state.
    consumer=[];driver=m.get(ACTOR+0x30);table=0x119e000;m.put(driver,table)
    running=False;calls=[]
    def status(x):x.ret(value=int(running))
    def play(x):calls.append('start');x.ret()
    def stop(x):calls.append('stop');x.ret()
    for off,at,name,fn in [(0x48,0x119e100,'audio-start',play),(0x4c,0x119e110,'audio-stop',stop),(0x50,0x119e120,'audio-status',status)]:
        m.put(table+off,at);m.hook(at,name,callback=fn)
    for direction in (False,True):
      for age in (0,399,400,401,799,800,801,1199,1200,1201,2000):
       for manual in (False,True):
        for running in (False,True):
         before=[10000,.1,.2,.3,True,True,direction];setstate(before)
         m.put(cfg+4,2);m.put(ACTOR+0x854,int(manual),'B');m.put(0x73ad34,10000+age);calls.clear()
         m.run(0x429190,esi=ACTOR,budget=300000)
         desired=running
         if calls:desired=calls[-1]=='start'
         consumer.append([before,10000+age,manual,bool(m.get(ACTOR+0x855,'B')),desired])
    def lit(v):
        if isinstance(v,bool):return str(v).lower()
        if isinstance(v,float):return literal(v)
        if isinstance(v,(list,tuple)):return '{'+','.join(map(lit,v))+'}'
        return str(v)+'u' if v>0x7fffffff else str(v)
    lines=['// Unchanged original44C2E0 producer and429190 consumer.']
    for name,typ,rows in [('reactions','ReactionCase',cases),('reaction_outputs','ReactionOutputCase',consumer)]:
        lines+=['const '+typ+' '+name+'[]={']+[lit(v)+',' for v in rows]+['};']
    args.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(sha256=sha,scope=__doc__,cases=cases,outputs=consumer,services=m.events[start:]),indent=2)+'\n',encoding='ascii')
    print('Original reaction producers',len(cases),'outputs',len(consumer))
if __name__=='__main__':main()

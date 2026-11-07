"""Original strategy queries on declared world/context snapshots.
Executes unchanged450EC0/452630/462740; no semantic result hooks. This is not
an integrated race. Neighbor records and caller mode/time inputs are supplied.
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
    for key in ('root','output','evidence'):ap.add_argument('--'+key,type=Path,required=True)
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes()
    identity='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest()!=identity:raise ValueError('unsupported executable')
    im=Image(data);pair=PairedController(im,inputs(args.root),event_rides=True);m=pair.m
    first_event=len(m.events)
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    for a,b in list(EXEC_RANGES)+[(0x450ec0,0x451185),(0x44d5f0,0x44d637),(0x452630,0x452682),
        (0x4216f0,0x421740),(0x462740,0x4627db),(0x567370,0x5673dc),(0x435ca0,0x435d30),(0x452b40,0x452bec),
        (0x57a680,0x57a6b0),(0x5314d0,0x5314d3),(0x577fd0,0x577fd3)]:
        m.instructions.update({i.address:i.size for i in md.disasm(im.read(a,b-a),a)})
    chain=m.get(CTX+0x3a8);cell=0x119e000;output=0x119e100
    m.put(chain+4,1);m.put(chain+8,cell);m.put(cell+0x58,0.,'f')
    m.put(cell+0x20,-5.,'f');m.put(cell+0x3c,5.,'f')
    m.put(CTX+4,2);m.put(CTX+0x388,0,'b')
    nodes=[];node=m.get(0x73b07c)
    while node!=0x73b07c:
        ref=m.get(node+0x1d4)
        if ref==m.get(ACTOR+0x82c):selfnode=node
        if ref==m.get(HUMAN+0x82c):othernode=node
        nodes.append(node);node=m.get(node)
    cfg=m.get(ACTOR+0x14);othercfg=m.get(HUMAN+0x14);clock=m.get(0x73619c)
    modes=[[m.get(0x77a340+8*i,'i'),bool(m.get(0x77a344+8*i,'B'))] for i in range(3)]
    baseline_clock=bytes(m.uc.mem_read(clock,32));blocks=[]
    cases=[('human-near',-10.,1.,0.,20.,True,0,False),('human-far-slow',-40.,1.,0.,20.,True,0,False),
        ('human-far-fast',-40.,2.,0.,20.,True,0,False),('rival-slow',-10.,1.,0.,20.,False,0,False),
        ('rival-fast',-10.,8.,0.,20.,False,0,False),('outside',-10.,6.,6.,20.,True,0,False),
        ('slow-actor',-10.,6.,0.,9.,True,0,False),('too-far',-50.,30.,0.,30.,True,0,False),
        ('not-behind',0.,6.,0.,20.,True,0,False),('relative-limit',-10.,-5.,0.,20.,True,0,False),
        ('time-limit',-8.,6.,0.,30.,False,0,False),('min-speed',-10.,6.,0.,10.,True,0,False),
        ('held-start',-10.,6.,0.,20.,True,0,True)]
    cases += [('mode'+str(i),-30.,1.,0.,20.,True,i,False) for i in (1,2)]
    for label,gap,relative,lateral,speed,human,mode,hold in cases:
        m.uc.mem_write(clock,baseline_clock);m.put(0x73ad34,500000)
        if hold:m.put(clock+0x10,500000)
        m.run(0x435ca0,(clock,ACTOR),budget=3000000);actual_hold=bool(m.reg('eax')&255)
        m.put(cfg+0x736,mode,'b');m.put(othercfg+4,1 if human else 2)
        m.uc.mem_write(CTX+8,bytes(112));m.put(CTX+8+16,100.,'f');m.put(CTX+8+12,2.,'f');m.put(CTX+8+52,selfnode)
        at=CTX+64;m.put(at+4,relative,'f');m.put(at+8,1.,'f');m.put(at+12,2.,'f')
        m.put(at+16,100.+gap,'f');m.put(at+20,lateral,'f');m.put(at+48,0);m.put(at+52,othernode)
        ref=m.get(othernode+0x1d4);state=m.get(ref+0x1c)
        m.put(state+0xe4,0)
        for k,v in enumerate((speed,0.,0.)):m.put(state+0x90+4*k,v,'f')
        m.put(output,0xdeadbeef);m.run(0x450ec0,(output,),eax=CTX,budget=3000000)
        priority=m.reg('eax');priority=priority-(1<<32) if priority>=1<<31 else priority
        target=m.get(output)
        if target not in (0,HUMAN):raise ValueError('unexpected target')
        blocks.append([gap,relative,lateral,speed,human,*modes[mode],actual_hold,priority,target!=0])
    roll=[]
    for contacts,up in [(4,(0.,0.,-1.)),(1,(0.,0.,1.)),(1,(0.,1.,0.)),(0,(0.,0.,-1.)),(2,(0.,1.,0.)),(0,(0.,0.,0.)),(1,(.71,0.,.70)),(1,(.70,0.,.71))]:
        m.put(ACTOR+0x3cc,contacts)
        for k,v in enumerate(up):m.put(ACTOR+0xb0+4*k,v,'f')
        m.run(0x452630,eax=CTX,budget=3000000);roll.append([contacts,list(up),m.reg('eax')==500])
    gates=[];world=m.get(0x7361f8);difficulty=m.get(cfg+0x735,'b');row=0x77a004+difficulty*0x54
    cooldown=m.get(0x6b734c,'f');prob=m.get(row,'f')
    for target,previous,age,seed in [(123,0,0,1),(123,0,0,0x7ffffffe),(123,123,0,1),
        (123,123,round(cooldown*4000)-1,1),(123,123,round(cooldown*4000),1),(124,123,0,1),
        (0,123,round(cooldown*4000),1)]:
        tick=500000;last=tick-age;m.put(CONTROLLER+0x4d0,previous);m.put(CONTROLLER+0x4d4,last)
        m.put(0x73ad34,tick);m.put(world+0x2d0,seed)
        m.run(0x462740,(target,0),ecx=CONTROLLER,budget=3000000)
        gates.append([target,tick,cooldown,prob,previous,last,seed,bool(m.reg('eax')&255),
            m.get(CONTROLLER+0x4d0),m.get(CONTROLLER+0x4d4),m.get(world+0x2d0)])
    virtuals=[];traffic=[]
    tables=(0x6c98d0,0x6c98b0,0x6c97fc,0x6c97d4,0x6c97b4)
    for kind,table in enumerate(tables,1):
        m.run(m.get(table+0xc));admit=bool(m.reg('eax')&255)
        m.run(m.get(table+0x1c));rebuild=bool(m.reg('eax')&255)
        virtuals.append([kind,admit,rebuild])
    othercontroller,otherstrategy,targetout,timeout=0x119d000,0x119d100,0x119d200,0x119d204
    m.put(HUMAN+0x2c,othercontroller);m.put(othercontroller+0x3fc,otherstrategy)
    collision_target=othernode
    def collision(x):
        x.put(x.get(x.reg('esp')+20),1.25,'f');x.ret(20,value=collision_target)
    m.hook(0x457590,'declared-physical-collision-query',callback=collision)
    for kind,table in enumerate(tables,1):
     for role in (2,3):
      for force in (False,True):
       for present in (False,True):
        m.put(otherstrategy,table);m.put(othercfg+4,role)
        m.put(0x78a344,int(force),'B');m.put(0x7361a8,0)
        collision_target=othernode if present else 0;m.put(targetout,0xdeadbeef);m.put(timeout,9.,'f')
        m.run(0x452b40,(timeout,),eax=CTX,edi=targetout,budget=3000000)
        priority=m.reg('eax');priority=priority-(1<<32) if priority>=1<<31 else priority
        traffic.append([kind,role,force,present,priority,m.get(targetout)!=0])
    def lit(v):
        if isinstance(v,bool):return str(v).lower()
        if isinstance(v,float):return literal(v)
        if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
        return str(v)
    lines=['// Unchanged original query code, declared input snapshots.']
    for name,typ,items in [('modes','ModeCase',modes),('blocks','BlockCase',blocks),('rolls','RollCase',roll),('gates','GateCase',gates),('strategy_virtuals','StrategyVirtualCase',virtuals),('traffic_admission','TrafficAdmissionCase',traffic)]:
        lines+=['const '+typ+' '+name+'[]={']+[lit(v)+',' for v in items]+['};']
    args.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,fpu_control_word='007F',
        query_hook_events=m.events[first_event:],modes=modes,blocks=blocks,rollover=roll,gates=gates,strategy_virtuals=virtuals,traffic_admission=traffic),indent=2)+'\n',encoding='ascii')
    print('Original blocking',len(blocks),'selected',sum(v[-1] for v in blocks),'rollover',len(roll),'gates',len(gates))
if __name__=='__main__':main()

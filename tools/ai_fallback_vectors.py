"""Original action4 speed decisions on supplied fresh neighbor snapshots.

Full450480 executes;45AE10 path emission is a declared no-op witness. This
isolates the speed stage, not path correctness or a live multi-car race.
"""
import argparse,hashlib,json,struct
from pathlib import Path
import capstone
from world_body_trace import Image
from controller293_scene import PairedController,HUMAN
from controller293_planner import inputs,CTX
from controller293_strategy import ACTION
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
    first_event=len(m.events);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    for a,b in list(EXEC_RANGES)+[(0x450480,0x450654),(0x44d5f0,0x44d637),(0x57b480,0x57b4d7)]:
        m.instructions.update({i.address:i.size for i in md.disasm(im.read(a,b-a),a)})
    path_calls=[]
    def path(machine):
        sp=machine.reg('esp');path_calls.append([machine.get(sp+4*i) for i in range(1,6)])
        machine.ret(20)
    m.hook(0x45ae10,'declared-path-producer-not-under-test',callback=path)
    node=m.get(0x73b07c)
    while node!=0x73b07c:
        ref=m.get(node+0x1d4)
        if ref==m.get(ACTOR+0x82c):selfnode=node
        if ref==m.get(HUMAN+0x82c):othernode=node
        node=m.get(node)
    state=m.get(m.get(othernode+0x1d4)+0x1c);cfg=m.get(HUMAN+0x14)
    limit=m.get(0x6b7628,'f');desired_gap=m.get(0x6b7620,'f')
    # role, self index, category, gap, lateral, lane lateral, velocity, old cap.
    cases=[(r,1,0,2.,0.,8.,[10.,0.,0.],30.) for r in (1,2,3)]
    cases += [(2,i,0,2.,0.,8.,[10.,0.,0.],30.) for i in (-1,0,2)]
    cases += [(2,1,c,2.,3.,.5,[3.,4.,12.],30.) for c in (-1,4,5,10,11,16,17)]
    for gap in (-2.,0.,limit-.000001,limit,limit+.000001):
        cases.append((2,1,0,gap,0.,8.,[10.,0.,0.],30.))
    for lateral in (-2.,-1.9999999,1.9999999,2.,2.0000002):
        cases.append((2,1,0,2.,lateral,8.,[10.,0.,0.],30.))
    for cap in (0.,10.-(desired_gap-2.),30.):
        cases.append((2,1,0,2.,0.,8.,[10.,0.,0.],cap))
    cases.append((1,1,0,1.1234567,.12345678,8.,[3.345678,-4.765432,1.987654],30.))
    rows=[]
    for role,index,category,gap,lateral,lane,velocity,cap in cases:
        m.put(CTX+4,2);m.put(CTX+0x388,index,'b');m.put(CTX+0x394,category,'i')
        m.put(CTX+0x398,1);m.put(cfg+4,role)
        m.uc.mem_write(CTX+8,bytes(112))
        for at,id_,distance,lat,ll in [(CTX+8,othernode,gap,lateral,lane),(CTX+64,selfnode,0.,0.,0.)]:
            m.put(at+8,1.,'f');m.put(at+16,distance,'f');m.put(at+20,lat,'f');m.put(at+52,id_)
            for j in range(6):m.put(at+24+4*j,ll,'f')
        m.put(state+0xe4,0)
        for j,v in enumerate(velocity):m.put(state+0x90+4*j,v,'f')
        m.put(ACTION+0x1e4,cap,'f');m.put(ACTION+0x1e8,9.,'f');m.put(ACTION+0x1ec,7.,'f')
        for at in (0x1e0,0x1f0,0x1f1,0x1f2):m.put(ACTION+at,0,'B')
        m.run(0x450480,(CTX,),ecx=ACTION,budget=3000000)
        result=[m.get(ACTION+at) for at in (0x1e4,0x1e8,0x1ec)]
        flags=[bool(m.get(ACTION+at,'B')) for at in (0x1f0,0x1f1,0x1f2,0x1e0)]
        rows.append([role,index,category,gap,lateral,lane,velocity,cap,limit,desired_gap,result,flags])
    def lit(v):
        if isinstance(v,bool):return str(v).lower()
        if isinstance(v,float):return literal(v)
        if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
        return str(v)
    args.output.write_text('// Original450480 speed stage;45AE10 is a declared path witness.\nconst FallbackCase fallback_cases[]={\n'+
        '\n'.join(lit(r)+',' for r in rows)+'\n};\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,fpu_control_word='007F',
        cases=rows,query_hook_events=m.events[first_event:],path_calls=path_calls,
        scope='Original speed stage with fresh supplied body state; path producer bypassed explicitly'),indent=2)+'\n',encoding='ascii')
    print('Original fallback speed:',len(rows),'cases;',len(path_calls),'declared path calls')
if __name__=='__main__':main()

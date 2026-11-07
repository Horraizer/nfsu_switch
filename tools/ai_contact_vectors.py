"""Original contact selection and action12 on supplied collision snapshots.

Selection runs the actual contact-list/endpoint lookup, without query hooks.
Action update uses declared path/road witnesses; no path or live-race claim.
"""
import argparse,hashlib,json,struct
from pathlib import Path
import capstone
from world_body_trace import Image
from controller293_scene import PairedController,HUMAN
from controller293_planner import inputs,CTX
from controller293_strategy import ACTION,CONTROLLER
from physics256_native import ACTOR,bits
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('root','output','evidence'):ap.add_argument('--'+key,type=Path,required=True)
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes()
    identity='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest()!=identity:raise ValueError('unsupported executable')
    im=Image(data);pair=PairedController(im,inputs(args.root),event_rides=True);m=pair.m
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    for a,b in [(0x44ed70,0x44efc3),(0x468820,0x46889d),(0x421970,0x42197d),
        (0x57b4e0,0x57b51a),(0x57b550,0x57b584),(0x466450,0x4664aa),
        (0x57a6b0,0x57a6e2),(0x452270,0x45229f),(0x4522a0,0x452568),
        (0x44e850,0x44e94e),(0x44e950,0x44e9b6),(0x466680,0x466687),
        (0x57b480,0x57b4d7),(0x578730,0x5787d7)]:
        instructions=list(md.disasm(im.read(a,b-a),a))
        if sum(i.size for i in instructions)!=b-a:raise ValueError('incomplete range '+hex(a))
        m.instructions.update({i.address:i.size for i in instructions})
    base=0x7000000;m.uc.mem_map(base,0x100000)
    actor=[ACTOR]+[base+i*0x2000 for i in range(1,5)]
    ref=[m.get(ACTOR+0x82c)]+[base+0x10000+i*0x100 for i in range(1,5)]
    node=[base+0x11000+i*0x200 for i in range(5)]
    cfg=[m.get(ACTOR+0x14)]+[base+0x13000+i*0x800 for i in range(1,5)]
    body=[base+0x16000+i*0x200 for i in range(5)]
    fresh=base+0x18000
    for i in range(1,5):
        m.uc.mem_write(actor[i],bytes(m.uc.mem_read(HUMAN,0x1000)))
        m.uc.mem_write(ref[i],bytes(m.uc.mem_read(m.get(HUMAN+0x82c),0x100)))
        m.uc.mem_write(cfg[i],bytes(m.uc.mem_read(m.get(HUMAN+0x14),0x750)))
        m.put(actor[i]+0x82c,ref[i]);m.put(actor[i]+0x14,cfg[i]);m.put(actor[i]+0x1c,cfg[i])
        m.put(ref[i]+0x90,actor[i]);m.put(ref[i]+0x1c,fresh)
        m.put(node[i]+0x1d4,ref[i])
    m.put(node[0]+0x1d4,ref[0]);m.put(ACTOR+0x3c,body[0]);m.put(body[0]+0x140,ACTOR)
    def scene(gaps,roles,records):
        m.put(CTX+4,4);m.put(CTX+0x388,0,'b');m.uc.mem_write(CTX+8,bytes(4*56))
        for i,gap in enumerate([0.]+gaps):
            at=CTX+8+i*56;m.put(at+16,100.+gap,'f');m.put(at+52,node[i]);m.put(at+12,2.,'f')
            if i:
                m.put(cfg[i]+4,max(roles[i-1],0));m.put(ref[i]+0x24,6 if roles[i-1]>=0 else 5)
                m.put(actor[i]+0x1c,cfg[i] if records[i-1] else 0)
    def contacts(rows):
        m.put(body[0]+0x1c,len(rows),'b')
        m.put(0x779be0,base+0x20000+0x250 if rows else 0x779be0)
        for i,(value,ka,kb,ia,ib) in enumerate(rows):
            p=base+0x20000+i*0x1000;c=p+0x300
            endpoints=[]
            for j,(kind,id_) in enumerate(((ka,ia),(kb,ib))):
                b=body[0] if kind==0 and id_==1 else p+0x500+j*0x200
                m.put(b+0x15,kind,'b');m.put(b+0x140,actor[id_-1] if kind==0 else ref[id_-1])
                endpoints.append(b)
            m.put(p+0x258,endpoints[0]);m.put(p+0x25c,endpoints[1]);m.put(p+0x264,c)
            m.put(c+0x60,value,'f');m.put(c+0x64,p)
            m.put(p+0x250,p+0x1000+0x250 if i+1<len(rows) else 0x779be0)
    def target_id(address):
        if not address:return 0
        value=m.get(address+52)
        return node.index(value)+1
    selection=[];start_event=len(m.events)
    tests=[]
    standard=[[1.,0,0,1,2],[1.,0,0,3,1],[1.,0,0,1,4]]
    for gaps in ([8.,3.,-2.],[-2.,3.,8.],[3.,-2.,8.],[0.,0.,0.]):
        for roles in ([2,2,2],[2,1,2],[1,2,1]):tests.append((gaps,roles,[True]*3,standard,-1.,True))
    for ka,kb,ia,ib in [(0,4,1,2),(4,0,2,1),(0,3,1,2),(0,0,1,5)]:
        tests.append(([8.,3.,-2.],[2,1,2],[True]*3,[[1.,ka,kb,ia,ib]],-1.,True))
    for strength in (-1.0000001192092896,-1.,-.9999999403953552):
        tests.append(([8.,3.,-2.],[2,1,2],[True]*3,[[strength,0,0,1,2]],-1.,True))
    for only in (False,True):
        tests.append(([8.,3.,-2.],[-1,2,2],[True]*3,standard,-1.,only))
        tests.append(([8.,3.,-2.],[1,2,2],[False,True,True],standard,-1.,only))
    tests.append(([8.,3.,-2.],[2,1,2],[True]*3,[],-1.,True))
    for gaps,roles,records,rows,minimum,only in tests:
        scene(gaps,roles,records);contacts(rows)
        m.run(0x44ed70,(CTX,bits(minimum),int(only)),budget=1000000);chosen=target_id(m.reg('eax'))
        m.put(ACTION+0x200,node[3]);m.run(0x452270,(CTX,),ecx=ACTION,budget=1000000)
        complete=bool(m.reg('eax')&255);after=node.index(m.get(ACTION+0x200))+1
        selection.append([gaps,roles,records,rows+[[0.,0,0,0,0]]*(3-len(rows)),len(rows),minimum,only,chosen,complete,after])
    selection_events=m.events[start_event:]
    if selection_events:raise ValueError('unexpected contact selection hook')

    # Action12's path and road query remain explicit supplied witnesses.
    calls=[];road=[0]
    def path(machine):
        sp=machine.reg('esp');d=machine.get(sp+4)
        calls.append([1,machine.get(d),node.index(machine.get(d+4))+1 if machine.get(d+4) else 0])
        machine.ret(20)
    def missing(machine):
        calls.append([2,0,0]);machine.ret(24)
    def road_query(machine):
        calls.append([3,road[0],0]);machine.ret(0,road[0])
    m.hook(0x45ae10,'declared-forward-path-witness',callback=path)
    m.hook(0x45b150,'declared-missing-target-path-witness',callback=missing)
    m.hook(0x44e9c0,'declared-road-class-witness',callback=road_query)
    config=[m.get(a,'f') for a in (0x6b769c,0x6b76a0,0x6b76a4,0x6b76a8,0x6b76ac)]
    controller_speed=m.get(0x6cc844,'f')
    m.put(CTX,ACTOR);m.put(ACTOR+0x2c,CONTROLLER);m.put(CONTROLLER,0x6c89f0)
    physics=m.get(ACTOR+0x20)
    slowdown=[m.get(physics+x,'f') for x in (0x264,0x268,0x110,0xdc,0xfc)]+[m.get(0x7000ac,'f')]
    cases=[]
    for human in (False,True):
        for gap in (-5.,-1.20001,-1.2,0.,1.2,1.20001,5.):
            for lateral in (-1.,0.,1.):cases.append([human,gap,lateral,0,2,0,4.,True,False])
    cases += [[False,5.,1.,0,2,1,4.,True,False],[True,5.,-1.,0,2,1,4.,True,False],
              [False,5.,0.,11,3,0,.001,True,False],[False,5.,0.,16,3,0,-2.,True,True],
              [True,5.,1.,0,2,0,0.,False,False],[False,5.,1.,17,2,0,-2.,True,False]]
    updates=[];start_event=len(m.events)
    for human,gap,lateral,category,role,road_value,relative,present,opposite in cases:
        scene([gap,0.,0.],[1 if human else 2,2,2],[True]*3)
        m.put(CTX+4,2 if present else 1);m.put(CTX+0x394,category,'i');m.put(cfg[0]+4,role)
        at=CTX+64;m.put(at+20,lateral,'f');m.put(at+4,relative,'f')
        for j in range(6):m.put(at+24+4*j,-lateral,'f')
        velocity=[17.123456 if category==17 else 31.123456,3.234567,1.345678]
        direction=[-1. if opposite else 1.,0.,0.]
        m.put(fresh+0xe4,0)
        for j,v in enumerate(velocity):m.put(fresh+0x90+4*j,v,'f')
        # Original578730 returns refreshed body+10 (forward basis).
        for j,v in enumerate(direction):m.put(fresh+0x10+4*j,v,'f')
        for j,v in enumerate([1.,0.,0.]):m.put(ACTOR+0x90+4*j,v,'f')
        m.put(ACTOR+0x3d4,30.,'f');m.put(ACTION+0x200,node[1]);m.put(CTX+0x398,1)
        m.put(CTX+0x39c,0);m.put(CTX+0x3a0,0);m.put(CTX+0x3a4,0,'B')
        for offset,value in ((0x1e4,37.),(0x1e8,9.),(0x1ec,7.)):m.put(ACTION+offset,value,'f')
        for offset in (0x1e0,0x1f0,0x1f1,0x1f2):m.put(ACTION+offset,0,'B')
        road[0]=road_value;calls.clear()
        try:m.run(0x4522a0,(CTX,),ecx=ACTION,budget=1000000)
        except Exception:
            print('Action case',len(updates),'actor/controller',hex(m.get(CTX)),hex(m.get(ACTOR+0x2c)),hex(m.get(CONTROLLER)),
                  'registers',{r:hex(m.reg(r)) for r in ('esp','ecx','ebx','ebp','edi','esi')},
                  'stack',[hex(m.get(m.reg('esp')+i*4)) for i in range(9)])
            raise
        output=[m.get(ACTION+x) for x in (0x1e4,0x1e8,0x1ec)]
        flags=[bool(m.get(ACTION+x,'B')) for x in (0x1f0,0x1f1,0x1f2,0x1e0)]
        descriptor=m.get(CTX+0x398)
        updates.append([human,gap,lateral,category,role,road_value,relative,present,opposite,
                        velocity,config,controller_speed,slowdown,output,flags,descriptor,list(calls)])
    def lit(v):
        if isinstance(v,bool):return str(v).lower()
        if isinstance(v,float):return literal(v)
        if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
        return str(v)
    lines=['// Original44ED70/452270; original4522A0 with declared path/road witnesses.']
    lines+=['const ContactSelectionCase contact_selection_cases[]={']+[lit(v)+',' for v in selection]+['};']
    lines+=['const ContactUpdateCase contact_update_cases[]={']+[lit(v[:-1])+',' for v in updates]+['};']
    args.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,fpu_control_word='007F',
        selection=selection,selection_hooks=selection_events,updates=updates,
        action_hooks=m.events[start_event:],scope='Supplied contact/body snapshots; path and road witnesses only for action update'),indent=2)+'\n',encoding='ascii')
    print('Original contact selection/lifecycle:',len(selection),'unhooked cases; action12:',len(updates),'path/road-witness cases')
if __name__=='__main__':main()

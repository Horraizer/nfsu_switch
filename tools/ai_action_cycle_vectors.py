"""Original owner, constructors, contact predicates, paths and updates in one cycle.
Only strategy factory choices are supplied; no path/contact/road result hooks.
"""
import argparse,hashlib,json
from pathlib import Path
import capstone
from unicorn.x86_const import UC_X86_REG_EIP
from world_body_trace import Image
from controller293_scene import PairedController,HUMAN
from controller293_planner import inputs,CTX
from controller293_strategy import CONTROLLER,ACTION
from controller293_native import EXEC_RANGES
from physics256_native import ACTOR,bits
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('root','output','evidence'):ap.add_argument('--'+key,type=Path,required=True)
    a=ap.parse_args();data=(a.root/'speed.exe').read_bytes()
    sha=hashlib.sha256(data).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    im=Image(data);pair=PairedController(im,inputs(a.root),event_rides=True);m=pair.m
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    ranges=list(EXEC_RANGES)+[(0x45c760,0x45cafd),(0x45cb00,0x45ce94),(0x45cea0,0x45cfa1),
        (0x45c370,0x45c4fa),(0x45c500,0x45c54a),(0x45c550,0x45c636),(0x45c640,0x45c75e),
        (0x5a6310,0x5a65a4),(0x5a7270,0x5a733f),(0x4594d0,0x4596a6),(0x4596b0,0x459710),
        (0x459710,0x459865),(0x57b480,0x57b4d7),(0x44d5f0,0x44d637),(0x57a6b0,0x57a6e2),
        (0x57a720,0x57a752),(0x57a8e0,0x57a903),(0x5a7560,0x5a75b2),(0x401c40,0x401c59),
        (0x45ae10,0x45afce),(0x45ad30,0x45adc4),(0x458f10,0x459094),(0x45a7d0,0x45a8b2),
        (0x45a8c0,0x45a918),(0x57b520,0x57b550),(0x44ed70,0x44efc3),(0x468820,0x46889d),
        (0x421970,0x42197d),(0x57b4e0,0x57b51a),(0x57b550,0x57b584),(0x466450,0x4664aa),
        (0x452270,0x45229f),(0x4522a0,0x452568),(0x44e850,0x44e94e),(0x44e950,0x44e9b6),
        (0x466680,0x466687),(0x578730,0x5787d7),(0x44b94e,0x44b9f1),(0x44f700,0x44f741),
        (0x44a0e0,0x44a103),(0x44bc30,0x44bca6),(0x44bbd0,0x44bc25),(0x450400,0x45041a),
        (0x4521c0,0x4521f2),(0x44e2b0,0x44e335),(0x44a080,0x44a0d6),
        (0x452870,0x4528a7),(0x454c50,0x454c84),(0x452d10,0x452d15),(0x450160,0x450170),
        (0x450480,0x450660),(0x44d4d0,0x44d4fb),(0x44d410,0x44d417),
        (0x44e9c0,0x44eccb)]+[(p,p+n) for p,n in [(0x57b358,0x11),(0x44ace8,6),(0x44d0cf,0x24),(0x5a7007,0xd)]]
    for begin,end in ranges:m.instructions.update({i.address:i.size for i in md.disasm(im.read(begin,end-begin),begin)})
    pair.actor.pose(30,20.);m.put(0x73ad34,8000);m.run(0x44cf30,(CTX,CONTROLLER),budget=3000000)
    chain=m.get(CTX+0x3a8);selected=m.get(CTX+0x388,'b');saved=bytes(m.uc.mem_read(CTX+8+selected*56,56))
    node=m.get(0x73b07c);other=0
    while node!=0x73b07c:
        if m.get(node+0x1d4)==m.get(HUMAN+0x82c):other=node
        node=m.get(node)
    if not other:raise ValueError('target absent')
    ref=m.get(other+0x1d4);body=m.get(ref+0x1c);target_cell=4
    cell=m.get(chain+8+4*target_cell);route=m.get(cell+0x5c,'b');index=m.get(cell+0x5e,'h')
    raw=m.get(0x77af30+4*route)+0x27c+index*0x88
    x,y,z,fx,fy=[m.get(raw+k,'f') for k in (0,4,8,12,16)]
    lateral=m.get(raw+0x3c,'h')/256+m.get(cell+4,'f')
    for k,v in enumerate((x+fy*lateral,y-fx*lateral,z)):m.put(body+0x40+4*k,v,'f')
    for k,v in enumerate((fx*20,fy*20,0)):m.put(body+0x90+4*k,v,'f')
    m.put(body+0xe4,0);m.put(ref+0x24,6);m.put(m.get(HUMAN+0x14)+4,1)
    m.put(HUMAN+0xc,0x1151000);m.put(0x1151c54,0)
    path=0x1150000;m.uc.mem_write(path,bytes(0x1c0))
    m.run(0x45c370,(path,CTX,bits(1.),2,bits(0.),bits(99999.)),budget=3000000)
    initial_path=bytes(m.uc.mem_read(path,0x1c0));m.uc.mem_write(other+0x10,initial_path)
    m.put(CTX+4,2);m.put(CTX+0x388,0,'b');m.put(CTX+0x394,0)
    m.uc.mem_write(CTX+8,saved);m.uc.mem_write(CTX+64,bytes(56))
    for at in (CTX+8,CTX+64):
        m.put(at+8,1.,'f');m.put(at+12,2.,'f')
        for off in (20,24,28,32,36,40,44):m.put(at+off,0.,'f')
    m.put(CTX+64+16,m.get(cell+0x58,'f'),'f');m.put(CTX+64+48,target_cell);m.put(CTX+64+52,other)
    m.put(m.get(ACTOR+0x14)+4,2);m.put(CONTROLLER+0x3fc,0);m.put(CONTROLLER+0x4bc,1,'B')
    m.put(CTX+0x398,1);m.put(CTX+0x39c,0);m.put(CTX+0x3a0,0)
    # Original collision list walk over one declared current contact pair.
    self_body=0x1152000;other_body=0x1152200;pair_at=0x1153000;contact=0x1153300
    m.put(ACTOR+0x3c,self_body);m.put(self_body+0x140,ACTOR);m.put(self_body+0x15,0,'B')
    m.put(other_body+0x15,4,'B');m.put(other_body+0x140,ref)
    m.put(pair_at+0x258,self_body);m.put(pair_at+0x25c,other_body);m.put(pair_at+0x264,contact)
    m.put(contact+0x60,1.,'f');m.put(contact+0x64,pair_at);m.put(pair_at+0x250,0x779be0)
    # Declared fixed pool storage; actual567160 and44D4D0 own all links/counts.
    pool=0x1156000;first=0x1157000;second=0x1157300
    m.uc.mem_write(pool,bytes(0x40));m.put(pool+0x10,first);m.put(pool+0x28,0x300)
    m.put(first,second);m.put(second,0);m.put(0x736098,pool)
    choice=[4,100];decisions=[];factory_frame={}
    allocation_return=0x1158000;construction_return=0x1158010
    def factory(machine):
        sp=machine.reg('esp');decisions.append(machine.get(sp+8))
        factory_frame.update(sp=sp,out=machine.get(sp+4),
            saved={r:machine.reg(r) for r in ('ebx','esi','edi','ebp')})
        machine.put(sp-4,allocation_return);machine.reg('esp',sp-4)
        machine.reg('eax',machine.get(0x736098))
        machine.uc.reg_write(UC_X86_REG_EIP,0x567160)
    def allocated(machine):
        at=machine.reg('eax')
        if not at:raise ValueError('allocation refused')
        factory_frame['action']=at;kind,priority=choice
        args=(bits(-1.),) if kind==4 else (bits(-1.),other)
        sp=factory_frame['sp']-4*(len(args)+1)
        machine.put(sp,construction_return)
        for j,value in enumerate(args):machine.put(sp+4+4*j,value)
        machine.reg('esp',sp);machine.reg('esi',at);machine.reg('ecx',priority)
        machine.uc.reg_write(UC_X86_REG_EIP,0x450400 if kind==4 else 0x4521c0)
    def constructed(machine):
        machine.put(factory_frame['out'],factory_frame['action'])
        for r,value in factory_frame['saved'].items():machine.reg(r,value)
        machine.reg('esp',factory_frame['sp']);machine.ret(8)
    m.hook(0x4624d0,'declared-factory-choice-only',callback=factory)
    m.hook(allocation_return,'factory-allocation-continuation',callback=allocated)
    m.hook(construction_return,'factory-construction-continuation',callback=constructed)
    def construct(kind,priority):
        m.run(0x567160,eax=m.get(0x736098),budget=300000);at=m.reg('eax')
        if not at:raise ValueError('allocation refused')
        m.run(0x450400 if kind==4 else 0x4521c0,
              (bits(-1.),) if kind==4 else (bits(-1.),other),esi=at,ecx=priority,budget=300000)
        return at
    initial=construct(4,100);m.put(CONTROLLER+0x3f8,initial);m.put(CONTROLLER+0x3f4,20)
    config=[m.get(v,'f') for v in (0x6b769c,0x6b76a0,0x6b76a4,0x6b76a8,0x6b76ac)]
    config += [m.get(0x6cc844,'f'),m.get(0x6b7628,'f'),m.get(0x6b7620,'f')]
    lanes=[]
    for j in range(m.get(chain+4)):
        at=m.get(chain+8+4*j);r=m.get(at+0x5c,'B');n=m.get(at+0x5e,'H')
        raw=m.get(0x77af30+4*r)+0x27c+0x88*n
        lanes.append([m.get(raw+0x3c+2*k,'h')/256 for k in range(5)])
    rows=[];start=len(m.events);previous=initial_path
    for number,(kind,priority,present) in enumerate(((12,400,True),(4,100,True),(4,100,False))):
        tick=8000+number*80;m.put(0x73ad34,tick)
        m.put(self_body+0x1c,int(present),'B');m.put(0x779be0,pair_at+0x250 if present else 0x779be0)
        m.put(CTX+4,2 if present else 1)
        m.uc.mem_write(m.get(ACTOR+0x30)+0x10,previous)
        m.put(ACTOR+0x88c,1,'B');choice[:]=[kind,priority]
        m.visited.clear();m.run(0x44b94e,end=0x44b9f1,ebx=CONTROLLER,edi=CTX,budget=3000000)
        current=m.get(CONTROLLER+0x3f8);p=current+0x20;n=m.get(p,'B')

        points=[[[m.get(p+16+16*j+4*k) for k in range(3)],m.get(p+0x150+j,'B'),m.get(p+0x164+2*j,'H')] for j in range(n)]
        rows.append([tick,m.get(CTX+0x394),m.get(current+0x1f8),m.get(current+0x1f4,'i'),m.get(current+0x10),
            bool(m.get(current+4,'B')),m.get(current+8),m.get(current+0xc),
            [m.get(current+o) for o in (0x1e4,0x1e8,0x1ec)],
            [bool(m.get(current+o,'B')) for o in (0x1f0,0x1f1,0x1f2,0x1e0)],
            not bool(m.get(ACTOR+0x88c,'B')),n,m.get(p+0x1ac),points,
            0x4522a0 in m.visited,0x450480 in m.visited,0x45b150 in m.visited])
        previous=bytes(m.uc.mem_read(p,0x1c0))
    hooks=m.events[start:]
    allowed={'declared-factory-choice-only','factory-allocation-continuation','factory-construction-continuation'}
    if any(e['hook'] not in allowed for e in hooks):raise ValueError('unexpected hook '+str(hooks))
    def lit(v):
        if isinstance(v,bool):return str(v).lower()
        if isinstance(v,float):return literal(v)
        if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
        return str(v)
    a.output.write_text('// Original connected action cycle; supplied factory decisions only.\n'+
        'const float cycle_lanes[][5]='+lit(lanes)+';\n'+
        'const float cycle_config[]={'+','.join(map(literal,config))+'};\nconst CycleNative cycle_native[]={\n'+
        '\n'.join(lit(r)+',' for r in rows)+'\n};\n',encoding='ascii')
    a.evidence.write_text(json.dumps(dict(executable_sha256=sha,config=config,lanes=lanes,frames=rows,factory_decisions=decisions,
        hooks=hooks,scope='Connected original constructors/owner/contact/path/road/speed; supplied factory choices and static current poses, no live race'),indent=2)+'\n',encoding='ascii')
    print('Original connected cycle:',[r[2] for r in rows],'; factory decisions',decisions,'; hooks',len(hooks))
if __name__=='__main__':main()

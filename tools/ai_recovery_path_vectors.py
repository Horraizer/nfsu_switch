"""Original452070 recovery path on initialized contexts and supplied lane categories."""
import argparse,hashlib,json
from pathlib import Path
import capstone
from world_body_trace import Image
from controller293_scene import PairedController
from controller293_planner import inputs,CTX
from controller293_strategy import CONTROLLER,ACTION
from controller293_native import EXEC_RANGES
from physics256_native import ACTOR,bits
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('root','output','evidence'):ap.add_argument('--'+key,type=Path,required=True)
    ap.add_argument('--check-fixture',action='store_true')
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes();sha=hashlib.sha256(data).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    im=Image(data);pair=PairedController(im,inputs(args.root),event_rides=True);m=pair.m
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    ranges=list(EXEC_RANGES)+[(0x452070,0x45218f),(0x451d50,0x451dbf),(0x45c640,0x45c75e),
        (0x4594d0,0x4596a6),(0x44d0cf,0x44d0f3),(0x5a7007,0x5a7014),
        (0x44ace8,0x44acee),(0x57b31c,0x57b332),(0x57b358,0x57b369),(0x44cf30,0x44d3d4),
        (0x5a6150,0x5a6306),(0x5a71f0,0x5a726b),(0x5a7270,0x5a733f),(0x5a7390,0x5a7449),
        (0x5a75c0,0x5a7688),(0x4134b0,0x4134b7),(0x582f50,0x582f57),(0x45c370,0x45c4fa),
        (0x462840,0x4629dc),(0x4629e0,0x4629ff),(0x458bd0,0x458cbb),
        (0x4216f0,0x421740),(0x44bad0,0x44baef),(0x44adc0,0x44b060),
        (0x5a6d20,0x5a6e0f),(0x462a00,0x462a52),(0x462a60,0x462a99),(0x435ca0,0x435d30)]
    for a,b in ranges:m.instructions.update({i.address:i.size for i in md.disasm(im.read(a,b-a),a)})
    def raw(at):
        return [m.get(at+a,'f') for a in (0,4,8,12,16,0x2c,0x30)]+[m.get(at+0x3c+2*j,'h')/256 for j in [*range(5),*range(11,17)]]
    cases=[];first=len(m.events)
    selections=[(0,3,None),(30,3,None),(36,3,None),(30,4,None)]
    selections += [(30,selector,category)for selector in(3,4)for category in range(11,17)]
    for position,selector,forced_category in selections:
        pair.actor.pose(position,10.);m.put(ACTOR+0x24,3)
        m.run(0x44cf30,(CTX,CONTROLLER),budget=3000000)
        route=m.get(CTX+0x38c);point=m.get(CTX+0x390);category=m.get(CTX+0x394)
        if forced_category is not None:
            category=forced_category;m.put(CTX+0x394,category)
        if not route or (point-route-0x27c)//0x88+3>=m.get(route+0x10,'h'):raise ValueError('lookahead outside initialized route')
        chain=m.get(CTX+0x3a8);roads=[]
        for j in range(m.get(chain+4)):
            cell=m.get(chain+8+4*j);rid=m.get(cell+0x5c,'B');index=m.get(cell+0x5e,'H')
            at=m.get(0x77af30+4*rid)+0x27c+0x88*index
            roads.append([raw(at),rid,index,m.get(cell+0x58,'f')]+
                [[m.get(cell+base+4*k,'f') for k in range(7)] for base in (4,0x20,0x3c)])
        actor=[m.get(ACTOR+a,'f') for a in (0x60,0x64,0x68,0x70,0x74,0x78,0x90,0x94,0x98,0x3d4)]
        clearance=m.get(CTX+8+m.get(CTX+0x388,'b')*0x38+8,'f')
        selected=[m.get(route+0xa,'B'),(point-route-0x27c)//0x88]
        ahead=raw(point+3*0x88)
        m.put(ACTOR+0x24,selector)
        m.run(0x452070,(CTX,),ecx=ACTION,budget=3000000)
        path=ACTION+0x20;n=m.get(path,'B')
        expected=[[m.get(path+16+j*16+4*k) for k in range(3)]+
                  [m.get(path+0x150+j,'B'),m.get(path+0x164+2*j,'H')] for j in range(n)]
        cases.append([ahead,selected,category,clearance,actor,selector,len(roads),roads,n,expected,
                      m.get(path+0x1ac),m.get(ACTION+0x1e4),[m.get(ACTION+a,'B') for a in (0x1e0,0x1f0,0x1f2)]])
    def lit(v):
        if isinstance(v,float):return literal(v)
        if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
        return str(v)+'u'
    text='// Complete original452070; initialized supplied road contexts and lane categories.\nconst RecoveryPathCase recovery_paths[]={\n'+'\n'.join(lit(c)+',' for c in cases)+'\n};\n'
    if args.check_fixture:
        assert args.output.read_bytes()==text.encode('ascii'),'native recovery path fixture differs'
    else:args.output.write_bytes(text.encode('ascii'))
    args.evidence.write_text(json.dumps(dict(executable_sha256=sha,query_hooks=m.events[first:],cases=cases),indent=2)+'\n',encoding='ascii')
    print('Original complete recovery paths:',len(cases),'counts:',[c[8] for c in cases])
if __name__=='__main__':main()

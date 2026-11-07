"""Original forward road-context continuation across supplied retail poses.
The spatial road-context producer remains original code; no full race claim.
"""
import argparse,hashlib,json,struct
from pathlib import Path
import capstone
from world_body_trace import Image
from controller293_scene import PairedController
from controller293_planner import inputs,CTX
from controller293_strategy import CONTROLLER,ACTION
from physics256_native import ACTOR,bits
from controller293_native import EXEC_RANGES
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--evidence',type=Path,required=True)
    ap.add_argument('--action-policy',action='store_true',help='original45C550 offset producer with supplied action modes')
    ap.add_argument('--ordinary-action',action='store_true',help='full45AE10 ordinary branch with supplied previous driver path and offset intention')
    ap.add_argument('--rival-default',action='store_true',help='original450210 default rival action, with its strategy rebuild virtual')
    ap.add_argument('--grade',type=int,choices=range(5),help='explicit ride performance category for the default-action witness')
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes()
    identity='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest()!=identity:raise ValueError('unsupported executable')
    im=Image(data);pair=PairedController(im,inputs(args.root),event_rides=True);m=pair.m
    if args.rival_default:
        args.ordinary_action=True
        if args.grade is None:raise ValueError('default rival witness requires a supplied grade')
        m.put(m.get(ACTOR+0x10)+0x424,args.grade,'b');m.put(CTX+0x394,args.grade)
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    ranges=list(EXEC_RANGES)+[(a,a+n) for a,n in ((0x57b358,0x11),(0x44ace8,6),(0x44d0cf,0x24),(0x5a7007,0xd))]+[(0x459710,0x459865),(0x45a7d0,0x45a8b2),(0x45a8c0,0x45a918),
            (0x45c370,0x45c4fa),(0x45c640,0x45c75e),(0x4594d0,0x4596a6),
            (0x5a6150,0x5a6306),(0x5a7390,0x5a7449)]
    ranges += [(0x5a6000,0x5a607e),(0x5a75c0,0x5a7688),(0x45c550,0x45c636),(0x45ae10,0x45afcc),(0x45ad30,0x45adc4)]
    for a,b in ranges:m.instructions.update({i.address:i.size for i in md.disasm(im.read(a,b-a),a)})
    store,result=0x119f100,0x119f000;code=b'\xd9\x1d'+struct.pack('<I',result)+b'\xc3'
    m.uc.mem_write(store,code);m.instructions.update({i.address:i.size for i in md.disasm(code,store)})
    def scalar(address,**regs):
        m.run(address,budget=300000,**regs);m.run(store,budget=20);return m.get(result,'f')
    path=0x1150000;m.uc.mem_write(path,bytes(0x1c0));rows=[]
    lines=['// Original450210 default action; supplied grade and poses.' if args.rival_default else
           '// Original retained forward45C370; original spatial context, supplied poses.']
    for sequence,pose in enumerate([30,31,33,36,37]):
        pair.actor.pose(pose,20.)
        tick=500100+sequence*80;m.put(0x73ad34,tick)
        m.run(0x44CF30,(CTX,CONTROLLER),budget=3000000)
        selected=m.get(CTX+0x388,'b')
        if selected<0:raise ValueError('no selected spatial entry')
        entry=CTX+8+selected*0x38;chain=m.get(CTX+0x3a8)
        category=m.get(CTX+0x394)
        if not 0<=category<5:raise ValueError('nonordinary category')
        roads=[]
        for j in range(m.get(chain+4)):
            cell=m.get(chain+8+4*j)
            if m.get(cell)!=0x6c8f90:raise ValueError('nonforward road')
            route=m.get(cell+0x5c,'b');index=m.get(cell+0x5e,'h')
            raw=m.get(0x77af30+4*route)+0x27c+index*0x88
            roads.append(dict(route=route,index=index,v=[m.get(raw+a,'f') for a in (0,4,8,12,16,0x2c,0x30)]+
                [m.get(raw+0x3c+2*category,'h')/256,m.get(cell+4,'f'),m.get(cell+0x20,'f'),m.get(cell+0x3c,'f'),
                 m.get(raw+0x24,'f'),m.get(raw+0x28,'f'),m.get(cell+0x58,'f')]))
        actor=[m.get(ACTOR+a,'f') for a in (0x60,0x64,0x68,0x70,0x74,0x78,0x90,0x94,0x98,0x3d4)]
        selected_road=m.get(entry+0x30);initial_offset=m.get(entry+0x14,'f');clearance=m.get(entry+8,'f')
        params=[m.get(a,'f') for a in (0x77a4a8,0x6b7520,0x6b7524)]
        using_policy=args.action_policy or args.ordinary_action
        mode=(0,3,4,1,2)[sequence] if args.action_policy else 0
        elapsed=scalar(0x432c90,ecx=m.get(0x73619c)) if using_policy else -1.
        step=scalar(0x45c550,eax=mode,ecx=CTX) if using_policy else 1.25
        policy=[mode,m.get(ACTOR+0x24),actor[9],elapsed,False]
        target=(0.,-2.,2.,0.,1.)[sequence] if args.ordinary_action and not args.rival_default else 0.
        rebuild=False;category_available=True
        if args.ordinary_action:
            m.uc.mem_write(m.get(ACTOR+0x30)+0x10,bytes(m.uc.mem_read(path,0x1c0)))
            m.put(CTX+0x398,1);m.put(CTX+0x3a0,target,'f')
            m.run(0x45ad30,eax=CTX,ecx=CTX+0x398,budget=300000)
            category_available=bool(m.reg('eax')&255);m.visited.clear()
            if args.rival_default:
                action=m.get(CONTROLLER+0x3f8)
                m.run(0x450210,(CTX,),ecx=action,budget=3000000)
                m.uc.mem_write(path,bytes(m.uc.mem_read(action+0x20,0x1c0)))
            else:m.run(0x45ae10,(CTX+0x398,path,CTX,0,0),budget=3000000)
            rebuild=0x45ae55 in m.visited
            if not rebuild and 0x45af0f not in m.visited:raise ValueError('unsupported nonzero strategy prefix crop')
        else:
            m.run(0x459710,(bits(20.),ACTOR+0x60,ACTOR+0x70),esi=path,budget=300000)
            m.run(0x45a7d0,(1,),ecx=path,edx=m.reg('eax'),budget=300000)
            m.run(0x45c370,(path,CTX,bits(1.),mode,bits(target),bits(step)),budget=3000000)
        count=m.get(path,'B');length=m.get(path+0x1ac,'f')
        points=[[m.get(path+16+16*j+4*k,'f') for k in range(3)] for j in range(count)]
        ids=[[m.get(path+0x150+j,'B'),m.get(path+0x164+2*j,'H')] for j in range(count)]
        lines.append('{%d,%d,%d,%s,%s,{%s},{%s},%d,{'%(category,tick,selected_road,literal(initial_offset),literal(clearance),
            ','.join(map(literal,actor)),','.join(map(literal,params)),len(roads)))
        for road in roads:lines.append('    {{%s},%d,%d},'%(','.join(map(literal,road['v'])),road['route'],road['index']))
        lines.append('},%d,0x%08Xu,{'%(count,bits(length)))
        for point,identity_ids in zip(points,ids):lines.append('    {{%s},%d,%d},'%(','.join('0x%08Xu'%bits(v) for v in point),*identity_ids))
        lines.append('},{%d,%d,%s,%s,false},%s,%s,%s},'%(mode,policy[1],literal(actor[9]),literal(elapsed),literal(target),str(rebuild).lower(),str(category_available).lower()) if using_policy else '}},')
        rows.append(dict(pose=pose,category=category,tick=tick,selected_road=selected_road,initial_offset=initial_offset,
            clearance=clearance,actor=actor,params=params,roads=roads,count=count,length=length,points=points,ids=ids,policy=policy,offset_step=step,target_offset=target,rebuild=rebuild,category_available=category_available))
    args.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,cases=rows,
        original_default_action=args.rival_default,supplied_grade=args.grade),indent=2)+'\n',encoding='ascii')
    print('Original forward context continuation:',len(rows),'cases')
if __name__=='__main__':main()

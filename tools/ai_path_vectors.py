"""Original45C370 forward path emission on initialized retail road context.
The context/spatial producer is original code, not a portable implementation.
Print ASCII C++ vectors; never modify retail inputs.
"""
import argparse,hashlib,struct,json
from pathlib import Path
import capstone
from unicorn import UC_HOOK_CODE
from world_body_trace import Image
from controller293_scene import PairedController
from controller293_planner import inputs,CTX
from controller293_strategy import CONTROLLER,ACTION
from controller293_native import EXEC_RANGES
from physics256_native import ACTOR,bits

def literal(x):
    s=format(x,'.17g')
    if '.' not in s and 'e' not in s:s+='.'
    return s+'F'

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',type=Path,required=True)
    ap.add_argument('--evidence',type=Path,required=True)
    ap.add_argument('--retained',type=Path,required=True)
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes()
    identity='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest()!=identity:raise ValueError('unsupported original executable')
    im=Image(data);pair=PairedController(im,inputs(args.root),event_rides=True);m=pair.m
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    # Already observed original branch arms in the moving controller witness.
    ranges=list(EXEC_RANGES)+[(0x45c640,0x45c75e),(0x4594d0,0x4596a4),(0x459710,0x459865),(0x45a7d0,0x45a8b2),(0x45a8c0,0x45a918)]+[(a,a+n) for a,n in ((0x57b358,0x11),(0x45c74c,0xc),
       (0x45c739,6),(0x44ace8,6),(0x44d0cf,0x24),(0x5a7007,0xd))]
    for a,b in ranges:m.instructions.update({i.address:i.size for i in md.disasm(im.read(a,b-a),a)})
    path=0x1150000;records=[];captured=[];candidates=[];retained=[];retain_lines=[]
    def observe(uc,address,size,_):
        if address==0x45c640:
            ctx=m.reg('eax');point=m.reg('esi');sp=m.reg('esp');category=m.reg('ecx')
            route=m.get(point+0x5c,'b');index=m.get(point+0x5e,'h')
            raw=m.get(0x77af30+4*route)+0x27c+index*0x88
            slot=0 if category<5 else category-10
            selected=m.get(ctx+0x388,'b')
            captured.append(dict(category=category,mode=m.reg('edx'),offset=m.get(m.reg('edi'),'f'),
                step=m.get(sp+4,'f'),target=m.get(sp+8,'f'),margin=m.get(sp+12,'f'),
                clearance=m.get(ctx+8+selected*0x38+8,'f'),
                road=[m.get(raw+a,'f') for a in (0,4,8,12,16,0x2c,0x30)]+
                    [m.get(raw+0x3c+2*category,'h')/256,m.get(point+4+4*slot,'f'),
                     m.get(point+0x20+4*slot,'f'),m.get(point+0x3c+4*slot,'f'),
                     m.get(raw+0x24,'f'),m.get(raw+0x28,'f')],
                route=route,index=index))
        else:
            candidates.append([m.get(m.reg('edi')+4*k,'f') for k in range(3)])
    h1=m.uc.hook_add(UC_HOOK_CODE,observe,begin=0x45c640,end=0x45c640)
    h2=m.uc.hook_add(UC_HOOK_CODE,observe,begin=0x4594d0,end=0x4594d0)
    lines=['// Original speed.exe SHA256 '+identity,'// tools/ai_path_vectors.py; original initialized road context, 007F x87.']
    for name,mode,step,margin in [('ordinary',0,99999.,1.),('return',0,1.25,1.),
                                 ('left',3,.5,1.),('right',4,.5,1.),('tight',4,.5,10.)]:
        m.run(0x44CF30,(CTX,CONTROLLER),budget=3000000)
        m.run(0x450210,(CTX,),ecx=ACTION,budget=3000000)
        m.uc.mem_write(path,bytes(0x1c0));captured.clear();candidates.clear()
        m.run(0x45c370,(path,CTX,bits(margin),mode,bits(0.),bits(step)),budget=3000000)
        expected=dict(count=m.get(path,'B'),length=m.get(path+0x1ac,'f'),
             points=[[m.get(path+16+16*j+4*k,'f') for k in range(3)] for j in range(m.get(path,'B'))],
             routes=[m.get(path+0x150+j,'B') for j in range(m.get(path,'B'))],
             indices=[m.get(path+0x164+2*j,'H') for j in range(m.get(path,'B'))])
        if len(captured)!=len(candidates):raise ValueError('source emit correspondence')
        roads=captured.copy()
        actor=[m.get(ACTOR+a,'f') for a in (0x60,0x64,0x68,0x70,0x74,0x78,0x90,0x94,0x98,0x3d4)]
        c=roads[0]
        lines.append('// '+name)
        lines.append('{%d,%d,{%s},{%s},%d,{'%(c['category'],c['mode'],
            ','.join(literal(c[k]) for k in ('offset','step','target','clearance','margin')),
            ','.join(map(literal,actor)),len(roads)))
        for r in roads:lines.append('    {{%s},%d,%d},'%(','.join(map(literal,r['road'])),r['route'],r['index']))
        lines.append('},%d,0x%08Xu,{'%(expected['count'],bits(expected['length'])))
        for i,point in enumerate(expected['points']):lines.append('    {{%s},%d,%d},'%(','.join('0x%08Xu'%bits(v) for v in point),expected['routes'][i],expected['indices'][i]))
        lines.append('}},')
        original_path=bytes(m.uc.mem_read(path,0x1c0))
        for sample,speed,sign in [(0,0.,1),(3,20.,1),(6,40.,1),(8,-5.,-1)]:
            m.uc.mem_write(path,original_path)
            position=expected['points'][sample]
            velocity=[sign*actor[6],sign*actor[7],0.]
            for k,v in enumerate(position+velocity):m.put(0x1150300+4*k,v,'f')
            m.put(0x73ad34,12345)
            m.run(0x459710,(bits(speed),0x1150300,0x115030c),esi=path,budget=300000)
            cursor=m.reg('eax')
            m.run(0x45a7d0,(1,),ecx=path,edx=cursor,budget=300000)
            params=[m.get(a,'f') for a in (0x77a4a8,0x6b7520,0x6b7524)]
            count=m.get(path,'B');length=m.get(path+0x1ac,'f')
            points=[[m.get(path+16+16*j+4*k,'f') for k in range(3)] for j in range(count)]
            ids=[[m.get(path+0x150+j,'B'),m.get(path+0x164+2*j,'H')] for j in range(count)]
            retain_lines.append('{%d,%s,{%s},{%s},{%s},%d,0x%08Xu,{'%(len(records),literal(speed),
                ','.join(map(literal,position)),','.join(map(literal,velocity)),','.join(map(literal,params)),count,bits(length)))
            for point,identity_ids in zip(points,ids):
                retain_lines.append('    {{%s},%d,%d},'%(','.join('0x%08Xu'%bits(v) for v in point),*identity_ids))
            retain_lines.append('}},')
            retained.append(dict(case=name,position=position,velocity=velocity,speed=speed,params=params,
                                 cursor=cursor,count=count,length=length,points=points,ids=ids))
        records.append(dict(name=name,actor=actor,roads=roads,expected=expected))
    m.uc.hook_del(h1);m.uc.hook_del(h2)
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,cases=records,retained=retained),indent=2)+'\n',encoding='ascii')
    args.retained.write_text('// Original459710 and45A7D0 on retained native paths.\n'+'\n'.join(retain_lines)+'\n',encoding='ascii')
    print('\n'.join(lines))
if __name__=='__main__':main()

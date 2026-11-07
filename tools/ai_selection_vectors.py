"""Original44ADC0/442690 road admission on initialized event0; supplied poses."""
import argparse,hashlib,json
from pathlib import Path
import capstone
from world_body_trace import Image
from controller293_scene import PairedController
from controller293_planner import inputs
from controller293_strategy import CONTROLLER
from controller293_native import EXEC_RANGES
from physics256_native import ACTOR
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('root','output','evidence'):ap.add_argument('--'+name,type=Path,required=True)
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes()
    identity='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest()!=identity:raise ValueError('unsupported executable')
    im=Image(data);pair=PairedController(im,inputs(args.root),event_rides=True);m=pair.m
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    for a,b in list(EXEC_RANGES)+[(0x44adc0,0x44b060),(0x442690,0x442813),(0x44ab50,0x44abba)]:
        m.instructions.update({i.address:i.size for i in md.disasm(im.read(a,b-a),a)})
    selection=m.get(CONTROLLER+0x3d8);route=m.get(0x77af34);records=[]
    maximum=m.get(0x6b7704,'f');role=m.get(m.get(ACTOR+0x14)+4);direction=m.get(0x78a2fc)
    for point_index in (0,5,10,15,20,25,30,35,37):
      for offset in (-12.,0.,12.):
       for outside in (False,True):
        point=route+0x27c+point_index*0x88
        position=[m.get(point,'f')+offset*m.get(point+0x10,'f'),m.get(point+4,'f')-offset*m.get(point+0xc,'f'),m.get(point+8,'f')]
        progress=m.get(point+0x38,'f')
        for j,v in enumerate(position):m.put(ACTOR+0x60+4*j,v,'f')
        m.put(ACTOR+0x814,progress,'f');m.uc.mem_write(selection,bytes(12));m.put(selection+8,0xffffffff)
        m.run(0x44adc0,(CONTROLLER,0,99,0,int(outside)),budget=3000000)
        result_route=m.get(selection);result_point=m.get(selection+4)
        selected=[m.get(result_route+0xa,'h'),(result_point-result_route-0x27c)//0x88] if result_route else [-1,0]
        records.append(dict(position=position,progress=progress,maximum=maximum,direction=direction,role=role,outside=outside,selected=selected))
    lines=['// Authenticated original44ADC0/442690 supplied event0 poses.','const Selection selections[]={']
    for r in records:lines.append('{{{%s},%s,%s,0,99,%d,%d,-1,false,%s},%s,{%d,%d}},'%(
        ','.join(map(literal,r['position'])),literal(r['progress']),literal(r['maximum']),r['direction'],r['role'],str(r['outside']).lower(),
        str(r['selected'][0]>=0).lower(),*r['selected']))
    lines.append('};');args.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,cases=records),indent=2)+'\n',encoding='ascii')
    print('original spatial selection',len(records),'found',sum(r['selected'][0]>=0 for r in records))
if __name__=='__main__':main()

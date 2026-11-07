"""Original458750 future overlap; explicit oriented boxes, no hooks."""
import argparse,hashlib,json,math,struct
from pathlib import Path
from physics256_native import Machine
from world_body_trace import Image
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('root','output','evidence'):ap.add_argument('--'+key,type=Path,required=True)
    a=ap.parse_args();data=(a.root/'speed.exe').read_bytes();sha=hashlib.sha256(data).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    m=Machine(Image(data));m.allowed=[(0x458750,0x458a4a),(0x4586a0,0x45874d),(0x4584f0,0x458692)]
    one,two,pos,vel,store,out=0x1150000,0x1150100,0x1150200,0x1150210,0x1150300,0x1150400
    code=b'\xd9\x1d'+struct.pack('<I',out)+b'\xc3';m.uc.mem_write(store,code);m.allowed.append((store,store+len(code)))
    f32=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
    rows=[];lines=['// Original458750/4586A0/4584F0. 007F; zero hooks. '+sha]
    for angle in (0.,.47,1.5707963267948966):
      for name,position,velocity in [('overlap',(0.,0.),(0.,0.)),('stopped-gap',(10.,0.),(0.,0.)),
          ('approach',(10.,0.),(-5.,0.)),('depart',(10.,0.),(5.,0.)),('past',(-10.,0.),(-5.,0.)),
          ('cross',(0.,10.),(0.,-5.)),('miss',(10.,10.),(-5.,0.)),('diagonal',(10.,10.),(-5.,-5.)),
          ('touch',(4.,0.),(5.,0.)),('enter-touch',(4.,0.),(-5.,0.)),
          ('slow-below',(10.,0.),(-.000099,0.)),('slow-equal',(10.,0.),(-.0001,0.)),
          ('slow-above',(10.,0.),(-.000101,0.)),('oblique',(13.,-3.7),(-12.3,4.7))]:
        axes_a=[1.,0.,0.,1.];axes_b=list(map(f32,(math.cos(angle),math.sin(angle),-math.sin(angle),math.cos(angle))))
        ext_a=[2.,1.];ext_b=[2.,1.]
        for at,axes,extent in ((one,axes_a,ext_a),(two,axes_b,ext_b)):
            for off,value in zip((0x10,0x14,0x20,0x24),axes):m.put(at+off,value,'f')
            for k,value in enumerate(extent):m.put(at+0x40+4*k,value,'f')
        for at,values in ((pos,position),(vel,velocity)):
            for k,value in enumerate(values):m.put(at+4*k,value,'f')
        m.visited.clear();m.run(0x458750,(one,pos,vel),edi=two);visited=sorted(m.visited);m.run(store)
        expected=m.get(out)
        lines.append('// '+str(angle)+'-'+name)
        fields=[axes_a,ext_a,axes_b,ext_b,position,velocity]
        lines.append('{'+','.join('{'+','.join(map(literal,values))+'}' for values in fields)+',0x%08Xu},'%expected)
        rows.append(dict(name=name,angle=angle,input=fields,result_bits=expected,result=m.get(out,'f'),visited=visited))
    a.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    a.evidence.write_text(json.dumps(dict(executable_sha256=sha,fpu='007F',hooks=m.events,cases=rows),indent=2)+'\n',encoding='ascii')
    print('Original swept overlap:',len(rows),'cases; hooks',len(m.events))
if __name__=='__main__':main()

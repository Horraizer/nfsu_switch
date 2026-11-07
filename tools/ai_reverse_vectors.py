"""Complete original45E970 signed-speed driver on supplied state sequences."""
import argparse,hashlib,json
from pathlib import Path
from world_body_trace import Image
from controller293_native import fixture
from physics256_native import ACTOR,DRIVER,RIDE,bits
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('root','output','evidence'):ap.add_argument('--'+name,type=Path,required=True)
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes();sha=hashlib.sha256(data).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    image=Image(data);cases=[];hooks=[]
    speeds=[10.,7.,6.,2.,1.,0.,-1.,-2.,-6.,-7.,-8.,-15.]
    for sequence in range(8):
        case=sequence%4
        slips=[5.,-6.,2.,7.] if sequence>=4 else [0.,0.,0.,0.]
        direction=1 if case%2 else -1;points=[(10.,direction*10.,0.),(100.,direction*60.,0.)]
        m=fixture(image,points,speed=10.,category=case,heading=case*83)
        for j,v in enumerate(slips):m.put(ACTOR+0x374+j*8,v,'f')
        drive=(0.,.5,1.,1.)[case];m.put(RIDE+0x1c0,drive,'f');frames=[];first=len(m.events)
        for step in range(28):
            speed=speeds[step%12] if step<12 else -speeds[step%12]
            desired=-15. if step<12 else 15. if step<24 else (-1.,0.,1.,-6.)[step-24]
            contact=[True,True,True,True]
            if case==3 and step in (6,7):contact=[False,False,False,False]
            if case==2 and step in (8,9):contact=[False,False,True,True]
            m.put(ACTOR+0x3d0,speed,'f');m.put(ACTOR+0x3d4,abs(speed),'f');m.put(ACTOR+0x70,speed,'f')
            for j,v in enumerate(contact):m.put(ACTOR+0x350+j,int(v),'B')
            m.put(0x73ad34,40000+step*67)
            reset=case>=2 and step==12
            if reset:m.run(0x45e650,ecx=DRIVER)
            m.run(0x45e180,(bits(desired),0,0,int(step!=26)),ecx=DRIVER)
            m.run(0x45e970,(bits(1/60),),ecx=DRIVER)
            history=m.get(DRIVER+0x23c)
            frames.append([speed,desired,contact,step!=26,reset,m.get(DRIVER+0x1d0,'H'),
                m.get(DRIVER+0x1dc),m.get(DRIVER+0x1e4),m.get(DRIVER+0x1d4,'i'),bool(m.get(DRIVER+0x218,'B')),
                m.get(DRIVER+0x1f4),m.get(DRIVER+0x1f8),m.get(history+0x7c)])
        cases.append([case,drive,slips,frames]);hooks+=m.events[first:]
    def lit(v):
        if isinstance(v,bool):return str(v).lower()
        if isinstance(v,float):return literal(v)
        if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
        return str(v) if v<0 else str(v)+'u'
    args.output.write_text('// Complete45E970, original signed-speed hysteresis and retained histories.\nconst ReverseSequence reverse_sequences[]={\n'+
        '\n'.join(lit(c)+',' for c in cases)+'\n};\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=sha,query_hooks=hooks,cases=cases),indent=2)+'\n',encoding='ascii')
    print('Original signed driver:',len(cases)*28,'updates; hooks:',len(hooks))
if __name__=='__main__':main()

"""Unmodified short-path driver and cached contact projection on supplied poses."""
import argparse,hashlib,json
from pathlib import Path
from world_body_trace import Image
from controller293_native import fixture
from physics256_native import Machine,ACTOR,DRIVER,bits
from ai_path_vectors import literal

def lit(v):
    if isinstance(v,bool):return str(v).lower()
    if isinstance(v,float):return literal(v)
    if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
    return str(v) if v<0 else str(v)+'u'

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('root','driver-output','contact-output','evidence'):
        ap.add_argument('--'+name,type=Path,required=True)
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes();sha=hashlib.sha256(data).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    image=Image(data);drivers=[];contacts=[];hooks=[]
    for heading in (0,4137,16384,23791,32768,49152,65535):
        for count in (0,1):
            for speed in (-8.,0.,4.999999523162842,5.,25.):
                category=heading%4;position=[13.25,-27.75,1.5]
                velocity=[0.,0.,0.] if speed==5 else [speed*.375,-speed*.625,speed*.25]
                m=fixture(image,[(999.,-888.,7.)]*count,speed=speed,target=15.,category=category,heading=heading)
                m.allowed += [(0x456a80,0x456b1d),(0x456b20,0x456b9b),(0x564bd0,0x564cc6)]
                for j in range(3):m.put(ACTOR+0x60+4*j,position[j],'f');m.put(ACTOR+0x70+4*j,velocity[j],'f')
                frames=[]
                for step in range(3):
                    m.put(0x73ad34,40000+step*67)
                    m.run(0x45e970,(bits(1/60),),ecx=DRIVER)
                    frames.append([m.get(DRIVER+0x1f4),m.get(DRIVER+0x1f8),m.get(DRIVER+0x1d0,'H'),
                        m.get(DRIVER+0x1dc),m.get(DRIVER+0x1e4),m.get(DRIVER+0x1d4,'i'),bool(m.get(DRIVER+0x218,'B'))])
                drivers.append([heading,count,category,speed,position,velocity,frames]);hooks+=m.events
    m=Machine(image)
    m.allowed=[(0x464f20,0x4650d0),(0x4650d0,0x465141),(0x565230,0x565336),
        (0x40bd40,0x40be3c),(0x567370,0x5673dc),(0x564b10,0x564bd0),(0x5abdaf,0x5abe40)]
    # Explicit original scalar D3DX binding, without host Win32/CPU selection.
    # Original transform instructions execute; no projection result is hooked.
    m.put(0x6d4ac4,0x5abdb5)
    out=0x1150000;point=out+0x100
    cases=[([0.,0.,0.],0.,h) for h in (0,4137,16384,23791,32768,49152,65535)]
    cases += [(v,s,9137) for v,s in (([1.,0.,0.],1.),([1.0000001192092896,0.,0.],1.),
        ([3.,4.,2.],5.),([3.,4.,2.],-5.),([0.,0.,2.],2.),([.5,.5,.8],0.))]
    for movement,speed,heading in cases:
        m.put(ACTOR+0x460,0,'B')
        for step in range(3):
            # Second call changes motion but preserves native cached axes;
            # third invalidates that same cache before projection.
            invalidate=step!=1
            velocity=movement if step==0 else [-2.,3.,-.5]
            if invalidate:m.put(ACTOR+0x460,0,'B')
            position=[12.5+step,-20.25];world=[9.25,4.75-step]
            for j,v in enumerate(position):m.put(ACTOR+0x60+4*j,v,'f')
            for j,v in enumerate(velocity):m.put(ACTOR+0x70+4*j,v,'f')
            for j,v in enumerate(world):m.put(point+4*j,v,'f')
            m.put(ACTOR+0x3d0,speed,'f');m.put(ACTOR+0x3b4,heading,'H')
            m.run(0x4650d0,(out,),eax=ACTOR+0x40,esi=point)
            contacts.append([invalidate,world,position,velocity,speed,heading,
                [m.get(out+4*j) for j in range(2)],
                [[m.get(ACTOR+0x420+16*j+4*k) for k in range(3)] for j in range(3)]])
    hooks+=m.events
    args.driver_output.write_text('// Original45E970 short/empty path snapshots.\nconst ShortPathCase short_path_cases[]={\n'+
        '\n'.join(lit(c)+',' for c in drivers)+'\n};\n',encoding='ascii')
    args.contact_output.write_text('// Original4650D0 with cache reuse and invalidation.\nconst ContactLocalCase contact_local_cases[]={\n'+
        '\n'.join(lit(c)+',' for c in contacts)+'\n};\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=sha,fpu_control_word='007F',query_hooks=hooks,
        dispatch_binding='Supplied6D4AC4 -> original scalar5ABDB5; live CPU selection unclaimed',
        driver=drivers,contact=contacts),indent=2)+'\n',encoding='ascii')
    print('Original short-path driver:',len(drivers)*3,'updates; contact:',len(contacts),'projections; hooks:',len(hooks))
if __name__=='__main__':main()

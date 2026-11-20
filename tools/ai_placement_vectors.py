"""Original4273D0 wheel placement on explicit queried surfaces, retail read-only."""
import argparse,hashlib,json,struct
from pathlib import Path
from unicorn.x86_const import UC_X86_REG_EIP
from world_body_trace import Image
from physics256_native import Machine,ACTOR,RIDE,STOP

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('root','output','evidence'):ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args();raw=(a.root/'speed.exe').read_bytes();sha=hashlib.sha256(raw).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    image=Image(raw);cases=[];hooks=[]
    for heading in (0,9137,16384,32768,49152):
        for variant in range(5):
            m=Machine(image);m.allowed=[(0x4273d0,0x4278c9),(0x40b930,0x40ba80),
                (0x564b10,0x564bd0),(0x5abdaf,0x5abe40),(0x5ac69c,0x5acced)]
            m.put(ACTOR+0x20,RIDE);m.put(RIDE+8,.28,'f')
            wheels=[[1.39,.73,.08,.288],[1.39,-.73,.08,.288],[-1.04878,-.73,.08,.3135],[-1.04878,.73,.08,.3135]]
            for i,w in enumerate(wheels):
                for off,v in zip((0x10,0x14,0x18,0x20),w):m.put(RIDE+i*0x30+off,v,'f')
            matrix,pos,cache,scalar=0x1150000,0x1150100,0x1150200,0x1150300
            start=[12.5,-4.25,3.]
            for i,v in enumerate(start):m.put(pos+4*i,v,'f')
            m.put(0x6d4ac4,0x5abdb5);m.put(0x6d4acc,0x5ac6a2)
            # Only external surface query results are supplied. All transforms,
            # sums, square roots, cross products and output stores run natively.
            taps=[];heights=[2.,2.,2.,2.] if variant==0 else [2.,2.3,1.5,1.8]
            missing=1 if variant==2 else 8 if variant==3 else 15 if variant==4 else 0
            trampoline=STOP+0x400
            m.uc.mem_write(trampoline,b'\xd9\x05'+struct.pack('<I',scalar)+b'\xc2\x14\x00')
            m.allowed.append((trampoline,trampoline+9))
            def query(x):
                sp=x.reg('esp');hub=x.get(sp+8);found=x.get(sp+20);i=len(taps)
                taps.append([x.get(hub+4*j,'f') for j in range(3)])
                x.put(found,int(not(missing&(1<<i))),'B');x.put(scalar,heights[i],'f')
                x.uc.reg_write(UC_X86_REG_EIP,trampoline)
            m.hook(0x440a40,'explicit_surface_query',callback=query)
            # Empty dynamic list executes original577060, retaining ST0 height.
            m.allowed.extend([(0x577060,0x577209),(0x40cb70,0x40cb7b)])
            m.put(0x73b14c,0x73b14c)
            m.run(0x4273d0,(ACTOR,matrix,pos,cache),eax=heading)
            if len(taps)!=4:raise ValueError('placement did not query all wheels')
            output=[m.get(pos+4*j,'f') for j in range(3)]
            axes=[[m.get(matrix+16*k+4*j,'f') for j in range(3)] for k in range(3)]
            cases.append([heading,missing,heights,output,axes,taps]);hooks.extend(m.events)
    def lit(v):
        if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
        if isinstance(v,int):return str(v)+'u'
        text=format(v,'.9g');return text+('.F' if '.' not in text and 'e' not in text else 'F')
    a.output.write_text('// Original4273D0, explicit surface queries; no placement or math hooks.\nconst PlacementCase placement_cases[]={\n'+
        '\n'.join(lit(c)+',' for c in cases)+'\n};\n',encoding='ascii')
    a.evidence.write_text(json.dumps(dict(executable_sha256=sha,cases=cases,hooks=hooks),indent=2)+'\n',encoding='ascii')
    print('Original placement:',len(cases),'cases;',len(hooks),'explicit surface queries')

if __name__=='__main__':main()

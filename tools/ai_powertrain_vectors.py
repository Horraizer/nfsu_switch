from pathlib import Path
import argparse,hashlib,json,math,os,struct,subprocess,sys
parser=argparse.ArgumentParser(description='Bounded original selector3 component comparison and test vectors')
parser.add_argument('--root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args();tree=Path.cwd();out=args.out.resolve();retail=args.root.resolve()
relative=out.relative_to(tree)
if not str(relative).isascii() or not relative.parts[0].startswith('build') or retail==out or retail in out.parents:
    parser.error('fresh ASCII workspace build output outside retail required')
out.mkdir(parents=True,exist_ok=False)
sys.path.insert(0,str(tree/'tools'))
from world_body_trace import Image
from physics256_native import Machine,bits
data=(retail/'speed.exe').read_bytes();bun=(retail/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(data).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
image=Image(data);scratch=out
record=bun[1736752:1736752+3216];(scratch/'record.bin').write_bytes(record)
env=os.environ.copy()
args=['c++','-std=c++17','-O3','-DNDEBUG','-fno-fast-math','-ffp-contract=off','-Wall','-Wextra','-Wpedantic',
      '-I'+str(tree/'include'),str(tree/'tools/ai_powertrain_probe.cpp'),str(tree/'src/ai_powertrain.cpp'),
      '-o',str(scratch/'probe.exe'),'-Wl,--start-group',*[str(p) for p in sorted((tree/'build').glob('lib*.a'))],'-Wl,--end-group']
r=subprocess.run(args,capture_output=True,text=True,env=env)
(out/'ug0-ai-powertrain-build.log').write_text((r.stdout+r.stderr).encode('ascii','backslashreplace').decode('ascii'),encoding='ascii');r.check_returncode()
f32=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
engine,setup,body,derivative=0x1100000,0x1101000,0x1102000,0x1105000
cases=[]
for index in range(240):
    mode=0 if index<180 else 1
    yaw=(0,.3,-1.1,2.7)[index%4];roll=(0,.1,-.2)[index%3]
    cy,sy,cr,sr=map(f32,(math.cos(yaw),math.sin(yaw),math.cos(roll),math.sin(roll)))
    axes=[cy,sy,0,f32(-sy*cr),f32(cy*cr),sr,f32(sy*sr),f32(-cy*sr),cr]
    speed=(0,.05,1,8,20,35)[index%6]
    velocity=[f32(speed*cy),f32(speed*sy),0];omega=[0,0,f32((index%5-2)*.13)]
    wheels=[];ground=[]
    for k in range(4):
        spin=f32(speed/.32+(index%7-3)*2)
        wheels.append([spin,f32((k-2)*.005),f32(2+.3*k),int((index+k)%6!=0),int((index+k)%9==0),3,0,0,1])
        ground.append([0,0,0,f32((index%8-4)*.075),0,0,1])
    tables=[[1000+800*k for k in range(8)],[500+500*k for k in range(8)]]
    controls=[(0,.5,1)[index%3],(900,2500,6000)[index%3],(0,.4,1)[index%3],(0,.5,1)[(index//3)%3],
        (0,2000,65000)[index%3],2+index%4,(-1,0,2)[index%3],(1,1.2)[index%2],(1,.8)[index%2],1,
        int(index%5==0),.15,5000 if index%11==0 else 0,1000,f32(1/120)]
    inputs=[mode]+controls+axes+velocity+omega+sum(wheels,[])+sum(tables,[])+sum(ground,[])
    m=Machine(image)
    m.allowed=[(0x475097,0x475179),(0x475530,0x4757c8),(0x4757d0,0x475bea),(0x475d00,0x47623f),
        (0x564cd0,0x564f20),(0x564b10,0x564c00),(0x674898,0x674920),(0x5abdaf,0x5abf00),
        (0x584cd0,0x584e00),(0x583ea0,0x583fe5),(0x4387e0,0x4387f6)]
    m.uc.mem_write(setup,record[0x110:]);m.run(0x475097,end=0x475179,esi=engine,ebp=setup)
    m.put(engine+0x18,setup);m.put(engine+0x14,body);m.put(0x6d4ac4,0x5abdb5)
    for k,v in enumerate(axes):m.put(body+0x30+(k//3)*16+(k%3)*4,v,'f')
    for k,v in enumerate(velocity):m.put(body+0xa0+k*4,v,'f')
    for k,v in enumerate(omega):m.put(body+0xc0+k*4,v,'f')
    share,rpm,throttle,brake,steering,gear,request,boost,grip,low,extra,extra_scale,until,time,dt=controls
    m.put(setup+0x1c0,share,'f');m.put(setup+0x2ac,extra_scale,'f');m.put(0x73ad34,time)
    for offset,value in ((0x120,rpm),(0x128,throttle),(0x12c,brake),(0x138,boost),(0x13c,grip),(0x140,low)):
        m.put(engine+offset,value,'f')
    for offset,value in ((0x124,gear),(0x134,request),(0x194,extra),(0x1e0,until)):m.put(engine+offset,value&0xffffffff)
    m.put(engine+0x130,steering,'H')
    for k,w in enumerate(wheels):
        spin,compression,load,contact,locked,preload,nx,ny,nz=w
        for offset,value in ((0x24,spin),(0x34,compression),(0x54,load),(0x144,preload)):m.put(engine+offset+k*4,value,'f')
        m.put(engine+0x64+k*4,contact);m.put(engine+0xb8+k*4,locked)
        for j,v in enumerate((nx,ny,nz)):m.put(engine+0xe0+k*16+j*4,v,'f')
    for base,values in zip((0x198,0x1bc),tables):
        for k,v in enumerate(values):m.put(engine+base+k*4,v,'f')
    if mode==0:
        m.run(0x475d00,ecx=engine);m.run(0x475530,(derivative,),ecx=engine)
        for k in range(4):
            spin=m.get(engine+0x24+4*k,'f');rate=m.get(derivative+4*k,'f')
            m.put(engine+0x24+4*k,f32(spin+f32(rate*dt)),'f')
        m.hook(0x475a00,'held-strut-inputs-for-completion-only',pop=4)
        m.run(0x4757d0,ecx=engine)
    else:
        def query(machine):
            heights,hubs=machine.reg('ecx'),machine.reg('edx')
            for k,g in enumerate(ground):
                height,x,y,z,nx,ny,nz=g
                machine.put(heights+4*k,height,'f')
                for j,v in enumerate((x,y,z)):machine.put(hubs+16*k+j*4,v,'f')
                for j,v in enumerate((nx,ny,nz)):machine.put(engine+0xe0+16*k+j*4,v,'f')
            machine.ret(4)
        m.hook(0x475bf0,'supplied-ground-query-height-hub-normal',callback=query)
        m.put(0x6f0738,dt,'f');m.run(0x475a00,(engine,))
    expected=[]
    for k in range(4):
        expected.extend(m.get(engine+b+k*4) for b in (0x24,0x34,0x44,0x54))
        expected.extend(m.get(engine+b+k*8) for b in (0x74,0x78,0x94,0x98))
        expected.extend((m.get(engine+0x64+k*4),m.get(engine+0xb4+k,'B'),m.get(engine+0xb8+k*4),m.get(engine+0xc8+k*4)))
    expected.extend(m.get(body+b+k*4) for b in (0x110,0x120) for k in range(3))
    expected.extend(m.get(derivative+4*k) for k in range(4));expected.extend((m.get(engine+0x120),m.get(engine+0x124)))
    cases.append(dict(index=index,mode=mode,input=inputs,expected=expected))
p=subprocess.run([str(scratch/'probe.exe'),str(scratch/'record.bin')],input='\n'.join(' '.join(map(str,c['input'])) for c in cases)+'\n',capture_output=True,text=True,env=env)
p.check_returncode();lines=p.stdout.splitlines();assert len(lines)==len(cases),(len(lines),p.stderr)
failures=[]
for c,line in zip(cases,lines):
    assert line.startswith('RESULT '),line
    actual=list(map(int,line.split()[1:]));assert len(actual)==len(c['expected'])
    difference=[dict(field=k,expected=x,actual=y) for k,(x,y) in enumerate(zip(c['expected'],actual)) if x!=y and not (x in (0,0x80000000) and y in (0,0x80000000))]
    if difference:failures.append(dict(index=c['index'],mode=c['mode'],differences=difference))
report=dict(cases=len(cases),passed=len(cases)-len(failures),failures=failures,
    boundary='Original475D00 force/feedback plus475530 derivative and4757D0 held-strut completion; separate475A00 supplied-ground struts. Original007F/scalar dispatch, authentic240SX setup with declared drive-share changes. Not live selector3 system, contact adapter or planner parity.',
    oracle_executable_sha256=hashlib.sha256(data).hexdigest(),signed_zero_equivalent=True)
(out/'ug0-ai-powertrain-comparison.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
(scratch/'cases.json').write_text(json.dumps(cases)+'\n',encoding='ascii')
selected={0,1,2,3,4,5,17,29,44,75,106,179,180,181,182,183,196,205,238,239}
lines=['// Original speed.exe SHA256 '+hashlib.sha256(data).hexdigest(),
       '// Generated by tools/ai_powertrain_vectors.py; declared primitive inputs, not live race.',
       '// Signed zero is equivalent; every other output bit is compared.']
def literal(x):
    text=format(x,'.9g')
    if '.' not in text and 'e' not in text:text+='.0'
    return text+'F'
if not failures:
    for c in cases:
        if c['index'] not in selected:continue
        lines.append('{'+str(c['index'])+', {{'+','.join(literal(x) for x in c['input'])+'}}, {{'+','.join('0x%08Xu'%x for x in c['expected'])+'}}},')
    (out/'ai_powertrain_native.inc').write_text('\n'.join(lines)+'\n',encoding='ascii')
print(json.dumps(dict(cases=report['cases'],passed=report['passed'],first_failures=failures[:3]),indent=2))
sys.exit(bool(failures))

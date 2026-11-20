"""Original463F20 until wheel visual effects; explicit road surface only."""
import argparse,hashlib,json,math,random,struct
from pathlib import Path
from physics256_native import Machine,ACTOR,PHYSICS,RIDE,DRIVER,STOP,STACK,bits
from world_body_trace import Image
from unicorn.x86_const import UC_X86_REG_EIP

SPLINE,POINTS,SMOOTH,FILTER,VALUES,SCALAR=0x1160000,0x1161000,0x1162000,0x1163000,0x1164000,0x1165000

def fixture(image,points,heading=0,speed=25.,lateral=.5,lateral_derivative=.1,yaw=.2):
    m=Machine(image)
    m.allowed=[
        (0x463800,0x4638A0),(0x463A50,0x463B1B),(0x463F20,0x464470),(0x463BC0,0x463E7F),(0x4645D0,0x4647BA),
        (0x59C850,0x59D350),(0x59D350,0x59D42D),
        (0x593EC0,0x593FB8),(0x583F20,0x584022),(0x584270,0x5842DC),
        (0x565200,0x565360),(0x567000,0x567800),(0x564CD0,0x565000),
        (0x4010A0,0x4010D0),(0x5AC443,0x5AC4E8),(0x5B065B,0x5B0762),
        (0x4387E0,0x438820),(0x674898,0x67490D),
        (0x4273D0,0x427927),(0x40B930,0x40BA80),(0x564B10,0x564BD0),
        (0x5ABDAF,0x5ABE40),(0x5AC69C,0x5ACCED),
        (0x577060,0x577209),(0x40CB70,0x40CB7B),(0x4AD8D0,0x4AD8D3)]
    m.run(0x5B065B,(0,))
    m.put(0x6D4AC4,0x5ABDB5);m.put(0x6D4ACC,0x5AC6A2)
    for off,value in ((8,ACTOR),(0x14,SPLINE),(0x18,SMOOTH),(0x28,SMOOTH+0x30),
                      (0x2C,SMOOTH+0x90),(0x30,FILTER)):
        m.put(PHYSICS+off,value)
    for off,value in ((0x20,RIDE),(0x30,DRIVER),(0x34,PHYSICS),(0x40,RIDE)):
        m.put(ACTOR+off,value)
    for off,value in ((0x10,.1),(0x1C,1.),(0x20,speed*.1),(0x24,.2)):
        m.put(PHYSICS+off,value,'f')
    m.put(SPLINE+4,2);m.put(SPLINE+0x1A,2,'B')
    m.put(SPLINE+0x24,POINTS);m.put(SPLINE+0x28,0x7376B0)
    m.run(0x59C850,edi=SPLINE)
    for i,p in enumerate(points):
        for k,v in enumerate(p):m.put(POINTS+i*16+k*4,v,'f')
    m.run(0x59CB20,eax=SPLINE,ecx=len(points))
    m.put(FILTER+4,4,'B');m.put(FILTER+5,30,'B');m.put(FILTER+0x10,VALUES)
    # Allocate initialized channel storage, then execute the original
    # constructor's seed block. No trigonometric or smoothing substitute.
    for j in range(5):
        at=SMOOTH+j*0x30
        m.put(at+0x24,1.,'f');m.put(at+0x28,1.,'f');m.put(at+0x2E,1,'H')
    m.put(ACTOR+0x3D4,speed,'f');m.put(ACTOR+0xD8,yaw,'f')
    m.put(ACTOR+0x3B4,heading,'H')
    m.allowed.append((0x463436,0x463514))
    m.run(0x463436,esi=PHYSICS,eax=SMOOTH+0x90,ebp=0x11E0000,end=0x463514)
    seed=snapshot(m)['channels']
    for off,value in ((0,lateral),(4,lateral_derivative),(0x24,.5)):
        m.put(SMOOTH+off,value,'f')
    m.put(SMOOTH+0x2C,2 if lateral or lateral_derivative else 0,'H')
    m.put(SMOOTH+0x2E,0,'H')
    for j in (1,2):m.put(SMOOTH+j*0x30+0x24,.4,'f')
    m.put(ACTOR+0x3B4,heading,'H')
    initial=(points[1][0],points[1][1]+lateral,points[1][2]+.494375)
    for k,v in enumerate(initial):m.put(ACTOR+0x60+4*k,v,'f')
    for off,v in ((8,.28),(0xD8,1.),(0xF8,1.),(0xDC,1.2),(0xFC,1.2),
                  (0xE0,.9),(0x100,.9),(0x110,1.5),(0x264,.012),(0x268,.014)):
        m.put(RIDE+off,v,'f')
    wheels=[[1.39,.73,.08,.288],[1.39,-.73,.08,.288],[-1.04878,-.73,.08,.3135],[-1.04878,.73,.08,.3135]]
    for i,w in enumerate(wheels):
        for off,v in zip((0x10,0x14,0x18,0x20),w):m.put(RIDE+i*0x30+off,v,'f')
    m.put(0x73B14C,0x73B14C)
    trampoline=STOP+0x400
    m.uc.mem_write(trampoline,b'\xd9\x05'+struct.pack('<I',SCALAR)+b'\xc2\x14\x00')
    m.allowed.append((trampoline,trampoline+9))
    taps=[];control=dict(missing=0)
    def query(x):
        sp=x.reg('esp');hub=x.get(sp+8);found=x.get(sp+20)
        p=[x.get(hub+4*k,'f') for k in range(3)]
        taps.append(p)
        x.put(found,int(not(control['missing']&(1<<(len(taps)-1)%4))),'B');x.put(SCALAR,p[0]*.02+p[1]*.01,'f')
        x.uc.reg_write(UC_X86_REG_EIP,trampoline)
    m.hook(0x440A40,'explicit_road_surface',callback=query)
    return m,taps,control,seed

def snapshot(m):
    def floats(at,n):return [m.get(at+4*k,'f') for k in range(n)]
    return dict(position=floats(ACTOR+0x60,3),velocity=floats(ACTOR+0x70,3),
        yaw=m.get(ACTOR+0xD8,'f'),heading=m.get(ACTOR+0x3B4,'H'),
        axes=[floats(ACTOR+0x90+k*16,3) for k in range(3)],
        speed=floats(ACTOR+0x3D0,2),sliding=m.get(PHYSICS+0x34,'B'),
        channels=[floats(SMOOTH+j*0x30,11)+[m.get(SMOOTH+j*0x30+0x2C,'H')] for j in range(5)],
        curvature=m.get(FILTER+0xC,'f'),elapsed=m.get(PHYSICS+0xC,'f'))

def departure_cases(image):
    rows=[]
    for slope in (1.90,1.92768505859375,1.95):
        for factor in (1.,1.4):
            points=[(float(i),0.,10.+slope*i) for i in range(6)]
            m,_,_,_=fixture(image,points)
            for off,v in ((0x1c,-1.6),(0x20,2.5),(0x24,5.),(0x0c,.2)):
                m.put(PHYSICS+off,v,'f')
            front,rear=.012*factor,.014*factor
            m.put(RIDE+0x264,front,'f');m.put(RIDE+0x268,rear,'f')
            entry,ret,out=STOP+0x620,STOP+0x650,SCALAR+0x300
            code=b'\xc7\x04\x24'+struct.pack('<I',ret)+b'\xe9'+struct.pack('<i',0x463D50-entry-12)
            capture=b'\xd9\x1d'+struct.pack('<I',out)+b'\xe9'+struct.pack('<i',STOP-ret-11)
            m.allowed.extend(((entry,entry+len(code)),(ret,ret+len(capture))))
            m.uc.mem_write(entry,code);m.uc.mem_write(ret,capture)
            m.run(entry,(bits(.5),),eax=PHYSICS)
            gap=m.get(out,'f')
            rows.append([points,-1.6,2.5,5.,.2,.1,1.5,front,rear,gap,int(gap>1)])
    if not rows[2][-1] or rows[3][-1]:raise ValueError('departure witness did not cross threshold')
    return rows

def projection_cases(image):
    # Original59D430/59D540, including difficult near-vertical large-coordinate
    # splines. No semantic hook is reached by either queried function.
    rng=random.Random(7041);rows=[]
    f=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
    for case in range(80):
        origin=rng.choice([0.,10.,1800.])
        points=[[f(origin+i*10),f(5*math.sin(i*.7+case)),f(i*.1)] for i in range(6)]
        if case>=60:points=[[f(origin),0.,f(i*.2)] for i in range(6)]
        m,_,_,_=fixture(image,points)
        m.allowed.append((0x59D430,0x59D73C))
        for sample in range(4):
            position=[f(origin+rng.uniform(-5,60)),f(rng.uniform(-12,12)),f(rng.uniform(-.5,1))]
            at=SCALAR+0x500
            for k,x in enumerate(position):m.put(at+4*k,x,'f')
            m.run(0x59D430,(0,),eax=SPLINE,edx=at);nearest=m.reg('eax')
            entry,ret,out=STOP+0x620,STOP+0x650,SCALAR+0x600
            code=b'\xc7\x04\x24'+struct.pack('<I',ret)+b'\xe9'+struct.pack('<i',0x59D540-entry-12)
            capture=b'\xd9\x1d'+struct.pack('<I',out)+b'\xe9'+struct.pack('<i',STOP-ret-11)
            m.allowed.extend(((entry,entry+len(code)),(ret,ret+len(capture))))
            m.uc.mem_write(entry,code);m.uc.mem_write(ret,capture)
            before=len(m.events)
            m.run(entry,(at,bits(float(nearest)),m.get(0x6B72E4),0),eax=SPLINE)
            if len(m.events)!=before:raise ValueError('projection called a semantic hook')
            rows.append([points,position,m.get(out,'f')])
    return rows

def speed_publication_cases(image):
    rng=random.Random(463910);rows=[]
    f=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
    curve=[f(max(0.,7.-i*.13+math.sin(i)*.7)) for i in range(64)]
    for case in range(120):
        points=[[f(1200+i*8),f(500+math.sin(i*.8)*2),f(20+i*.1)] for i in range(12)]
        if case>=80:points=[[f(1200+i*8),f(500+math.sin(i*.8)*2),f(20+i*1.2)] for i in range(12)]
        speed=f(rng.uniform(0,85)) if case<100 else f(rng.uniform(-1.5,0))
        heading=137;mass=f(1280.);drag=f(.45);scale=f(rng.choice([1,1.15,1.3]));draft=f(rng.choice([0,.3]));limit=f(rng.choice([0,60]))
        duration=f(rng.choice([-.2,0,.5,1.2]));desired=f(rng.uniform(0,250));length=f(88.5)
        m,_,_,_=fixture(image,points,heading=heading,speed=speed)
        m.allowed.extend([(0x463910,0x463B22),(0x59D430,0x59D73C),(0x456830,0x456901),
                          (0x4638A0,0x463905),(0x583EA0,0x583F18),(0x45DB60,0x45DB77)])
        m.put(DRIVER,0x6C8C18);m.put(DRIVER+0x10,12,'B')
        m.put(DRIVER+0x1BC,length,'f');m.put(DRIVER+0x200,desired,'f');m.put(DRIVER+0x204,duration,'f')
        m.put(ACTOR+0x10,RIDE);m.put(ACTOR+0x28,4);m.put(ACTOR+0x3D0,speed,'f')
        m.put(ACTOR+0x404,scale,'f');m.put(ACTOR+0x47C,draft,'f')
        m.put(RIDE+0x110,mass,'f');m.put(RIDE+0x260,drag,'f');m.put(RIDE+0x42C,limit,'f')
        m.put(RIDE+0x410,64);m.put(RIDE+0x414,0.,'f');m.put(RIDE+0x418,98.34600067138672,'f')
        m.put(RIDE+0x41C,f(63/98.34600067138672),'f');m.put(RIDE+0x420,SCALAR+0x800);m.put(RIDE+0x424,1,'B')
        for i,v in enumerate(curve):m.put(SCALAR+0x800+i*4,v,'f')
        position=[f(points[1][0]+rng.uniform(0,35)),f(points[1][1]+rng.uniform(-2,2)),f(points[1][2])]
        velocity=[speed,1.,0.]
        for j in range(3):m.put(ACTOR+0x60+4*j,position[j],'f');m.put(ACTOR+0x70+4*j,velocity[j],'f')
        m.run(0x463910,(PHYSICS,))
        assert not m.events,m.events
        result=snapshot(m)
        rows.append(dict(points=points,position=position,velocity=velocity,speed=speed,desired=desired,duration=duration,
                         mass=mass,drag=drag,scale=scale,draft=draft,limit=limit,length=length,
                         parameter=m.get(PHYSICS+0x1C,'f'),rate=m.get(PHYSICS+0x20,'f'),acceleration=m.get(PHYSICS+0x24,'f'),
                         ppm=m.get(PHYSICS+0x10,'f'),channels=result['channels']))
    return curve,rows

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('root','output','evidence'):ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args();raw=(a.root/'speed.exe').read_bytes();sha=hashlib.sha256(raw).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    image=Image(raw);speed_curve,speed_rows=speed_publication_cases(image);cases=[];rows=[];records=[];departures=departure_cases(image);projections=projection_cases(image)
    for variant in range(9):
        speed=65. if variant==2 else (0. if variant==7 else 25.)
        acceleration=0. if variant==7 else .2
        heading=9137 if variant==3 else 0
        yaw=-.2 if variant==1 else .2
        points=[(i*10.,0. if i<2 else (i-1)**2*2.*(-1 if variant==1 else 1),i*.2) for i in range(12)]
        if variant in (6,8):points=[(10. if variant==6 else 0.,0.,i*.2) for i in range(12)]
        m,taps,control,seed=fixture(image,points,heading,speed,yaw=yaw)
        parameter=-.2 if variant==4 else 1.
        m.put(PHYSICS+0x1C,parameter,'f')
        m.put(PHYSICS+0x24,acceleration,'f')
        start=[m.get(ACTOR+0x60+4*k,'f') for k in range(3)]
        cases.append([points,heading,speed,yaw,parameter,acceleration,start,seed])
        native=[]
        for frame in range(100):
            dt=([1/60,1/30,.01,.025][frame%4] if variant==3 else 1/60)
            if variant==5:dt=struct.unpack('<f',struct.pack('<I',0x38D1B717+(frame%3)-1))[0]
            if frame==50:
                # Retain the component and filter while republishing a Driver
                # path. Execute original projection-delta/normalization and
                # the lateral/publication tail, with the new parameter given.
                elapsed=m.get(PHYSICS+0xC,'f');rate=m.get(PHYSICS+0x20,'f');acc=m.get(PHYSICS+0x24,'f')
                parameter=(elapsed*acc*.5+rate)*elapsed+m.get(PHYSICS+0x1C,'f')
                parameter=struct.unpack('<f',struct.pack('<f',parameter))[0]
                m.put(PHYSICS+0x1C,parameter,'f');m.put(PHYSICS+0x20,elapsed*acc+rate,'f');m.put(PHYSICS+0xC,0.,'f')
                delta,tangent=SCALAR+0x100,SCALAR+0x200
                m.run(0x463800,(PHYSICS,ACTOR+0x60,bits(parameter)),esi=delta,edi=tangent)
                m.run(0x567370,ecx=tangent,edx=tangent)
                for k in range(3):
                    m.put(STACK+0x30+4*k,m.get(tangent+4*k,'f'),'f')
                    m.put(STACK+0x40+4*k,m.get(delta+4*k,'f'),'f')
                m.run(0x463A50,ebx=PHYSICS,end=0x463B1B)
            control['missing']=1 if variant==4 and 20<=frame<25 else 0
            
            try:m.run(0x463F20,(bits(dt),),ecx=PHYSICS,end=0x464470)
            except Exception:
                print('Failed variant/frame',variant,frame,flush=True);raise
            r=snapshot(m);native.append(r)
            r['parameter']=m.get(PHYSICS+0x1C,'f');r['rate']=m.get(PHYSICS+0x20,'f')
            channels=[[c[0],c[1],c[9],c[11]] for c in r['channels']]
            rows.append([variant,frame,dt,control['missing'],r['parameter'],r['rate'],r['position'],r['velocity'],r['axes'],
                r['heading'],r['yaw'],r['speed'],r['sliding'],r['curvature'],channels])
        if len(taps)!=len(native)*4:raise ValueError('missing ground query')
        if any(e['hook']!='explicit_road_surface' for e in m.events):raise ValueError('unexpected hook')
        records.append(dict(variant=variant,rows=native,queries=taps,hooks=m.events))
    def lit(v):
        if isinstance(v,(list,tuple)):return '{'+','.join(map(lit,v))+'}'
        if isinstance(v,int):return str(v)
        text=format(v,'.9g');return text+('.F' if '.' not in text and 'e' not in text else 'F')
    out='// Original463436 seed and463F20 body; explicit440A40 road query only.\n'
    out+='static const PathBodyCase path_body_cases[]={\n'+'\n'.join(lit(c)+',' for c in cases)+'\n};\n'
    out+='static const PathBodyFrame path_body_frames[]={\n'+'\n'.join(lit(r)+',' for r in rows)+'\n};\n'
    out+='static const PathDepartureCase path_departure_cases[]={\n'+'\n'.join(lit(r)+',' for r in departures)+'\n};\n'
    out+='static const PathProjectionCase path_projection_cases[]={\n'+'\n'.join(lit(r)+',' for r in projections)+'\n};\n'
    out+='static const std::array<float,64> path_speed_curve='+lit(speed_curve)+';\n'
    speed_keys=('points','position','velocity','speed','desired','duration','mass','drag','scale','draft','limit','length','parameter','rate','acceleration','ppm','channels')
    out+='static const PathSpeedCase path_speed_cases[]={\n'+''.join(lit([r[k] for k in speed_keys])+',\n' for r in speed_rows)+'};\n'
    a.output.write_text(out,encoding='ascii')
    a.evidence.write_text(json.dumps(dict(executable_sha256=sha,speed_curve=speed_curve,speed_publications=speed_rows,cases=cases,departures=departures,projections=projections,runs=records),indent=2)+'\n',encoding='ascii')
    print('PASS original selector4:',len(cases),'constructor seeds;',len(rows),'body frames;',len(rows)*4,'road queries;',len(projections),'path projections;',len(speed_rows),'whole speed publications')

if __name__=='__main__':main()

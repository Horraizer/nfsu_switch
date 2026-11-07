"""Execute original57A910 on supplied initialized world poses; read-only retail.
Bounds are output-only observations of original shape dispatch, not invented
car dimensions. This covers neighborhood production, not a physical race.
"""
import argparse,hashlib,json,struct
from pathlib import Path
import capstone
from unicorn import UC_HOOK_CODE
from world_body_trace import Image
from controller293_scene import PairedController,HUMAN
from controller293_planner import inputs,CTX
from controller293_strategy import CONTROLLER
from controller293_native import EXEC_RANGES
from physics256_native import ACTOR
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('root','output','evidence'):ap.add_argument('--'+key,type=Path,required=True)
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes()
    identity='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest()!=identity:raise ValueError('unsupported executable')
    im=Image(data);pair=PairedController(im,inputs(args.root),event_rides=True);m=pair.m
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    for a,b in list(EXEC_RANGES)+[(0x57a910,0x57b47a),(0x4569b0,0x456a7b),(0x57a5f0,0x57a67c),
        (0x429c50,0x429cb2),(0x567370,0x5673dc),(0x44ace8,0x44acee),(0x44d0cf,0x44d0f3),
        (0x5a7007,0x5a7014),(0x5a6150,0x5a6306),(0x5a7390,0x5a7447),(0x462a60,0x462a99),
        (0x435ca0,0x435d30),(0x44cf30,0x44d3d4),(0x462840,0x4629dc),(0x4629e0,0x4629ff),
        (0x458bd0,0x458cbb),(0x4216f0,0x421740),(0x44bad0,0x44baef),(0x44adc0,0x44b060),
        (0x5a6d20,0x5a6e0f),(0x462a00,0x462a52),(0x433b0a,0x433b15)]:
        m.instructions.update({i.address:i.size for i in md.disasm(im.read(a,b-a),a)})
    store,scalar_result=0x119f100,0x119f000;code=b'\xd9\x1d'+struct.pack('<I',scalar_result)+b'\xc3'
    m.uc.mem_write(store,code);m.instructions.update({i.address:i.size for i in md.disasm(code,store)})
    chain=m.get(CTX+0x3a8);bounds={};active=False;horizon=0;predictive=[]
    def observe(uc,address,size,user):
        nonlocal horizon
        if address==0x57aa18:horizon=m.get(m.reg('esp')+0x3c,'f')
        if address==0x57b1e1:
            sp=m.reg('esp');node=m.reg('ebx')
            bounds[node]=[[m.get(sp+at+4*k,'f') for k in range(2)] for at in (0x60,0x50)]
        if active and address==0x4569b0:predictive.append(m.reg('eax'))
    m.uc.hook_add(UC_HOOK_CODE,observe)
    def vector(a):return [m.get(a+4*k,'f') for k in range(3)]
    def pose(actor,point,speed,side=0,flip=False):
        pair.actor.pose(point,speed,actor=actor)
        if side:
            m.put(actor+0x60,m.get(actor+0x60,'f')+side*m.get(actor+0x94,'f'),'f')
            m.put(actor+0x64,m.get(actor+0x64,'f')-side*m.get(actor+0x90,'f'),'f')
        if flip:
            for at in (0x90,0x94,0xa0,0xa4,0x70,0x74):m.put(actor+at,-m.get(actor+at,'f'),'f')
            m.put(actor+0x3b4,(m.get(actor+0x3b4,'H')+0x8000)&65535,'H')
        m.run(0x57a380,esi=m.get(m.get(actor+0x82c)+0x20))
        pair.project(actor);m.run(0x4338c0,edi=m.get(actor+0x1c),budget=3000000)
    def nodes():
        result=[];node=m.get(0x73b07c)
        while node!=0x73b07c:
            if node in result or len(result)>64:raise ValueError('invalid world list')
            result.append(node);node=m.get(node)
        return result
    records=[]
    scenarios=[('near',30,20.,31,14.,0,False),('behind',32,25.,30,32.,0,False),
        ('side',30,20.,30,20.,3.5,False),('opposite',30,20.,31,25.,0,True),
        ('far_slow',15,20.,30,21.,0,False),('far_fast',15,20.,30,65.,0,False),
        ('stopped',30,20.,30,0.,60,False),('off_road',30,20.,30,40.,80,False),
        ('bend',36,35.,37,27.,-2,False),('same',30,20.,30,20.,0,False),
        ('wrap',30,20.,31,14.,0,False),('progress_reject',30,20.,31,14.,0,False),
        ('accelerating',15,20.,30,21.,0,False),('braking',15,20.,30,65.,0,False)]
    # First prime the shape observations with both admitted original actors.
    pose(ACTOR,30,20.);pose(HUMAN,30,20.)
    m.run(0x44cf30,(CTX,CONTROLLER),budget=3000000)
    for label,point,speed,hpoint,hspeed,side,flip in scenarios:
        pose(ACTOR,point,speed);pose(HUMAN,hpoint,hspeed,side,flip)
        m.run(0x44cf30,(CTX,CONTROLLER),budget=3000000)
        # Declared caller snapshots, not an integrated physical trajectory.
        delta=m.get(0x7372b0,'f') if label=='wrap' else 1000. if label=='progress_reject' else 0.
        if delta:m.put(HUMAN+0x814,m.get(HUMAN+0x814,'f')+delta,'f')
        acceleration=35. if label=='accelerating' else -5. if label=='braking' else 0.
        for k in range(3):m.put(ACTOR+0x80+4*k,acceleration*m.get(ACTOR+0x90+4*k,'f'),'f')
        category=m.get(CTX+0x394,'i');ride_category=m.get(m.get(ACTOR+0x10)+0x424,'b')
        road=[]
        for j in range(m.get(chain+4)):
            c=m.get(chain+8+4*j);rid=m.get(c+0x5c,'b');index=m.get(c+0x5e,'h')
            p=m.get(0x77af30+rid*4)+0x27c+index*0x88
            road.append([vector(p),[m.get(p+0xc+4*k,'f') for k in range(2)],
                [m.get(p+0x3c+2*k,'h')/256. for k in range(5)],
                [m.get(p+0x3c+2*(k+11),'h')/256. for k in range(12)],
                m.get(c+0x58,'f'),[m.get(c+4+4*k,'f') for k in range(7)]])
        actors=[];self_node=None
        for node in nodes():
            ref=m.get(node+0x1d4);state=m.get(ref+0x1c)
            if m.get(state+0xe4):m.run(0x5791c0,ecx=state)
            m.run(m.get(m.get(ref)+0x20),ecx=ref);enabled=bool(m.reg('eax')&255)
            m.run(0x578730,esi=ref);forward=vector(m.reg('eax'))
            m.run(0x647b70,ecx=ref+0x1c);kind=m.reg('eax')
            if kind==4:progress=m.get(m.get(node+0x1d0)+0x14,'f')
            else:
                m.run(m.get(m.get(ref)+0x24),ecx=ref);m.run(store,budget=20);progress=m.get(scalar_result,'f')
            if ref==m.get(ACTOR+0x82c):self_node=node
            if node not in bounds:raise ValueError('missing original bounds')
            actors.append([node,vector(state+0x40),vector(state+0x90),forward,progress,*bounds[node],enabled])
        if self_node is None:raise ValueError('missing self registration')
        circuit=m.get(0x7372b8)>0 and bool(m.get(m.get(0x7372bc)+1,'B'))
        indata=[self_node,vector(ACTOR+0x60),vector(ACTOR+0x70),vector(ACTOR+0x90),
            vector(ACTOR+0x80),vector(0x7372a0),m.get(ACTOR+0x3b4,'H'),m.get(ACTOR+0x814,'f'),
            horizon,m.get(0x7372b0,'f'),category,ride_category,circuit]
        predictive.clear();active=True;m.run(0x57a910,(CTX,chain,category),budget=3000000);active=False
        result=[[m.get(CTX+8+56*j+4*k) for k in range(14)] for j in range(m.get(CTX+4))]
        records.append(dict(label=label,road=road,input=indata,actors=actors,entries=result,
            self_index=m.get(CTX+0x388,'b'),predictive=list(predictive)))
    def lit(v):
        if isinstance(v,bool):return str(v).lower()
        if isinstance(v,float):return literal(v)
        if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
        return str(v)
    lines=['// Original57A910, initialized event0 actors, supplied poses.']
    for r in records:
        lines.append('{'+','.join([str(len(r['road'])),lit(r['road']),lit(r['input']),str(len(r['actors'])),
            lit(r['actors']),str(len(r['entries'])),lit(r['entries']),str(r['self_index'])])+'}, // '+r['label'])
    args.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    constants={hex(a):m.get(a,'f') for a in (0x700680,0x7006f0,0x6cc7ac,0x6cc8a8)}
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,constants=constants,cases=records),indent=2)+'\n',encoding='ascii')
    print('Original neighbor cases:',len(records),'entries:',[len(r['entries']) for r in records],
        'predictive calls:',sum(len(r['predictive']) for r in records))
if __name__=='__main__':main()

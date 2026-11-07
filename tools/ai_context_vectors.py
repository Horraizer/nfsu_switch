"""Original44CF30 spatial publication; supplied poses, no replacement callbacks."""
import argparse,hashlib,json,struct
from pathlib import Path
import capstone
from unicorn import UC_HOOK_CODE
from world_body_trace import Image
from controller293_scene import PairedController
from controller293_planner import inputs,CTX
from controller293_strategy import CONTROLLER
from controller293_native import EXEC_RANGES
from physics256_native import ACTOR,STATUS
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('root','output','evidence'):ap.add_argument('--'+name,type=Path,required=True)
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes()
    identity='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest()!=identity:raise ValueError('unsupported executable')
    im=Image(data);pair=PairedController(im,inputs(args.root),event_rides=True);m=pair.m
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    for a,b in list(EXEC_RANGES)+[(0x57b358,0x57b369),(0x44ace8,0x44acee),(0x44d0cf,0x44d0f3),(0x5a7007,0x5a7014),
        (0x5a6150,0x5a6306),(0x5a7390,0x5a7449),(0x462a60,0x462a99),(0x435ca0,0x435d30),
        (0x44cf30,0x44d3d4),(0x462840,0x4629dc),(0x4629e0,0x4629ff),
        (0x458bd0,0x458cbb),(0x4216f0,0x421740),(0x44bad0,0x44baef),(0x44adc0,0x44b060),
        (0x5a6d20,0x5a6e0f),(0x462a00,0x462a52)]:
        m.instructions.update({i.address:i.size for i in md.disasm(im.read(a,b-a),a)})
    store,result=0x119f100,0x119f000;code=b'\xd9\x1d'+struct.pack('<I',result)+b'\xc3'
    m.uc.mem_write(store,code);m.instructions.update({i.address:i.size for i in md.disasm(code,store)})
    def scalar(address,**regs):
        m.run(address,budget=300000,**regs);m.run(store,budget=20);return m.get(result,'f')
    def virt(slot):return m.get(m.get(CONTROLLER)+slot)
    chain=m.get(CTX+0x3a8);world=m.get(0x7361f8);ride=m.get(ACTOR+0x10)
    def snapshot():
        route=m.get(CTX+0x38c);raw=m.get(CTX+0x390)
        state=dict(selected=bool(route),road=[m.get(route+0xa,'h'),(raw-route-0x27c)//0x88] if route else [0,0],
            category=m.get(CTX+0x394,'i') if route else m.get(ride+0x424,'b'),nearest=m.get(CONTROLLER+0x4a4,'b'),
            origin=m.get(chain+0xac,'i'),cells=[],rng=m.get(world+0x2d0),
            cache=[m.get(CONTROLLER+0xc+4*k) for k in range(8)],marker=m.get(CONTROLLER+0x478,'i'),
            flags=[bool(m.get(chain+0xb0+k,'B')) for k in range(7)],limited=m.get(chain+0xb8,'i'),
            obstacle=m.get(chain+0xc0,'i'),event=bool(m.get(chain+0xbc,'B')))
        for j in range(m.get(chain+4)):
            c=m.get(chain+8+4*j)
            if m.get(c)!=0x6c8f90:raise ValueError('nonforward cell')
            expected=m.get(chain+8+4*(j-1)) if j else 0
            if m.get(c+0x68)!=expected:raise ValueError('unexpected preceding-cell link')
            state['cells'].append([m.get(c+0x5c,'b'),m.get(c+0x5e,'h'),m.get(c+0x58,'f'),bool(m.get(c+0x60,'B')),
                *[[m.get(c+at+4*k,'f') for k in range(7)] for at in (4,0x20,0x3c)]])
        return state
    observations={}
    def observe(uc,address,size,user):
        if address==0x44d2f4:observations['after']=snapshot()
        if address==0x44c250:observations['reselect']=True
        if address==0x44d12d:observations['width_seen']=True
        if address==0x44d128:observations['clear']=True
        if address==0x44d0e5:
            event=m.reg('eax');observations['passed'].append((event-m.get(0x736150))//32 if event else -1)
        if address==0x44d23f:observations['category_available']=bool(m.reg('eax')&255)
    m.uc.hook_add(UC_HOOK_CODE,observe)
    records=[]
    poses=[(1,p,o,c) for p,o,c in ((30,0,False),(31,0,False),(33,0,False),(36,0,False),(37,0,False),
        (37,25,False),(36,0,False),(10,0,False),(11,0,False),(12,0,True),(13,0,False))]
    poses += [(35,90,0,False),(35,93,0,False),(35,95,0,False),(37,12,0,False),(37,13,0,False),(37,15,0,False),
              (1,30,10000,False),(1,30,0,False)]
    for seq,(route_id,pose,offset,cold) in enumerate(poses):
        route=m.get(0x77af30+4*route_id)
        if not route or not 0<=pose<m.get(route+0x10,'h'):raise ValueError('invalid supplied pose')
        raw=route+0x27c+pose*0x88
        for k in range(3):m.put(ACTOR+0x60+4*k,m.get(raw+4*k,'f'),'f')
        for k in range(2):
            tangent=m.get(raw+0xc+4*k,'f')
            m.put(ACTOR+0x70+4*k,20.*tangent,'f');m.put(ACTOR+0x90+4*k,tangent,'f')
        m.put(ACTOR+0xa0,-m.get(ACTOR+0x94,'f'),'f');m.put(ACTOR+0xa4,m.get(ACTOR+0x90,'f'),'f')
        m.run(0x564db0,(m.get(ACTOR+0x94),m.get(ACTOR+0x90)));m.put(ACTOR+0x3b4,m.reg('eax')&65535,'H')
        m.put(ACTOR+0x3d0,20.,'f');m.put(ACTOR+0x3d4,20.,'f');m.put(ACTOR+0x814,m.get(raw+0x38,'f'),'f')
        if offset:
            m.put(ACTOR+0x60,m.get(ACTOR+0x60,'f')+offset,'f')
        m.run(0x57a380,esi=m.get(m.get(ACTOR+0x82c)+0x20))
        if cold:m.run(0x5a5340,esi=chain,budget=300000)
        m.put(0x73ad34,500100+80*seq)
        before=snapshot();position=[m.get(ACTOR+0x60+4*k,'f') for k in range(3)]
        body=m.get(ACTOR+0x3c);width=m.get(body+0x104,'f')-m.get(body+0xf4,'f')
        keep_addr=virt(0x24);m.run(keep_addr,ecx=CONTROLLER);keep=bool(m.reg('eax')&255)
        m.run(virt(0x64),ecx=CONTROLLER);allow=bool(m.reg('eax')&255)
        initial=max(m.get(0x700680,'f'),scalar(virt(0x50),ecx=CONTROLLER)*20.)
        retained=scalar(0x458b90,ecx=CONTROLLER)
        scratch=0x1150600;m.run(virt(0x54),(scratch,scratch+4,scratch+8),ecx=CONTROLLER)
        require=bool(m.get(scratch+8,'B'))
        selection=[position,m.get(ACTOR+0x814,'f'),m.get(0x6b7704,'f'),m.get(scratch,'i'),m.get(scratch+4,'i'),
            m.get(0x78a2fc),m.get(m.get(ACTOR+0x14)+4),-1,require,True]
        config=m.get(ACTOR+0x14);difficulty=m.get(config+0x735,'b')
        probabilities=[m.get(0x779ff8+difficulty*0x54+k*4,'f') for k in range(3)]
        gate=m.get(m.get(0x736174)+0xb8,'h');m.run(0x435ca0,(STATUS,ACTOR),budget=300000)
        policy=[m.get(0x78a2fc),m.get(config+4),gate,m.get(STATUS+0x14,'i')*.00025,bool(m.reg('eax')&255),probabilities]
        observations.clear();observations.update(reselect=False,clear=False,passed=[],category_available=False)
        m.run(0x44cf30,(CTX,CONTROLLER),budget=3000000)
        if 'after' not in observations:raise ValueError('missing spatial publication')
        records.append(dict(route=route_id,pose=pose,offset=offset,cold=cold,before=before,after=observations['after'],
            selection=selection,values=[width,m.get(0x6ccc30,'f'),initial,retained],keep=keep,allow=allow,policy=policy,
            reselected=observations['reselect'],clear=observations['clear'],passed=observations['passed'],category_available=observations['category_available']))
    def lit(v):
        if isinstance(v,bool):return str(v).lower()
        if isinstance(v,float):return literal(v)
        if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
        return str(v)
    def snap(s):
        return '{'+','.join(map(lit,[s['selected'],s['road'],s['category'],s['nearest'],s['origin'],len(s['cells']),s['cells'],
            s['rng'],s['cache'],s['marker'],s['flags'],s['limited'],s['obstacle'],s['event']]))+'}'
    lines=['// Original44CF30 spatial update, captured before neighbor publication57A910.','const AiRoadEvent context_events[]={']
    base=m.get(0x736150)
    for j in range(m.get(0x736154)):
        a=base+32*j;lines.append(lit([m.get(a,'f'),m.get(a+4,'H')]+[m.get(a+k,'B') for k in (8,9,11,13,15,16)])+',')
    lines+=['};','const ContextPointEvent context_point_events[]={']
    for rid in range(128):
        route=m.get(0x77af30+4*rid)
        if not route:continue
        for index in range(m.get(route+0x10,'h')):
            p=route+0x27c+index*0x88;ids=[m.get(p+at,'B') for at in (0x81,0x82)]
            if ids!=[255,255]:lines.append(lit([rid,index,*ids])+',')
    lines+=['};','const ContextCase context_cases[]={']
    for r in records:
        lines.append('{'+','.join([snap(r['before']),snap(r['after']),'{'+','.join([lit(r['selection']),*map(lit,r['values']),lit(r['keep']),lit(r['allow'])])+'}',
            lit(r['policy']),lit(r['reselected']),lit(r['clear']),lit(r['category_available']),lit(len(r['passed'])),lit(r['passed'])])+'},')
    lines.append('};');args.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,cases=records),indent=2)+'\n',encoding='ascii')
    print('Original spatial context:',len(records),'cases; reselected',sum(r['reselected'] for r in records),'width getter %08X'%virt(0x28))
if __name__=='__main__':main()

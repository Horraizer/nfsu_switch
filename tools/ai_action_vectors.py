"""Authenticated action offset and speed publication; supplied event0 contexts."""
import argparse,hashlib,json,struct
from pathlib import Path
import capstone
from unicorn import UC_HOOK_CODE
from world_body_trace import Image
from controller293_scene import PairedController
from controller293_planner import inputs,CTX
from controller293_strategy import CONTROLLER,ACTION
from controller293_native import EXEC_RANGES
from physics256_native import ACTOR,bits
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('root','output','evidence'):ap.add_argument('--'+name,type=Path,required=True)
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes()
    identity='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest()!=identity:raise ValueError('unsupported executable')
    im=Image(data);pair=PairedController(im,inputs(args.root),event_rides=True);m=pair.m
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    for a,b in list(EXEC_RANGES)+[(0x45c550,0x45c636),(0x583f20,0x583fe5),(0x44c550,0x44c7f1),(0x442110,0x442131)]:
        m.instructions.update({i.address:i.size for i in md.disasm(im.read(a,b-a),a)})
    store,result=0x119f100,0x119f000;code=b'\xd9\x1d'+struct.pack('<I',result)+b'\xc3'
    m.uc.mem_write(store,code);m.instructions.update({i.address:i.size for i in md.disasm(code,store)})
    def scalar(address,args=(),**regs):
        m.run(address,args,budget=3000000,**regs);m.run(store,budget=20)
        return m.get(result,'f')
    offsets=[];original_selector=m.get(ACTOR+0x24);clock=m.get(0x73619c)
    for mode,selector,special,seconds,speed in (
        (mode,selector,special,seconds,speed)
        for mode in (0,1,2,3,4) for selector in (3,4)
        for special in (False,True) for seconds,speed in ((4.9,40.),(5.,26.821636199951172),(6.,28.),(6.,30.),(6.,60.))):
        m.put(ACTOR+0x24,selector);m.put(ACTOR+0x3d4,speed,'f');m.put(0x78a344,int(special),'B')
        m.put(0x73ad34,500000);m.put(clock+4,500000-round(seconds*4000))
        elapsed=scalar(0x432c90,ecx=clock)
        value=scalar(0x45c550,eax=mode,ecx=CTX)
        offsets.append(dict(mode=mode,selector=selector,special=special,seconds=elapsed,speed=speed,value=bits(value)))
    m.put(ACTOR+0x24,original_selector);m.put(0x78a344,0,'B');pair.actor.pose(30,20.)
    m.run(0x44cf30,(CTX,CONTROLLER),budget=3000000)
    descriptor=m.get(CONTROLLER+0x3d8);saved=bytes(m.uc.mem_read(descriptor,12));world=m.get(0x7361f8)
    publications=[];snapshot={}
    def capture(uc,address,size,user):
        if address!=0x44c578:return
        route=m.get(descriptor);point=m.get(descriptor+4);category=m.get(descriptor+8)
        context=m.get(m.get(ACTOR+0x14)+0x735,'b');row=0x779ff8+max(0,context)*0x54
        marker=m.get(CONTROLLER+0x478,'i');marker_speeds=None
        if marker>=0:
            event=m.get(0x736150)+32*marker
            raw=m.get(0x77af30+4*m.get(event+9,'B'))+0x27c+m.get(event+4,'H')*0x88
            marker_speeds=list(m.uc.mem_read(raw+0x6a,23))
        snapshot.clear();snapshot.update(route=bool(route),point=(point-route-0x27c)//0x88 if route else 0,category=category,
            direction=m.get(0x78a2fc),context=context,tick=m.get(0x73ad34),interval=m.get(0x77a498,'f'),
            context_scale=m.get(row+0x14,'f'),catchup=m.get(CONTROLLER+0x4b8,'f'),amplitude=m.get(row+0x50,'B'),
            offset_tick=m.get(CONTROLLER+0x400),offset=m.get(CONTROLLER+0x404,'i'),rng=m.get(world+0x2d0),
            rows=[list(m.uc.mem_read(route+0x27c+j*0x88+0x6a,23)) for j in range(m.get(route+0x10,'h'))] if route else [],
            marker=marker_speeds,route_speed=bool(m.get(ACTION+0x1f0,'B')),enabled=bool(m.get(ACTION+0x1f1,'B')),
            action_speed=m.get(ACTION+0x1e4,'f'),duration=m.get(ACTION+0x1e8,'f'),auxiliary=m.get(ACTION+0x1ec,'f'),
            debug_constant=bool(m.get(0x7360b8)),debug_route=bool(m.get(0x736090)),debug_auxiliary=m.get(0x6b77f8,'f'))
    hook=m.uc.hook_add(UC_HOOK_CODE,capture,begin=0x44c578,end=0x44c578)
    for case in range(12):
        m.uc.mem_write(descriptor,saved);m.put(CONTROLLER+0x478,0xffffffff)
        m.put(0x73ad34,600000+case*8000);m.put(CONTROLLER+0x400,600000 if case%2==0 else 0)
        m.put(CONTROLLER+0x404,case-6,'i');m.put(world+0x2d0,0x12345678+case)
        m.put(ACTION+0x1f0,int(case!=7),'B');m.put(ACTION+0x1f1,int(case!=8),'B')
        m.put(ACTION+0x1e4,33.5,'f');m.put(ACTION+0x1e8,.75,'f');m.put(ACTION+0x1ec,.2,'f')
        m.put(0x7360b8,int(case==9));m.put(0x736090,int(case==10))
        if case==6:m.put(CONTROLLER+0x478,0)
        if case==11:m.put(descriptor,0)
        m.run(0x44c550,(CONTROLLER,),end=0x44c7f1,budget=3000000)
        driver=m.get(CONTROLLER+8);strategy=m.get(CONTROLLER+0x3fc)
        cap=scalar(m.get(m.get(strategy)+0x10),ecx=strategy)
        publications.append(dict(input=dict(snapshot),cap=cap,result=[m.get(driver+a) for a in (0x200,0x204,0x208)]+[m.get(driver+0x20c,'B')],
            after_tick=m.get(CONTROLLER+0x400),after_offset=m.get(CONTROLLER+0x404,'i'),rng=m.get(world+0x2d0)))
    m.uc.hook_del(hook)
    lines=['// Original45C550 and full44C550 through actual driver setter.','const OffsetCase offsets[]={']
    for c in offsets:lines.append('{{%d,%d,%s,%s,%s},0x%08Xu},'%(c['mode'],c['selector'],literal(c['speed']),literal(c['seconds']),str(c['special']).lower(),c['value']))
    lines.append('};');lines.append('const PublicationCase publications[]={')
    for c in publications:
        i=c['input'];rows=i['rows'];marker=i['marker'] or [0]*23
        lines.append('{%d,{%s},{%s},%s,%d,%d,%d,%d,%du,%s,%s,%s,%s,%d,%du,%d,0x%08Xu,%s,%s,%s,%s,%s,%s,%s,%s,%s,{%s},%du,%d,0x%08Xu},'%(
            len(rows),','.join('{'+','.join(map(str,r))+'}' for r in rows),','.join(map(str,marker)),str(i['marker'] is not None).lower(),
            i['point'],i['category'],i['direction'],i['context'],i['tick'],literal(i['interval']),literal(i['context_scale']),literal(i['catchup']),literal(c['cap']),
            i['amplitude'],i['offset_tick'],i['offset'],i['rng'],str(i['route_speed']).lower(),str(i['enabled']).lower(),str(i['debug_constant']).lower(),str(i['debug_route']).lower(),
            literal(i['action_speed']),literal(i['duration']),literal(i['auxiliary']),literal(i['debug_auxiliary']),str(i['route']).lower(),
            ','.join('0x%08Xu'%v for v in c['result']),c['after_tick'],c['after_offset'],c['rng']))
    lines.append('};');args.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,offsets=offsets,publications=publications),indent=2)+'\n',encoding='ascii')
    print('original offset',len(offsets),'publication',len(publications))
if __name__=='__main__':main()

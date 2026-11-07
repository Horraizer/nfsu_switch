"""Unmodified original recovery query on declared road/body/contact snapshots.
Retail is read-only. Original code resolves contact-list entries, body-local
projection, road classification and the actual controller reset callback.
"""
import argparse,hashlib,json,struct
from pathlib import Path
import capstone
from world_body_trace import Image
from controller293_scene import PairedController,HUMAN
from controller293_planner import inputs,CTX
from controller293_strategy import CONTROLLER,ACTION
from physics256_native import ACTOR,bits
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for k in ('root','output','evidence'):ap.add_argument('--'+k,type=Path,required=True)
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes();sha=hashlib.sha256(data).hexdigest()
    if sha!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':raise ValueError('unsupported executable')
    image=Image(data);pair=PairedController(image,inputs(args.root),event_rides=True);m=pair.m
    dis=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    for a,b in ((0x44e9c0,0x44eccb),(0x44ecd0,0x44ed6f),(0x451b00,0x451cac),
        (0x4556d0,0x45575e),(0x57a7d0,0x57a814),(0x468820,0x46889d),(0x424b50,0x424b6e),
        (0x462aa0,0x462ad2),(0x5672b0,0x567304),(0x464f20,0x4650d0),
        (0x4650d0,0x465141),(0x565230,0x565336),(0x45db50,0x45db57),(0x435ca0,0x435d30),
        (0x40bd40,0x40be3c),(0x567370,0x5673dc),(0x421970,0x421980),
        (0x4519f0,0x451a65),(0x451cb0,0x451d4d),(0x451dc0,0x451f01),
        (0x451f10,0x45206d),(0x454c50,0x454c84),(0x44f700,0x44f741),
        (0x44a080,0x44a0d6),(0x44e2b0,0x44e335)):
        m.instructions.update({i.address:i.size for i in dis.disasm(image.read(a,b-a),a)})
    road=0x119c000;contact=0x119c100;link=0x119c200;out=0x119c500
    m.uc.mem_write(road,bytes(0x88));m.put(road+0x10,1.,'f');m.put(road+0x24,5.,'f');m.put(road+0x28,5.,'f')
    node=m.get(0x73b07c)
    while node!=0x73b07c:
        if m.get(node+0x1d4)==m.get(ACTOR+0x82c):selfnode=node;break
        node=m.get(node)
    body=m.get(ACTOR+0x3c);driver=m.get(ACTOR+0x30);clock=m.get(0x73619c)
    m.run(m.get(m.get(driver)+0x7c),ecx=driver,budget=3000000);target=m.reg('eax')
    baseline_clock=bytes(m.uc.mem_read(clock,32));rows=[];lifetimes=[];first_event=len(m.events)
    # x, prediction vector, signed speed, heading, target, contact point/age,
    # support count, held start. Two motion vectors remain distinct inputs.
    specs=[(0,0,10,10,16384,(0,100),None,0,4,False),
        (4,10,0,10,0,(0,100),None,0,4,False),
        (6,10,0,10,0,(0,100),None,0,4,False),
        (-6,-10,0,10,32768,(0,100),None,0,4,False),
        (6,-10,0,10,32768,(0,100),None,0,4,False),
        (-6,10,0,10,0,(0,100),None,0,4,False),
        (6,0,0,0,0,(0,100),None,0,4,False),
        (-6,0,0,0,32768,(0,100),None,0,4,False),
        (6,0,0,0,32768,(0,100),None,0,4,False),
        (4,10,0,-10,32768,(0,100),None,0,4,False),
        (4,10,0,10,0,(100,0),None,0,4,False),
        (25,0,30,30,16384,(25,100),None,0,4,False),
        (25,0,30,30,16384,(25,100),None,0,1,False),
        (25,0,30,30,16384,(25,100),None,0,4,True)]
    for point,age,speed in [((0,1),1599,0),((0,1),1600,0),((0,1),2000,5),((0,1),2000,5.01),((0,-1),2000,0),((3,1),2000,0)]:
        specs.append((0,0,speed,speed,16384,(0,100),point,age,4,False))
    for x,vx,vy,speed,heading,goal,point,age,support,hold in specs:
        m.uc.mem_write(clock,baseline_clock);m.put(0x73ad34,500000)
        if hold:m.put(clock+0x10,500000)
        m.put(CTX+0x38c,1);m.put(CTX+0x390,road);m.put(CTX+0x388,0,'b');m.put(CTX+0x3c,selfnode)
        m.put(CTX+4,1);m.put(ACTOR+0x24,3);m.put(ACTOR+0x3cc,support)
        for j,v in enumerate((float(x),0.,0.)):m.put(ACTOR+0x60+4*j,v,'f')
        for j,v in enumerate((float(vx),float(vy),0.)):m.put(ACTOR+0x70+4*j,v,'f')
        m.put(ACTOR+0x3b4,heading,'H');m.put(ACTOR+0x3d0,float(speed),'f');m.put(ACTOR+0x3d4,abs(float(speed)),'f')
        # Declared cardinal body axes. Native low-speed prediction still calls
        # its original564B10 table routine; no trig result is intercepted.
        direction={0:(1.,0.),16384:(0.,1.),32768:(-1.,0.)}[heading]
        for j,v in enumerate((*direction,0.)):m.put(ACTOR+0x90+4*j,float(v),'f')
        for j,v in enumerate(goal):m.put(target+4*j,float(v),'f')
        m.put(ACTOR+0x460,0,'B');m.put(body+0x1c,int(point is not None),'b')
        m.put(0x779be0,link+0x250);m.put(0x779be4,link+0x250)
        m.put(link+0x250,0x779be0);m.put(link+0x254,0x779be0)
        m.put(link+0x258,body);m.put(link+0x25c,m.get(HUMAN+0x3c));m.put(link+0x264,contact)
        local=[0.,0.];classification=-1
        if point is not None:
            m.put(contact+8,500000-age)
            for j,v in enumerate(point):m.put(contact+0x30+4*j,float(v),'f')
            m.run(0x4650d0,(out,),eax=ACTOR+0x40,esi=contact+0x30,budget=3000000)
            local=[m.get(out+4*j,'f') for j in range(2)]
            m.run(0x44ecd0,ecx=ACTOR,edx=contact,budget=3000000);classification=m.reg('eax')
        m.run(0x44e9c0,(1,),edi=ACTOR,edx=CTX+0x38c,budget=3000000);road_class=m.reg('eax')
        m.run(0x451b00,(CTX,),budget=3000000);request=m.reg('eax')==500
        motion=[[float(x),0.],[float(vx),float(vy)],list(direction),list(map(float,goal)),float(speed),abs(float(speed)),heading]
        rows.append([motion,local,500000-age,m.get(body+0xf4,'f'),m.get(body+0x104,'f'),point is not None,support>=3,hold,classification,road_class,request])
        # Actual constructor and complete predicate dispatcher. The raw point
        # at selected+3 is a declared lookahead, not a hooked target result.
        m.put(CTX+0x38c,road-0x27c);m.put(CTX+0x390,road);m.put(CTX+0x394,0)
        ahead=road+3*0x88;m.uc.mem_write(ahead,bytes(0x88));m.put(ahead+4,30.,'f');m.put(ahead+0x10,1.,'f')
        for hold_seconds,elapsed_ticks,duration in ((-1.,0,5.),(.5,1999,5.),(.5,2000,5.),(-1.,20000,5.),(-1.,20001,5.)):
            m.put(0x73ad34,500000-elapsed_ticks)
            m.run(0x4519f0,(bits(duration),bits(hold_seconds)),esi=ACTION,ecx=500,budget=3000000)
            lifetime=[m.get(ACTION+0x200),m.get(ACTION+8),hold_seconds,duration]
            m.put(0x73ad34,500000)
            m.run(0x44f700,esi=ACTION,edi=CTX,budget=3000000)
            lifetimes.append([len(rows)-1,lifetime,m.get(ACTION+0x10)])
    cooldown=[]
    for age in (23999,24000,24001,30000):
        m.put(0x73ad34,500000);m.put(CONTROLLER+0x408,500000-age)
        m.run(0x462aa0,ecx=CONTROLLER);cooldown.append([500000,500000-age,bool(m.reg('eax'))])
    def lit(v):
        if isinstance(v,bool):return str(v).lower()
        if isinstance(v,float):return literal(v)
        if isinstance(v,list):return '{'+','.join(map(lit,v))+'}'
        return str(v)
    lines=['// Unchanged native recovery with supplied road/contact snapshots.','const RecoveryCase recovery_cases[]={']
    lines += [lit(v)+',' for v in rows];lines+=['};','const CooldownCase cooldown_cases[]={']
    lines += [lit(v)+',' for v in cooldown];lines+=['};','const LifetimeCase lifetime_cases[]={']
    lines += [lit(v)+',' for v in lifetimes];lines+=['};']
    args.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=sha,fpu_control_word='007F',query_hooks=m.events[first_event:],cases=rows,cooldown=cooldown,lifetimes=lifetimes),indent=2)+'\n',encoding='ascii')
    print('Original recovery:',len(rows),'snapshots;',sum(v[-1] for v in rows),'requests;',len(cooldown),'cooldown cases;',len(lifetimes),'lifetime cases')
if __name__=='__main__':main()

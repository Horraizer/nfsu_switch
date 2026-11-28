"""Check original PC driving-style producers and base-grip source.

This executes bounded431020 initialization, whole456BA0 AI propagation,
command494B90 updates, and bounded477775 consumption without semantic hooks. The port
normal-context caller is covered by runtime_base_grip in CTest.
"""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

from launch_traction_boundary import authenticated,PC_HASH
from physics256_native import ACTOR,PHYSICS,STACK,WORLD,STATUS,Machine
from world_body_trace import Image


def style_case(image,styles,reverse=False):
    m=Machine(image)
    m.allowed=[(0x431020,0x431036),(0x456BA0,0x456C8E),(0x477775,0x4777BF),(0x494B90,0x494BE1)]
    players=len(styles)
    total=players+4
    cars=[0x1120000+0x1000*i for i in range(total)]
    participants=[0x1130000+0x1000*i for i in range(total)]
    positions=[1,4,2,3,5,6] if players==2 else [1,2,3,4,5]
    if reverse:positions[0],positions[1]=positions[1],positions[0]
    before=[]
    for i,(car,participant) in enumerate(zip(cars,participants)):
        m.put(participant+0x22,styles[i] if i<players else 0,'b')
        m.put(car+0x24,2) # Skip unrelated human-only initialization.
        m.put(car+0x38,i%3) # Collision states deliberately differ from styles.
        m.run(0x431020,(participant,),end=0x431036,eax=car)
        before.append(m.get(car+0x400))
        info=0x1140000+0x1000*i
        m.put(car+0x1C,info)
        m.put(info+0xA,positions[i],'b')
        m.put(WORLD+0x28+4*i,car)
    m.put(0x7361F8,WORLD)
    m.put(0x73619C,STATUS)
    m.put(0x78A320,players)
    m.put(0x78A324,4)
    m.put(WORLD+0x1C,players)
    m.put(WORLD+0x20,total)
    for i in range(players):m.put(0x7361B4+4*i,participants[i])
    m.put(0x7361BC,participants[0])
    m.run(0x456BA0)
    after=[m.get(car+0x400) for car in cars]
    def expected_styles(current):
        if players==1:return [current[0]]*total
        first=0 if positions[0]<positions[1] else 1
        last=1-first
        return list(current)+[current[first] if rank<=3 else current[last] for rank in positions[2:]]
    if after!=expected_styles(styles) or m.events:
        raise AssertionError('original driving-style producer changed')
    grips=[]
    for car in cars:
        m.put(PHYSICS+0x58,car+0x40)
        m.put(PHYSICS+0x78,1.0,'f')
        m.run(0x477775,end=0x4777BF,ecx=PHYSICS,ebx=0)
        grips.append(m.get(STACK+0x20,'f'))
        expected_bits=0x3F59999A if m.get(car+0x400)==0 else 0x3F800000
        if m.get(STACK+0x20)!=expected_bits:
            raise AssertionError('original collision-independent base grip changed')
    updates=[]
    current=list(styles)
    for player in range(players):
        current[player]=0 if current[player] else 1
        command=0x1150000
        for at,value in ((0,1),(8,player),(12,current[player])):m.put(command+at,value)
        m.run(0x494B90,eax=command)
        actual=[m.get(car+0x400) for car in cars]
        if actual!=expected_styles(current) or m.get(participants[player]+0x22,'b')!=current[player] or m.events:
            raise AssertionError('original dynamic driving-style update changed')
        updates.append(dict(player=player,requested=current[player],propagated=actual))
    return dict(local_players=players,styles=list(styles),positions=positions,
                collision_states=[m.get(car+0x38) for car in cars],
                initialized=before,propagated=after,base_grip=grips,
                command_updates=updates,hooks=m.events)


def archived_styles(path):
    blob=path.read_bytes()
    length=int.from_bytes(blob[:4],'little')
    meta=json.loads(blob[4:4+length])
    raw=zlib.decompress(blob[4+length:])
    if hashlib.sha256(raw).hexdigest()!=meta['sha256']:
        raise ValueError('cached start checksum')
    offset=raw.find(b'GNDE69\0\0')
    if offset<0:raise ValueError('original MEM1 missing')
    def data(address,length):
        if not 0x80000000<=address<=address+length<=0x81800000:
            raise ValueError('invalid MEM1 address')
        at=offset+address-0x80000000
        return raw[at:at+length]
    def word(address):return int.from_bytes(data(address,4),'big')
    if word(0x80000020)!=0x0D15EA5E or word(0x80000028)!=0x01800000:
        raise ValueError('original MEM1 header')
    # GNDE69 ELF symbols: pCurrentWorld and Player.pPlayersByNumber/NumPlayers.
    world=word(0x803C0B70)
    count=word(world+0x20)
    if count>64:raise ValueError('invalid original car count')
    actors=[]
    for i in range(count):
        car=word(world+0x28+4*i)
        actors.append(dict(index=i,id=int.from_bytes(data(car+0xA,2),'big'),
            collision_state=word(car+0x38),driving_style=word(car+0x3FC),
            neighbor_float=struct.unpack('>f',data(car+0x400,4))[0]))
    players=[]
    count=word(0x803C0C14)
    if count>2:raise ValueError('unsupported original local player count')
    for i in range(count):
        player=word(0x803C0C04+4*i)
        players.append(dict(index=i,driving_style=int.from_bytes(data(player+0x22,1),'big',signed=True)))
    return dict(case=meta['case'],state_sha256=meta['sha256'],mem1_offset=offset,
                actors=actors,players=players)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--gc-state',type=Path,action='append',default=[],help='read an existing GNDE69 grid-cache .zstate')
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1]
    output=args.out.resolve()
    try:
        relative=output.relative_to(repo)
    except ValueError:
        parser.error('output must stay inside this checkout')
    retail=args.retail.resolve()
    if not relative.parts or not all(x.isascii() for x in relative.parts) or output==retail or retail in output.parents:
        parser.error('fresh ASCII output outside retail required')
    if output.exists():
        parser.error('fresh output directory required')
    image=Image(authenticated(retail/'speed.exe',PC_HASH))
    rows=[]
    for mode in (0,1,2):
        machine=Machine(image)
        machine.allowed=[(0x477775,0x4777BF)]
        machine.put(PHYSICS+0x58,ACTOR+0x40)
        machine.put(PHYSICS+0x78,1.0,'f')
        machine.put(ACTOR+0x40+0x3C0,mode)
        machine.run(0x477775,end=0x4777BF,ecx=PHYSICS,ebx=0)
        result=machine.get(STACK+0x20)
        expected=0x3F59999A if mode==0 else 0x3F800000
        if result!=expected or machine.events:
            raise AssertionError('original base-grip state producer changed')
        rows.append(dict(owner_mode=mode,base_grip=machine.get(STACK+0x20,'f'),bits=result,hooks=[]))
    producers=[style_case(image,(style,)) for style in (0,1,2)]
    producers.extend(style_case(image,styles,reverse) for styles in ((0,1),(1,0),(0,0)) for reverse in (False,True))
    gc=[archived_styles(path) for path in args.gc_state]
    if hashlib.sha256((retail/'speed.exe').read_bytes()).hexdigest()!=PC_HASH:
        raise AssertionError('retail input changed')
    output.mkdir(parents=True)
    (output/'report.json').write_bytes((json.dumps(dict(pc_sha256=PC_HASH,tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),rows=rows,producers=producers,gc=gc),indent=2)+'\n').encode('ascii'))
    print('PASS: original431020/456BA0/494B90/477775, 9 driving-style cases, no semantic hooks; GC states',len(gc))


if __name__=='__main__':
    main()

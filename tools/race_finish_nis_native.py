"""Execute original finish NIS selection and two-human actor-slot assignment.

Authenticated PC1.4 x86. LoadNis and the RNG-index supplier are explicit seams;
the index selects from the authenticated original playlist. No live game,
retail writes or claim of identical RNG history. Slot probes supply the
original manager's finishing-order pointer array and the loaded scene kind.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from world_body_trace import Image
from physics256_native import Machine,ACTOR,DRIVER,BODY,CONFIG,RIDE,WORLD,STATUS

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--write-fixture',action='store_true')
args=parser.parse_args()
args.out.mkdir(parents=True,exist_ok=False)
work=Path(__file__).resolve().parents[1]
raw=(args.root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw)
bun=(args.root/'Global/INGAMEB.BUN').read_bytes()
assert hashlib.sha256(bun).hexdigest()=='044724ab4c167831587e80f29961a1e7d8404c9dd5266533f3563c555a997299'

def chunks(data,begin=0,end=None):
    end=len(data) if end is None else end
    while begin+8<=end:
        kind,size=struct.unpack_from('<II',data,begin)
        after=begin+8+size
        assert after<=end
        yield kind,begin+8,after
        if kind&0x80000000:yield from chunks(data,begin+8,after)
        begin=after

playlists=[]
for kind,begin,end in chunks(bun):
    if kind!=0x34036:continue
    data=bun[begin:end];offset=96
    for n in range(12):
        count=struct.unpack_from('<I',data,4*n)[0]
        playlists.append(list(struct.unpack_from('<'+'I'*count,data,offset)))
        offset+=4*count
assert len(playlists)==12

def machine():
    m=Machine(image)
    m.allowed=[(0x47BED0,0x47BFC0),(0x47C160,0x47C310),(0x68A7A0,0x68A7FB)]
    for address in range(0x68A7A0,0x68A800,0x10):m.run(address)
    m.put(0x7361b4,ACTOR);m.put(ACTOR+4,DRIVER);m.put(DRIVER+0x1C,CONFIG)
    m.put(0x73619C,BODY);m.put(0x7361F8,WORLD)
    return m

rows=[]
for humans in (1,2):
    for racers in range(humans,5):
        for place in ((0,1,2) if racers==1 else range(1,racers+1)):
            for track in (1003,1104,1301):
                for variant in range(3):
                    m=machine();m.put(0x7361C4,humans);m.put(BODY+0x24,racers)
                    m.put(CONFIG+0xC,place,'B');m.put(0x777CD0,place)
                    m.put(0x735F64,RIDE)
                    for n,entries in enumerate(playlists):
                        pointer=RIDE+0x100+n*0x100
                        m.put(RIDE+4*n,len(entries));m.put(RIDE+0x30+4*n,pointer)
                        for j,value in enumerate(entries):m.put(pointer+4*j,value)
                    calls=[]
                    def load(x):
                        calls.append(dict(kind='load_nis',scene=x.get(x.reg('esp')+4)))
                        x.ret(4)
                    def random(x):
                        count=x.get(x.reg('esp')+4)
                        calls.append(dict(kind='supplied_rng_index',count=count,index=variant%count))
                        x.ret(4,variant%count)
                    m.hook(0x47BA30,'external LoadNis',callback=load)
                    m.hook(0x4216B0,'explicit supplied RNG index',callback=random)
                    m.run(0x47BED0,args=(track,),eax=STATUS)
                    selected=[c['scene'] for c in calls if c['kind']=='load_nis']
                    assert len(selected)==1
                    rows.append(dict(humans=humans,racers=racers,place=place,track=track,variant=variant,
                                     scene=selected[0],calls=calls))

slots=[]
for humans in (1,2):
    for order in ((0,1),(1,0)):
        m=machine();m.put(0x7361C4,humans);m.put(BODY+0x24,2)
        for actor in range(2):
            handle=DRIVER+actor*0x100;result=CONFIG+actor*0x100
            m.put(WORLD+0x28+4*actor,handle);m.put(handle+0x1C,result)
        for place,actor in enumerate(order):m.put(BODY+0x520+place*4,CONFIG+actor*0x100)
        m.put(ACTOR+4,DRIVER);m.put(STATUS+0x10,STATUS+0x100);m.put(STATUS+0x160,1)
        m.run(0x47C160,args=(STATUS,))
        mapping=[m.get(0x779904+slot*0x18,'i') for slot in range(4)]
        slots.append(dict(humans=humans,finish_order=order,slot_actors=mapping,hooks=m.events))
        assert mapping[:2]==(list(order) if humans==2 else [0,1])

fixture='// Authenticated original47BED0,68A7A0 initializers and47C160 slot mapping.\n'
fixture+='struct NativeFinishNis {unsigned humans,racers,place,track,variant,scene;};\n'
fixture+='static constexpr NativeFinishNis kNativeFinishNis[]={\n'
fixture+=''.join('    {%d,%d,%d,%d,%d,%d},\n'%tuple(row[k] for k in ('humans','racers','place','track','variant','scene')) for row in rows)
fixture+='};\nstruct NativeFinishSlots {unsigned humans,first;int actors[4];};\nstatic constexpr NativeFinishSlots kNativeFinishSlots[]={\n'
fixture+=''.join('    {%d,%d,{%s}},\n'%(row['humans'],row['finish_order'][0],','.join(str(v) for v in row['slot_actors'])) for row in slots)
fixture+='};\n'
target=work/'tests/race_finish_nis_native.inc'
if args.write_fixture:target.write_bytes(fixture.encode('ascii'))
else:assert target.read_text(encoding='ascii')==fixture,'finish NIS fixture stale'
(args.out/'oracle.json').write_bytes((json.dumps(dict(rows=rows,slots=slots,playlists=playlists,boundary=__doc__),indent=2,ensure_ascii=True)+'\n').encode('ascii'))
print('PASS original NIS selection%d rows; actor-slot mappings%d'%(len(rows),len(slots)))

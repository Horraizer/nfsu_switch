"""Original FEng per-frame integer ticks, restart and timed Race Over geometry.

Executes4F5CF0..4F5D02 in24-bit precision (binary32 product to integer budget), the actual
4F6B97 restart branch and4FFEE0..4FFF05 elapsed addition before completion.
Original4FC100/FEng consumers sample the resulting authored terminal pose.
These are bounded component witnesses, not a live game-loop ordering trace.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from unicorn.x86_const import UC_X86_REG_FPCW

from world_body_trace import Image
from physics256_native import Machine,ACTOR,DRIVER,BODY,RIDE,STACK
from hud253_trace import jdlz
from hud254_trace import chunks
from race_finish_modes_native import Renderer,resources,feng_objects,tracks,sample

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--write-fixture',action='store_true')
parser.add_argument('--gc-audit',type=Path)
args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
work=Path(__file__).resolve().parents[1]
raw=(args.root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=Image(raw)
bun=(args.root/'Global/INGAMEB.BUN').read_bytes()
assert hashlib.sha256(bun).hexdigest()=='044724ab4c167831587e80f29961a1e7d8404c9dd5266533f3563c555a997299'
language=(args.root/'Languages/English.bin').read_bytes()
assert hashlib.sha256(language).hexdigest()=='6cbab64235523149b95966b1844cdca9fb858744126f8d9ad10e72c921ab8790'
package=jdlz(bun[0x16F40C:])
requests=next(c['records'] for c in resources(package)[0]['chunks'] if c['tag']==b'RsRq'.hex())
metadata={}
for obj in feng_objects(package,('raceover',)):
    start=obj['offset'];end=start+8+struct.unpack_from('<I',package,start+4)[0]
    for kind,begin,finish in chunks(package[start+8:end]):
        if kind!=int.from_bytes(b'Scrp','little'):continue
        data=package[start+8+begin:start+8+finish];at=0;fields=[]
        while at<len(data):
            tag=data[at:at+2];length=struct.unpack_from('<H',data,at+2)[0]
            fields.append((tag,data[at+4:at+4+length]));at+=4+length
        if not any(tag==b'Sn' and value.split(b'\0')[0].lower()==b'animate' for tag,value in fields):continue
        assert all(tag in (b'Sn',b'Sh',b'FI',b'To',b'Kd') for tag,value in fields),'unexpected terminal script transition/event field'
        header=next(value for tag,value in fields if tag==b'Sh')
        words=struct.unpack('<IIII',header);assert words[2]==0
        metadata[obj['name']]=dict(duration=words[1],loop=words[2],header_hex=header.hex())
assert metadata['RaceOverMessage']['duration']==700 and metadata['RaceOverBacking']['duration']==300
gc=None
if args.gc_audit:
    source=args.gc_audit.read_bytes();audit=json.loads(source)
    packets=audit['validated_clock_packets']
    f32=lambda value:struct.unpack('<f',struct.pack('<f',value))[0]
    for packet in packets:
        assert int(f32(f32(packet['dt'])*1200))==packet['expected']==sum(packet['chunks'])
        assert all(0<chunk<=20 for chunk in packet['chunks'])
    gc=dict(sha256=hashlib.sha256(source).hexdigest(),source_state_sha256=audit['source_state_sha256'],
            source_duration_title=audit['source_duration_title'],source_duration_backing=audit['source_duration_backing'],
            first_enable_limit=audit['first_enable_limit'],validated_packets=len(packets))

def clock(first,second,frames,fpcw):
    m=Machine(image);m.uc.reg_write(UC_X86_REG_FPCW,fpcw)
    m.allowed=[(0x4F5CF0,0x4F5D02),(0x674898,0x67490D),
               (0x4F6B97,0x4F6BC2),(0x4FFEE0,0x4FFF05)]
    #4A7D60 requests Animate with restart=1. The selected script owns+10.
    m.put(ACTOR+0x54,DRIVER);m.put(DRIVER+0x10,123)
    m.run(0x4F6B97,args=(0,0,0,1),end=0x4F6BC2,edi=ACTOR,esi=DRIVER)
    assert m.get(DRIVER+0x10)==0
    steps=[]
    for n in range(frames):
        seconds=second if n&1 else first
        m.put(0x73AD38,seconds,'f')
        m.run(0x4F5CF0,end=0x4F5D02)
        ticks=m.reg('eax');m.put(BODY+0x28,ticks)
        m.run(0x4FFEE0,end=0x4FFF05,ebp=DRIVER,ebx=BODY)
        steps.append(dict(seconds=m.get(0x73AD38,'f'),ticks=ticks,total=m.get(DRIVER+0x10)))
    assert not m.events
    return dict(first=first,second=second,frames=frames,ticks=m.get(DRIVER+0x10),steps=steps,fpcw=fpcw)

def geometry(tick):
    result=[]
    for obj in feng_objects(package,('raceover',)):
        if obj['name'] not in ('RaceOverMessage','RaceOverBacking'):continue
        fields={f['tag']:bytes.fromhex(f['raw']) for f in obj['fields']}
        state=bytearray(fields['SA'])
        for track in tracks(package,obj):
            value=sample(image,track,tick);at=track['target']*4;state[at:at+len(value)]=value
        index=struct.unpack_from('<H',fields['OP'],12)[0]
        native=dict(obj,default_resource=requests[index],fields=[dict(tag=k,raw=(bytes(state) if k=='SA' else v).hex()) for k,v in fields.items()])
        renderer=Renderer(image);text=obj['name']=='RaceOverMessage'
        if text:renderer.load_font(language,'font_conduitmditctt38bi',(512,256))
        renderer.object_fields(native,'Race Over' if text else '')
        rendered=renderer.render_text() if text else renderer.render_image((16,16))
        vertices=[v for q in rendered['local']['quads'] for v in q['positions']]
        bounds=[min(v[0] for v in vertices),max(v[0] for v in vertices),min(v[1] for v in vertices),max(v[1] for v in vertices)]
        result.append(dict(text=text,bounds=bounds,alpha=int(rendered['local']['quads'][0]['colors'][0],16)>>24))
    return result

rows=[]
schedules=[(a/1000000,b/1000000) for a,b in [(16666,16666),(16667,16667),(16666,16667),(15625,15625),(33333,33333),(1000,1000),(2500,2500)]]
schedules += [(0.021666666492819786,0.021666666492819786)]
for first,second in schedules:
    for frames in (1,9,12,16,18,20,25):
        row=clock(first,second,frames,0x007F)
        row['extended_fpcw_ticks']=clock(first,second,frames,0x037F)['ticks']
        row['geometry']=geometry(row['ticks']);rows.append(row)

transitions=[]
# The authenticated Animate headers are nonlooping with no next-state tag.
# Execute the whole numeric object updater with empty channel/notification
# lists, then sample actual channels separately above. Test both durations.
for duration in sorted({entry['duration'] for entry in metadata.values()}):
    for initial in (duration-30,duration-20,duration-10,duration-1,duration,duration+1):
        for budget in (26,41,70):
            ages=[]
            for split in (False,True):
                m=Machine(image);m.allowed=[(0x4FFEB0,0x5001A0),(0x5001B0,0x500380)]
                m.put(ACTOR+0x54,DRIVER);m.put(ACTOR+0x2C,RIDE)
                m.put(DRIVER+0xC,duration);m.put(DRIVER+0x10,initial);m.put(BODY+0x14,1,'B')
                chunks=[budget] if not split else [20]*(budget//20)+([budget%20] if budget%20 else [])
                for chunk in chunks:
                    m.put(BODY+0x28,chunk);m.run(0x4FFEB0,args=(ACTOR,chunk),ecx=BODY)
                assert not m.events
                ages.append(m.get(DRIVER+0x10))
            assert ages[0]==ages[1]
            transitions.append(dict(duration=duration,initial=initial,budget=budget,direct_age=ages[0],chunked_age=ages[1]))

def number(value):
    text=format(value,'.9g');return text+('' if '.' in text or 'e' in text else '.')+'F'
fixture='// Authenticated original FEng per-frame clock, restart and terminal geometry.\n'
fixture+='struct NativeRaceOverClock {float first_seconds,second_seconds;unsigned frames,ticks,text,alpha;float left,right,top,bottom;};\n'
fixture+='static constexpr NativeRaceOverClock kNativeRaceOverClock[]={\n'
for row in rows:
    for geom in row['geometry']:
        fixture+='    {%s,%s,%d,%d,%d,%d,%s},\n'%(number(row['first']),number(row['second']),row['frames'],row['ticks'],geom['text'],geom['alpha'],','.join(number(v) for v in geom['bounds']))
fixture+='};\n'
target=work/'tests/race_over_clock_native.inc'
if args.write_fixture:target.write_bytes(fixture.encode('ascii'))
else:assert target.read_text(encoding='ascii')==fixture,'Race Over clock fixture stale'
(args.out/'oracle.json').write_bytes((json.dumps(dict(rows=rows,transitions=transitions,metadata=metadata,gc_audit=gc,boundary=__doc__,precision='007F selected;037F recorded separately. GC measured binary32 product; PC24-bit context inferred from unpreserved Direct3D device flags. No local fldcw in the inspected FEng update blocks.'),indent=2,ensure_ascii=True)+'\n').encode('ascii'))
print('PASS original per-frame UI clock, script restart and geometry:%d schedules'%len(rows))

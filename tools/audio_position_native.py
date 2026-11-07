"""Original fresh-profile positional gain and final human/music consumers.

Retail stays readonly. Single listener/car/view registration is supplied.
The common positional interface executes with original actor contexts; child
sound refresh, renderer, device IO and custom user volume settings are excluded.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.dont_write_bytecode=True
from world_body_trace import Image
from physics256_native import Machine


def prepared(image,context):
    m=Machine(image)
    m.allowed=[(0x400000,0x6a0000)]
    m.uc.mem_map(0,0x1000)
    prefs,parent,obj,listener,view,human,status,holder,pos,axis=(
        0x1100000,0x1101000,0x1102000,0x1103000,0x1104000,
        0x1105000,0x1106000,0x1107000,0x1108000,0x1108100)
    m.run(0x4aa21e,end=0x4aa23e,esi=prefs)
    m.put(parent+8,prefs+0x38)
    m.put(0x7356a8,parent)
    m.put(0x73619c,status)
    m.put(status,4)
    m.run(0x52e3f0,eax=obj)
    m.put(obj,0x6bdd08)  # Common positional virtuals, no child sound objects.
    m.put(obj+0xbc,context)
    m.put(obj+0x204,listener)
    m.put(parent+0xa0,holder)
    m.put(holder+4,listener)
    m.put(holder+0x10,1)  # Real update publication flag, not listener state+10.
    m.put(listener+0x60,view)
    m.put(listener+0x168,human)
    m.put(listener+0x178,1/60,'f')
    m.put(view+0x40,6.,'f')
    m.put(view+0x50,1.,'f')
    m.put(human+0x90,1.,'f')
    m.put(axis,1.,'f')
    m.run(0x5225c0,ecx=obj)
    m.run(0x52ea00,(pos,axis),ecx=obj)
    assert not m.events
    return m,obj


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('retail_root',type=Path)
    ap.add_argument('--write-fixture',action='store_true')
    ap.add_argument('--out',type=Path)
    args=ap.parse_args()
    raw=(args.retail_root/'speed.exe').read_bytes()
    assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    image=Image(raw)
    levels=[]
    for context in (0x6f41f0,0x6f4260,0x6f4340,0x6f49d0):
        m,obj=prepared(image,context)
        levels.append([context,m.get(obj+0xa8),m.get(obj+0x50)])
    engine=[]
    default_impact=None
    m,obj=prepared(image,0x6f41f0)
    settings,gain=0x1110000,0x1115000
    m.run(0x5364e0,(settings,))
    m.run(0x5365e0,edi=settings);default_impact=[[m.get(settings+0x3e1c)]]
    m.put(0x7356ac,settings)
    owner=obj-0x20
    m.put(owner+0x264,0)
    for category in (0,1000,16358,32767):
        m.put(settings+10*0x130+8,category)
        for value in (0,1,1234,10000,25000,32767,70000,150000,0xffffffff,0xffff0000):
            m.put(gain,value)
            m.run(0x526aa0,(gain,10),ecx=owner)
            engine.append([value,category,m.get(gain)])
    m,obj=prepared(image,0x6f49d0)
    m.put(obj+0x220,4)
    m.put(obj+0x240,32767)  # Explicit unity selected-song gain.
    m.run(0x539ac0,esi=obj)
    descriptor=m.get(obj+0x244)
    outer,voice,settings=0x1110000,0x1111000,0x1112000
    m.put(outer+0x50,voice)
    m.put(outer+0x50+0x2d0,obj+0x218)
    m.put(0x7356ac,settings)
    m.put(voice,0)  # Consumer returns before device IO.
    music=[]
    for goal in (0,6300,16358,16400,26499,26500,32767):
        m.put(settings+0x35cc,goal)
        m.run(0x53ca50,ecx=outer)
        music.append([goal,m.get(voice+0xc)])
    assert not m.events
    fixture='// Fresh original preferences and positional/category/music consumers.\n'
    fixture+='// Common positional interface; supplied listener, unity song, no device.\n'
    for name,rows,width in [('kNativeDefaultPositionGain',levels,3),('kNativeEngineDeviceGain',engine,3),('kNativeMusicDeviceGain',music,2),('kNativeDefaultImpactGlobalGain',default_impact,1)]:
        fixture+='static constexpr unsigned '+name+'[]['+str(width)+']={\n'
        fixture+=''.join('    {'+','.join(map(str,row))+'},\n' for row in rows)+'};\n'
    path=Path(__file__).resolve().parents[1]/'tests/audio_position_native.inc'
    if args.write_fixture:path.write_bytes(fixture.encode('ascii'))
    else:assert path.read_text(encoding='ascii')==fixture,'native audio position fixture differs'
    if args.out:
        args.out.mkdir(exist_ok=False)
        (args.out/'oracle.json').write_text(json.dumps(dict(levels=levels,engine=engine,music=music,
            music_descriptor=descriptor,hooks=[],scope=__doc__),indent=2)+'\n',encoding='ascii')
    print('PASS actual fresh preferences, common position gains and final human/music consumers; no IO or external hooks')


if __name__=='__main__':main()

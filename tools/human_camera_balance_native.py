"""Original human positional angle and52C3A0/52C140 sound balance.

Supplied single camera owner, mover id, POV, source axes and sound record.
Executes the actual positional interface and human balance producer, no hooks.
"""
import argparse,hashlib,json,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image
from audio_position_native import prepared
from physics256_native import bits


def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path)
 args=ap.parse_args();raw=(args.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
 image=Image(raw);rows=[]
 m,obj=prepared(image,0x6f41f0);env=obj-0x20;pos,axis,view,human=0x1108000,0x1108100,0x1104000,0x1105000
 owner,control,tuning=0x1110000,0x1111000,0x1112000
 m.put(env+0x234,human);m.put(human+0x18,owner);m.put(owner+0x27c,control);m.put(env+0x1f0c,tuning)
 for case in range(3):
  for tick in range(240):
   xyz=[float(tick-120)*.3,float(tick%17)-8.,case*1.5]
   forward=[float((tick*7)%19)-9.,float((tick*13)%17)-8.,2.]
   for i,x in enumerate(xyz):m.put(pos+4*i,x,'f')
   for i,x in enumerate(forward):m.put(axis+4*i,x,'f')
   pov=2 if case==1 else 0;mover=6 if case==2 else 1
   m.put(control+0xc,mover);m.put(owner+0x24,pov,'B');m.put(tuning+0x3c,-50,'h');m.put(tuning+0x3e,765,'h')
   m.run(0x52c3a0,ecx=env);m.run(0x52ea00,(pos,axis),ecx=obj);m.run(0x52c140,ecx=env)
   rows.append([case,tick]+[bits(x) for x in xyz+forward]+[mover,pov,m.get(obj+0x200,'H'),m.get(env+0x2ac)])
 assert not m.events
 fixture='// Actual human positional angle, camera routing and engine/exhaust balance.\nstatic constexpr unsigned kNativeHumanCameraBalance[][12]={\n'
 fixture+=''.join('    {'+','.join(map(str,r))+'},\n' for r in rows)+'};\n'
 path=Path(__file__).resolve().parents[1]/'tests/human_camera_balance_native.inc'
 if args.write_fixture:path.write_bytes(fixture.encode('ascii'))
 else:assert path.read_text(encoding='ascii')==fixture,'native human camera balance differs'
 if args.out:
  args.out.mkdir(exist_ok=False);(args.out/'oracle.json').write_text(json.dumps(dict(rows=rows,hooks=[],scope=__doc__),indent=2)+'\n',encoding='ascii')
 print('PASS actual human positional angle, camera owner routing and balance; no hooks')


if __name__=='__main__':main()

"""Original single-listener remote positional publication, no backend hooks.

Real context6F4260, fresh preferences, supplied car/camera/listener registry.
The listener wall interval is supplied in milliseconds; routing/multiplayer
and actual hardware listener scheduling are outside this fixture.
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
 for case in range(8):
  m,obj=prepared(image,0x6f4260);pos,axis,listener,view,human=0x1108000,0x1108100,0x1103000,0x1104000,0x1105000
  m.put(listener+0x178,1000/60,'f')
  for tick in range(150):
   xyz=[(tick-75)*2.9,(case-4)*20.,case*1.5]
   if case==2:xyz[0]=10 if tick<50 else 50
   if case==3:xyz=[0.,0.,0.]
   for i,x in enumerate(xyz):m.put(pos+i*4,x,'f')
   m.run(0x52ea00,(pos,axis),ecx=obj)
   rows.append([case,tick]+[bits(x) for x in xyz]+[m.get(obj+0x50),m.get(obj+0x4c),m.get(obj+0xb8)])
  assert not m.events
 fixture='// Actual52EA00 remote context; supplied single listener/car/camera.\nstatic constexpr unsigned kNativeRemotePosition[][8]={\n'
 fixture+=''.join('    {'+','.join(map(str,r))+'},\n' for r in rows)+'};\n'
 path=Path(__file__).resolve().parents[1]/'tests/remote_position_native.inc'
 if args.write_fixture:path.write_bytes(fixture.encode('ascii'))
 else:assert path.read_text(encoding='ascii')==fixture,'native remote position fixture differs'
 if args.out:
  args.out.mkdir(exist_ok=False);(args.out/'oracle.json').write_text(json.dumps(dict(rows=rows,hooks=[],scope=__doc__),indent=2)+'\n',encoding='ascii')
 print('PASS original remote position, distance gain, camera pan and Doppler; no backend hooks')


if __name__=='__main__':main()

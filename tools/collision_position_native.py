"""Actual531350 spatial/profile output on supplied finite contact/listener states.

Original6F47A0 and common position/category producers execute. Only final
impact dispatch is observed. No PCM, natural record/lifecycle or whole-race
admission claim. Inputs and executable remain read-only.
"""
from pathlib import Path
import argparse,hashlib,itertools,sys,json,struct
sys.dont_write_bytecode=True
from hud254_trace import PE
from audio312_contacts import spatial_witness
from physics256_native import Machine
from world_body_trace import Image
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail',type=Path);ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path);a=ap.parse_args()
raw=(a.retail/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image=PE(raw);rows=[]
bank=Machine(Image(raw));bank.allowed=[(0x64d0b0,0x64d0cf)];node=0x1100000
bank.put(node,1./32768,'f');bank.put(node+8,16384)
def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
points=((0.,0.,0.),(0.,1.,0.),(1.,0.,0.),(0.,-20.,0.),(20.,0.,0.),(0.,20.,0.),(0.,60.,0.),(0.,80.,0.),(0.,90.,0.),(.1,29.99,4.),(12.,-13.,3.),(0.,10.0001,0.),(0.,9.9999,0.),(0.,29.9999,0.),(0.,30.0001,0.),(0.,79.9999,0.))
for point,actor,eye,forward,global_gain in itertools.product(points,((0.,0.,0.),(0.,19.,0.)),((0.,0.,0.),(5.,2.,3.)),((0.,1.,0.),(1.,0.,0.),(.6,.8,0.)),(32768,31689)):
 result=spatial_witness(image,point,listener=eye,listener_forward=forward,actor_position=actor,category=32768,global_gain=global_gain)
 configured=result['event']['gain_q15'];bank.put(node+4,configured);bank.run(0x64d0b0,(node,));assert not bank.events
 rows.append([[bits(v)for v in point],[bits(v)for v in actor],[bits(v)for v in eye],[bits(v)for v in forward],global_gain,result['spatial_attenuation'],result['event']['pan'],configured,bank.reg('eax')])
def lit(v):return '{'+','.join(map(lit,v))+'}'if isinstance(v,list)else str(v)
text='// Actual531350 position output with supplied record/listener and observed dispatch.\n'
text+='struct NativeCollisionPosition {unsigned point[3],actor[3],eye[3],forward[3],global_gain,gain,pan,configured,bank_gain;};\n'
text+='static constexpr NativeCollisionPosition kNativeCollisionPosition[]={\n'+''.join(lit(row)+',\n'for row in rows)+'};\n'
p=Path(__file__).resolve().parents[1]/'tests/collision_position_native.inc';assert text.isascii()
if a.write_fixture:p.write_bytes(text.encode('ascii'))
else:assert p.read_text('ascii')==text,'native collision spatial fixture differs'
if a.out:
 relative=a.out.resolve().relative_to(Path.cwd().resolve());assert relative.parts[0].startswith('build')and relative.as_posix().isascii();assert not a.out.exists()
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(dict(scope=__doc__,rows=rows),indent=2)+'\n',encoding='ascii')
print('PASS actual collision spatial/profile output',len(rows))

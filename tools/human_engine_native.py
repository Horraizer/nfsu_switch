"""Actual human load/RPM/shift/gain control slices, not remote524620.

Original52C620 dispatch order. Supplied physical publications, gear-change
notification and camera-balance input. Shift FX, adaptive gain actors and
backend IO are excluded; no physical acceleration equivalence is claimed.
"""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image,chunks
from physics256_native import Machine,bits,ACTOR,DRIVER,STATUS,CONFIG


def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail_root',type=Path);ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path)
 args=ap.parse_args();raw=(args.retail_root/'speed.exe').read_bytes();assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
 im=Image(raw);assert int.from_bytes(im.read(0x6beeb0+0x2c,4),'little')==0x526420
 assert int.from_bytes(im.read(0x6bf2c0+0x2c,4),'little')==0x5234c0
 bun=(args.retail_root/'Global/GLOBALB.BUN').read_bytes();records={}
 for tag,first,last in chunks(bun):
  if tag==0x34600:
   for at in range((first+8+15)&~15,last,0xc90):
    record=bun[at:at+0xc90]
    if len(record)==0xc90:records[record[:16].split(b'\0')[0].decode('ascii')]=record
 env,tuning,ring,ring2,event,settings,parent=0x1120000,0x1110000,0x1130000,0x1130100,0x1140000,0x1141000,0x1146000
 configs=[];rows=[];states=set()
 for car in ['CIVIC','RX7','SUPRA','INTEGRA']:
  for upgrade in [0,3]:
   data=records[car][0x9c0+upgrade*100:0xa24+upgrade*100];maximum=struct.unpack_from('<f',records[car],0x308)[0]
   ci=len(configs);configs.append([*struct.unpack_from('<3h',data,0x18),bits(struct.unpack_from('<f',data,0x20)[0]),*struct.unpack_from('<4h',data,0x2c),bits(struct.unpack_from('<f',data,0x34)[0]),*struct.unpack_from('<4h',data,0x14),*struct.unpack_from('<2h',data,0x28),*struct.unpack_from('<2h',data,0xa)])
   m=Machine(im);m.allowed=[(0x400000,0x6a0000)];m.uc.mem_map(0,0x1000)
   m.run(0x5364e0,(settings,));m.put(0x7356ac,settings);m.put(0x7356a8,parent)
   m.put(env,0x6beeb0);m.put(env+0x234,ACTOR);m.put(ACTOR+0x30,DRIVER);m.put(ACTOR+0x14,CONFIG);m.put(CONFIG+4,1);m.put(0x73619c,STATUS)
   m.put(env+0x1f0c,tuning);m.uc.mem_write(tuning,data);
   # Verified human setup526624 calls4647C0 on the selected RideInfo+1F0.
   ride=0x1150000;m.put(ACTOR+0x40,ride)
   m.uc.mem_write(ride+0x200,records[car][0x310:0x334])
   m.run(0x526624,end=0x526638,ebx=ACTOR,ebp=env)
   configs[-1].append(m.get(env+0x1f80))
   m.put(env+0x1f24,maximum,'f');m.put(0x7000e4,100.,'f')
   m.put(env+0x11c8,1.,'f');m.put(env+0x70,19437);m.put(env+0x1d04,1,'B')
   for at,size,storage in [(env+0x1f28,10,ring),(env+0x1f50,5,ring2)]:
    m.put(at,0x6b9694);m.put(at+5,size,'B');m.put(at+0x10,storage)
   prior=2
   for tick in range(480):
    gear=2 if tick<100 else 3 if tick<155 else 4 if tick<205 else 3 if tick<340 else 4 if tick<344 else 2
    rpm=1000.+float((tick*47)%5900);torque=(float((tick*13)%220)-20.)*(.001 if ci%2 else 1.)
    throttle=.2 if 62<=tick<90 else 1.;brake=1. if 260<=tick<290 else 0.;dt=[1/60,1/30,.01,.025][ci%4]
    if tick==0:m.put(env+0x1f1c,rpm,'f')
    m.put(env+0x1d18,gear)
    if gear!=prior:
     if gear>prior:
      end=0x5293c7 if m.get(env+0x1f1c,'f')>=3000 else 0x5293d2
      m.run(0x5292b0,end=end,ecx=env)
     else:m.run(0x5293e0,ecx=env)
    prior=gear;m.put(env+0x244,dt,'f');m.put(env+0x240,m.get(env+0x240,'f')+m.get(env+0x244,'f'),'f')
    m.put(ACTOR+0x3ec,torque,'f');m.put(ACTOR+0x3e0,rpm,'f');m.put(DRIVER+0x1dc,throttle,'f');m.put(DRIVER+0x1e0,brake,'f');m.put(STATUS,4)
    m.run(0x52c3f0,esi=env)
    m.put(env+0x1d48,0,'B') # Deliberately excluded independent shift-FX dispatch.
    state=m.get(env+0x1d14)
    ends={1:0x52961f,2:0x5296c3,3:0x529767,4:0x529810,5:0x529897,6:0x5298b1}
    # Stops after every RPM/load producer, before independent FX; state0 hits return.
    end=ends[state] if state and m.get(env+0x1c0c,'B') else 0x5298b1
    try:m.run(0x529530,end=end,ecx=env)
    except Exception as e:raise RuntimeError(f"human state ci={ci} tick={tick} state={state} done={m.get(env+0x1c0c,chr(66))} end={end:x}") from e
    m.run(0x527080,(bits(dt),),ecx=env);m.run(0x527180,(bits(dt),),ecx=env)
    # Native camera-balance adapter is a separately supplied value for this slice.
    m.put(env+0x2ac,(tick*7)%1025)
    m.run(0x529cd0,(bits(dt),),ecx=env)
    state=m.get(env+0x1d14);states.add(state)
    rows.append([ci,bits(rpm),bits(maximum),bits(torque),bits(throttle),bits(brake),gear,bits(dt),(tick*7)%1025,m.get(env+0x1f1c),m.get(env+0x1f8c),state,m.get(env+0x1d00),m.get(env+0x238),m.get(env+0x23c),m.get(env+0x240),m.get(env+0x1f18),m.get(env+0x1f14),m.get(env+0x1d10)])
   assert not m.events
 fixture='// Actual human52C620 control order. See tool exclusions.\n'
 for name,array in [('kNativeHumanEngineConfig',configs),('kNativeHumanEngineRows',rows)]:
  fixture+='static constexpr unsigned '+name+'[]['+str(len(array[0]))+']={\n'
  fixture+=''.join('    {'+','.join('0x%08XU'%(n&0xffffffff) for n in row)+'},\n' for row in array)+'};\n'
 path=Path(__file__).resolve().parents[1]/'tests/human_engine_native.inc'
 if args.write_fixture:path.write_bytes(fixture.encode('ascii'))
 else:assert path.read_text(encoding='ascii')==fixture,'actual human engine fixture differs'
 if args.out:
  args.out.mkdir(exist_ok=False);(args.out/'oracle.json').write_text(json.dumps(dict(configs=configs,rows=rows,states=sorted(states),hooks=[],scope=__doc__),indent=2)+'\n',encoding='ascii')
 print('PASS actual human control slices, states='+str(sorted(states))+'; explicit boundaries, no IO')


if __name__=='__main__':main()

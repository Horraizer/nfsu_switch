"""Original PC1.4 USA engine control sequences; retail inputs stay read-only."""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
from world_body_trace import Image,chunks
from physics256_native import Machine,ACTOR,DRIVER,STATUS

def bits(value):return struct.unpack("<I",struct.pack("<f",value))[0]
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument("--root",type=Path,required=True)
 ap.add_argument("--out",type=Path,required=True)
 ap.add_argument("--write-fixture",action="store_true")
 a=ap.parse_args();cwd=Path.cwd();out=a.out.resolve();rel=out.relative_to(cwd)
 assert rel.as_posix().isascii() and rel.parts[0].startswith("build")
 assert a.root.resolve() not in out.parents
 out.mkdir(parents=True,exist_ok=False)
 raw=(a.root/"speed.exe").read_bytes();bun=(a.root/"Global/GLOBALB.BUN").read_bytes()
 assert hashlib.sha256(raw).hexdigest()=="c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7"
 assert hashlib.sha256(bun).hexdigest()=="39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9"
 im=Image(raw);records={}
 for tag,first,last in chunks(bun):
  if tag!=0x34600:continue
  for at in range((first+8+15)&~15,last,0xc90):
   record=bun[at:at+0xc90]
   if len(record)==0xc90:records[record[:16].split(b"\0")[0].decode("ascii")]=record
 env,tuning,ring,ring2=0x1120000,0x1110000,0x1130000,0x1130100
 event=0x1144000
 publication=[]
 m=Machine(im);m.allowed=[(0x46e020,0x46e043),(0x478fa9,0x478fb8)]
 box,engine,state,physics=0x1140000,0x1141000,0x1142000,0x1143000
 m.put(box+0x3c,2);m.put(box+0x20,engine)
 m.put(physics+0x4c,box);m.put(physics+0x58,state)
 for value in (.15,-.03,.0125,-.002,.001,-.0001):
  m.run(0x46e020,args=(bits(value),),end=0x46e043,ecx=box)
  m.run(0x478fa9,end=0x478fb8,edi=physics)
  publication.append([bits(value),m.get(box+0x70),m.get(state+0x3ac)])
 assert not m.events and all(row[1]==row[2] for row in publication)
 configurations=[];rows=[];states=set()
 for car in ("CIVIC","RX7","SUPRA","INTEGRA"):
  for upgrade in (0,3):
   data=records[car][0x9c0+upgrade*100:0xa24+upgrade*100]
   maximum=struct.unpack_from("<f",records[car],0x308)[0]
   config=[*struct.unpack_from("<3h",data,0x18),bits(struct.unpack_from("<f",data,0x20)[0]),*struct.unpack_from("<4h",data,0x2c),bits(struct.unpack_from("<f",data,0x34)[0])]
   config.extend([*struct.unpack_from("<2h",data,0xa),struct.unpack_from("<h",data,0x3c)[0]])
   ci=len(configurations);configurations.append(config)
   m=Machine(im);m.allowed=[(0x5236d0,0x523888),(0x5235a0,0x52369b),(0x523990,0x523c0e),(0x523c30,0x523e19),(0x523e30,0x524116),(0x527180,0x5271ff),(0x532610,0x5327ed),(0x584210,0x584356),(0x529a00,0x529a40),(0x401c80,0x401cbe),(0x674898,0x67490d)]
   m.put(env+0x234,ACTOR);m.put(ACTOR+0x30,DRIVER);m.put(0x73619c,STATUS)
   m.put(env+0x1f0c,tuning);m.uc.mem_write(tuning,data)
   m.put(env+0x2ac,config[11]);m.put(env+0x1760,event);m.put(env+0x1fd4,16383)
   m.put(env+0x1f80,160.,"f");m.put(env+0x1f24,maximum,"f");m.put(0x7000e4,100.,"f")
   for at,size,storage in ((env+0x1f28,10,ring),(env+0x1f50,5,ring2)):
    m.put(at,0x6b9694);m.put(at+5,size,"B");m.put(at+0x10,storage)
   previous_gear=2
   for tick in range(480):
    gear=2 if tick<100 else 3 if tick<155 else 4 if tick<205 else 3 if tick<340 else 4 if tick<344 else 2
    rpm=1000.+float((tick*47)%5900);torque=(float((tick*13)%220)-20.)*(.001 if ci%2 else 1.)
    throttle=.2 if 62<=tick<90 else 1.;brake=1. if 260<=tick<290 else 0.
    handbrake=300<=tick<330
    dt=(1./60.,1./30.,.01,.025)[ci%4]
    game=3 if tick<12 and ci%2 else 4
    # First envelope starts with constructor zero input caches. Gear events
    # run their actual control setup, ending before separate shift FX dispatch.
    if gear!=previous_gear:
     if gear>previous_gear:m.run(0x523990,end=0x523a37,ecx=env)
     else:m.run(0x523a70,ecx=env)
    previous_gear=gear
    m.put(env+0x244,dt,"f");m.put(env+0x240,float(tick)*dt,"f")
    m.run(0x523b30,args=(bits(dt),),ecx=env)
    m.run(0x527180,args=(bits(dt),),ecx=env)
    m.put(ACTOR+0x3ec,torque,"f");m.put(ACTOR+0x3e0,rpm,"f")
    m.put(DRIVER+0x1dc,throttle,"f");m.put(DRIVER+0x1e0,brake,"f");m.put(DRIVER+0x1e4,float(handbrake),"f");m.put(STATUS,game)
    m.run(0x523c30,ecx=env)
    state=m.get(env+0x1d14)
    # Control math is executed, never hooked. Stops exclude unrelated FX
    # admission/dispatch, whose writes do not produce these RPM/load fields.
    ends={0:0x524113,1:0x523f13,2:0x523f6b,3:0x523fed,4:0x524077,5:0x5240fb,6:0x524112}
    end=ends[state] if state and m.get(env+0x1c0c,"B") else 0x524112 if state else 0x524113
    m.run(0x523e30,args=(bits(dt),),end=end,ecx=env)
    m.run(0x523c30,ecx=env)
    m.run(0x5235a0,args=(bits(dt),),end=0x52369b,ecx=env)
    m.run(0x5236d0,ecx=env)
    state=m.get(env+0x1d14);states.add(state)
    rows.append([ci,bits(rpm),bits(maximum),bits(torque),bits(throttle),bits(brake),int(handbrake),gear,game,bits(dt),m.get(env+0x1f1c),m.get(env+0x1f8c),state,m.get(env+0x1d00),m.get(env+0x238),m.get(env+0x23c),m.get(event+0x14),m.get(event+0x1c)])
   assert not m.events
 assert states==set(range(7)),states
 balance_rows=[]
 balance_machine=Machine(im);balance_machine.allowed=[(0x523629,0x52369b)]
 for phase in range(1025):
  balance_machine.put(env+0x238,11911);balance_machine.put(env+0x23c,32600)
  balance_machine.put(env+0x2ac,phase)
  balance_machine.run(0x523629,end=0x52369b,esi=env,ebx=env+0x238,edi=env+0x23c)
  balance_rows.append([phase,balance_machine.get(env+0x238),balance_machine.get(env+0x23c)])
 assert not balance_machine.events
 balance_text="// Original523629 balance producer across its entire phase range; no hooks.\nstatic constexpr unsigned kNativeEngineBalance[][3]={\n"+"".join("    {%dU,%dU,%dU},\n"%tuple(row) for row in balance_rows)+"};\n"
 balance_fixture=cwd/"tests/engine_balance_native.inc"
 if a.write_fixture:balance_fixture.write_text(balance_text,encoding="ascii")
 else:assert balance_fixture.read_text(encoding="ascii")==balance_text,"native balance fixture stale"
 report={"input_sha256":hashlib.sha256(raw).hexdigest(),"balance_rows":balance_rows,"publication":publication,"configurations":configurations,"rows":rows,"states":sorted(states),"hooks":[],"boundary":"Original control slices in524620 order; remote divisor160 override; supplied gearbox-load publication. FX dispatch, adaptive gain modifiers, camera/category gains, countdown scripted revving and limiter RPM subtraction are outside this witness."}
 (out/"oracle.json").write_text(json.dumps(report,indent=2)+"\n",encoding="ascii")
 lines=["// Generated by tools/engine_envelope_native.py. Actual original control math; no hooks.","// See tool for scheduler slices and excluded FX/category/countdown paths.","static constexpr unsigned kNativeEngineEnvelopeConfig[][12]={"]
 for c in configurations:lines.append("    {"+",".join("0x%08XU"%(x&0xffffffff) for x in c)+"},")
 lines.extend(["};","static constexpr unsigned kNativeEngineEnvelopeRows[][18]={"])
 for row in rows:lines.append("    {"+",".join("0x%08XU"%(x&0xffffffff) for x in row)+"},")
 lines.append("};");text="\n".join(lines)+"\n";fixture=cwd/"tests/engine_envelope_native.inc"
 if a.write_fixture:fixture.write_text(text,encoding="ascii")
 else:assert fixture.read_text(encoding="ascii")==text,"native engine fixture stale"
 print("PASS original engine envelope: %d ticks, all7 states, no hooks"%len(rows))
if __name__=="__main__":main()

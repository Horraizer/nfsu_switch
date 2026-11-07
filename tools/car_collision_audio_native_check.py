"""Original car impact subtype on supplied SAT-independent contexts."""
import argparse,hashlib,json,subprocess
from pathlib import Path
from audio312_native import Machine,x
from hud254_trace import PE
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--probe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=False)
data=(a.root/'speed.exe').read_bytes();assert hashlib.sha256(data).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
cases=[[(0,0,0),(0,2,0),(10,0,0),(10,0,0),(0,1,0)],[(0,0,0),(2,0,0),(10,0,0),(-10,0,0),(1,0,0)],[(0,0,0),(0,2,0),(10,0,0),(0,10,0),(0,1,0)],[(0,0,0),(0,2,0),(0,0,0),(0,0,0),(0,1,0)],[(1,2,3),(4,5,6),(12,3,1),(9,2,0),(2,4,4)],[(0,0,0),(3,2,0),(10,5,0),(10,5,0),(1,0,0)]]
results=[]
for vs in cases:
 m=Machine(PE(data),[(0x531ac0,0x531d10),(0x4655b0,0x4655dc),(0x567370,0x5673dc)])
 obj,ba,bb=0x1100000,0x1101000,0x1102000;m.wi(obj+0x360,ba);m.wi(obj+0x364,bb)
 for address,values in zip([ba+0x60,bb+0x60,ba+0x90,bb+0x90,obj+0x220],vs):
  for j,v in enumerate(values):m.wf(address+j*4,v)
 m.run(0x531ac0,regs={x.UC_X86_REG_ECX:obj});original=m.uc.reg_read(x.UC_X86_REG_EAX)
 r=subprocess.run([str(a.probe.resolve()),'--car']+[str(v) for vector in vs for v in vector],capture_output=True,text=True)
 port=int(r.stdout.strip()) if r.returncode==0 else None
 results.append(dict(inputs=vs,original=original,port=port,ok=port==original,stderr=r.stderr))
(a.out/'report.json').write_text(json.dumps(results,indent=2),encoding='ascii')
print(json.dumps(results));raise SystemExit(0 if all(v['ok'] for v in results) else 1)

import sys, struct, hashlib, json, math, argparse
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path.cwd()/"tools"))
from world_body_trace import Image, manifest
from physics256_native import Machine, STOP, bits
parser=argparse.ArgumentParser()
parser.add_argument('--root',type=Path,default=Path('E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground'))
parser.add_argument('--out',type=Path,default=Path('build/integration328-tyre-native'))
args=parser.parse_args()
root=args.root.resolve()
base=args.out.resolve()
relative=base.relative_to(Path.cwd())
assert relative.parts[0].startswith('build') and str(relative).isascii()
assert root!=base and root not in base.parents
base.parent.mkdir(parents=True,exist_ok=True)
data=(root/"speed.exe").read_bytes()
assert hashlib.sha256(data).hexdigest()=="c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7"
image=Image(data);before=manifest(root)
base.mkdir(exist_ok=False)
rows=[]
try:
    m=Machine(image)
    m.allowed=[(0x587740,0x587850),(0x583EA0,0x583F20),(0x674898,0x674920),(0x4387E0,0x4387F6)]
    record,tyre,result=0x1100000,0x1101000,0x1102000
    m.put(record+0x18,tyre);m.put(record+0x158,1.0,"f")
    capture=STOP+0x100
    m.uc.mem_write(capture,b"\xd9\x1d"+struct.pack("<I",result)+b"\xc3")
    m.allowed.append((capture,capture+7))
    for scale,load_scale,force_scale in [(12.,1.,1.),(24.,1.,1.),(12.,2.,1.),(12.,1.,2.)]:
        for off,value in [(0,scale),(4,load_scale),(8,force_scale)]:m.put(tyre+off,value,"f")
        for load_kn in (0.,1.,3.,6.,10.):
            for slip in (0.,.01,.05,.1,.2,.5,1.5):
                m.run(0x587740,(record,bits(load_kn),bits(slip)))
                m.run(capture)
                force_kn=m.get(result,"f")
                assert math.isfinite(force_kn) and force_kn>=0
                rows.append(dict(slip_scale=scale,load_scale=load_scale,force_scale=force_scale,
                    load_kn=load_kn,slip_radians=slip,original_force_kn=force_kn))
    assert not m.events
finally:
    after=manifest(root)
    (base/"before.json").write_bytes(before);(base/"after.json").write_bytes(after)
    assert before==after,"retail manifest changed"
report=dict(input_sha256=hashlib.sha256(data).hexdigest(),boundary="Original isolated lateral force on synthetic wheel/load/angle; no live coupling/cadence",rows=rows,visited=["%08X"%a for a in sorted(m.visited)],external_hooks=m.events)
(base/"report.json").write_text(json.dumps(report,indent=2,ensure_ascii=True)+"\n",encoding="ascii")
print("PASS: authenticated original lateral force cases",len(rows),"no replaced math or force functions; retail manifest unchanged")
print(json.dumps(rows[15:20],ensure_ascii=True))

"""Original selector3 feedback/derivative/integration on a synthetic list."""
import argparse
import subprocess
import hashlib
import json
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import Image,manifest
from physics256_native import Machine,bits
from handling_brake_compare import EXE_SHA,BUN_SHA

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args()
root=args.root.resolve()
relative=args.out.resolve().relative_to(Path.cwd())
if not relative.parts[0].startswith('build') or not str(relative).isascii() or root==args.out.resolve() or root in args.out.resolve().parents:
    parser.error('output must be a fresh ASCII build directory within workspace, outside retail')
out=args.out.resolve(); out.mkdir(parents=True,exist_ok=False)
before=manifest(root); (out/'before.json').write_bytes(before)
try:
    data=(root/'speed.exe').read_bytes(); bun=(root/'Global/GLOBALB.BUN').read_bytes()
    assert hashlib.sha256(data).hexdigest()==EXE_SHA and hashlib.sha256(bun).hexdigest()==BUN_SHA
    record=bun[1736752:1736752+3216]
    assert hashlib.sha256(record).hexdigest()=='212f69ad8e739250f95e04c54fb6613a39a7c76014cb402df966f64b5d9fff9b'
    image=Image(data); m=Machine(image)
    m.allowed=[(0x475D00,0x47623F),(0x564CD0,0x564F20),(0x564B10,0x564C00),(0x674898,0x674920),
        (0x5ABDAF,0x5ABF00),(0x584CD0,0x584E00),(0x475097,0x475179),
        (0x475530,0x4757C8),(0x4757D0,0x4759FF),(0x583EA0,0x58400A),
        (0x46AA40,0x46AC08),(0x474010,0x474041),(0x586720,0x58672F),(0x575940,0x575945),
        (0x4387E0,0x4387F6)]
    engine,setup,body,system,vtable,derivative=0x1100000,0x1101000,0x1102000,0x1103000,0x1104000,0x1105000
    m.uc.mem_write(setup,record[0x110:])
    # Execute constructor's actual curve-descriptor initialization, ending
    # before the separate transmission-predictor construction call.
    m.run(0x475097,end=0x475179,esi=engine,ebp=setup)
    m.put(engine,0x6C847C); m.put(engine+0x18,setup); m.put(engine+0x14,body)
    m.put(0x6D4AC4,0x5ABDB5)
    for i,value in enumerate((1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)):
        m.put(body+0x30+i*4,float(value),'f')
    m.put(body+0xA0,8.316501835256739,'f')
    m.put(engine+0x138,1.0,'f'); m.put(engine+0x13C,1.0,'f'); m.put(engine+0x140,1.0,'f')
    m.put(engine+0x124,2); m.put(engine+0x134,2)
    for k in range(4):
        m.put(engine+0x64+k*4,1); m.put(engine+0x54+k*4,3.0,'f')
        m.put(engine+0x24+k*4,26.0,'f')
    # One engine in a synthetic circular component list. Original virtual
    # methods collect/integrate four state floats; synthetic system update
    # callback binds to original component-update walker46ABD0.
    m.put(system,vtable); m.put(vtable+4,0x46ABD0)
    m.put(system+0xC,engine+4); m.put(engine+4,system+0xC)
    m.hook(0x475A00,'strut-pass-stub-synthetic-contact-and-load',pop=4)
    rows=[]
    for axis,throttle,phase in [(1,0,'pin'),(.5,1,'hold'),(0,1,'release'),(0,1,'gas-after-release')]:
        m.put(engine+0x12C,axis,'f'); m.put(engine+0x128,throttle,'f')
        if not rows: m.run(0x4757D0,ecx=engine)
        # Reset synthetic initial speed once after obtaining initial RPM so
        # the first original derivative sees an unlatched wheel approaching
        # the pin, rather than an already locked fixture.
        if not rows:
            for k in range(4):
                m.put(engine+0x24+k*4,26.0,'f'); m.put(engine+0xB8+k*4,0)
            m.put(engine+0x12C,0.0,'f'); m.run(0x4757D0,ecx=engine); m.put(engine+0x12C,axis,'f')
        m.put(body+0x110,0.0,'f'); m.put(body+0x114,0.0,'f'); m.put(body+0x118,0.0,'f')
        m.run(0x475D00,ecx=engine)
        feedback=[m.get(engine+0x74+k*8,'f') for k in range(4)]
        pre_spin=[m.get(engine+0x24+k*4,'f') for k in range(4)]
        pre_lock=[m.get(engine+0xB8+k*4) for k in range(4)]
        m.run(0x475530,(derivative,),ecx=engine)
        rates=[m.get(derivative+k*4,'f') for k in range(4)]
        m.run(0x46AA40,(system,bits(1/120)))
        rows.append(dict(phase=phase,axis=axis,throttle=throttle,pre_spin=pre_spin,pre_lock=pre_lock,
            actual_feedback=feedback,angular_derivative=rates,
            completed_spin=[m.get(engine+0x24+k*4,'f') for k in range(4)],
            completed_lock=[m.get(engine+0xB8+k*4) for k in range(4)],rpm=m.get(engine+0x120,'f'),
            body_force=[m.get(body+0x110+k*4,'f') for k in range(3)]))
    report=dict(executable_sha256=EXE_SHA,globalb_sha256=BUN_SHA,source_head=subprocess.check_output(['git','rev-parse','HEAD']).decode('ascii').strip(),source_tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),rows=rows,allowed=[['%08X'%a,'%08X'%b] for a,b in m.allowed],
        visited=['%08X'%a for a in sorted(m.visited)],hooks=m.events,
        boundary='Authentic producer, unlocked/locked derivative, integration kernel and completed pin/RPM; synthetic one-engine circular list/stock setup, fixed identity body velocity/load, actual constructor curve slice, original scalar dispatch, strut stub; not full vehicle/live parity')
    (out/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n',encoding='ascii')
    print(json.dumps(rows,indent=2))
finally:
    after=manifest(root); (out/'after.json').write_bytes(after)
    if before!=after: raise RuntimeError('retail manifest changed')
    print('PASS: complete retail content/metadata manifest unchanged')

from pathlib import Path
import sys,json,hashlib
sys.path.insert(0,str(Path(__file__).resolve().parent))
from controller293_planner import PlannerMachine,low_memory
from world_body_trace import Image,chunks,jdlz
from unicorn import UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
import argparse
parser=argparse.ArgumentParser()
parser.add_argument('retail_root',type=Path)
parser.add_argument('output',type=Path)
args=parser.parse_args()
root=args.retail_root
args.output.mkdir(parents=True,exist_ok=True)
exe=(root/'speed.exe').read_bytes()
assert hashlib.sha256(exe).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
for track in (1003,1102,1210):
    image=jdlz((root/('Tracks/TRACKB%d.lzc'%track)).read_bytes())
    pe=Image(exe);m=PlannerMachine(pe)
    md=Cs(CS_ARCH_X86,CS_MODE_32)
    for lo,hi in ((0x59D6B3,0x59D6BF),(0x59D3B3,0x59D3B9),(0x4430D6,0x4430F5),(0x59CB38,0x59CB4F),(0x59CBBA,0x59CCA9),(0x59CE57,0x59CE5C),(0x59CE96,0x59CE9E),(0x567500,0x56753E),(0x59D3D3,0x59D42D),(0x4AD8D0,0x4AD8D3),(0x59CEA0,0x59d342)):
        rows=list(md.disasm(pe.read(lo,hi-lo),lo));assert sum(x.size for x in rows)==hi-lo
        m.instructions.update({x.address:x.size for x in rows})
    m.run(0x689FD0);m.run(0x689FF0)
    m.uc.mem_map(0,4096);m.uc.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,low_memory,begin=0,end=4095)
    m.uc.mem_map(0x2000000,0x1000000);m.uc.mem_write(0x2000000,image)
    heap=[0x2800000]
    def allocate(m):
        size=m.get(m.reg('esp')+4);assert 0<size<0x100000
        at=heap[0];heap[0]+=(size+15)&~15;assert heap[0]<0x3000000
        m.ret(value=at)
    for address in (0x67121A,0x565B60):m.hook(address,'external_heap_allocate',callback=allocate)
    for address in (0x671102,0x565CE0):m.hook(address,'external_heap_release')
    m.run(0x442B00,edx=0x77AF20);m.run(0x5B065B,(0,));m.run(0x59C850)
    for tag,at,end in chunks(image):
        if tag==0x80034147:m.run(0x4434D0,(0x7372B0,),eax=0x2000000+at,budget=3000000)
        if tag in (0x34121,0x34122,0x34123,0x34124):m.run(0x442DD0,(0x2000000+at,))
    m.run(0x4430B0,budget=30000000)
    routes=[]
    for i in range(128):
        r=m.get(0x77AF30+4*i)
        if r:routes.append(dict(id=i,begin=m.get(r+0x18,'f'),end=m.get(r+0x1c,'f')))
    events=[];base=m.get(0x736150)
    for i in range(m.get(0x736154)):
        a=base+32*i
        events.append(dict(source_route=m.get(a+10,'B'),source_point=m.get(a+6,'H'),target_route=m.get(a+9,'B'),target_point=m.get(a+4,'H'),progress=m.get(a,'f')))
    out=dict(track=track,sha256=hashlib.sha256(image).hexdigest(),routes=routes,events=events,boundary='Original4430B0 route/event initialization only; allocator supplied; no actor or trajectory')
    (args.output/('road-init-%d.json'%track)).write_text(json.dumps(out,indent=2),encoding='ascii')
    print('PASS native initialization',track,len(routes),len(events),flush=True)

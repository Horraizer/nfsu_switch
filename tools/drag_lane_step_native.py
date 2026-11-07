"""Original human drag input and manual controller stage on initialized roads.

The unchanged retail input4605E0 and update45D800 run through real lane/road
geometry. Execution stops at45D908, BEFORE the common context call462430.
Original constructors prepare an isolated traffic context, then the supplied
owner uses the actual drag vtable6C8D20. This is a bounded manual-stage check,
not a natural drag race, the common update or integrated steering/physics.
"""
from pathlib import Path
import sys,json,hashlib,argparse,struct,capstone
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('retail',type=Path)
ap.add_argument('--write-fixture',action='store_true');ap.add_argument('--out',type=Path)
options=ap.parse_args()
source=(root/'tools/traffic_context_native.py').read_text('ascii')
source=source[:source.index('\nrecords=[]')]
saved=sys.argv;sys.argv=['traffic_context_native.py',str(options.retail)]
ns={'__name__':'drag_lane_setup','__file__':str(root/'tools/traffic_context_native.py')}
exec(compile(source,'traffic-context-initialization','exec'),ns);sys.argv=saved
m=ns['m'];actor=ns['actor'];controller=ns['CONTROLLER'];body=ns['ACTOR'];image=ns['image']
assert m.get(0x6c8d20+0xc)==0x45d800 and m.get(0x6c8d20+0x50)==0x462390,'unexpected drag vtable'
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for a,b in [(0x44ac10,0x44adc0),(0x45d800,0x45d913),(0x45d920,0x45d96b),
            (0x45d970,0x45db00),(0x4605e0,0x460784),(0x456e90,0x456fc0),
            (0x5a6700,0x5a6a00),(0x5a7700,0x5a78b0)]:
    m.instructions.update({i.address:i.size for i in md.disasm(image.read(a,b-a),a)})
m.put(controller,0x6c8d20);m.put(body+0x24,2);m.put(body+0x2c,controller)
driver=m.get(body+0x30);m.put(driver+0x1f0,body)
m.put(0x73608c,0);m.put(0x7003d8,1)
descriptor=m.get(controller+0x3d8);route=m.get(descriptor);rawpoint=m.get(descriptor+4)
origin=[m.get(rawpoint+4*k,'f')for k in range(3)]
normal=[m.get(rawpoint+0x10,'f'),-m.get(rawpoint+0xc,'f')]
centers=[m.get(rawpoint+0x3c+2*k,'h')/256 for k in range(11,17)if m.get(route+0x265+k,'B')]
assert len(centers)>=2
records=[]
for start in sorted({x+d for x in centers for d in (-.6,0.,.6)}):
    m.put(controller+0x500,2);m.put(controller+0x504,255);m.put(controller+0x508,255);m.put(descriptor+8,13)
    sequence=[(start,-1),(start,0),(centers[0],0),(centers[0],1),(centers[0],0),(centers[-1],0)]
    for lateral,held in sequence:
        for k in range(3):m.put(body+0x60+4*k,origin[k]+(normal[k]*lateral if k<2 else 0),'f')
        m.put(body+0x3b8,10.,'f');m.put(controller+0x4bc,0,'B')
        before=ns['snapshot']();position=[m.get(body+0x60+4*k,'f')for k in range(3)]
        lane_before=[m.get(descriptor+8,'i')]+[m.get(controller+at,'i')for at in (0x500,0x504,0x508)]
        start_events=len(m.events)
        # Native steering event A6/A7, byte amplitude; a release selects mode2.
        event=0xa6 if held<0 else 0xa7
        m.run(0x4605e0,(0,event,int(held!=0),driver),budget=300000)
        m.run(0x45d800,(struct.unpack('<I',struct.pack('<f',1/60))[0],),ecx=controller,end=0x45d908,budget=300000)
        after=[m.get(descriptor+8,'i')]+[m.get(controller+at,'i')for at in (0x500,0x504,0x508)]
        assert not m.events[start_events:],m.events[start_events:]
        records.append(dict(before=before,position=position,held=held,lane_before=lane_before,lane_after=after,
                            dirty=bool(m.get(controller+0x4bc,'B'))))
def lit(v):
    if isinstance(v,bool):return str(v).lower()
    if isinstance(v,(list,tuple)):return '{'+','.join(lit(x)for x in v)+'}'
    if isinstance(v,float):return ns['literal'](v)
    return str(v)
text='// Actual4605E0/45D800 manual stage stops before the common update.\n'
text+='struct NativeDragLaneCell {int route;unsigned point,distance,offset[7];};\n'
text+='struct NativeDragLaneState {int category,mode,first,last;};\n'
text+='struct NativeDragLaneCase {AiRoadCursor road;int origin;unsigned count;NativeDragLaneCell cells[40];AiDriverPoint position;int held;NativeDragLaneState before,after;};\n'
text+='static const NativeDragLaneCase kNativeDragLaneStep[]={\n'
for row in records:
    before=row['before'];cells=[[c[0],c[1],c[2],c[4]]for c in before['cells']]
    text+=lit([before['road'],before['origin'],len(cells),cells,row['position'],row['held'],row['lane_before'],row['lane_after']])+',\n'
text+='};\n';text.encode('ascii')
target=root/'tests/drag_lane_step_native.inc'
if options.write_fixture:target.write_bytes(text.encode('ascii'))
else:assert target.read_text('ascii')==text,'original manual drag stage fixture differs'
if options.out:
    relative=options.out.resolve().relative_to(Path.cwd());assert relative.as_posix().isascii() and relative.parts[0].startswith('build')
    options.out.mkdir(exist_ok=False)
    (options.out/'oracle.json').write_text(json.dumps(dict(scope=__doc__,exe_sha256=hashlib.sha256((options.retail/'speed.exe').read_bytes()).hexdigest(),records=records,stage_semantic_hooks=[]),indent=2)+'\n',encoding='ascii')
print('PASS actual human drag input/manual stage, native geometry, cases='+str(len(records)))

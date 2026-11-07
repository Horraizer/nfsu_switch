"""Original engine1/wheel/tyre/clutch slices on a synthetic flat fixture."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
sys.path.insert(0, str(Path.cwd()/'tools'))
from world_body_trace import Image, manifest
from physics256_native import Machine, STACK, bits
from handling_brake_compare import EXE_SHA, BUN_SHA


def witness(root, out):
    data=(root/'speed.exe').read_bytes()
    bun=(root/'Global/GLOBALB.BUN').read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXE_SHA or hashlib.sha256(bun).hexdigest()!=BUN_SHA:
        raise ValueError('unsupported retail inputs')
    record=bun[1736752:1736752+3216]
    if hashlib.sha256(record).hexdigest()!='212f69ad8e739250f95e04c54fb6613a39a7c76014cb402df966f64b5d9fff9b':
        raise ValueError('unsupported stock240SX record')
    image=Image(data)
    m=Machine(image)
    m.allowed=[(0x46C7C0,0x46C912),(0x46C6D0,0x46C764),
        (0x46D390,0x46D4D1),(0x46D1C0,0x46D388),(0x46D760,0x46EA43),(0x476CA3,0x476CFC),(0x4607A0,0x4607D4),(0x478365,0x478395),(0x46D680,0x46D6F5),
        (0x46E270,0x46E356),(0x46E020,0x46E26E),
        (0x46CF80,0x46CFB8),(0x46CFF0,0x46D1B1),
        (0x583EA0,0x583F18),(0x674898,0x67490D),(0x4387E0,0x4387F6),
        (0x46AA40,0x46AC08),(0x46C770,0x46C7C0),(0x4551B0,0x4551B6),
        (0x40A880,0x40A885),(0x575940,0x575945),(0x46CA90,0x46CF71),
        (0x478FBB,0x479051),(0x5868E0,0x586960),(0x47721B,0x47723F),
        (0x586710,0x586762),(0x587170,0x58721E),(0x586DA0,0x586F0D),
        (0x564B10,0x564C00),(0x46E360,0x46E435),(0x46D9F0,0x46DAB5),
        (0x587330,0x587822),(0x587830,0x587A91),(0x587220,0x587330),
        (0x565230,0x5652FF),(0x5ABDAF,0x5ABF00),(0x564CD0,0x564F20)]
    engine,setup,physics,actor,gearbox,wheels,system,vtable,output=[0x1100000+k*0x1000 for k in range(9)]
    state=actor+0x40
    m.uc.mem_write(setup,record[0x110:])
    m.put(physics+0x58,state);m.put(physics+0x48,engine);m.put(physics+0x54,setup)
    m.uc.mem_map(0,0x1000)
    m.put(0x7361F8,0x1119000);m.put(0x1119000+0x2D0,12345)
    m.put(0x73619C,0x111A000);m.put(0x111A000,4)
    m.put(0x78A344,0,'B');m.put(0x7361A8,0);m.put(0x73AD34,0)
    m.put(0x7000E4,100.0,'f');m.put(0x6D4AC4,0x5ABDB5)
    m.run(0x46C7C0,(state,),esi=engine,ecx=setup+0x1F0)
    if m.get(engine+0x18)!=setup+0x1F0 or m.get(engine+0x1C)!=state:
        raise RuntimeError('engine constructor identity differs')
    for k in range(4):
        obj=wheels+0x200*k
        m.put(physics+0x38+4*k,obj)
        m.run(0x5868E0,(setup+0x10+0x30*k,setup+0xD0+0x20*(k//2),setup+0x270),eax=obj,ecx=state)
    m.run(0x47721B,end=0x47723F,edi=physics)
    # Execute actual drive-class selection at the gearbox constructor caller.
    m.run(0x476CA3,end=0x476CFC,ebp=physics)
    tuning=m.reg('edx')
    m.run(0x46D390,(gearbox,setup+0x1B0,physics+0x38,state),ecx=engine,edx=tuning)
    if tuning!=0x6F1098:raise RuntimeError('stock rear-drive factory class differs')
    m.put(engine+0x20,gearbox)
    if (m.get(gearbox+0x18)!=setup+0x1B0 or m.get(gearbox+0x20)!=engine
            or m.get(gearbox+0x34)!=state
            or [m.get(gearbox+0x24+4*k) for k in range(4)] != [wheels+0x200*k for k in range(4)]):
        raise RuntimeError('gearbox constructor identity differs')
    m.put(gearbox+0xDC,1.0,'f')
    m.put(system,vtable);m.put(vtable+4,0x46ABD0)
    components=[wheels+0x200*k for k in range(4)]+[engine,gearbox]
    m.put(system+0xC,components[0]+4)
    for index,obj in enumerate(components):
        m.put(obj+4,components[index+1]+4 if index+1<len(components) else system+0xC)
    conversion=struct.unpack('<f',image.read(0x6CCB78,4))[0]
    # Actual helper return capture only; this does not replace its arithmetic.
    m.uc.mem_write(0x1001000,b'\xd9\x1d'+struct.pack('<I',gearbox+0x4C)+b'\xc3')
    m.allowed.append((0x1001000,0x1001007))
    gearbox_initial=bytes(m.uc.mem_read(gearbox,0x100))
    rows=[]
    for gear in (1,2,3):
        for mode in (0,1,2,3):
            for clutch in (0.0,0.5,1.0):
                for throttle in (0.0,1.0):
                    for spin in (0.0,26.0,50.0):
                        axis=1.0
                        m.uc.mem_write(gearbox,gearbox_initial)
                        m.put(state+0x3A0,5000.0,'f')
                        # Authentic construction/reset RPM-to-angular-speed
                        # slice. This is NOT called from each solver step.
                        m.run(0x478365,end=0x478395,esi=physics)
                        m.put(engine+0x78,throttle,'f');m.put(engine+0x80,1.0,'f')
                        m.put(engine+0x7C,0.0,'f');m.put(engine+0x9C,0.0,'f')
                        m.put(engine+0xBC,0.0,'f')
                        m.put(gearbox+0x3C,gear);m.put(gearbox+0x38,gear);m.put(gearbox+0x44,mode)
                        m.put(gearbox+0x14,clutch,'f');m.put(gearbox+0x58,0.0,'f')
                        for k in range(4):
                            obj=wheels+0x200*k
                            m.put(obj+0x28,spin,'f');m.put(obj+0x108,0.0,'f')
                            m.put(obj+0x10C,0);m.put(obj+0x155,0,'B')
                            m.put(obj+0x24,0.0,'f');m.put(obj+0x110,1)
                            m.put(obj+0x114,0.0,'f');m.put(obj+0x118,0.0,'f')
                            m.put(obj+0x160,0.0,'f');m.put(obj+0x16C,0.0,'f')
                            m.put(obj+0x164,0.0,'f')
                            m.put(obj+0x170,axis,'f');m.put(obj+0x174,0.0,'f')
                            m.put(obj+0x158,3.0,'f');m.put(obj+0x155,1,'B')
                            for j,v in enumerate((1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)):
                                m.put(obj+0x70+4*j,float(v),'f')
                            m.put(obj+0xF0,8.316501835256739,'f')
                            m.put(obj+0xF4,0.0,'f');m.put(obj+0xF8,0.0,'f')
                        m.run(0x46D680,edx=gearbox)
                        # The helper returns st(0); consume it with a declared
                        # synthetic fstp/ret trampoline, without replacing math.
                        m.run(0x1001000)
                        prior=m.get(engine+0x14,'f')
                        m.run(0x46E270,esi=gearbox)
                        for k in range(4):m.run(0x587330,(0,0),esi=wheels+0x200*k)
                        for iteration in range(5):m.run(0x46E360,esi=gearbox)
                        for k in range(4):m.run(0x587830,(k,),end=0x587A91,eax=wheels+0x200*k)
                        feedback=m.get(engine+0x7C,'f')
                        pre_wheel=[m.get(wheels+0x200*k+0x28,'f') for k in range(4)]
                        pre_torque=[m.get(wheels+0x200*k+0x108,'f') for k in range(4)]
                        pre_lock=[m.get(wheels+0x200*k+0x10C) for k in range(4)]
                        pre_reaction=[m.get(wheels+0x200*k+0x118,'f') for k in range(4)]
                        pre_lateral=[m.get(wheels+0x200*k+0x11C,'f') for k in range(4)]
                        pre_slip=[m.get(wheels+0x200*k+0x160,'f') for k in range(4)]
                        radii=[m.get(setup+0x10+0x30*k+0x10,'f') for k in range(4)]
                        m.put(0x6F0738,1/120,'f')
                        m.run(0x46CF80,(output,),ecx=engine)
                        rate=m.get(output,'f')
                        m.run(0x46AA40,(system,bits(1/120)))
                        completed=m.get(engine+0x14,'f')
                        cleared=m.get(engine+0x7C,'f')
                        m.put(STACK+4,1/120,'f')
                        m.run(0x478FBB,end=0x479051,eax=engine,edi=physics,ebp=STACK-4)
                        rows.append(dict(gear=gear,gearbox_mode=mode,clutch=clutch,
                            throttle=throttle,initial_wheel_spin=spin,
                            initial_engine_rad_s=prior,wheel_equivalent_engine_rad_s=m.get(gearbox+0x4C,'f'),
                            engine_feedback_torque=feedback,completed_clutch=m.get(gearbox+0x14,'f'),completed_gearbox_mode=m.get(gearbox+0x44),factory_tuning=tuning,wheel_total_torque=pre_torque,tyre_reaction=pre_reaction,tyre_lateral=pre_lateral,contact_slip=pre_slip,radii=radii,pre_wheel=pre_wheel,pre_lock=pre_lock,completed_wheel=[m.get(wheels+0x200*k+0x28,'f') for k in range(4)],
                            engine_angular_acceleration=rate,completed_engine_rad_s=completed,
                            completed_engine_rpm=completed*conversion,exported_actor_rpm=m.get(state+0x3A0,'f'),
                            feedback_after_component_update=cleared))
    if m.events: raise RuntimeError('unexpected external hook')
    for row in rows:
        if any(not math.isfinite(value) for value in row.values() if isinstance(value,float)):
            raise RuntimeError('nonfinite original result')
        if row['feedback_after_component_update']!=0.0:
            raise RuntimeError('engine update did not clear consumed torque')
        if row['gear']==1 and row['engine_feedback_torque']!=0.0:
            raise RuntimeError('neutral transmitted clutch torque')
        if row['gearbox_mode']==1 and row['engine_feedback_torque']!=0.0:
            raise RuntimeError('decoupled mode transmitted clutch torque')
    for row in rows:
        values=row['wheel_total_torque']+row['tyre_reaction']+row['tyre_lateral']+row['contact_slip']+row['radii']+row['completed_wheel']
        if any(not math.isfinite(value) for value in values):raise RuntimeError('nonfinite wheel result')
        if row['initial_wheel_spin']==26 and any(row['pre_lock'][:2]):
            raise RuntimeError('selector1 common full service unexpectedly pinned front26')
        if any(abs(value)>1e-5 for value in row['tyre_lateral']):
            raise RuntimeError('declared straight fixture acquired lateral force')
    def pick(mode):
        return next(r for r in rows if r['gear']==2 and r['gearbox_mode']==mode
            and r['clutch']==0.5 and r['throttle']==0 and r['initial_wheel_spin']==26)
    if not (pick(0)['completed_engine_rpm'] < pick(0)['exported_actor_rpm'] < 5000
            and pick(0)['completed_wheel'][2] > pick(1)['completed_wheel'][2]
            and any(abs(value)>0 for value in pick(0)['tyre_reaction'])
            and pick(0)['completed_clutch'] < 0.5
            and pick(2)['completed_clutch'] > 0.5
            and pick(3)['completed_gearbox_mode']==3):
        raise RuntimeError('coupled clutch/tyre/export witnesses did not distinguish states')
    report=dict(executable_sha256=EXE_SHA,globalb_sha256=BUN_SHA,
        source_head=subprocess.check_output(['git','rev-parse','HEAD']).decode('ascii').strip(),
        source_tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        boundary='Actual wheel constructors/inertia writer and authentic tyre/brake producer/lock, wheel-equivalent speed, clutch distribution/coupled torque solver five passes, engine/wheel/gearbox derivatives and original six-component Euler/update kernel, biased RPM export. Synthetic initial wheel speed, gear/clutch/mode/throttle, full common service inputs, authentic tyre producer587330 and combined-limit slice587830 before body force transform; declared identity wheel heading/fixed world velocity3kNload/surface0/rolling input0/timer0/boost0/damage0; actual full gearbox state update46DB60 and clutch derivative46DFA0 with declared SEH page0, seeded RNG context and game-state4; no body/contact production, live cadence or whole-player parity.',
        x87_control_word='0x007F synthetic; live configuration unverified',
        source_ranges=[dict(start='%08X'%a,end='%08X'%b,sha256=hashlib.sha256(image.read(a,b-a)).hexdigest()) for a,b in m.allowed if a<0x800000],
        synthetic_trampoline='fstp dword[gearbox+4C];ret; actual helper return capture only',
        external_hooks=[],visited=['%08X'%a for a in sorted(m.visited)],rows=rows)
    (out/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n',encoding='ascii')
    print('PASS: original engine1/wheel/tyre/clutch/integration/export %d declared cases; no hooks'%len(rows))
    for mode in range(4):
        row=pick(mode)
        print('Mode%d: prelock%s; torque%s; tyre%s; completedwheel%s; physicalRPM%.6f; actorRPM%.6f' %
            (mode,row['pre_lock'],row['wheel_total_torque'],row['tyre_reaction'],
             row['completed_wheel'],row['completed_engine_rpm'],row['exported_actor_rpm']))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();root=args.root.resolve();out=args.out.resolve()
    relative=out.relative_to(Path.cwd())
    if not relative.parts[0].startswith('build') or not str(relative).isascii() or root==out or root in out.parents:
        parser.error('output must be a fresh ASCII build directory within workspace, outside retail')
    out.mkdir(parents=True,exist_ok=False);before=manifest(root);(out/'before.json').write_bytes(before)
    try: witness(root,out)
    finally:
        after=manifest(root);(out/'after.json').write_bytes(after)
        if before!=after: raise RuntimeError('retail manifest changed')
        print('PASS: complete retail content/metadata manifest unchanged')


if __name__=='__main__': main()

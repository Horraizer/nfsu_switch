"""Original engine1 and clutch slices on declared stationary wheel fixtures."""
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
        (0x46D3B4,0x46D41E),(0x476CA3,0x476CFC),
        (0x478365,0x478395),(0x46D680,0x46D6F5),
        (0x46E270,0x46E356),(0x46E020,0x46E26E),
        (0x46CF80,0x46CFB8),(0x46CFF0,0x46D1B1),
        (0x583EA0,0x583F18),(0x674898,0x67490D),(0x4387E0,0x4387F6),
        (0x46AA40,0x46AC08),(0x46C770,0x46C7C0),(0x4551B0,0x4551B6),
        (0x40A880,0x40A885),(0x575940,0x575945),(0x46CA90,0x46CF71),
        (0x478FBB,0x479051)]
    engine,setup,physics,actor,gearbox,wheels,system,vtable,output=[0x1100000+k*0x1000 for k in range(9)]
    state=actor+0x40
    m.uc.mem_write(setup,record[0x110:])
    m.put(physics+0x58,state);m.put(physics+0x48,engine);m.put(physics+0x54,setup)
    m.put(0x78A344,0,'B');m.put(0x7361A8,0);m.put(0x73AD34,0)
    m.put(0x7000E4,100.0,'f')
    m.run(0x46C7C0,(state,),esi=engine,ecx=setup+0x1F0)
    if m.get(engine+0x18)!=setup+0x1F0 or m.get(engine+0x1C)!=state:
        raise RuntimeError('engine constructor identity differs')
    for k in range(4): m.put(physics+0x38+4*k,wheels+0x200*k)
    # Actual gearbox pointer/state constructor prefix, before table/random
    # initialization. Dynamic gear/clutch fields below are synthetic inputs.
    m.run(0x476CA3,end=0x476CFC,ebp=physics)
    tuning=m.reg('edx')
    if tuning!=0x6F1098:raise RuntimeError('stock rear-drive factory class differs')
    m.put(STACK+0x28,state)
    m.run(0x46D3B4,end=0x46D41E,esi=gearbox,edi=setup+0x1B0,
          edx=tuning,ecx=engine,eax=physics+0x38)
    m.put(engine+0x20,gearbox)
    if (m.get(gearbox+0x18)!=setup+0x1B0 or m.get(gearbox+0x20)!=engine
            or m.get(gearbox+0x34)!=state
            or [m.get(gearbox+0x24+4*k) for k in range(4)] != [wheels+0x200*k for k in range(4)]):
        raise RuntimeError('gearbox constructor identity differs')
    m.put(gearbox+0xDC,1.0,'f')
    m.put(system,vtable);m.put(vtable+4,0x46ABD0)
    m.put(system+0xC,engine+4);m.put(engine+4,system+0xC)
    conversion=struct.unpack('<f',image.read(0x6CCB78,4))[0]
    # Actual helper return capture only; this does not replace its arithmetic.
    m.uc.mem_write(0x1001000,b'\xd9\x1d'+struct.pack('<I',gearbox+0x4C)+b'\xc3')
    m.allowed.append((0x1001000,0x1001007))
    rows=[]
    for gear in (1,2,3):
        for mode in (0,1,2,3):
            for clutch in (0.0,0.5,1.0):
                for throttle in (0.0,1.0):
                    for spin in (0.0,26.0,50.0):
                        m.put(state+0x3A0,5000.0,'f')
                        # Authentic construction/reset RPM-to-angular-speed
                        # slice. This is NOT called from each solver step.
                        m.run(0x478365,end=0x478395,esi=physics)
                        m.put(engine+0x78,throttle,'f');m.put(engine+0x80,1.0,'f')
                        m.put(engine+0x7C,0.0,'f');m.put(engine+0x9C,0.0,'f')
                        m.put(engine+0xBC,0.0,'f')
                        m.put(gearbox+0x3C,gear);m.put(gearbox+0x44,mode)
                        m.put(gearbox+0x14,clutch,'f');m.put(gearbox+0x58,0.0,'f')
                        for k in range(4):
                            obj=wheels+0x200*k
                            m.put(obj+0x28,spin,'f');m.put(obj+0x108,0.0,'f')
                            m.put(obj+0x10C,0);m.put(obj+0x155,0,'B')
                        m.run(0x46D680,edx=gearbox)
                        # The helper returns st(0); consume it with a declared
                        # synthetic fstp/ret trampoline, without replacing math.
                        m.run(0x1001000)
                        prior=m.get(engine+0x14,'f')
                        m.run(0x46E270,esi=gearbox)
                        feedback=m.get(engine+0x7C,'f')
                        m.run(0x46CF80,(output,),ecx=engine)
                        rate=m.get(output,'f')
                        m.run(0x46AA40,(system,bits(1/120)))
                        completed=m.get(engine+0x14,'f')
                        cleared=m.get(engine+0x7C,'f')
                        m.put(STACK+4,1/120,'f')
                        m.run(0x478FBB,end=0x479051,eax=engine,edi=physics,ebp=STACK-4)
                        rows.append(dict(gear=gear,gearbox_mode=mode,clutch=clutch,
                            throttle=throttle,fixed_wheel_spin=spin,
                            initial_engine_rad_s=prior,wheel_equivalent_engine_rad_s=m.get(gearbox+0x4C,'f'),
                            engine_feedback_torque=feedback,wheel_drive_torque=[m.get(wheels+0x200*k+0x108,'f') for k in range(4)],
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
        if row['gear']!=1 and row['gearbox_mode']==3 and row['engine_angular_acceleration']!=0.0:
            raise RuntimeError('fully coupled isolated engine did not balance crank torque')
    def pick(mode, clutch):
        return next(r for r in rows if r['gear']==2 and r['gearbox_mode']==mode
            and r['clutch']==clutch and r['throttle']==0 and r['fixed_wheel_spin']==26)
    disconnected, slipping = pick(1,0.5), pick(0,0.5)
    if not (slipping['completed_engine_rpm'] < disconnected['completed_engine_rpm']
            and slipping['completed_engine_rpm'] < slipping['exported_actor_rpm'] < 5000
            and pick(0,0)['engine_feedback_torque']==0
            and pick(0,1)['completed_engine_rpm'] < slipping['completed_engine_rpm']):
        raise RuntimeError('engine inertia/clutch/export witnesses did not distinguish states')
    report=dict(executable_sha256=EXE_SHA,globalb_sha256=BUN_SHA,
        source_head=subprocess.check_output(['git','rev-parse','HEAD']).decode('ascii').strip(),
        source_tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        boundary='Original full engine constructor, gearbox pointer/state constructor prefix, wheel-equivalent speed, clutch torque distributor, engine derivative, single-engine original Euler kernel/component update, and biased actor RPM export. Synthetic fixed wheel spins, gear/clutch/mode/throttle, zero timer/boost/damage; no wheel/body dynamics, full gearbox update, live cadence or whole-player parity.',
        x87_control_word='0x007F synthetic; live configuration unverified',
        source_ranges=[dict(start='%08X'%a,end='%08X'%b,sha256=hashlib.sha256(image.read(a,b-a)).hexdigest()) for a,b in m.allowed if a<0x800000],
        synthetic_trampoline='fstp dword[gearbox+4C];ret; actual helper return capture only',
        external_hooks=[],visited=['%08X'%a for a in sorted(m.visited)],rows=rows)
    (out/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n',encoding='ascii')
    print('PASS: original engine1/clutch/integration/export %d declared cases; no hooks'%len(rows))
    for mode in range(4):
        print(next(r for r in rows if r['gear']==2 and r['gearbox_mode']==mode and r['clutch']==0.5 and r['throttle']==0 and r['fixed_wheel_spin']==26))


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

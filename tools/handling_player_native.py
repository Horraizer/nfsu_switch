"""Original full selector1 planar constraints on a synthetic flat fixture."""
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


def witness(root, out, owner_speed=0.0, lateral_speed=0.0, history_torque=0.0,
            longitudinal_speed=8.316501835256739, initial_rpm=5000.0, service_axis=1.0,
            owner_magnitude=0.0, rear_spin_spread=0.0,
            front_heading_x=1.0, front_heading_y=0.0,
            body_heading_x=1.0, body_heading_y=0.0, external_yaw=0.0):
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
        (0x46D390,0x46D4D1),(0x46D1C0,0x46D388),(0x46D760,0x46EA43),(0x476CA3,0x476CFC),(0x4607A0,0x4607D4),(0x478365,0x4783B9),(0x46D680,0x46D6F5),
        (0x46E270,0x46E356),(0x46E020,0x46E26E),
        (0x46CF80,0x46CFB8),(0x46CFF0,0x46D1B1),
        (0x583EA0,0x583F18),(0x674898,0x67490D),(0x4387E0,0x4387F6),
        (0x46AA40,0x46AC08),(0x46C770,0x46C7C0),(0x4551B0,0x4551B6),
        (0x40A880,0x40A885),(0x575940,0x575945),(0x46CA90,0x46CF71),
        (0x478FBB,0x479051),(0x5868E0,0x586960),(0x47721B,0x47723F),
        (0x586710,0x586762),(0x587170,0x58721E),(0x586DA0,0x586F0D),
        (0x564B10,0x564C00),(0x46E360,0x46E435),(0x46D9F0,0x46DAB5),
        (0x587330,0x587822),(0x587830,0x587DD7),(0x478570,0x4789C8),(0x46E440,0x46E53F),(0x401C80,0x401CAD),(0x454F30,0x454FE6),(0x587220,0x587330),
        (0x565230,0x5652FF),(0x5ABDAF,0x5ABF00),(0x5AC69C,0x5AC8D8),(0x564CD0,0x564F20)]
    engine,setup,physics,actor,gearbox,wheels,system,vtable,output=[0x1100000+k*0x1000 for k in range(9)]
    state=actor+0x40
    rigid,carbody,struts=0x111B000,0x111C000,0x111D000
    m.put(physics+0x20,rigid);m.put(physics+0x5C,actor);m.put(actor+0x3C,carbody)
    m.put(rigid+0x144,struct.unpack_from("<f",record,0x220)[0],"f")
    m.put(rigid+0x178,1.0/struct.unpack_from("<f",record,0x258)[0],"f")
    m.put(rigid+0x128,external_yaw,'f')
    for j,v in enumerate((body_heading_x,body_heading_y,0,0,-body_heading_y,body_heading_x,0,0,0,0,1,0,0,0,0,1)):
        m.put(rigid+0x30+4*j,float(v),"f")
    for k in range(4):
        st=struts+0x200*k;m.put(physics+0x28+4*k,st)
        m.put(st+0x90,struct.unpack_from("<f",record,0x120+0x30*k)[0]-struct.unpack_from("<f",record,0x110)[0],"f")
        m.put(st+0x94,struct.unpack_from("<f",record,0x124+0x30*k)[0],"f")
    m.uc.mem_write(setup,record[0x110:])
    m.put(physics+0x58,state);m.put(physics+0x48,engine);m.put(physics+0x54,setup)
    m.uc.mem_map(0,0x1000)
    m.put(0x7361F8,0x1119000);m.put(0x1119000+0x2D0,12345)
    m.put(0x73619C,0x111A000);m.put(0x111A000,4)
    m.put(0x78A344,0,'B');m.put(0x7361A8,0);m.put(0x73AD34,0)
    m.put(0x7000E4,100.0,'f');m.put(0x6D4AC4,0x5ABDB5);m.put(0x6D4ACC,0x5AC6A2)
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
    m.put(engine+0x20,gearbox);m.put(physics+0x4C,gearbox)
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
    engine_initial=bytes(m.uc.mem_read(engine,0x100))
    rng_initial=m.get(0x1119000+0x2D0)
    reset_rows=[]
    for prior_mode in range(4):
        for prior_clutch in (0.0,0.5,1.0):
            for requested in (0,1,2,4):
                m.uc.mem_write(gearbox,gearbox_initial)
                m.put(state+0x398,requested)
                m.put(gearbox+0x44,prior_mode);m.put(gearbox+0x14,prior_clutch,'f')
                m.put(gearbox+0x58,777.0,'f')
                #4782E2 establishes ecx0 in the full reset prefix. Execute
                # its actual gearbox tail, independently of ctor defaults.
                m.run(0x478395,end=0x4783B9,esi=physics,ecx=0)
                row=dict(prior_mode=prior_mode,prior_clutch=prior_clutch,actor_gear=requested,
                         gear=m.get(gearbox+0x3C),requested=m.get(gearbox+0x38),
                         mode=m.get(gearbox+0x44),clutch=m.get(gearbox+0x14,'f'),
                         previous_slip_rpm=m.get(gearbox+0x58,'f'))
                if (row['gear'],row['requested'],row['mode'],row['clutch'],row['previous_slip_rpm'])!=(requested,requested,3,1.0,0.0):
                    raise RuntimeError('original reset gearbox fields differ')
                reset_rows.append(row)
    rows=[]
    for gear in (1,2,3):
        for mode in (0,1,2,3):
            for clutch in (0.0,0.5,1.0):
                for throttle in (0.0,1.0):
                    for spin in (0.0,26.0,50.0):
                        axis=service_axis
                        m.uc.mem_write(gearbox,gearbox_initial)
                        m.uc.mem_write(engine,engine_initial)
                        m.put(0x1119000+0x2D0,rng_initial)
                        m.put(state+0x390,owner_speed,'f')
                        m.put(state+0x394,owner_magnitude,'f')
                        m.put(state+0x3A0,initial_rpm,'f')
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
                            wheel_spin=spin-rear_spin_spread if k==2 else spin+rear_spin_spread if k==3 else spin
                            m.put(obj+0x28,wheel_spin,'f');m.put(obj+0x108,0.0,'f')
                            m.put(obj+0x10C,0);m.put(obj+0x155,0,'B')
                            m.put(obj+0x24,0.0,'f');m.put(obj+0x110,1)
                            m.put(obj+0x114,history_torque,'f');m.put(obj+0x118,0.0,'f')
                            m.put(obj+0x160,0.0,'f');m.put(obj+0x16C,0.0,'f')
                            m.put(obj+0x164,0.0,'f')
                            m.put(obj+0x170,axis,'f');m.put(obj+0x174,0.0,'f')
                            m.put(obj+0x158,3.0,'f');m.put(obj+0x155,1,'B')
                            hx,hy=(front_heading_x,front_heading_y) if k<2 else (1.0,0.0)
                            m.put(obj+0xB0,hx,"f");m.put(obj+0xB4,hy,"f");m.put(obj+0xC0,-hy,"f");m.put(obj+0xC4,hx,"f")
                            for j,v in enumerate((hx,hy,0,0,-hy,hx,0,0,0,0,1,0,0,0,0,1)):
                                m.put(obj+0xB0+4*j,float(v),'f')
                            # Original contact producer586C19..586C66 combines
                            # body-local wheel basis with rigid world matrix.
                            m.run(0x5AC69C,(obj+0x70,obj+0xB0,rigid+0x30))
                            m.put(obj+0xF0,longitudinal_speed,'f')
                            m.put(obj+0xF4,lateral_speed,'f');m.put(obj+0xF8,0.0,'f')
                        m.run(0x46D680,edx=gearbox)
                        # The helper returns st(0); consume it with a declared
                        # synthetic fstp/ret trampoline, without replacing math.
                        m.run(0x1001000)
                        prior=m.get(engine+0x14,'f')
                        pre_equivalent=m.get(gearbox+0x4C,'f')
                        m.run(0x478570,edi=physics)
                        feedback=m.get(engine+0x7C,'f')
                        pre_wheel=[m.get(wheels+0x200*k+0x28,'f') for k in range(4)]
                        pre_torque=[m.get(wheels+0x200*k+0x108,'f') for k in range(4)]
                        pre_lock=[m.get(wheels+0x200*k+0x10C) for k in range(4)]
                        pre_sliding=[m.get(wheels+0x200*k+0x154,'B') for k in range(4)]
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
                            initial_engine_rad_s=prior,pre_kernel_wheel_equivalent_engine_rad_s=pre_equivalent,post_update_wheel_equivalent_engine_rad_s=m.get(gearbox+0x4C,'f'),
                            engine_feedback_torque=feedback,completed_clutch=m.get(gearbox+0x14,'f'),completed_gearbox_mode=m.get(gearbox+0x44),completed_up_offset=m.get(gearbox+0x78,'f'),completed_down_offset=m.get(gearbox+0x7C,'f'),completed_rng=m.get(0x1119000+0x2D0),factory_tuning=tuning,wheel_total_torque=pre_torque,tyre_reaction=pre_reaction,tyre_lateral=pre_lateral,contact_slip=pre_slip,radii=radii,pre_wheel=pre_wheel,pre_lock=pre_lock,pre_sliding=pre_sliding,completed_wheel=[m.get(wheels+0x200*k+0x28,'f') for k in range(4)],
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
        if longitudinal_speed==8.316501835256739 and row['initial_wheel_spin']==26 and any(row['pre_lock'][:2]):
            raise RuntimeError('selector1 common full service unexpectedly pinned front26')
        if body_heading_y==0.0 and front_heading_y==0.0 and lateral_speed==0.0 and longitudinal_speed!=0.0 and any(abs(value)>1e-5 for value in row['tyre_lateral']):
            raise RuntimeError('declared straight fixture acquired lateral force')
    def pick(mode):
        return next(r for r in rows if r['gear']==2 and r['gearbox_mode']==mode
            and r['clutch']==0.5 and r['throttle']==0 and r['initial_wheel_spin']==26)
    if initial_rpm==5000.0 and longitudinal_speed==8.316501835256739 and not (pick(0)['completed_engine_rpm'] < pick(0)['exported_actor_rpm'] < 5000
            and pick(0)['completed_wheel'][2] > pick(1)['completed_wheel'][2]
            and any(abs(value)>0 for value in pick(0)['tyre_reaction'])
            and pick(0)['completed_clutch'] < 0.5
            and pick(2)['completed_clutch'] > 0.5
            and pick(3)['completed_gearbox_mode']==3):
        raise RuntimeError('coupled clutch/tyre/export witnesses did not distinguish states')
    report=dict(owner_speed=owner_speed,lateral_speed=lateral_speed,longitudinal_speed=longitudinal_speed,
        initial_rpm=initial_rpm,service_axis=service_axis,owner_magnitude=owner_magnitude,rear_spin_spread=rear_spin_spread,
        front_heading_x=front_heading_x,front_heading_y=front_heading_y,
        body_heading_x=body_heading_x,body_heading_y=body_heading_y,
        fixture_external_yaw=external_yaw,
        initial_history_torque=history_torque,fixture_external_force=[0,0,0],fixture_heading='explicit rigid and relative front wheel headings; original matrix composition',rng_reset_per_case=rng_initial,executable_sha256=EXE_SHA,globalb_sha256=BUN_SHA,
        source_head=subprocess.check_output(['git','rev-parse','HEAD']).decode('ascii').strip(),
        source_tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        boundary='Full478570 constraints/full587830 force transform with explicit rigid and relative front wheel headings, composed by original5AC69C and actual stock mass/inverse yaw inertia, zero rigid external force. Actual wheel constructors/inertia writer and authentic tyre/brake producer/lock, wheel-equivalent speed, clutch distribution/coupled torque solver five passes, engine/wheel/gearbox derivatives and original six-component Euler/update kernel, biased RPM export. Owner+390 speed and wheel world lateral velocity are explicit owner_speed/lateral_speed, not inferred; wheel-equivalent pre-kernel and post-update epochs are separate. Engine/gearbox bytes and RNG context restored before each case. Synthetic initial wheel speed, gear/clutch/mode/throttle, explicit service axis/initial RPM/history inputs, authentic tyre producer587330 and combined-limit slice587830 before body force transform; explicit front wheel heading and world longitudinal/lateral velocity;3kNload/surface0/rolling input0/timer0/boost0/damage0; actual full gearbox state update46DB60 and clutch derivative46DFA0 with declared SEH page0, seeded RNG context and game-state4; no body/contact/basis production, genuine multi-frame history, live cadence or whole-player parity.',
        x87_control_word='0x007F synthetic; live configuration unverified',
        source_ranges=[dict(start='%08X'%a,end='%08X'%b,sha256=hashlib.sha256(image.read(a,b-a)).hexdigest()) for a,b in m.allowed if a<0x800000],
        synthetic_trampoline='fstp dword[gearbox+4C];ret; actual helper return capture only',
        reset_rows=reset_rows,reset_boundary='Actual478395..4783B9 tail with authentic zero ecx from4782E2 prefix; actor gear supplied. Full body/wheel/contact reset not executed.',
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
    parser.add_argument('--owner-speed',type=float,default=0.0)
    parser.add_argument('--lateral-speed',type=float,default=0.0)
    parser.add_argument('--history-torque',type=float,default=0.0)
    parser.add_argument('--longitudinal-speed',type=float,default=8.316501835256739)
    parser.add_argument('--initial-rpm',type=float,default=5000.0)
    parser.add_argument('--service-axis',type=float,default=1.0)
    parser.add_argument('--owner-magnitude',type=float,default=0.0)
    parser.add_argument('--rear-spin-spread',type=float,default=0.0)
    parser.add_argument('--front-heading-x',type=float,default=1.0)
    parser.add_argument('--front-heading-y',type=float,default=0.0)
    parser.add_argument('--body-heading-x',type=float,default=1.0)
    parser.add_argument('--body-heading-y',type=float,default=0.0)
    parser.add_argument('--external-yaw',type=float,default=0.0)
    args=parser.parse_args();root=args.root.resolve();out=args.out.resolve()
    relative=out.relative_to(Path.cwd())
    if not relative.parts[0].startswith('build') or not str(relative).isascii() or root==out or root in out.parents:
        parser.error('output must be a fresh ASCII build directory within workspace, outside retail')
    out.mkdir(parents=True,exist_ok=False);before=manifest(root);(out/'before.json').write_bytes(before)
    try: witness(root,out,args.owner_speed,args.lateral_speed,args.history_torque,
                 args.longitudinal_speed,args.initial_rpm,args.service_axis,args.owner_magnitude,args.rear_spin_spread,
                 args.front_heading_x,args.front_heading_y,args.body_heading_x,args.body_heading_y,args.external_yaw)
    finally:
        after=manifest(root);(out/'after.json').write_bytes(after)
        if before!=after: raise RuntimeError('retail manifest changed')
        print('PASS: complete retail content/metadata manifest unchanged')


if __name__=='__main__': main()

"""Authenticated original physics selection and selector1 brake/RPM slices.

Synthetic declared role/config/global, wheel and completed-engine fixtures.
No live game, scheduler or whole-model parity is inferred from these slices.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from world_body_trace import Image, manifest
from physics256_native import Machine, STACK, bits
from handling_brake_compare import EXE_SHA, BUN_SHA


def witness(root, out):
    data = (root/'speed.exe').read_bytes()
    bun = (root/'Global/GLOBALB.BUN').read_bytes()
    if hashlib.sha256(data).hexdigest() != EXE_SHA or hashlib.sha256(bun).hexdigest() != BUN_SHA:
        raise ValueError('unsupported retail inputs')
    record = bun[1736752:1736752+3216]
    if hashlib.sha256(record).hexdigest() != '212f69ad8e739250f95e04c54fb6613a39a7c76014cb402df966f64b5d9fff9b':
        raise ValueError('unsupported stock240SX record')
    image = Image(data)
    m = Machine(image)
    m.allowed = [(0x425150,0x425195),(0x47792A,0x4779F8),
                 (0x587220,0x5872A5),(0x478FBB,0x479051),
                 (0x4B6102,0x4B611C),(0x5809CB,0x5809E9),
                 (0x580A0C,0x580A13),(0x425582,0x425585),
                 (0x422ACA,0x422AEA),(0x42253A,0x422545),
                 (0x476898,0x4768AF),(0x46C7C0,0x46C8E0),
                 (0x42D470,0x42D4E1),(0x42D82E,0x42D83C)]
    actor, config, physics, driver, setup, state, wheel = [0x1100000+k*0x1000 for k in range(7)]
    # Original player configuration caller supplies role1. The constructor
    # slice copies the actual caller arguments, omitting RideInfo construction.
    m.run(0x4B6102,end=0x4B611C,esi=0)
    arguments=[m.get(m.reg('esp')+4*k) for k in range(7)]
    m.run(0x5809CB,(0,*arguments),end=0x5809E9,esi=config,edi=config+0x10)
    m.run(0x580A0C,end=0x580A13,esi=config)
    m.run(0x425582,end=0x425585,eax=config,ebx=actor)
    m.run(0x422ACA,end=0x422AEA,eax=config)
    controller=m.get(m.reg('esp'))
    m.run(0x42253A,end=0x422545,edi=controller)
    factory_selector=m.reg('ecx')
    for address,fmt in ((0x736208,'I'),(0x78A344,'B'),(0x7361A8,'I')): m.put(address,0,fmt)
    m.run(0x425150,eax=actor)
    role_proof=dict(player_call_arguments=arguments,config_role=m.get(config+4),
        config734=m.get(config+0x734,'B'),actor_config=m.get(actor+0x14),
        controller_from_role=controller,direct_factory_selector=factory_selector,
        selector_if_common_helper_called=m.reg('eax'),
        boundary='Actual player argument and role-store slices; ordinary controller1 factory branch bypasses425150 and requests physics1 directly; RideInfo/full factory omitted')
    selections = []
    for role in (1,2,3):
        for flagged in (0,1):
            for override in (0,1):
                for drag in (0,1):
                    for special in (0,1):
                        m.put(config+4,role); m.put(config+0x734,flagged,'B')
                        m.put(0x736208,override); m.put(0x78A344,drag,'B'); m.put(0x7361A8,special)
                        m.run(0x425150,eax=actor)
                        selections.append(dict(role=role,config734=flagged,global736208=override,
                            global78a344=drag,global7361a8=special,selector=m.reg('eax')))
    # Original physical setup is retail CarTypeInfo record+110, not the
    # beginning of the serialized record. Its brake block+270 is raw+380.
    m.uc.mem_write(setup,record[0x110:])
    m.run(0x476898,end=0x4768AF,ebp=physics,ecx=actor,eax=setup,edx=driver)
    state=m.get(physics+0x58)
    if state != actor+0x40 or m.get(physics+0x54)!=setup:
        raise RuntimeError('original actor/setup constructor bias differs')
    for k in range(4): m.put(physics+0x38+k*4,wheel+k*0x200)
    m.put(state+0x3C0,0)
    brakes = []
    for axis in (0.0,0.5,0.95,1.0):
        for hand in (0.0,1.0):
            for k in range(4):
                obj=wheel+k*0x200
                m.put(driver+0x1E4,axis,'f'); m.put(driver+0x1E0,hand,'f')
                m.put(STACK+0x18,physics)
                m.run(0x47792A,end=0x477953,ecx=physics,edx=driver,edi=0x38+4*k)
                m.run(0x477953,end=0x4779F8,edi=0x38+4*k,ebx=0x6FFE28-0x38)
                service=m.get(obj+0x170,'f'); parking=m.get(obj+0x174,'f')
                m.put(obj+0x14,setup+0x10+k*0x30); m.put(obj+0x1C,setup+0x270)
                for spin in (0.0,10.0,19.2,26.0,30.0,-26.0):
                    for force in (0.0,1.0,4.0):
                        m.put(obj+0x28,spin,'f'); m.put(obj+0x10C,123)
                        m.run(0x587220,(bits(force),),ecx=obj)
                        brakes.append(dict(axis=axis,handbrake=hand,wheel=k,service170=service,
                            parking174=parking,before_spin=spin,requested_force_kn=force,
                            after_spin=m.get(obj+0x28,'f'),after_locked=m.get(obj+0x10C)))
    # The actual selector1 export consumes completed engine angular speed.
    # Execute its RPM slice with positive supplied physics dt, ordinary100
    # global sentinel, authentic stock inertia and a biased actor+40 target.
    engine=0x1110000
    m.run(0x46C7C0,(state,),end=0x46C8E0,esi=engine,ecx=setup+0x1F0)
    m.put(physics+0x48,engine)
    if m.get(engine+0x18)!=setup+0x1F0 or m.get(engine+0x1C)!=state:
        raise RuntimeError('original engine setup/owner differs')
    constructor_proof=dict(physical_setup='stock record+0x110',
        physical_setup_pointer=m.get(physics+0x54),actor_pointer=m.get(physics+0x5C),
        biased_actor_pointer=state,engine_setup_pointer=m.get(engine+0x18),
        engine_owner_pointer=m.get(engine+0x1C),
        completed_engine_rad_s_after_constructor=m.get(engine+0x14,'f'),
        boundary='Actual physics constructor pointer-store and engine constructor prefixes; engine allocator/full factory omitted')
    rpm = []
    for previous in (800.0,3000.0,5000.0):
        for target in (800.0,3000.0,6000.0):
            for dt in (1/120,1/60,1/30):
                m.put(state+0x3A0,previous,'f')
                m.put(actor+0x3A0,1111.0,'f')
                conversion=struct.unpack('<f',image.read(0x6CCB78,4))[0]
                m.put(engine+0x14,target/conversion,'f')
                m.put(0x7000E4,100.0,'f')
                m.put(STACK+4,dt,'f')
                m.run(0x478FBB,end=0x479051,eax=engine,edi=physics,ebp=STACK-4)
                if m.get(actor+0x3A0,'f')!=1111.0: raise RuntimeError('unbiased actor canary changed')
                rpm.append(dict(previous_rpm=previous,requested_rpm=target,dt=dt,
                    completed_engine_rad_s=m.get(engine+0x14,'f'),
                    actor_rpm=m.get(state+0x3A0,'f')))
    # Qualify the explicit42E230 mode3 edge by its enclosing view-state gate.
    # Stop phase calculation before calling the complete state-change handler,
    # then execute only that handler's actual state4 branch test. No input
    # registration/controller/physics switch call is substituted or executed.
    view, stage=0x1111000,0x1112000
    m.put(0x7361F0,stage);m.put(view+0x58,0);m.put(view+0x60,0)
    m.put(0x700B3C,0,'B')
    view_cases=[]
    for phase in (4,5,6,8,10):
        for flag4ae,flag4af in ((0,0),(1,0),(0,1),(1,1)):
            m.put(stage,phase);m.put(view+0x4AE,flag4ae,'B');m.put(view+0x4AF,flag4af,'B')
            m.run(0x42D470,end=0x42D4E1,eax=view)
            selected=m.reg('ecx');m.put(view+0x278,selected)
            # The conditional jump targets either83C (state4) or88B.
            m.run(0x42D82E,end=0x42D83C if selected==4 else 0x42D88B,esi=view)
            eligible=m.reg('eax')==4
            expected=3 if flag4af else 4 if flag4ae else 2
            if selected!=expected or eligible!=(expected==4):
                raise RuntimeError('view state/mode3 caller qualification differs')
            view_cases.append(dict(stage=phase,view4ae=flag4ae,view4af=flag4af,
                selected_view_state=selected,state4_mode3_call_branch_eligible=eligible))
    if m.events: raise RuntimeError('unexpected external hook')
    report=dict(executable_sha256=EXE_SHA,globalb_sha256=BUN_SHA,
        source_head=subprocess.check_output(['git','rev-parse','HEAD']).decode('ascii').strip(),
        source_tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        boundary='Synthetic role/config/globals; selector1 common service branch with gas0, ordinary state0 handbrake branch, stock setup, requested force magnitude, completed-engine RPM slice; no full original player or mode-frequency measurement',
        x87_control_word='0x007F synthetic; live configuration unverified',
        source_ranges=[dict(start='%08X'%a,end='%08X'%b,sha256=hashlib.sha256(image.read(a,b-a)).hexdigest()) for a,b in m.allowed],
        visited=['%08X'%a for a in sorted(m.visited)],external_hooks=[],
        role_proof=role_proof,constructor_proof=constructor_proof,
        selections=selections,brakes=brakes,rpm=rpm,view_state_cases=view_cases,
        view_state_boundary='Original phase selector and state4 conditional branch only; meanings of view4AE/4AF, full state handler, explicit switch and live frequencies unverified')
    (out/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n',encoding='ascii')
    print('PASS: original selection %d, selector1 brake %d and RPM-export %d cases; no hooks' % (len(selections),len(brakes),len(rpm)))
    print('Ordinary human choice:',next(r for r in selections if r['role']==1 and not any(r[k] for k in ('config734','global736208','global78a344','global7361a8'))))
    print('Front brake witness:',next(r for r in brakes if r['axis']==1 and r['handbrake']==0 and r['wheel']==0 and r['before_spin']==26 and r['requested_force_kn']==0))
    print('RPM drop witness:',next(r for r in rpm if r['previous_rpm']==5000 and r['requested_rpm']==800 and r['dt']==1/60))
    print('PASS: original view-state phase/exceptional-mode3 branch %d cases'%len(view_cases))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    root,out=args.root.resolve(),args.out.resolve()
    relative=out.relative_to(Path.cwd())
    if not relative.parts[0].startswith('build') or not str(relative).isascii() or root==out or root in out.parents:
        parser.error('output must be a fresh ASCII build directory in the assigned workspace, outside retail')
    out.mkdir(parents=True,exist_ok=False)
    before=manifest(root); (out/'before.json').write_bytes(before)
    try: witness(root,out)
    finally:
        after=manifest(root); (out/'after.json').write_bytes(after)
        if before!=after: raise RuntimeError('retail manifest changed')
        print('PASS: complete retail content/metadata manifest unchanged')


if __name__=='__main__': main()

"""Issue 314 original input dispatcher and clock witnesses.

Synthetic controller contexts execute authenticated PE instructions. No retail
controller recording, effects/audio stub, import or OS call is involved.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from event0_trace import Image, digest_file, manifest
from nitrous314_recovery import Oracle, EXE_SHA


def dispatch(image):
    ranges = ((0x431960, 0x431CBD), (0x4318D0, 0x4318F3),
              (0x431900, 0x431920), (0x41F600, 0x41F619),
              (0x42AB30, 0x42AB49), (0x42ABB0, 0x42ABC8))
    machine = Oracle(image, ranges)
    u, x = machine.uc, machine.x
    car, controller, mode = 0x1000100, 0x1001000, 0x1002000
    machine.obj = car + 0x40
    # Selected mode context takes the original branch with no audio/effect call.
    machine.write(controller + 4, car)
    machine.write(0x73619C, mode)
    machine.write(mode, 4)
    jumps = struct.unpack('<14I', image.read(0x431CC0, 56))
    mapping = image.read(0x431CF8, 55)
    ids = [0x75 + i for i, value in enumerate(mapping) if jumps[value] == 0x431C0F]
    if ids != [0x9B, 0x9C]:
        raise AssertionError('nitrous dispatch table changed')
    rows = []
    def check(name, input_id, pressed, state, expected, drag=0, drift=0,
              drag_override=0, drift_override=0, car_present=True, clock=10000,
              callback=False):
        machine.prepare(state, clock)
        machine.write(controller + 4, car if car_present else 0)
        u.mem_write(0x78A345, bytes([drag]))
        u.mem_write(0x78A346, bytes([drift]))
        machine.write(0x7361A4, drag_override)
        machine.write(0x7361A0, drift_override)
        machine.write(machine.stack, machine.stop)
        machine.write(machine.stack + 4, 0)
        machine.write(machine.stack + 8, input_id if callback else pressed)
        if callback:
            machine.write(machine.stack + 12, pressed)
            machine.write(machine.stack + 16, controller)
        u.reg_write(x.UC_X86_REG_ESP, machine.stack)
        u.reg_write(x.UC_X86_REG_EAX, input_id)
        u.reg_write(x.UC_X86_REG_ECX, controller)
        u.emu_start(0x42ABB0 if callback else 0x431960, machine.stop, count=20000)
        if u.reg_read(x.UC_X86_REG_EIP) != machine.stop:
            raise RuntimeError('dispatcher instruction limit')
        actual = machine.state()
        if actual != expected:
            raise AssertionError('%s: %r versus %r' % (name, actual, expected))
        rows.append(dict(name=name, input_id=input_id, pressed=pressed, state=state,
                         expected=expected, actual=actual, clock=clock, drag=drag,
                         drift=drift, drag_override=drag_override,
                         drift_override=drift_override, car_present=car_present,
                         mode_context=4, audio_flag=False, callback_wrapper=callback))
    full = [8000, 4000, 8000, 0]
    active = [8000, 8000, 8000, 10000]
    for input_id in ids:
        check('press', input_id, 1, full, active)
        check('duplicate-press', input_id, 1, active, active, clock=11000)
        check('release', input_id, 0, active, [8000, 0, 7000, 0], clock=11000)
        check('empty', input_id, 1, [8000, 0, 0, 0], [8000, 0, 0, 0])
        check('missing-car', input_id, 1, full, full, car_present=False)
        check('drag-suppressed', input_id, 1, full, full, drag=1)
        check('drift-suppressed', input_id, 1, full, full, drift=1)
        check('drag-override-suppressed', input_id, 1, full, full, drag_override=1)
        check('drift-override-suppressed', input_id, 1, full, full, drift_override=1)
        check('registered-callback-press', input_id, 1, full, active, callback=True)
    check('unmapped-input', 0x74, 1, full, full)
    return rows, sorted('%08X' % a for a in machine.reached)




def clock_and_constructor(image):
    ranges = ((0x585E00, 0x585EBA), (0x464860, 0x4648B3))
    machine = Oracle(image, ranges, (0x4648B3,))
    u, x = machine.uc, machine.x
    rows = []
    def floating(at, value):
        u.mem_write(at, struct.pack('<f', value))
    def clock(name, delta, expected):
        machine.write(0x73AD34, 10000)
        machine.write(0x6F08A0, 0)
        floating(0x73457C, delta)
        floating(0x734580, 0)
        machine.write(machine.stack, machine.stop)
        u.reg_write(x.UC_X86_REG_ESP, machine.stack)
        u.emu_start(0x585E00, machine.stop, count=20000)
        if u.reg_read(x.UC_X86_REG_EIP) != machine.stop:
            raise RuntimeError('clock instruction limit')
        actual = struct.unpack('<I', u.mem_read(0x73AD34, 4))[0]
        if actual != expected:
            raise AssertionError('%s clock %r versus %r' % (name, actual, expected))
        rows.append(dict(name=name, initial_clock=10000, delta=delta, clock=actual))
    clock('paused-zero-world-delta', 0, 10000)
    clock('fixed-sixtieth-world-delta', 1 / 60, 10067)
    clock('quarter-second-world-delta', 0.25, 11000)
    machine.prepare([8000, 0, 0, 0], 10000)
    machine.write(machine.stack, machine.stop)
    machine.write(machine.stack + 4, machine.obj + 0x1000)
    u.reg_write(x.UC_X86_REG_ESP, machine.stack)
    u.reg_write(x.UC_X86_REG_EAX, machine.obj)
    u.reg_write(x.UC_X86_REG_ECX, 0)
    u.reg_write(x.UC_X86_REG_EDX, 0)
    u.emu_start(0x464860, 0x4648B3, count=20000)
    if u.reg_read(x.UC_X86_REG_EIP) != 0x4648B3 or machine.state() != [0, 0, 0, 0]:
        raise AssertionError('native constructor prefix did not clear depleted resource')
    actual = machine.initialize(machine.state(), 2, 2)
    if actual != [8000, 4000, 8000, 0]:
        raise AssertionError('fresh constructor resource did not initialize full')
    rows.append(dict(name='constructor-prefix-and-resource-initializer', initial=[8000, 0, 0, 0], state=actual,
                     omitted='wheel-transform/reset setup between prefix and resource block'))
    # The original restart dispatcher index, then original calls to teardown/reload.
    label = struct.unpack('<I', image.read(0x700E38, 4))[0]
    if image.string(label) != 'RC_RESTART_RACE':
        raise AssertionError('restart message identity changed')
    jumps = struct.unpack('<5I', image.read(0x422C20, 20))
    mapping = image.read(0x422C34, 10)
    if jumps[mapping[7]] != 0x422BD4:
        raise AssertionError('restart route changed')
    for at, target in ((0x422BD4, 0x422130), (0x422BDA, 0x421F00),
                       (0x421FC7, 0x422A70), (0x4648B3, 0x464C80)):
        raw = image.read(at, 5)
        if raw[0] != 0xE8 or at + 5 + struct.unpack('<i', raw[1:])[0] != target:
            raise AssertionError('restart/constructor call changed at %08X' % at)
    rows.append(dict(name='authenticated-restart-call-chain', evidence='message8 -> 422BD4 -> teardown422130, reload421F00 -> actor instantiate422A70; constructor464860 clears resource then reset464C80',
                     context='static authenticated calls; not complete world reload execution'))
    registration = struct.unpack('<7I', image.read(0x42D97C, 28))
    if registration[:3] != (0x42D74C,) * 3:
        raise AssertionError('controller states2/3/4 callback registration route changed')
    for callback_at, id_at, input_id, call_at in (
            (0x42D7C9, 0x42D7CE, 0x9B, 0x42D7D3),
            (0x42D7DC, 0x42D7E1, 0x9C, 0x42D7E6)):
        if image.read(callback_at, 5) != b'\x68' + struct.pack('<I', 0x42ABB0):
            raise AssertionError('nitrous callback pointer changed')
        if image.read(id_at, 5) != b'\x68' + struct.pack('<I', input_id):
            raise AssertionError('nitrous callback ID changed')
        raw = image.read(call_at, 5)
        if raw[0] != 0xE8 or call_at + 5 + struct.unpack('<i', raw[1:])[0] != 0x574020:
            raise AssertionError('nitrous callback registration call changed')
    rows.append(dict(name='authenticated-controller-callback-registration',
        evidence='controller +278 states2/3/4 route42D74C; IDs9B/9C register callback42ABB0 with controller context via574020',
        context='authenticated table and instructions; event0 phase producers still pending'))
    return rows



def finish_consumer(image):
    machine = Oracle(image, ((0x42C1F0, 0x42C338), (0x4352EA, 0x4352FC)), (0x4352FC,))
    u, x = machine.uc, machine.x
    car, controller, mode, racer = 0x1000100, 0x1001000, 0x1002000, 0x1003000
    machine.obj = car + 0x40
    machine.write(controller + 4, car)
    machine.write(controller + 0x58, 0)
    machine.write(controller + 0x60, 0)
    machine.write(car + 0x1C, racer)
    u.mem_write(racer + 9, b'\x02')
    machine.write(0x73619C, mode)
    machine.write(mode + 0x530, 1)
    machine.write(0x6F08A0, 60)
    machine.write(0x7361A0, 0)
    u.mem_write(0x78A346, b'\0')
    state = [8000, 8000, 8000, 10000]
    machine.prepare(state, 11000)
    def run():
        machine.write(machine.stack, machine.stop)
        u.reg_write(x.UC_X86_REG_ESP, machine.stack)
        u.reg_write(x.UC_X86_REG_EAX, controller)
        u.emu_start(0x42C1F0, machine.stop, count=20000)
        if u.reg_read(x.UC_X86_REG_EIP) != machine.stop:
            raise RuntimeError('finish consumer instruction limit')
    run()
    if machine.state() != [8000, 0, 7000, 0]:
        raise AssertionError('new finish did not stop active resource')
    frame = struct.unpack('<I', u.mem_read(controller + 0x58, 4))[0]
    if frame != 60:
        raise AssertionError('finish stamp not latched')
    rows = [dict(name='new-finish-resource-stop', initial=state, clock=11000,
                 frame=frame, state=machine.state(), place_context=2, selected_place=1)]
    machine.write(0x73AD34, 13000)
    run()
    if machine.state() != [8000, 0, 7000, 0]:
        raise AssertionError('latched finish consumed again')
    rows.append(dict(name='latched-finish-resource-unchanged', clock=13000, state=machine.state()))
    machine.write(controller + 0x58, 0)
    machine.prepare(state, 11000)
    machine.write(machine.stack, machine.stop)
    u.reg_write(x.UC_X86_REG_ESP, machine.stack)
    u.reg_write(x.UC_X86_REG_EDI, mode)
    u.reg_write(x.UC_X86_REG_EBX, controller)
    u.emu_start(0x4352EA, 0x4352FC, count=20000)
    if u.reg_read(x.UC_X86_REG_EIP) != 0x4352FC or machine.state() != [8000, 0, 7000, 0]:
        raise AssertionError('finish caller suffix did not reach resource stop')
    rows.append(dict(name='original-finish-caller-suffix', entry='4352EA', endpoint='4352FC',
                     state=machine.state(), context='lap/place update suffix; earlier producer eligibility omitted'))
    return rows

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    if out == root or root in out.parents or out in root.parents:
        parser.error('output must be outside input root')
    out.mkdir(parents=True, exist_ok=False)
    before = manifest(root)
    (out / 'before.json').write_bytes(before)
    try:
        if digest_file(root / 'speed.exe') != EXE_SHA:
            raise ValueError('unsupported executable SHA-256')
        import unicorn
        if unicorn.__version__ != '2.1.4':
            raise ValueError('expected Unicorn 2.1.4')
        image = Image((root / 'speed.exe').read_bytes())
        rows, reached = dispatch(image)
        lifecycle = clock_and_constructor(image)
        finishes = finish_consumer(image)
        (out / 'dispatch.json').write_text(json.dumps(dict(executable_sha256=EXE_SHA,
            context='synthetic bounded original dispatcher; not controller recording',
            witnesses=rows, reached=reached, lifecycle=lifecycle, finishes=finishes), indent=2) + '\n', encoding='ascii')
        print('PASS: %d original dispatcher witnesses' % len(rows))
        print('PASS: %d clock/constructor/restart checks' % len(lifecycle))
        print('PASS: %d original finish consumer witnesses' % len(finishes))
    finally:
        after = manifest(root)
        (out / 'after.json').write_bytes(after)
        if after != before:
            raise RuntimeError('retail input tree changed')
        print('PASS: retail manifests identical SHA256 ' + hashlib.sha256(before).hexdigest().upper())


if __name__ == '__main__':
    main()

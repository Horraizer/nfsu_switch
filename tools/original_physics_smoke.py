"""Live x86 instrumentation test on an isolated generated fixture, not the game."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time
from unittest import mock

import frida
import contact256_native as n
from world_body_trace import Image
from physics256_native import bits
from original_recorder import EXE_SHA256, digest, write_json
from original_physics_recorder import AGENT, Writer, package
from original_physics_replay import native_step, compare_port


def host_lifecycle(args, source):
    """Exercise the production host; only synthetic discovery/auth are substituted."""
    import original_recorder as base
    from original_physics_recorder import Recorder
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    exe = args.out / 'host-fixture.exe'
    create_fixture(source, exe)
    data = bytearray(exe.read_bytes()); offset = 0x3ff400
    for insn in Cs(CS_ARCH_X86, CS_MODE_32).disasm(data[offset + 0x1000:offset + 0x1100], 0x801000):
        if insn.mnemonic == 'call' and insn.op_str == '0x46aa40':
            at = offset + insn.address - 0x800000
            data[at:at + 5] = b'\xe8' + struct.pack('<i', 0x802000 - insn.address - 5)
            break
    else:
        raise RuntimeError('Synthetic call site missing')
    # Only the fixture's caller changes: route its return through the authentic
    # caller address before resuming the synthetic loop. No retail file is patched.
    thunk = b'\x58\xa3' + struct.pack('<I', 0x800f20) + b'\x68' + struct.pack('<I', 0x477df2)
    thunk += b'\xe9' + struct.pack('<i', 0x46aa40 - (0x802000 + len(thunk) + 5))
    data[offset + 0x2000:offset + 0x2000 + len(thunk)] = thunk
    at = 0x400 + 0x477df2 - 0x401000
    data[at:at + 6] = b'\xff\x25' + struct.pack('<I', 0x800f20)
    exe.write_bytes(data)
    process = subprocess.Popen([str(exe.resolve())], creationflags=subprocess.CREATE_NO_WINDOW)
    session = frida.attach(process.pid)
    control = session.create_script("rpc.exports={go(){ptr('0x800f0c').writeU32(1)}}")
    control.load()
    def authenticate(target):
        return dict(pid=target.pid, creation_filetime=target.created, sha256=EXE_SHA256,
                    synthetic_fixture=True, fixture_sha256=digest(exe))
    recorder = None
    try:
        with mock.patch.object(base.WindowsAPI, 'processes', return_value=[process.pid]), \
             mock.patch.object(base.Process, 'authenticate', authenticate):
            recorder = Recorder(args.out / 'host-capture', 'SYNTHETIC HOST LIFECYCLE TEST')
            recorder.start(); deadline = time.monotonic() + 10; started = False
            while time.monotonic() < deadline:
                while not recorder.messages.empty():
                    item = recorder.messages.get()
                    if item[0] == 'status' and item[1].startswith('Recording every'):
                        started = True
                if started:
                    break
                time.sleep(.05)
            if not started:
                raise RuntimeError('Production host did not start recording')
            control.exports_sync.go(); time.sleep(.8)
            recorder.stop(); recorder.thread.join(timeout=10)
            if recorder.thread.is_alive():
                raise RuntimeError('Production host did not stop')
            done = [item for item in recorder.messages.queue if item[0] == 'done']
            if len(done) != 1 or not done[0][2]['complete_stop'] or done[0][2]['steps'] < 10:
                raise RuntimeError('Production host did not produce a complete archive')
            return {'synthetic_fixture': True, 'summary': done[0][2]}
    finally:
        if recorder is not None:
            recorder.stop(); recorder.thread.join(timeout=5)
        control.unload(); session.detach(); process.terminate(); process.wait(timeout=5)


def create_fixture(source, destination):
    image = Image(source.read_bytes())
    m = n.fixture(image, (4, 0, 0), (0, 0, 0), (0, 0, 0), mass=1.22)
    rigid, props = n.RIGID_A, 0x1132000
    m.put(props, 1.22, 'f'); n.matrix(m, props + 0x10)
    for offset, value in ((0, .45), (0x14, 2.25), (0x28, 2.36)):
        m.put(props + 0x10 + offset, value, 'f')
    m.put(props + 0x50, .4, 'f')
    m.run(0x584800, ecx=rigid, eax=props)
    m.put(rigid + 0x130, 1); m.put(rigid + 0x134, 1)
    m.put(rigid + 0x140, 1)
    n.vector(m, rigid + 0x70, (20, 3, -1))
    n.vector(m, rigid + 0x80, (.1, -.2, .3))
    m.run(0x585810, ecx=rigid)
    original = bytearray(m.uc.mem_read(0x400000, 0x400000))
    test = bytearray(0x10000)
    def put(at, value, fmt='I'):
        struct.pack_into('<' + fmt, test, at - 0x800000, value)
    rb, pp, actor, physics, driver, config, table = [0x803000 + i * 0x1000 for i in range(7)]
    test[rb - 0x800000:rb - 0x800000 + 0x200] = m.uc.mem_read(rigid, 0x200)
    test[pp - 0x800000:pp - 0x800000 + 0x54] = m.uc.mem_read(props, 0x54)
    put(rb + 0x14, pp)
    system = physics + 8
    put(system, table); put(table + 4, 0x46abd0)
    put(system + 0xc, rb + 4); put(rb + 4, system + 0xc); put(rb + 8, system + 0xc)
    put(physics + 0x20, rb); put(physics + 0x5c, actor)
    put(actor + 0x14, config); put(actor + 0x28, 1); put(actor + 0x34, physics)
    put(actor + 0x30, driver); put(config + 4, 1); put(config + 0x10, 4)
    put(driver + 0x1dc, .75, 'f')
    put(0x800f00, 0x7f, 'H'); put(0x800f02, 0x27f, 'H')
    put(0x800f10, 0x1f80); put(0x800f14, 0x3f80)
    struct.pack_into('<f', original, 0x6f0738 - 0x400000, 1 / 120)
    # Minimal, non-ASLR PE32. Only Sleep/ExitProcess imports; no original entry/TLS/import initialization.
    header = bytearray(0x400)
    header[:2] = b'MZ'; struct.pack_into('<I', header, 0x3c, 0x80)
    header[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', header, 0x84, 0x14c, 2, 0, 0, 0, 0xe0, 0x103)
    at = 0x98
    struct.pack_into('<HBBIIIIII', header, at, 0x10b, 0, 0, 0x3ff000, 0x10000, 0, 0x401000, 0x1000, 0x400000)
    struct.pack_into('<III', header, at + 28, 0x400000, 0x1000, 0x200)
    struct.pack_into('<HHHHHH', header, at + 40, 4, 0, 0, 0, 4, 0)
    struct.pack_into('<III', header, at + 56, 0x410000, 0x400, 0)
    struct.pack_into('<HH', header, at + 68, 3, 0)
    struct.pack_into('<IIIIII', header, at + 72, 0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
    struct.pack_into('<II', header, at + 104, 0x400100, 40)
    for pos, name, size, va, rawsize, raw, flags in (
        (0x178, b'.orig', 0x3ff000, 0x1000, 0x3ff000, 0x400, 0xe0000060),
        (0x1a0, b'.test', 0x10000, 0x400000, 0x10000, 0x3ff400, 0xe0000060)):
        struct.pack_into('<8sIIIIIIHHI', header, pos, name, size, va, rawsize, raw, 0, 0, 0, 0, flags)
    struct.pack_into('<IIIII', test, 0x100, 0x400180, 0, 0, 0x400160, 0x4001a0)
    test[0x160:0x160 + 13] = b'kernel32.dll\0'
    for offset in (0x180, 0x1a0):
        struct.pack_into('<III', test, offset, 0x4001c0, 0x4001d0, 0)
    test[0x1c0:0x1c8] = b'\0\0Sleep\0'
    test[0x1d0:0x1de] = b'\0\0ExitProcess\0'
    code = bytearray()
    def emit(data):
        code.extend(data)
    def call(target):
        emit(b'\xe8' + struct.pack('<i', target - (0x801000 + len(code) + 5)))
    # Wait until the test script starts capture; no artificial gameplay input.
    emit(b'\x6a\x0a\xff\x15' + struct.pack('<I', 0x8001a0))
    emit(b'\x83\x3d' + struct.pack('<I', 0x800f0c) + b'\x00\x74\xef')
    loop = len(code)
    emit(b'\xdb\xe3')  # finit
    emit(b'\xd9\x2d' + struct.pack('<I', 0x800f00))
    emit(b'\x0f\xae\x15' + struct.pack('<I', 0x800f10))
    # After half the fixture, change x87 precision to test capture honesty.
    emit(b'\x83\x3d' + struct.pack('<I', 0x800f04) + b'\x18\x72\x0d')
    emit(b'\xd9\x2d' + struct.pack('<I', 0x800f02))
    emit(b'\x0f\xae\x15' + struct.pack('<I', 0x800f14))
    emit(b'\xc7\x05' + struct.pack('<II', rb + 0x110, bits(1.7)))
    emit(b'\x68' + struct.pack('<I', bits(1 / 120)) + b'\x68' + struct.pack('<I', system))
    call(0x46aa40)
    emit(b'\xff\x05' + struct.pack('<I', 0x800f04))
    emit(b'\x6a\x0a\xff\x15' + struct.pack('<I', 0x8001a0))
    emit(b'\xe9' + struct.pack('<i', loop - (len(code) + 5)))
    test[0x1000:0x1000 + len(code)] = code
    destination.write_bytes(header + original[0x1000:] + test)
    return {'rigid': rb, 'physics': physics, 'counter': 0x800f04, 'gate': 0x800f0c,
            'code_bytes': len(code), 'sha256': digest(destination)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--compiler', default='c++')
    args = parser.parse_args()
    if args.out.exists() or args.out.resolve().is_relative_to(args.retail.resolve()):
        raise ValueError('Fresh non-retail output required')
    source = args.retail / 'speed.exe'
    if digest(source) != EXE_SHA256:
        raise ValueError('Unsupported source')
    args.out.mkdir()
    fixture = args.out / 'physics-fixture.exe'
    identity = create_fixture(source, fixture)
    image = Image(source.read_bytes())
    events, records = [], []
    process = subprocess.Popen([str(fixture.resolve())], creationflags=subprocess.CREATE_NO_WINDOW)
    session = script = None
    try:
        session = frida.attach(process.pid)
        script = session.create_script(AGENT.read_text('ascii'), runtime='qjs')
        def receive(message, data):
            if message.get('type') == 'send' and message['payload'].get('kind') == 'batch':
                records.extend(message['payload']['records'])
                events.append(message['payload'])
            else:
                events.append(message)
        script.on('message', receive)
        script.load(); script.exports_sync.start(False)
        script2 = session.create_script("Process.setExceptionHandler(d => {send({fault:String(d.address),context:d.context,memory:d.memory});return false;}); rpc.exports = {go(){ptr('0x800f0c').writeU32(1);}, reset(){ptr('0x800f04').writeU32(0);}, role(n){ptr('0x808004').writeU32(n);}, counter(){return ptr('0x800f04').readU32();}, entry(){return Array.from(new Uint8Array(ptr('0x46aa40').readByteArray(6)));}};")
        script2.on('message', lambda message, data: print('FIXTURE', json.dumps(message), flush=True))
        script2.load(); script2.exports_sync.go()
        time.sleep(.15)
        wrong_caller_refused = script.exports_sync.status()['sequence'] == 0 and script2.exports_sync.counter() > 0
        if not wrong_caller_refused:
            raise RuntimeError('Production caller filter accepted the synthetic caller')
        script.exports_sync.stop(); script.unload()
        script2.exports_sync.role(2); script2.exports_sync.reset()
        script = session.create_script(AGENT.read_text('ascii'), runtime='qjs')
        script.on('message', receive); script.load(); script.exports_sync.start(True)
        time.sleep(.1)
        wrong_role_refused = script.exports_sync.status()['sequence'] == 0
        if not wrong_role_refused:
            raise RuntimeError('Actor role filter accepted a rival')
        script2.exports_sync.reset(); script2.exports_sync.role(1)
        deadline, ack = time.monotonic() + 15, 0
        while len(records) < 48 and time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError('Fixture crashed: ' + str(process.returncode))
            script.exports_sync.status()
            for event in list(events):
                if event.get('kind') == 'batch' and event['batch'] == ack:
                    script.exports_sync.acknowledge(ack); ack += 1
            time.sleep(.05)
        state = script.exports_sync.stop()
        deadline = time.monotonic() + 2
        while state['active'] and time.monotonic() < deadline:
            time.sleep(.01); state = script.exports_sync.stop()
        time.sleep(.1)
        for event in list(events):
            if event.get('kind') == 'batch' and event['batch'] == ack:
                script.exports_sync.acknowledge(ack); ack += 1
        state = script.exports_sync.status()
        if len(records) < 48 or state['errors'] or state['completed'] != len(records):
            raise RuntimeError('Incomplete capture: ' + str(state) + ' ' + str(events[:1]))
        script.unload(); script = None
        # Confirm detach restores actual function bytes and the process still advances.
        restored = script2.exports_sync.entry() == [0x81, 0xec, 0x34, 2, 0, 0]
        before = script2.exports_sync.counter(); time.sleep(.1)
        advances = script2.exports_sync.counter() > before
        script2.unload(); session.detach(); session = None
        if not restored or not advances:
            raise RuntimeError('Hook removal/continued execution failed')
    finally:
        if script is not None:
            try:
                script.unload()
            except frida.InvalidOperationError:
                pass
        if session is not None:
            try:
                session.detach()
            except frida.InvalidOperationError:
                pass
        process.terminate(); process.wait(timeout=5)
    captures = args.out / 'capture'; captures.mkdir()
    writer = Writer(captures)
    batches = [e for e in events if e.get('kind') == 'batch']
    for event in batches:
        writer.batch(event)
    writer.close()
    write_json(captures / 'metadata.json', {'schema': 'nfsu-rigid-steps-1', 'synthetic_fixture': True,
                                          'source': {'sha256': EXE_SHA256}, 'fixture': identity})
    write_json(captures / 'summary.json', {'steps': len(records), 'complete_stop': True, 'synthetic_fixture': True})
    archive = package(captures)
    results = []
    for record in records:
        _, differences, instructions = native_step(image, record)
        results.append({'seq': record['seq'], 'differences': differences, 'instructions': instructions,
                        'fpcw': record['fpcw']})
    damaged = dict(records[0])
    changed = bytearray.fromhex(damaged['post_hex']); changed[0x20] ^= 1
    damaged['post_hex'] = changed.hex()
    _, mismatch, _ = native_step(image, damaged)
    if 0x20 not in mismatch:
        raise RuntimeError('Comparison failed to detect a changed recorded result')
    port = compare_port([r for r in records if r['fpcw'] == 0x7f], args.out, args.build, args.compiler)
    report = {'fixture': identity, 'steps': len(records), 'native': results, 'port': port,
              'control_words': sorted(set(r['fpcw'] for r in records)), 'restored_hook': restored,
              'sse_control_modes': sorted(set(r['mxcsr'] & 0xffc0 for r in records)),
              'continued_execution': advances, 'archive': archive.name,
              'wrong_caller_refused': wrong_caller_refused, 'wrong_role_refused': wrong_role_refused,
              'damaged_output_detected': bool(mismatch),
              'scope': 'Live generated x86 fixture with original body instructions; not retail game runtime acceptance'}
    report['host_lifecycle'] = host_lifecycle(args, source)
    write_json(args.out / 'report.json', report)
    if (any(r['differences'] for r in results) or not port['passed'] or
        report['control_words'] != [0x7f, 0x27f] or report['sse_control_modes'] != [0x1f80, 0x3f80]):
        raise RuntimeError('Native/port/control-word comparison failed; inspect report.json')
    if digest(source) != EXE_SHA256:
        raise RuntimeError('Retail executable changed')
    print('PASS live x86 capture/replay/port/cleanup: ' + str(len(records)) + ' steps')


if __name__ == '__main__':
    main()

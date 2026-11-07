"""Live generated x86 AI fixture; never launches or changes the retail game."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import time
from unittest import mock

import frida
import original_recorder as base
from original_ai_recorder import Recorder, AGENT
from original_ai_replay import RetailCode, replay
from original_physics_smoke import create_fixture as physics_fixture
from world_body_trace import Image
from controller293_native import fixture
from physics256_native import ACTOR,DRIVER,CONFIG,bits


def create_fixture(source,destination,scene=False):
    physics_fixture(source,destination)
    image = Image(source.read_bytes())
    if scene:
        from controller293_scene import PairedController
        from controller293_planner import inputs
        model = PairedController(image,inputs(source.parent),elapsed_ticks=0,event_rides=True,native_ownership=True)
        m = model.m
        from controller293_scene import HUMAN
        from controller293_planner import TRACK
        from world_body_trace import chunks,jdlz
        m.run(0x441270,eax=0x77b130)
        m.run(0x4412b0,(0,),budget=3000000)
        for tag,at,end in chunks(jdlz(inputs(source.parent)['Tracks/TRACKB1003.lzc'])):
            if tag in range(0x34130,0x34135):m.run(0x441ed0,(TRACK+at,),budget=3000000)
        m.run(0x445720,esi=0x73b14c)
        m.run(0x4764e0,budget=3000000)
        for actor,selector in ((HUMAN,1),(ACTOR,3)):
            m.run(0x4255cb,end=0x4255e0,ebx=actor,ecx=m.get(actor+0x10))
            m.put(actor+0x28,0);m.put(actor+0x34,0)
            m.run(0x426110,esi=actor,ecx=selector,budget=3000000)
        m.put(m.get(0x73619c),4)
        model.step(30,20.)
    else:
        m = fixture(image,[(10.,0.,0.),(100.,0.,0.)],speed=20,target=30)
    m.put(ACTOR+0xa,100,'h')
    if not scene:m.put(ACTOR+0x30,DRIVER)
    m.put(0x78a2f0,1003)
    m.put(0x7364dc,10)
    data = bytearray(destination.read_bytes())
    data[0x400:0x3ff400] = m.uc.mem_read(0x401000,0x3ff000)
    end = max(b+1 for a,b,_ in m.uc.mem_regions() if a >= 0x1000000)
    payload = bytearray(end-0x1000000)
    for a,b,_ in m.uc.mem_regions():
        if a >= 0x1000000:
            payload[a-0x1000000:b+1-0x1000000] = m.uc.mem_read(a,b+1-a)
    struct.pack_into('<H',data,0x86,3)
    struct.pack_into('<I',data,0x98+56,end-0x400000)
    struct.pack_into('<I',data,0x1a0+8,0x800000)
    struct.pack_into('<8sIIIIIIHHI',data,0x1c8,b'.ai',len(payload),0xc00000,len(payload),len(data),0,0,0,0,0xe0000060)
    data += payload
    code = bytearray()
    def emit(value):code.extend(value)
    def call(address):emit(b'\xe8'+struct.pack('<i',address-(0x801000+len(code)+5)))
    emit(b'\x6a\x0a\xff\x15'+struct.pack('<I',0x8001a0))
    emit(b'\x83\x3d'+struct.pack('<I',0x800f0c)+b'\x00\x74\xef')
    loop = len(code)
    emit(b'\xdb\xe3\xd9\x2d'+struct.pack('<I',0x800f00))
    emit(b'\x0f\xae\x15'+struct.pack('<I',0x800f10))
    # Vary speed, x87 precision and SSE mode through fixture controls, never
    # through the recorder or retail process.
    if scene:
        from controller293_strategy import CONTROLLER
        emit(b'\xb9'+struct.pack('<I',CONTROLLER)+b'\x68'+struct.pack('<I',bits(.02)))
        call(0x462430)
    else:
        emit(b'\xb9'+struct.pack('<I',DRIVER)+b'\x68'+struct.pack('<I',bits(.02)))
        call(0x45e970)
    emit(b'\xff\x05'+struct.pack('<I',0x800f04))
    emit(b'\xff\x05'+struct.pack('<I',0x7364dc))
    emit(b'\x81\x05'+struct.pack('<II',0x73ad34,9000))
    emit(b'\x6a\x0a\xff\x15'+struct.pack('<I',0x8001a0))
    emit(b'\xe9'+struct.pack('<i',loop-(len(code)+5)))
    data[0x400400:0x400400+len(code)] = code
    destination.write_bytes(data)
    return {'sha256':base.digest(destination),'synthetic_fixture':True}


def run(args):
    if args.out.exists() or args.out.resolve().is_relative_to(args.retail.resolve()):
        raise ValueError('Fresh non-retail output required')
    source = args.retail/'speed.exe'
    code = RetailCode(source)
    args.out.mkdir(parents=True)
    exe = args.out/'ai-fixture.exe'
    identity = create_fixture(source,exe,args.scene)
    process = subprocess.Popen([str(exe.resolve())],creationflags=subprocess.CREATE_NO_WINDOW)
    session = frida.attach(process.pid)
    control = session.create_script("rpc.exports={go(){ptr('0x800f0c').writeU32(1)},counter(){return ptr('0x800f04').readU32()},entry(){return Array.from(new Uint8Array(ptr('0x45e970').readByteArray(7)))},role(n){ptr('0x1100014').readPointer().add(4).writeU32(n)},cw(n){ptr('0x800f00').writeU16(n)}};")
    control.load()
    faults = session.create_script("const exit=new NativeFunction(Process.getModuleByName('kernel32.dll').getExportByName('ExitProcess'),'void',['uint32']);Process.setExceptionHandler(d=>{send({fault:String(d.address),context:d.context,memory:d.memory});exit(99);return false;});")
    faults.on('message',lambda message,data:print('FIXTURE-FAULT '+base.encoded(message),flush=True))
    faults.load()
    recorder = None
    def authenticate(target):
        return {'pid':target.pid,'creation_filetime':target.created,'sha256':base.EXE_SHA256,
                'executable':str(source),'synthetic_fixture':True,'fixture_sha256':identity['sha256']}
    try:
        with mock.patch.object(base.WindowsAPI,'processes',return_value=[process.pid]), \
             mock.patch.object(base.Process,'authenticate',authenticate):
            recorder = Recorder(args.out/'capture','SYNTHETIC AI FIXTURE')
            recorder.start()
            deadline = time.monotonic()+12
            ready = False
            while time.monotonic() < deadline:
                while not recorder.messages.empty():
                    item = recorder.messages.get()
                    if item[0] == 'status' and item[1].startswith('Recording sampled'):
                        ready = True
                    if item[0] in ('done','save_error'):
                        raise RuntimeError('Recorder ended before fixture: '+str(item))
                if ready:break
                time.sleep(.05)
            if not ready:raise RuntimeError('AI recorder did not attach')
            if not args.scene:control.exports_sync.role(1)
            control.exports_sync.go();time.sleep(.15)
            folders = list((args.out/'capture').glob('ai-*'))
            if len(folders) != 1 or (not args.scene and (folders[0]/'steps.jsonl').stat().st_size):
                raise RuntimeError('Non-rival role accepted')
            role_filter = control.exports_sync.counter() > 0
            control.exports_sync.role(2)
            deadline = time.monotonic()+30
            steps = []
            while time.monotonic() < deadline:
                if process.poll() is not None:raise RuntimeError('AI fixture crashed: '+str(process.returncode))
                path = folders[0]/'steps.jsonl'
                steps = [json.loads(line) for line in path.read_text('ascii').splitlines() if line]
                if len(steps) >= 8:break
                time.sleep(.05)
            if len(steps) < 8:
                raise RuntimeError('Insufficient native pairs: '+str(list(recorder.messages.queue)))
            control.exports_sync.cw(0x27f)
            time.sleep(.15)
            recorder.stop();recorder.thread.join(timeout=15)
            if recorder.thread.is_alive():raise RuntimeError('AI recorder did not drain')
            done = [item for item in recorder.messages.queue if item[0]=='done']
            if len(done)!=1 or not done[0][2]['complete_stop']:
                raise RuntimeError('AI host lifecycle incomplete: '+str(list(recorder.messages.queue)))
            capture = folders[0]
            records = [json.loads(line) for line in (capture/'steps.jsonl').read_text('ascii').splitlines()]
            results = [replay(code,r) for r in records]
            if not results or any(not r['passed'] for r in results):
                base.write_json(args.out/'failure.json',{'results':results,'records':records[:1]})
                raise RuntimeError('Native replay differs; see failure.json')
            restored = control.exports_sync.entry()==[0x83,0xec,0x3c,0xd9,0x44,0x24,0x40]
            before = control.exports_sync.counter();time.sleep(.1)
            advancing = control.exports_sync.counter()>before
            if not restored or not advancing:raise RuntimeError('AI detach damaged native execution')
            report = {'fixture':identity,'records':len(records),'native_reproduced':len(results),
                      'role_filter':role_filter if not args.scene else 'Not performed; driver variant tests this gate',
                      'restored_hook':restored,'continued_execution':advancing,
                      'capture':capture.name,'summary':done[0][2],
                      'cw_modes':sorted(set(struct.unpack_from('<H',bytes.fromhex(r['fxsave']))[0] for r in records)),
                      'scope':'Live generated fixture, not a driven retail race; port AI parity not measured.'}
            if args.watchdog:
                agent = session.create_script(AGENT.read_text('ascii'),runtime='qjs')
                seen = []
                agent.on('message',lambda message,data:seen.append(message))
                agent.load();agent.exports_sync.start(False)
                deadline = time.monotonic()+13
                timed_out = None
                while time.monotonic()<deadline:
                    status = agent.exports_sync.status()
                    if status['errors'] and not status['active']:
                        timed_out = status;break
                    time.sleep(.05)
                if not timed_out or timed_out['completed'] or not any(m.get('payload',{}).get('kind')=='begin' for m in seen):
                    raise RuntimeError('Native watchdog did not release an abandoned shadow')
                agent.exports_sync.cleanup();agent.unload()
                before = control.exports_sync.counter();time.sleep(.1)
                if control.exports_sync.counter()<=before:raise RuntimeError('Fixture did not advance after watchdog')
                report['watchdog']={'released':True,'state':timed_out,'continued_execution':True}
            base.write_json(args.out/'report.json',report)
            print(base.encoded(report),flush=True)
    finally:
        if recorder:
            recorder.stop();recorder.thread.join(timeout=15)
        if process.poll() is None:
            faults.unload();control.unload();session.detach();process.terminate()
        process.wait(timeout=5)


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--scene',action='store_true')
    parser.add_argument('--watchdog',action='store_true')
    run(parser.parse_args())

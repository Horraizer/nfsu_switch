"""Compare native results with/without the world listener on copied x86 fixtures."""
import argparse,hashlib,json,struct,subprocess,time
from pathlib import Path
import frida
import original_recorder as base
from unittest import mock
from original_physics_smoke import create_fixture as physics_fixture
from original_ai_smoke import create_fixture as ai_fixture
from capstone import Cs,CS_ARCH_X86,CS_MODE_32

AGENT=Path(__file__).with_name('original_world_agent.js')

def finite_fixture(retail,path,kind,count):
    source=retail/'speed.exe'
    if kind=='physics':identity=physics_fixture(source,path);actor=0x805000;system=0x806008;target=0x46aa40
    else:
        ai_fixture(source,path,kind=='scene')
        from physics256_native import ACTOR,DRIVER
        from controller293_strategy import CONTROLLER
        actor=ACTOR;target=0x462430 if kind=='scene'else 0x45e970
    data=bytearray(path.read_bytes());code=bytearray()
    def emit(b):code.extend(b)
    def call(address):emit(b'\xe8'+struct.pack('<i',address-(0x801000+len(code)+5)))
    def mov(reg,value):emit(bytes([0xb8+reg])+struct.pack('<I',value))
    wait=len(code);emit(b'\x6a\x0a\xff\x15'+struct.pack('<I',0x8001a0))
    emit(b'\x83\x3d'+struct.pack('<I',0x800f0c)+b'\x00')
    emit(b'\x0f\x84'+struct.pack('<i',wait-(0x801000+len(code)+6-0x801000)))
    emit(b'\xdb\xe3\xd9\x2d'+struct.pack('<I',0x800f00))
    emit(b'\x0f\xae\x15'+struct.pack('<I',0x800f10))
    for reg in(0,2,3,5,6,7):mov(reg,0x202000+reg)
    if kind=='physics':emit(b'\x68'+struct.pack('<I',0x3c088889)+b'\x68'+struct.pack('<I',system));mov(1,0x202001)
    else:mov(1,CONTROLLER if kind=='scene'else DRIVER);emit(b'\x68'+struct.pack('<f',.02))
    call(target)
    if kind=='scene':
        from controller293_scene import HUMAN
        from world_body_trace import Image
        image=Image(data);human_driver=struct.unpack('<I',image.read(HUMAN+0x30,4))[0]
        assert struct.unpack('<I',image.read(human_driver,4))[0]==0x6c8ad0
        assert struct.unpack('<I',image.read(human_driver+0x26c,4))[0]==HUMAN
        for method,event,value in[(0x461840,0x87,255),(0x4618b0,0x89,255),(0x461da0,0x8c,0),
                (0x4619c0,0x8f,0),(0x461e10,0x91,0),(0x461e70,0x93,0),
                (0x461f40,0x94,0),(0x461ff0,0x95,0)]:
            for arg in(human_driver,value,event,0):emit(b'\x68'+struct.pack('<I',arg))
            call(method);emit(b'\x83\xc4\x10')
    # Snapshot caller-visible CPU/FPU before the harness changes flags/registers.
    emit(b'\x9c\x60\x89\xe6\xbf'+struct.pack('<I',0x800b00)+b'\xb9\x09\x00\x00\x00\xf3\xa5\x61\x9d')
    emit(b'\x0f\xae\x05'+struct.pack('<I',0x800c00))
    for at in(0x800f04,0x7364dc):emit(b'\xff\x05'+struct.pack('<I',at))
    emit(b'\x81\x05'+struct.pack('<II',0x73ad34,9000))
    emit(b'\x81\x3d'+struct.pack('<II',0x800f04,count))
    emit(b'\x0f\x83'+struct.pack('<i',0))
    branch=len(code)-4
    emit(b'\xe9'+struct.pack('<i',wait-len(code)-5))
    done=len(code);struct.pack_into('<i',code,branch,done-branch-4)
    emit(b'\xc7\x05'+struct.pack('<II',0x800f0c,0))
    emit(b'\xe9'+struct.pack('<i',wait-len(code)-5))
    data[0x400400:0x400400+len(code)]=code
    path.write_bytes(data)
    return actor,target

def run_case(path,kind,actor,target,out,hooked,cw):
    process=subprocess.Popen([str(path.resolve())],creationflags=subprocess.CREATE_NO_WINDOW)
    session=script=control=None;messages=[]
    try:
        session=frida.attach(process.pid)
        control=session.create_script("""Process.setExceptionHandler(d=>{send({exception:d.type,address:d.address,context:d.context,memory:d.memory});return false;});rpc.exports={
            go(cw){ptr('0x800f00').writeU16(cw);ptr('0x800f0c').writeU32(1)},
            counter(){return ptr('0x800f04').readU32()},
            read(a,n){return Array.from(new Uint8Array(ptr(a).readByteArray(n)))},
            actor(a){const x=ptr(a);return [x.add(0x30).readU32(),x.add(0x34).readU32()]}
        };""")
        control.on('message',lambda message,data:print('FIXTURE_DIAGNOSTIC',message,flush=True))
        control.load()
        if hooked:
            script=session.create_script(AGENT.read_text(encoding='ascii'),runtime='qjs')
            # Do not synchronously call an RPC from a Frida callback: drain acknowledgements below.
            script.on('message',lambda message,data:messages.append((message,data)))
            script.load();descriptor=script.exports_sync.start(True)
        control.exports_sync.go(cw);deadline=time.monotonic()+15;acked=0
        while time.monotonic()<deadline:
            while acked<len(messages):
                message,data=messages[acked];acked+=1
                if message.get('type')=='send'and message['payload'].get('kind')=='binary':script.exports_sync.acknowledge(message['payload']['batch'])
            if control.exports_sync.counter()>=24:break
            time.sleep(.01)
        assert control.exports_sync.counter()==24,'Fixture did not finish exactly 24 native calls'
        if hooked:
            state=script.exports_sync.stop();deadline=time.monotonic()+3
            while state['active']and time.monotonic()<deadline:time.sleep(.01);state=script.exports_sync.stop()
            state=script.exports_sync.detach();time.sleep(.05)
            while acked<len(messages):
                message,data=messages[acked];acked+=1
                if message.get('type')=='send'and message['payload'].get('kind')=='binary':script.exports_sync.acknowledge(message['payload']['batch'])
            state=script.exports_sync.status()
            assert not state['active']and not state['overflows']and not state['errors'],state
            if kind=='physics':assert state['events']==0 and state['snapshots']==0,state
            else:assert state['events']>0 and state['snapshots']>=48,state
            assert all(m.get('type')=='send'and m['payload'].get('kind')=='binary'for m,d in messages),messages[:2]
            events=b''.join(d for m,d in messages if m['payload']['stream']=='calls')
            snapshots=b''.join(d for m,d in messages if m['payload']['stream']=='snapshots')
            assert len(events)==state['events']*160 and len(snapshots)==state['snapshots']*5376
            (out/('calls-'+kind+'-'+str(cw)+'.bin')).write_bytes(events)
            (out/('snapshots-'+kind+'-'+str(cw)+'.bin')).write_bytes(snapshots)
            (out/('state-'+kind+'-'+str(cw)+'.json')).write_text(json.dumps(state,indent=2)+'\n',encoding='ascii')
            # Verify all instrumentation is removed while the fixture remains live.
            original=Path('E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground/speed.exe')
            from world_body_trace import Image
            assert bytes(control.exports_sync.read(target,8))==Image(original.read_bytes()).read(target,8)
        driver,physics=control.exports_sync.actor(actor)
        blocks={'actor':bytes(control.exports_sync.read(actor,0x400)),
                'driver':bytes(control.exports_sync.read(driver,0x1e8)),
                'registers':bytes(control.exports_sync.read(0x800b00,36)),
                'fxsave':bytes(control.exports_sync.read(0x800c00,512))}
        if kind=='physics':blocks['rigid']=bytes(control.exports_sync.read(0x803000,0x200))
        if kind=='scene':
            from controller293_scene import HUMAN
            human_driver,_=control.exports_sync.actor(HUMAN)
            blocks['human_actor']=bytes(control.exports_sync.read(HUMAN,0x400))
            blocks['human_driver']=bytes(control.exports_sync.read(human_driver,0x2c0))
        # ESP is process-specific. Everything else in the controlled caller is deterministic.
        registers=bytearray(blocks['registers']);registers[12:16]=bytes(4);blocks['registers']=bytes(registers)
        # FXSAVE reserved bytes are unspecified; compare architectural fields and ST/XMM payload.
        blocks['fxsave']=blocks['fxsave'][:416]
        for name,value in blocks.items():(out/(kind+'-'+str(cw)+'-'+('hooked'if hooked else'plain')+'-'+name+'.bin')).write_bytes(value)
        return blocks
    finally:
        if script is not None:
            try:script.unload()
            except frida.InvalidOperationError:pass
        if session is not None:
            try:session.detach()
            except frida.InvalidOperationError:pass
        process.terminate();process.wait(timeout=5)

def host_lifecycle(retail,out):
    from original_world_recorder import Recorder
    from original_world_report import report
    path=out/'host-scene-fixture.exe';actor,target=finite_fixture(retail,path,'scene',24)
    process=subprocess.Popen([str(path.resolve())],creationflags=subprocess.CREATE_NO_WINDOW)
    session=control=recorder=None
    try:
        session=frida.attach(process.pid)
        control=session.create_script("""rpc.exports={go(){ptr('0x800f00').writeU16(127);ptr('0x800f0c').writeU32(1)},counter(){return ptr('0x800f04').readU32()},read(a){return Array.from(new Uint8Array(ptr(a).readByteArray(8)))}}""")
        control.load()
        def authenticate(target):
            return dict(pid=target.pid,creation_filetime=target.created,sha256=base.EXE_SHA256,
                synthetic_fixture=True,fixture_sha256=base.digest(path),executable=str(path.resolve()))
        with mock.patch.object(base.WindowsAPI,'processes',return_value=[process.pid]),mock.patch.object(base.Process,'authenticate',authenticate):
            recorder=Recorder(out/'host-capture','SYNTHETIC WORLD HOST LIFECYCLE')
            recorder.start();deadline=time.monotonic()+15;ready=False
            while time.monotonic()<deadline:
                while not recorder.messages.empty():
                    item=recorder.messages.get()
                    if item[0]=='status'and item[1].startswith('Recording world calls'):ready=True
                    if item[0]in('done','save_error'):raise RuntimeError('Host ended before fixture: '+str(item))
                if ready:break
                time.sleep(.02)
            assert ready,'Host did not attach to isolated fixture'
            control.exports_sync.go();deadline=time.monotonic()+15
            while time.monotonic()<deadline and control.exports_sync.counter()<24:time.sleep(.02)
            assert control.exports_sync.counter()==24
            recorder.stop();recorder.thread.join(timeout=15);assert not recorder.thread.is_alive(),'Host did not stop'
        folders=[p for p in(out/'host-capture').iterdir()if p.is_dir()];assert len(folders)==1
        summary=json.loads((folders[0]/'summary.json').read_text(encoding='ascii'))
        assert summary['complete_stop']and summary['counts']['calls']>0 and summary['counts']['snapshots']>=48,summary
        state=summary['capture_state'];assert not state['errors']and not state['overflows']and not state['active'],state
        assert state['events']==summary['counts']['calls']and state['snapshots']==summary['counts']['snapshots']
        from world_body_trace import Image
        assert bytes(control.exports_sync.read(target))==Image((retail/'speed.exe').read_bytes()).read(target,8)
        control.exports_sync.go();time.sleep(.1);assert control.exports_sync.counter()>24,'Fixture stopped after hook removal'
        result=report(folders[0],out/'host-report');assert not result['unbalanced']and not result['open_calls']
        assert result['input_events']==24*8,'All eight actual human input callbacks must be observed'
        print('PASS production host: attach, world polling, native calls/states, binary validation, clean drain/detach, verified ZIP, offline report and continued fixture execution',flush=True)
    finally:
        if recorder is not None:recorder.stop();recorder.thread.join(timeout=5)
        if session is not None:
            try:session.detach()
            except frida.InvalidOperationError:pass
        process.terminate();process.wait(timeout=5)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--retail',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);ap.add_argument('--kind',choices=['physics','driver','scene','all'],default='all');ap.add_argument('--host',action='store_true');a=ap.parse_args()
    assert base.digest(a.retail/'speed.exe')==base.EXE_SHA256
    assert not a.out.exists()and not a.out.resolve().is_relative_to(a.retail.resolve());a.out.mkdir(parents=True)
    report=[]
    for kind in(['physics','driver','scene']if a.kind=='all'else[a.kind]):
        fixture=a.out/(kind+'-fixture.exe');actor,target=finite_fixture(a.retail,fixture,kind,24)
        for cw in(0x7f,0x27f):
            plain=run_case(fixture,kind,actor,target,a.out,False,cw)
            hooked=run_case(fixture,kind,actor,target,a.out,True,cw)
            mismatches={name:[i for i,(x,y)in enumerate(zip(value,hooked[name]))if x!=y]for name,value in plain.items()if value!=hooked[name]}
            row=dict(kind=kind,cw=cw,mismatches=mismatches,passed=not mismatches);report.append(row)
            (a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
            print('PASS'if not mismatches else 'MISMATCH',kind,hex(cw),mismatches,flush=True)
    assert all(row['passed']for row in report),'Raw differences remain; see report.json'
    if a.host:host_lifecycle(a.retail,a.out)
if __name__=='__main__':main()

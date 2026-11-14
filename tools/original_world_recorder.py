"""Bounded chronological PC world telemetry, without shadow-emulation pauses."""
import argparse,hashlib,json,queue,struct,threading,time,uuid,zipfile
from datetime import datetime,timezone
from pathlib import Path
import original_recorder as base

AGENT=Path(__file__).with_name('original_world_agent.js')
CALL=struct.Struct('<40I')
SNAP=struct.Struct('<1344I')
MAX_BYTES=16*1024*1024*1024
MAX_SECONDS=1800
PRIMARY_VIEW=0x740500
CURRENT_TRACK=0x78A2F0
CHASE_VTABLE=0x6C7FC8


def sample_chase_movers(sampler, sample):
    # 42C450 restores player+280 to player+27C. The player's actor
    # back-reference to this owner is at +18 (used by 52C3A0).
    actor=next((a.get('address') for a in sample.get('actors',()) if a.get('role')==1),None)
    owner=sampler.u32(actor+0x18) if base.pointer(actor) else None
    if not base.pointer(owner):
        return None
    raw=sampler.read(owner+0x27c,12)
    if raw is None or len(raw)!=12:
        return None
    active,saved,_=struct.unpack('<3I',raw)
    def read_mover(address):
        if not base.pointer(address):
            return None
        data=sampler.read(address,0x6c)
        if data is None or len(data)!=0x6c:
            return None
        mover=dict(address=address,vtable=base.field(data,0,'I'),type=base.field(data,0xc,'I'))
        if mover['vtable']==CHASE_VTABLE:
            mover.update(rear=base.field(data,0x34,'f'),rear_blend=base.field(data,0x38,'f'),
                         tilt_gate=base.field(data,0x44,'H'),body_pitch=base.field(data,0x54,'H'),
                         filtered_pitch=base.field(data,0x58,'H'),support_weight=base.field(data,0x68,'f'))
        return mover
    current=read_mover(active)
    previous=read_mover(saved)
    if sampler.read(owner+0x27c,12)!=raw:
        return None
    return dict(owner=owner,active=current,saved=previous,
                acceleration=base.field(sampler.read(actor+0x80,12),0,'3f'))


def sample_landing_camera(sampler, sample):
    frame=sampler.u32(base.FRAME)
    clock=sampler.u32(base.CLOCK)
    track=sampler.u32(CURRENT_TRACK)
    view=sampler.u32(PRIMARY_VIEW)
    raw=sampler.read(view,0x200) if base.pointer(view) else None
    movers=sample_chase_movers(sampler,sample)
    # 575040 applies this player-zero pulse after the mover's matrix.
    state=sampler.read(0x73b220,0x20)
    shake=dict(direction=base.field(state,0,'3f'),curve=base.field(state,0x10,'I'),
               phase=base.field(state,0x14,'f'),duration=base.field(state,0x18,'f'),
               pulse_count=base.field(state,0x1c,'f')) if state is not None else None
    world=sample.get('world')
    scale=base.field(sampler.read(world+0x2e0,4),0,'f') if base.pointer(world) else None
    world_dt=base.field(sampler.read(0x73457c,4),0,'f')
    stable=(None not in (frame,clock) and not sample['cross_frame'] and
            frame==sample['frame_after']==sampler.u32(base.FRAME) and
            clock==sample['clock_after']==sampler.u32(base.CLOCK) and
            view==sampler.u32(PRIMARY_VIEW))
    return dict(frame=frame,clock=clock,track=track,view=view,cross_frame=not stable,
                eye=base.field(raw,0x40,'3f'),eye_velocity=base.field(raw,0x1e0,'3f'),
                view_matrix=base.field(raw,0,'16f'),raw_hex=raw.hex() if raw is not None else None,
                movers=movers if stable else None,shake=shake if stable else None,
                world_dt=world_dt if stable else None,time_scale=scale if stable else None)

def package(folder):
    # Full world states can span several GiB. Verify archives in bounded chunks,
    # preserving the existing raw tape format and source session on every error.
    files=sorted(p for p in folder.iterdir()if p.is_file()and p.name!='manifest.json')
    manifest={p.name:dict(bytes=p.stat().st_size,sha256=base.digest(p))for p in files}
    base.write_json(folder/'manifest.json',manifest)
    temporary=folder.with_suffix('.partial.zip');target=folder.with_suffix('.zip')
    if temporary.exists()or target.exists():raise FileExistsError('Archive already exists')
    with zipfile.ZipFile(temporary,'x',compression=zipfile.ZIP_DEFLATED)as archive:
        for path in files+[folder/'manifest.json']:archive.write(path,arcname=path.name)
    with zipfile.ZipFile(temporary)as archive:
        if archive.testzip()is not None:raise ValueError('Archive CRC failed')
        for name,pin in manifest.items():
            digest=hashlib.sha256();size=0
            with archive.open(name)as source:
                while True:
                    data=source.read(1024*1024)
                    if not data:break
                    size+=len(data);digest.update(data)
            if size!=pin['bytes']or digest.hexdigest()!=pin['sha256']:raise ValueError('Archive verification failed: '+name)
    temporary.rename(target);return target

def signed(value):return value if value<0x80000000 else value-0x100000000

def decode_snapshot(blob):
    if len(blob)!=SNAP.size:raise ValueError('Wrong native snapshot size')
    w=SNAP.unpack(blob);valid=w[15]
    actor=blob[64:1088];driver=blob[1088:1088+w[1183]]if valid&2 else None
    result=base.decode_actor(w[4],actor,None,driver)
    result.update(seq=w[0],call=w[1],kind=w[2],phase=w[3],role=w[6],frame=w[7],clock=w[8],
        car_type=w[1174],ai_context_raw=signed(w[1175]),config_readable=True,
        category=w[1176]if valid&32 else None,physics_selector=w[1177],controller_selector=w[1178],
        stage=w[1179],track=w[1180],mode=w[1181],valid_bits=valid,
        driver_bytes=w[1183],controller_bytes=w[1184],action_address=w[1185],route_address=w[1186],
        strategy_address=w[1187],world_rng=w[1188],strategy_mode_raw=signed(w[1190]),setup_rng=w[1213])
    if valid&32:result.update(performance_levels=list(w[911:922]),unique_items=list(w[922:933]))
    if valid&64:result['drag_input_mode']=w[1182]
    result['heading_raw']=base.field(actor,0x3b4,'H')
    if valid&4096:
        result['engine']={'address':w[1214],'angular_velocity':base.field(blob,4864+0x14,'f'),
            'state_hex':blob[4864:5024].hex()}
    if valid&8192:
        box=blob[5024:5252]
        result['gearbox']={'address':w[1215],'gear':base.field(box,0x3c,'i'),
            'requested_gear':base.field(box,0x38,'i'),'mode':base.field(box,0x44,'i'),
            'shift_quality':base.field(box,0x68,'f'),'launch_target':base.field(box,0x84,'f'),
            'state_hex':box.hex()}
    if valid&8:
        rigid=blob[1920:2432]
        result['rigid']={'address':w[11],'position':base.field(rigid,0x20,'3f'),
            'momentum':base.field(rigid,0x70,'3f'),'velocity':base.field(rigid,0xa0,'3f'),
            'angular_velocity':base.field(rigid,0xc0,'3f')}
    if valid&128:
        result['controller']={'route_index_raw':w[608+0x478//4],
            'route_scale':base.field(blob,2432+0x4b8,'f')}
    if valid&256:
        action=blob[3732:4232]
        result['action']={'vtable':base.field(action,0,'I'),'desired_speed':base.field(action,0x1e4,'f'),
            'duration':base.field(action,0x1e8,'f'),'auxiliary':base.field(action,0x1ec,'f'),
            'route_enabled':base.field(action,0x1f0,'B'),'enabled':base.field(action,0x1f1,'B')}
    if driver is not None and len(driver)>=0x210:
        result['demand']={'speed':base.field(driver,0x200,'f'),'duration':base.field(driver,0x204,'f'),
            'auxiliary':base.field(driver,0x208,'f'),'enabled':base.field(driver,0x20c,'B')}
    result['driver_vtable']=base.field(driver,0,'I')
    result['controller_vtable']=w[608]if valid&16 else None
    return result

class BinaryWriter:
    def __init__(self,folder,max_bytes=MAX_BYTES):
        if type(max_bytes)is not int or max_bytes<=0:raise ValueError('Invalid capture byte ceiling')
        self.files={name:(folder/(name+'.bin')).open('xb')for name in('calls','snapshots')}
        self.counts={'calls':0,'snapshots':0};self.batches=0;self.bytes=0;self.max_bytes=max_bytes
    def accept(self,payload,data):
        stream=payload.get('stream');fmt={'calls':CALL,'snapshots':SNAP}.get(stream)
        if fmt is None or payload.get('kind')!='binary' or payload.get('batch')!=self.batches:raise ValueError('Invalid/out-of-order batch')
        count=payload.get('count');first=payload.get('first')
        if type(count)is not int or not 0<count<=16384 or first!=self.counts[stream]:raise ValueError('Missing/duplicate stream records')
        if payload.get('words')!=fmt.size//4 or data is None or len(data)!=count*fmt.size:raise ValueError('Wrong binary batch size')
        if self.bytes+len(data)>self.max_bytes:raise ValueError('Capture byte ceiling reached')
        for at in range(0,len(data),fmt.size):
            row=fmt.unpack_from(data,at)
            if stream=='calls'and row[0]!=first+at//fmt.size+1:raise ValueError('Non-contiguous native call sequence')
            if row[2]>=27 or row[2]==5 or row[3]not in(1,2)or row[6]not in(1,2,3,4):raise ValueError('Invalid native record identity')
            if stream=='snapshots'and not 0<row[0]<=self.counts['calls']:raise ValueError('Snapshot refers to an absent call')
        self.files[stream].write(data);self.counts[stream]+=count;self.bytes+=len(data);self.batches+=1
    def flush(self):
        for file in self.files.values():file.flush()
    def close(self):
        for file in self.files.values():file.close()

class Recorder:
    def __init__(self,output,note='',status_file=None,max_bytes=MAX_BYTES,landing_camera=False):
        self.landing_camera=landing_camera
        if type(max_bytes)is not int or max_bytes<=0:raise ValueError('Invalid capture byte ceiling')
        self.max_bytes=max_bytes
        self.output=Path(output);self.note=note;self.status_file=Path(status_file)if status_file else None
        self.messages=queue.Queue();self.incoming=queue.Queue(maxsize=64)
        self.stopping=threading.Event();self.detached=threading.Event();self.overflow=threading.Event()
        self.thread=threading.Thread(target=self.run,daemon=False)
        self.summary=None
    def start(self):self.thread.start()
    def stop(self):self.stopping.set()
    def status(self,message,**extra):
        self.messages.put(('status',message))
        if self.status_file:
            base.write_json(self.status_file,dict(utc=base.utc(),status=message,**extra))
    def receive(self,message,data):
        try:self.incoming.put_nowait((message,data))
        except queue.Full:self.overflow.set()
    def drain(self,script,writer,event_file):
        while True:
            try:message,data=self.incoming.get_nowait()
            except queue.Empty:return
            if message.get('type')!='send':raise RuntimeError('Native agent error: '+base.encoded(message))
            payload=message['payload']
            if payload.get('kind')!='binary':
                event_file.write(base.encoded(payload)+'\n');event_file.flush()
                raise RuntimeError('Capture stopped: '+base.encoded(payload))
            writer.accept(payload,data)
            if script is not None:script.exports_sync.acknowledge(payload['batch'])
    def run(self):
        folder=process=session=script=writer=polls=raw=events=None
        state=None;reason='Stopped by user';clean=False;samples=0;cross_frames=0;missed_polls=0
        try:
            import frida
            api=base.WindowsAPI();self.status('Waiting for original PC 1.4 USA speed.exe')
            waiting=time.monotonic()
            while not self.stopping.wait(.25):
                if time.monotonic()-waiting>MAX_SECONDS:raise RuntimeError('Game launch wait expired')
                pids=api.processes()
                if len(pids)>1:raise RuntimeError('More than one speed.exe process is running')
                if pids:
                    process=base.Process(api,pids[0]);identity=process.authenticate();break
            if process is None:return
            name='world-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-')+uuid.uuid4().hex[:8]
            folder=self.output/name;base.make_directory(folder,exclusive=True)
            sources=(Path(__file__),AGENT,Path(base.__file__))
            metadata=dict(schema='nfsu-world-trace-1',created_utc=base.utc(),source=identity,note=self.note,
                files={p.name:base.digest(p)for p in sources},frida=frida.__version__,poll_hz=60,clock_hz=4000,
                landing_camera=self.landing_camera,
                synthetic_fixture=bool(identity.get('synthetic_fixture')),call_words=40,snapshot_words=1344,byte_ceiling=self.max_bytes,time_ceiling_seconds=MAX_SECONDS,
                boundary='Chronological relevant driver/controller/outer-physics/path/action/RNG calls and vehicle states; native stages2..5 plus whole-world polling',
                limitations=['Not an all-instruction/all-function trace or a complete deterministic world replay.',
                    'Instrumentation affects timing; this is not an FPS benchmark.',
                    'The inner rigid integrator is deliberately not hooked; per-slice call order is outside coverage.',
                    '60Hz world polling is non-atomic; cross_frame and missed_poll counts expose gaps.',
                    'Native state snapshots are on the observed world thread; foreign callback counts are reported.',
                    'No game assets, physics parameters or user input are changed; function entry interception is temporary.'])
            base.write_json(folder/'metadata.json',metadata)
            writer=BinaryWriter(folder,self.max_bytes);polls=(folder/'samples.jsonl').open('x',encoding='ascii')
            raw=(folder/'memory.bin').open('xb');events=(folder/'events.jsonl').open('x',encoding='ascii')
            session=frida.get_local_device().attach(process.pid);session.on('detached',lambda *args:self.detached.set())
            check=base.Process(api,process.pid)
            try:assert check.created==process.created and process.alive(),'PID changed before attachment'
            finally:check.close()
            script=session.create_script(AGENT.read_text(encoding='ascii'),runtime='qjs')
            script.on('message',self.receive);script.load();descriptor=script.exports_sync.start(False)
            base.write_json(folder/'instrumentation.json',descriptor)
            sampler=base.Sampler(process);started=time.monotonic();next_poll=started;last_raw=-30.;last_notice=-1.;last_stage=object()
            self.status('Recording world calls, controls, acceleration, lane paths and traffic',folder=str(folder),pid=process.pid)
            while not self.stopping.is_set():
                if self.overflow.is_set():raise RuntimeError('Host queue overflow; capture incomplete')
                if self.detached.is_set()or not process.alive():reason='Original game exited; final in-flight records may be absent';break
                self.drain(script,writer,events);now=time.monotonic();elapsed=now-started
                if elapsed>=MAX_SECONDS:reason='Recording time ceiling reached';break
                if now>=next_poll:
                    if now-next_poll>1/60:missed_polls+=int((now-next_poll)*60)
                    sample=sampler.sample(samples,elapsed);samples+=1;cross_frames+=int(sample['cross_frame'])
                    if self.landing_camera and sample['stage'] in (2,3,4,5):
                        sample['camera']=sample_landing_camera(sampler,sample)
                    polls.write(base.encoded(sample)+'\n')
                    if sample['stage']!=last_stage or elapsed-last_raw>=30:
                        sampler.context();sampler.related_blocks(sample['actors'])
                        for address,blob in sampler.raw:raw.write(base.RAW_HEADER.pack(elapsed,address,len(blob)));raw.write(blob)
                        last_raw=elapsed;last_stage=sample['stage']
                    next_poll=now+1/60
                if elapsed-last_notice>=1:
                    state=script.exports_sync.status();writer.flush();polls.flush();raw.flush();events.flush()
                    if not state['enabled']or state['overflows']or state['errors']:raise RuntimeError('Native capture disabled: '+base.encoded(state))
                    if sum(p.stat().st_size for p in folder.iterdir()if p.is_file())>self.max_bytes:raise RuntimeError('Session byte ceiling reached')
                    self.status('Recording: '+str(writer.counts['calls'])+' calls, '+str(writer.counts['snapshots'])+' states; '+str(writer.bytes//(1024*1024))+' / '+str(self.max_bytes//(1024*1024))+' MiB',folder=str(folder),pid=process.pid,state=state)
                    last_notice=elapsed
                self.stopping.wait(min(.005,max(0,next_poll-time.monotonic())))
            if not self.detached.is_set()and process.alive():
                state=script.exports_sync.stop();deadline=time.monotonic()+5
                while time.monotonic()<deadline:
                    self.drain(script,writer,events);state=script.exports_sync.stop()
                    if not state['active']and state['events']==writer.counts['calls']and state['snapshots']==writer.counts['snapshots']:break
                    time.sleep(.01)
                if state['active']or state['events']!=writer.counts['calls']or state['snapshots']!=writer.counts['snapshots']:raise RuntimeError('Native capture did not drain completely')
                state=script.exports_sync.detach();self.drain(script,writer,events)
                if state['overflows']or state['errors']:raise RuntimeError('Native capture reported gaps')
                script.unload();script=None;session.detach();session=None;clean=True
            else:
                # Already delivered records are retained; never invent a drained tail.
                self.drain(None,writer,events)
        except Exception as error:
            reason='Recording error: '+str(error)
        finally:
            if script is not None and not self.detached.is_set():
                try:
                    state=script.exports_sync.stop()
                    # Do not free a native listener while an accepted call owns it.
                    while state['active']and process.alive()and not self.detached.wait(.02):
                        state=script.exports_sync.stop()
                    if not self.detached.is_set():script.exports_sync.detach()
                    script.unload()
                except Exception:pass
            if session is not None:
                try:session.detach()
                except Exception:pass
            if process is not None:process.close()
            for f in(polls,raw,events):
                if f is not None:f.close()
            if writer is not None:writer.close()
            summary=dict(reason=reason,complete_stop=clean,counts=writer.counts if writer else {},capture_state=state,
                coverage='Relevant call boundaries plus per-step vehicle states; not a complete deterministic world replay.',
                world_polls=samples,cross_frame_polls=cross_frames,missed_poll_intervals=missed_polls)
            self.summary=summary
            if folder is not None:
                base.write_json(folder/'summary.json',summary)
                try:
                    archive=package(folder);self.messages.put(('done',str(archive),summary))
                    self.status('Saved '+str(archive),summary=summary)
                except Exception as error:self.messages.put(('save_error',str(error)+'; raw session: '+str(folder)))
            else:
                self.status(reason)
                self.messages.put(('done','No session created',summary))

class App(base.App):
    def __init__(self,window,output,max_bytes=MAX_BYTES,landing_camera=False):
        super().__init__(window,output);window.title('NFSU Original World Recorder')
        self.max_bytes=max_bytes
        self.landing_camera=landing_camera
        if landing_camera:window.title('NFSU World Recorder - Stadium Landing and Traffic')
        labels=[child for child in window.winfo_children()[0].winfo_children()if child.winfo_class()=='TLabel']
        labels[0].configure(text='NFS Underground - World Call/State Recorder')
        labels[1].configure(text='Start, play the original, then Stop.\nTemporary native instrumentation; no game control or asset writes.')
    def start(self):
        if self.recorder and self.recorder.thread.is_alive():return
        self.recorder=Recorder(self.output,self.label.get()[:500],max_bytes=self.max_bytes,
                               landing_camera=self.landing_camera)
        self.start_button.configure(state='disabled')
        self.stop_button.configure(state='normal');self.recorder.start()

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',type=Path);ap.add_argument('--headless',action='store_true');ap.add_argument('--note',default='');ap.add_argument('--status-file',type=Path);ap.add_argument('--stop-file',type=Path)
    ap.add_argument('--max-gib',type=int,default=16,help='Capture size ceiling, 1..32 GiB (default: 16)')
    ap.add_argument('--landing-camera',action='store_true',help='Include the primary rendered view with each world poll')
    a=ap.parse_args()
    if not 1<=a.max_gib<=32:ap.error('--max-gib must be between 1 and 32')
    max_bytes=a.max_gib*1024*1024*1024
    output=a.output or base.special_folder(5)/'NFSU-Recordings';base.make_directory(output)
    if a.headless:
        recorder=Recorder(output,a.note,a.status_file,max_bytes,a.landing_camera);recorder.start()
        try:
            while recorder.thread.is_alive():
                if a.stop_file and a.stop_file.exists():recorder.stop()
                while not recorder.messages.empty():print(base.encoded(recorder.messages.get()),flush=True)
                recorder.thread.join(.25)
        except KeyboardInterrupt:recorder.stop();recorder.thread.join()
        while not recorder.messages.empty():print(base.encoded(recorder.messages.get()),flush=True)
        if recorder.summary and recorder.summary['reason'].startswith('Recording error:'):raise SystemExit(1)
    else:
        import tkinter as tk
        window=tk.Tk();App(window,output,max_bytes,a.landing_camera);window.mainloop()
if __name__=='__main__':main()

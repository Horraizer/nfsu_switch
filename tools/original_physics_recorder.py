"""User-operated, instrumented single-car rigid integration recorder."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import queue
import shutil
import struct
import subprocess
import sys
import threading
import time
import uuid
import zipfile

import original_recorder as base

VERSION = '0.1'
AGENT = Path(__file__).with_name('original_physics_agent.js')
MAX_BYTES = 512 * 1024 * 1024
REQUIRED = {'seq', 'thread', 'rigid', 'properties', 'dt_bits', 'fpcw', 'mxcsr',
            'pre_hex', 'post_hex', 'properties_hex', 'globals'}


def validate_record(record, expected):
    if not isinstance(record, dict) or not REQUIRED <= record.keys():
        raise ValueError('Incomplete integration record')
    if record['seq'] != expected:
        raise ValueError('Missing, duplicated or out-of-order integration step')
    for name, size in (('pre_hex', 0x200), ('post_hex', 0x200), ('properties_hex', 0x54)):
        if len(bytes.fromhex(record[name])) != size:
            raise ValueError('Invalid block length: ' + name)
    for name, size in (('rigid', 0x200), ('properties', 0x54)):
        address = record[name]
        if not isinstance(address, int) or not 0x10000 <= address < 0x7fff0000 - size:
            raise ValueError('Invalid block address: ' + name)
    pre = bytes.fromhex(record['pre_hex'])
    if struct.unpack_from('<I', pre, 0x14)[0] != record['properties']:
        raise ValueError('Property pointer differs from input body')
    expected_globals = {0x6b5b54: 0x38, 0x6f08c8: 0x40, 0x704478: 0x44,
                        0x6d4a00: 0x100, 0x7000ac: 4, 0x6f0738: 4}
    if len(record['globals']) != len(expected_globals):
        raise ValueError('Missing/duplicated globals')
    seen = set()
    for address, value in record['globals']:
        if address in seen or address not in expected_globals or len(bytes.fromhex(value)) != expected_globals[address]:
            raise ValueError('Invalid global block')
        seen.add(address)
    if not isinstance(record['mxcsr'], int) or not 0 <= record['mxcsr'] <= 0xffff:
        raise ValueError('Invalid SSE control register')
    dt = struct.unpack('<f', struct.pack('<I', record['dt_bits']))[0]
    if not 0 < dt <= 1:
        raise ValueError('Invalid solver interval')
    if record['fpcw'] not in (0x7f, 0x27f, 0x37f):
        raise ValueError('Unsupported x87 control word')
    return record


class Writer:
    def __init__(self, folder):
        self.path = folder / 'steps.jsonl'
        self.stream = self.path.open('x', encoding='ascii', newline='\n')
        self.steps = self.batches = self.bytes = 0

    def batch(self, payload):
        if payload['batch'] != self.batches or not 1 <= len(payload['records']) <= 16:
            raise ValueError('Invalid batch sequence/size')
        # Validate the whole batch before accepting any of it.
        lines = []
        for i, record in enumerate(payload['records']):
            validate_record(record, self.steps + i)
            lines.append(base.encoded(record) + '\n')
        data = ''.join(lines)
        if self.bytes + len(data) > MAX_BYTES:
            raise ValueError('Session byte ceiling reached')
        self.stream.write(data)
        self.stream.flush()
        self.bytes += len(data)
        self.steps += len(lines)
        self.batches += 1

    def close(self):
        self.stream.close()


def package(folder):
    files = sorted(p for p in folder.iterdir() if p.is_file() and p.name != 'manifest.json')
    manifest = {p.name: {'bytes': p.stat().st_size, 'sha256': base.digest(p)} for p in files}
    base.write_json(folder / 'manifest.json', manifest)
    temporary = folder.with_suffix('.partial.zip')
    target = folder.with_suffix('.zip')
    if temporary.exists() or target.exists():
        raise FileExistsError('Archive already exists')
    with zipfile.ZipFile(temporary, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for path in files + [folder / 'manifest.json']:
            archive.write(path, arcname=path.name)
    with zipfile.ZipFile(temporary) as archive:
        if archive.testzip() is not None:
            raise ValueError('Archive CRC failed')
        for name, pin in manifest.items():
            content = archive.read(name)
            if len(content) != pin['bytes'] or hashlib.sha256(content).hexdigest() != pin['sha256']:
                raise ValueError('Archive verification failed: ' + name)
    # Windows rename refuses an existing destination, including a concurrent one.
    temporary.rename(target)
    return target


class Recorder:
    agent_path = AGENT
    writer_type = Writer
    prefix = 'physics-'
    schema = 'nfsu-rigid-steps-1'
    boundary = '46AA40 entry/return, human physics1 caller477DF2 only'
    limitations = 'Rigid integration with supplied accumulated forces; not force production, full VehicleModel replay or performance profiling. Instrumentation changes timing. Game files remain read-only.'
    recording_status = 'Recording every human rigid integration. Play a race, then Stop.'
    coverage = 'Paired human rigid integration steps only; not full VehicleModel input replay.'

    def source_files(self):
        return (Path(__file__), self.agent_path, Path(base.__file__))

    def finish_agent(self, script):
        pass

    def abort_agent(self, script):
        pass

    def __init__(self, output, note):
        self.output, self.note = Path(output), note
        self.messages, self.incoming = queue.Queue(), queue.Queue(maxsize=32)
        self.stopping, self.detached, self.overflow = threading.Event(), threading.Event(), threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=False)

    def start(self):
        self.thread.start()

    def stop(self):
        self.stopping.set()

    def receive(self, message, data):
        try:
            self.incoming.put_nowait(message)
        except queue.Full:
            self.overflow.set()

    def drain(self, script, writer, events):
        while True:
            try:
                message = self.incoming.get_nowait()
            except queue.Empty:
                return
            if message.get('type') != 'send':
                raise RuntimeError('Instrumentation error: ' + base.encoded(message))
            payload = message['payload']
            if payload.get('kind') == 'batch':
                writer.batch(payload)
                script.exports_sync.acknowledge(payload['batch'])
            else:
                events.write(base.encoded(payload) + '\n')
                events.flush()
                raise RuntimeError('Capture failed: ' + str(payload))

    def run(self):
        folder = writer = process = session = script = events = None
        reason, state, verified_detach = 'Stopped by user', None, False
        try:
            import frida
            api = base.WindowsAPI()
            self.messages.put(('status', 'Waiting for original PC 1.4 USA speed.exe...'))
            while not self.stopping.wait(0.25):
                pids = api.processes()
                if len(pids) > 1:
                    raise RuntimeError('More than one speed.exe process is running')
                if pids:
                    process = base.Process(api, pids[0])
                    identity = process.authenticate()
                    break
            if process is None:
                self.messages.put(('save_error', 'Stopped before attachment; no session created'))
                return
            folder = self.output / (self.prefix + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8])
            base.make_directory(folder, exclusive=True)
            metadata = {'schema': self.schema, 'version': VERSION, 'source': identity,
                        'synthetic_fixture': bool(identity.get('synthetic_fixture')),
                        'created_utc': base.utc(), 'note': self.note, 'frida': frida.__version__,
                        'files': {p.name: base.digest(p) for p in self.source_files()},
                        'boundary': self.boundary, 'limitations': self.limitations}
            base.write_json(folder / 'metadata.json', metadata)
            self.source_identity = identity
            writer = self.writer_type(folder)
            events = (folder / 'events.jsonl').open('x', encoding='ascii')
            device = frida.get_local_device()
            # Authentication pins a live process handle and creation time before attach.
            if not process.alive():
                raise RuntimeError('Authenticated process exited before attachment')
            session = device.attach(process.pid)
            session.on('detached', lambda *args: self.detached.set())
            check = base.Process(api, process.pid)
            try:
                if check.created != process.created:
                    raise RuntimeError('PID was reused before attachment')
            finally:
                check.close()
            script = session.create_script(self.agent_path.read_text(encoding='ascii'), runtime='qjs')
            script.on('message', self.receive)
            script.load()
            script.exports_sync.start(False)
            self.messages.put(('status', self.recording_status))
            last_notice = 0
            while not self.stopping.wait(0.05):
                if self.overflow.is_set():
                    raise RuntimeError('Host queue overflow; session is incomplete')
                if self.detached.is_set() or not process.alive():
                    reason = 'Game exited or instrumentation detached'
                    break
                self.drain(script, writer, events)
                state = script.exports_sync.status()
                if not state['enabled']:
                    reason = 'Step limit reached' if not state['errors'] else 'Capture error'
                    break
                if time.monotonic() - last_notice > 1:
                    self.messages.put(('status', self.prefix.rstrip('-') + ' records saved: ' + str(writer.steps)))
                    last_notice = time.monotonic()
            if not self.detached.is_set():
                state = script.exports_sync.stop()
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline:
                    self.drain(script, writer, events)
                    state = script.exports_sync.stop()
                    if not state['active'] and state['completed'] == writer.steps and state['sent'] == writer.batches:
                        break
                    time.sleep(.01)
                if state['active'] or state['errors'] or state['completed'] != writer.steps or state['sent'] != writer.batches:
                    raise RuntimeError('Unpaired/incomplete capture at stop: ' + base.encoded(state))
                if writer.steps == 0:
                    raise RuntimeError('No accepted steps captured; record a driven race')
                self.finish_agent(script)
                script.unload()
                script = None
                session.detach()
                verified_detach = True
            else:
                # Already delivered complete pairs remain usable; do not invent a final pair.
                while not self.incoming.empty():
                    message = self.incoming.get_nowait()
                    if message.get('type') == 'send' and message['payload'].get('kind') == 'batch':
                        writer.batch(message['payload'])
                    else:
                        events.write(base.encoded(message) + '\n')
                reason += '; final in-flight batch may be absent'
        except Exception as error:
            reason = 'Recording error: ' + str(error)
        finally:
            if script is not None:
                try:
                    self.abort_agent(script)
                    script.unload()
                except Exception:
                    pass
            if session is not None and not self.detached.is_set():
                try:
                    session.detach()
                except Exception:
                    pass
            if process is not None:
                process.close()
            if events is not None:
                events.close()
            if writer is not None:
                writer.close()
            if folder is not None:
                summary = {'reason': reason, 'steps': writer.steps if writer else 0,
                           'capture_state': state, 'clean_detach': verified_detach,
                           'complete_stop': verified_detach and not reason.startswith('Recording error:'),
                           'coverage': self.coverage}
                summary.update(getattr(writer, 'summary_extra', {}))
                base.write_json(folder / 'summary.json', summary)
                try:
                    archive = package(folder)
                    self.messages.put(('done', str(archive), summary))
                except Exception as error:
                    self.messages.put(('save_error', str(error) + '; raw session: ' + str(folder)))
            elif reason.startswith('Recording error:'):
                self.messages.put(('save_error', reason))


class App(base.App):
    def __init__(self, window, output):
        super().__init__(window, output)
        window.title('NFSU Original Physics Recorder')
        labels = [child for child in window.winfo_children()[0].winfo_children()
                  if child.winfo_class() == 'TLabel']
        labels[0].configure(text='NFS Underground - Physics Step Recorder')
        labels[1].configure(text='Start, play the original, then Stop.\nInstrumented mode: captures actual solver inputs/outputs; affects timing.')

    def start(self):
        if self.recorder and self.recorder.thread.is_alive():
            return
        self.recorder = Recorder(self.output, self.label.get()[:500])
        self.start_button.configure(state='disabled')
        self.stop_button.configure(state='normal')
        self.recorder.start()


def install():
    import frida
    if frida.__version__ != '17.5.1':
        raise RuntimeError('Install the validated frida==17.5.1 first')
    folder = base.special_folder(5) / 'NFSU-Original-Recorder'
    base.make_directory(folder)
    sources = [Path(__file__), AGENT, Path(base.__file__)]
    for source in sources:
        target = folder / source.name
        if source.resolve() != target.resolve():
            shutil.copyfile(source, target)
        if base.digest(source) != base.digest(target):
            raise RuntimeError('Installed source hash differs: ' + source.name)
    doc = Path(__file__).resolve().parents[1] / 'docs' / 'ORIGINAL_PHYSICS_RECORDER.md'
    if doc.is_file():
        shutil.copyfile(doc, folder / 'PHYSICS.md')
    import os
    pythonw = Path(sys.executable).with_name('pythonw.exe')
    if not pythonw.is_file():
        raise RuntimeError('Complete Python for Windows with pythonw.exe required')
    env = dict(os.environ, NFSUREC_PYTHON=str(pythonw),
               NFSUREC_SCRIPT=str(folder / Path(__file__).name))
    command = '''$ErrorActionPreference='Stop'
$shell=New-Object -ComObject WScript.Shell
$shortcut=$shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'NFSU Physics Recorder.lnk'))
$shortcut.TargetPath=$env:NFSUREC_PYTHON
$shortcut.Arguments='"'+$env:NFSUREC_SCRIPT+'"'
$shortcut.WorkingDirectory=[IO.Path]::GetDirectoryName($env:NFSUREC_SCRIPT)
$shortcut.Description='Original single-car physics step recorder'
$shortcut.Save()
$check=$shell.CreateShortcut($shortcut.FullName)
if ($check.Arguments -ne $shortcut.Arguments) { throw 'Shortcut verification failed' }
'''
    subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', command],
                   env=env, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    print(base.encoded({'installed': str(folder), 'shortcut': 'NFSU Physics Recorder.lnk'}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--install', action='store_true')
    args = parser.parse_args()
    if args.install:
        install()
        return
    import tkinter as tk
    window = tk.Tk()
    App(window, args.output or base.special_folder(5) / 'NFSU-Recordings')
    window.mainloop()


if __name__ == '__main__':
    main()

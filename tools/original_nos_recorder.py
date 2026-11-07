#!/usr/bin/env python3
"""Bounded original NOS render capture. PC 1.4 USA only; no gameplay writes."""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import queue
import struct
import sys
import time
import uuid


def load_base():
    candidate = Path(__file__).with_name('original_recorder.py')
    if not candidate.exists():
        candidate = Path.cwd() / 'tools/original_recorder.py'
    spec = importlib.util.spec_from_file_location('nos_base_recorder', candidate)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=True, allow_nan=False, indent=2) + '\n', encoding='ascii')


def executable_bytes(path, address, length):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    rva = address - base
    for index in range(count):
        section = pe + 24 + optional + index * 40
        virtual_size, va, raw_size, raw = struct.unpack_from('<IIII', data, section + 8)
        if va <= rva and rva + length <= va + min(virtual_size, raw_size):
            return data[raw + rva - va:raw + rva - va + length].hex()
    raise RuntimeError('Instruction pin outside a file-backed PE section')


def config_for_original(executable):
    return {'synthetic': False, 'device': '0x73636c', 'frame': '0x7364dc',
            'target': '0x73654c', 'envelope': '0x73641c', 'blur': '0x40cd40',
            'pins': [{'address': hex(a), 'bytes': executable_bytes(executable, a, 16)}
                     for a in (0x40cd40, 0x40d1af)]}


class Capture:
    def __init__(self, pid, directory, identity, agent_path, config):
        import frida
        if frida.__version__ != '17.5.1':
            raise RuntimeError('This recorder requires tested Frida 17.5.1')
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=False)
        self.queue = queue.Queue(maxsize=512)
        self.overflow = False
        self.complete = False
        self.images = {}
        self.errors = []
        self.metadata = {'schema': 1, 'kind': 'original-nos-capture',
                         'synthetic': bool(config['synthetic']), 'identity': identity,
                         'config': config, 'agent_sha256': hashlib.sha256(agent_path.read_bytes()).hexdigest(),
                         'diagnostic_gpu_stalls': True, 'fps_measurement': False,
                         'gameplay_writes': False, 'images': self.images, 'errors': self.errors}
        write_json(self.directory / 'metadata.json', self.metadata)
        self.log = (self.directory / 'events.jsonl').open('x', encoding='ascii', buffering=1)
        self.session = frida.get_local_device().attach(pid)
        self.script = None
        try:
            if not config['synthetic']:
                base = load_base()
                same = base.Process(base.WindowsAPI(), pid)
                try:
                    if same.created != identity['creation_filetime'] or not same.alive():
                        raise RuntimeError('Process identity changed before instrumentation')
                    same.authenticate()
                finally:
                    same.close()
            self.script = self.session.create_script(agent_path.read_text('ascii'), runtime='qjs')
            self.script.on('message', self.on_message)
            self.script.load()
            self.script.exports_sync.start(config)
        except BaseException as error:
            self.metadata['initialization_error'] = str(error)
            write_json(self.directory / 'metadata.json', self.metadata)
            if self.script is not None:
                self.script.unload()
            self.session.detach()
            self.log.close()
            raise

    def on_message(self, message, data):
        try:
            self.queue.put_nowait((message, data))
        except queue.Full:
            self.overflow = True

    def drain(self):
        from PIL import Image
        while True:
            try:
                message, data = self.queue.get_nowait()
            except queue.Empty:
                break
            if message['type'] == 'error':
                self.errors.append(message)
                self.log.write(json.dumps(message, ensure_ascii=True) + '\n')
                continue
            payload = message.get('payload', {})
            self.log.write(json.dumps(payload, ensure_ascii=True, allow_nan=False) + '\n')
            if payload.get('type') == 'image':
                stage = payload.get('stage')
                if stage not in ('before', 'after', 'final') or stage in self.images:
                    raise RuntimeError('Invalid/duplicate image stage')
                width, height, pitch = (payload[k] for k in ('width', 'height', 'pitch'))
                if not isinstance(data, bytes) or len(data) != pitch * height:
                    raise RuntimeError('Image payload size mismatch')
                raw = self.directory / (stage + '.bgra')
                raw.write_bytes(data)
                image = Image.frombytes('RGBA', (width, height), data, 'raw', 'BGRA', pitch, 1)
                if payload['format'] == 22:
                    image.putalpha(255)
                image.save(self.directory / (stage + '.png'))
                self.images[stage] = dict(payload, raw_sha256=hashlib.sha256(data).hexdigest())
                write_json(self.directory / (stage + '.json'), self.images[stage])
                print('Captured ' + stage + ': ' + str(width) + 'x' + str(height), flush=True)
            elif payload.get('type') in ('capture_error', 'device_error'):
                self.errors.append(payload)
            elif payload.get('type') == 'complete':
                self.complete = True
            elif payload.get('type') in ('armed', 'device_ready'):
                print(json.dumps(payload, ensure_ascii=True), flush=True)
        if self.overflow:
            raise RuntimeError('Capture queue overflow; recording is incomplete')

    def close(self):
        try:
            self.metadata['agent_stop'] = self.script.exports_sync.stop()
        except Exception as error:
            self.errors.append({'stop_error': str(error)})
        self.drain()
        self.metadata['complete'] = self.complete and set(self.images) == {'before', 'after', 'final'} and not self.errors
        self.metadata['queue_overflow'] = self.overflow
        write_json(self.directory / 'metadata.json', self.metadata)
        self.log.close()
        self.script.unload()
        self.session.detach()
        return self.metadata['complete']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wait-seconds', type=int, default=900)
    parser.add_argument('--capture-seconds', type=int, default=600)
    args = parser.parse_args()
    if not (1 <= args.wait_seconds <= 3600 and 1 <= args.capture_seconds <= 3600):
        parser.error('Timeouts must be 1..3600 seconds')
    base = load_base()
    api = base.WindowsAPI()
    print('Waiting for PC 1.4 USA speed.exe. Start a race with installed NOS.', flush=True)
    deadline = time.monotonic() + args.wait_seconds
    process = None
    while time.monotonic() < deadline:
        pids = api.processes()
        if len(pids) > 1:
            raise RuntimeError('Close other speed.exe processes first')
        if pids:
            process = base.Process(api, pids[0])
            break
        time.sleep(0.5)
    if process is None:
        raise RuntimeError('No original game found before timeout')
    capture = None
    try:
        identity = process.authenticate()
        config = config_for_original(process.path)
        agent = Path(__file__).with_suffix('.js')
        directory = base.special_folder(5) / 'NFSU-Recordings' / (
            'nos-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8])
        capture = Capture(process.pid, directory, identity, agent, config)
        same = base.Process(api, process.pid)
        try:
            if same.created != process.created or not process.alive():
                raise RuntimeError('Process identity changed during attach')
        finally:
            same.close()
        print('READY: hold NOS for at least half a second. Output: ' + str(directory), flush=True)
        deadline = time.monotonic() + args.capture_seconds
        while time.monotonic() < deadline and process.alive() and not capture.complete:
            capture.drain()
            time.sleep(0.05)
        capture.drain()
    finally:
        good = capture.close() if capture else False
        process.close()
    if not good:
        raise RuntimeError('Capture incomplete; see metadata.json')
    print('COMPLETE: ' + str(capture.directory), flush=True)


if __name__ == '__main__':
    main()

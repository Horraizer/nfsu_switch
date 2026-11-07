"""User-operated bounded original AI decision recorder and native replay input."""
import argparse
import json
from pathlib import Path
import queue
import shutil
import struct
import subprocess
import sys
import time

import original_recorder as base
import original_physics_recorder as infra
from original_ai_replay import RetailCode, shadow, compare, validate_entry, MAX_PAGES, REG_NAMES

AGENT = Path(__file__).with_name('original_ai_agent.js')
MAX_BYTES = 512*1024*1024


def validate_record(record, expected):
    validate_entry(record)
    if record['seq'] != expected or 'native_check' not in record or 'post_fxsave' not in record:
        raise ValueError('Missing/duplicate/unpaired AI decision')
    if len(bytes.fromhex(record['post_fxsave'])) != 512:
        raise ValueError('Invalid native FPU output')
    if not isinstance(record['native_check'],dict) or type(record['native_check'].get('passed')) is not bool:
        raise ValueError('Invalid native comparison result')
    regs = record.get('post_regs',{})
    if set(regs) != set(REG_NAMES) or any(type(v) is not int or not 0 <= v <= 0xffffffff for v in regs.values()):
        raise ValueError('Missing/invalid native return registers')
    if not isinstance(record.get('native_calls'),list) or len(record['native_calls']) > 20000 or any(type(v) is not int for v in record['native_calls']):
        raise ValueError('Missing/invalid native producer order')
    pages = record.get('input_pages')
    if not isinstance(pages,list) or len(pages) > MAX_PAGES:
        raise ValueError('Missing/excessive AI memory input')
    seen = set()
    for address,value in pages:
        if type(address) is not int or not 0x10000 <= address < 0x7fff0000 or address%4096 or address in seen or len(bytes.fromhex(value)) != 4096:
            raise ValueError('Invalid/duplicated AI dependency page')
        seen.add(address)
    post = record.get('post')
    if not isinstance(post,list) or len(post) > 8192:
        raise ValueError('Missing/excessive AI output ranges')
    previous_end = 0
    for address,value in post:
        blob = bytes.fromhex(value)
        if type(address) is not int or not 0x10000 <= address < 0x7fff0000-len(blob) or address < previous_end or not 1 <= len(blob) <= 4096:
            raise ValueError('Invalid/overlapping AI output range')
        previous_end = address+len(blob)
    return record


class Writer(infra.Writer):
    def __init__(self,folder):
        super().__init__(folder)
        self.summary_extra = {'native_reproduced':0,'native_mismatch':0,'native_unsupported':0,
                              'whole_race_replay':False,'port_parity_verified':False}

    def batch(self,payload):
        if payload.get('batch') != self.batches or len(payload.get('records',[])) != 1:
            raise ValueError('Invalid AI batch sequence/size')
        record = validate_record(payload['records'][0],self.steps)
        line = base.encoded(record)+'\n'
        if self.bytes+len(line) > MAX_BYTES:
            raise ValueError('AI recording byte ceiling')
        self.stream.write(line); self.stream.flush()
        self.bytes += len(line); self.steps += 1; self.batches += 1
        check = record['native_check']
        key = 'native_reproduced' if check.get('passed') else 'native_unsupported' if check.get('unsupported') else 'native_mismatch'
        self.summary_extra[key] += 1
        self.summary_extra['coverage'] = ('AI calls saved: %d; native reproduced: %d; mismatched: %d; unsupported: %d. Sampled calls, not a chained race or full port comparison.' %
            (self.steps,self.summary_extra['native_reproduced'],self.summary_extra['native_mismatch'],self.summary_extra['native_unsupported']))


class Recorder(infra.Recorder):
    agent_path,writer_type,prefix,schema = AGENT,Writer,'ai-','nfsu-ai-decisions-1'
    boundary = '462430 controller and45E970 driver entry/return, linked role2/id100..102 rivals, racing stage4'
    recording_status = 'Recording sampled AI decisions for three rivals. Instrumentation pauses the game briefly; then Stop.'
    coverage = 'Independently replayable sampled AI calls, including demanded memory, CPU/FPU, native outputs and nested producer order. Gaps prevent chained race replay.'
    limitations = 'Instrumented diagnostics, not an FPS benchmark. External calls, missing pages, timeout and native mismatch fail closed. Shared-memory changes by other threads can invalidate a record. No full port AI parity claim.'

    def __init__(self,*args):
        super().__init__(*args)
        self.code, self.pending = None, {}

    def source_files(self):
        return (*super().source_files(),Path(__file__),Path(__file__).with_name('original_ai_replay.py'))

    def finish_agent(self,script):
        if self.pending:
            raise RuntimeError('Unpaired AI native return')
        script.exports_sync.cleanup()

    def abort_agent(self,script):
        state = script.exports_sync.cancel()
        deadline = time.monotonic()+11
        while state['active'] and time.monotonic()<deadline:
            time.sleep(.01)
            state = script.exports_sync.cancel()
        if state['active']:
            raise RuntimeError('Original AI call did not return during abort')
        script.exports_sync.cleanup()

    def drain(self,script,writer,events):
        while True:
            try:
                message = self.incoming.get_nowait()
            except queue.Empty:
                return
            if message.get('type') != 'send':
                raise RuntimeError('AI instrumentation error: '+base.encoded(message))
            payload = message['payload']
            if payload.get('kind') == 'begin':
                record = payload['record']; validate_entry(record)
                if self.pending or record['seq'] != writer.steps:
                    raise ValueError('Overlapping/out-of-order AI entry')
                if self.code is None:
                    self.code = RetailCode(Path(self.source_identity['executable']))
                def read(address):
                    pairs = script.exports_sync.pages(record['seq'],[address])
                    if len(pairs) != 1 or pairs[0][0] != address:
                        raise ValueError('AI page response identity differs')
                    return bytes.fromhex(pairs[0][1])
                result = shadow(self.code,record,read)
                self.pending[record['seq']] = result
                script.exports_sync.resume(record['seq'],result['watch'])
            elif payload.get('kind') == 'batch':
                for record in payload['records']:
                    result = self.pending.pop(record['seq'])
                    record['input_pages'] = result['pages']
                    record['native_check'] = compare(result,record)
                    record['producer_calls'] = result['calls']
                    record['shadow_instructions'] = result['instructions']
                writer.batch(payload)
                script.exports_sync.acknowledge(payload['batch'])
            else:
                events.write(base.encoded(payload)+'\n'); events.flush()
                raise RuntimeError('AI capture failed: '+str(payload))


class App(infra.App):
    def __init__(self,window,output):
        super().__init__(window,output)
        window.title('NFSU Original AI Recorder')
        labels = [child for child in window.winfo_children()[0].winfo_children() if child.winfo_class() == 'TLabel']
        labels[0].configure(text='NFS Underground - Rival AI Recorder')
        labels[1].configure(text='Start, play an original race, then Stop.\nDiagnostic sampling pauses decisions; not a performance benchmark.')

    def start(self):
        if self.recorder and self.recorder.thread.is_alive():
            return
        self.recorder = Recorder(self.output,self.label.get()[:500])
        self.start_button.configure(state='disabled'); self.stop_button.configure(state='normal')
        self.recorder.start()


def install():
    import frida,unicorn
    if frida.__version__ != '17.5.1' or unicorn.__version__ != '2.1.4':
        raise RuntimeError('Validated frida17.5.1 and unicorn2.1.4 required')
    folder = base.special_folder(5)/'NFSU-Original-Recorder'
    base.make_directory(folder)
    sources = [Path(__file__),AGENT,Path(__file__).with_name('original_ai_replay.py'),
               Path(infra.__file__),infra.AGENT,Path(base.__file__)]
    for source in sources:
        target = folder/source.name
        if source.resolve() != target.resolve():
            shutil.copyfile(source,target)
        if base.digest(source) != base.digest(target):
            raise RuntimeError('Installed AI source differs: '+source.name)
    doc = Path(__file__).resolve().parents[1]/'docs'/'ORIGINAL_AI_RECORDER.md'
    if doc.is_file():
        shutil.copyfile(doc,folder/'AI.md')
    import os
    pythonw = Path(sys.executable).with_name('pythonw.exe')
    if not pythonw.is_file():
        raise RuntimeError('Complete Python/Tkinter required')
    env = dict(os.environ,NFSUREC_PYTHON=str(pythonw),NFSUREC_SCRIPT=str(folder/Path(__file__).name))
    command = '''$ErrorActionPreference='Stop'
$shell=New-Object -ComObject WScript.Shell
$shortcut=$shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'NFSU AI Recorder.lnk'))
$shortcut.TargetPath=$env:NFSUREC_PYTHON
$shortcut.Arguments='"'+$env:NFSUREC_SCRIPT+'"'
$shortcut.WorkingDirectory=[IO.Path]::GetDirectoryName($env:NFSUREC_SCRIPT)
$shortcut.Description='Original rival AI decision recorder'
$shortcut.Save()
$check=$shell.CreateShortcut($shortcut.FullName)
if ($check.Arguments -ne $shortcut.Arguments) { throw 'Shortcut verification failed' }
'''
    subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',command],env=env,
                   check=True,creationflags=subprocess.CREATE_NO_WINDOW)
    print(base.encoded({'installed':str(folder),'shortcut':'NFSU AI Recorder.lnk'}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--install',action='store_true')
    args = parser.parse_args()
    if args.install:
        install(); return
    import tkinter as tk
    window = tk.Tk(); App(window,args.output or base.special_folder(5)/'NFSU-Recordings'); window.mainloop()


if __name__ == '__main__':
    main()

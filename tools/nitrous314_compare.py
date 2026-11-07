"""Compare the portable nitrous API against authenticated original execution.

Requires Unicorn 2.1.4, the retail speed.exe and nfsu_nitrous_tests. Original
contexts are synthetic; this is not an event0/controller recording. Every
probe process must produce fresh, complete output with no error lines.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

from event0_trace import Image, digest_file, manifest
from nitrous314_recovery import EXE_SHA, Oracle, witnesses, torque_witnesses
from nitrous314_lifecycle import dispatch
from nitrous314_bridge import engine_witnesses


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def compare(image, probe):
    commands, expected = [], []
    def add(command, state=None, result=None, torque=None, engine=None, descriptor=None):
        commands.append(command)
        expected.append(dict(command=command, state=state, result=result, torque=torque,
                             engine=engine, descriptor=descriptor))
    rows, _ = witnesses(image)
    operations = {'00465490': 'available', '00465430': 'active',
                  '004654F0': 'start', '00465530': 'stop'}
    for row in rows:
        initial = row.get('input', [0, 0, 0, 0])
        add('load ' + ' '.join(map(str, initial)), initial)
        if row['name'].startswith('initialize-capacity'):
            command = 'initialize %s %s' % (row['input_capacity'], row['input_count'])
            result = 1
        elif row['name'] == 'initializer-preserves-depleted-state':
            command, result = 'initialize 2 2', 1
        else:
            operation = operations[row['entry']]
            command = operation
            if operation != 'available': command += ' %s' % row['clock']
            if operation == 'start': command += ' %s' % int(bool(row['mode']))
            result = row['result'] if operation != 'stop' else None
        add(command, row['state'], result)
    dispatch_rows, _ = dispatch(image)
    dispatch_count = 0
    for row in dispatch_rows:
        if not row['car_present'] or row['input_id'] == 0x74:
            # Native pointer presence and ID routing belong to the absent
            # runtime caller, rather than a portable resource operation.
            continue
        add('load ' + ' '.join(map(str, row['state'])), row['state'])
        add('dispatch %s %s %s %s %s %s 1' % (row['clock'], row['pressed'],
            row['drag'], row['drag_override'], row['drift'], row['drift_override']), row['actual'])
        dispatch_count += 1
    oracle = Oracle(image)
    rng = random.Random(314)
    state, clock = [24000, 4000, 24000, 0], 10000
    add('load ' + ' '.join(map(str, state)), state)
    sequence_count = 0
    for _ in range(256):
        clock += rng.choice((0, 1, 67, 250, 1000, 4001))
        op = rng.choice(('available', 'start', 'active', 'stop', 'initialize'))
        if op == 'initialize':
            state = oracle.initialize(state, 6, 6)
            add('initialize 6 6', state, 1)
        else:
            mode = 1
            oracle.prepare(state, clock, mode)
            address, reg = {'available': (0x465490, oracle.x.UC_X86_REG_ECX),
                'start': (0x4654F0, oracle.x.UC_X86_REG_EDX),
                'active': (0x465430, oracle.x.UC_X86_REG_EAX),
                'stop': (0x465530, oracle.x.UC_X86_REG_ESI)}[op]
            result = oracle.call(address, reg)
            state = oracle.state()
            command = op if op == 'available' else op + ' %s' % clock
            if op == 'start': command += ' 1'
            add(command, state, result if op != 'stop' else None)
        sequence_count += 1
    torque_rows, _ = torque_witnesses(image,
        (0, 500, 999, 1000.01, 1333, 1999.999, 4555, 8999, 10000), (41999, 42000, 42001))
    conversion = struct.unpack('<f', image.read(0x6CCB78, 4))[0]
    for row in torque_rows:
        angular = f32(row['rpm'] / conversion)
        add('torque 2 %.17g 0.5 %s 1000 0.001 10 %s %s %s' %
            (angular, int(row.get('active', True)), row.get('clock', 40000),
             row.get('flag', 0), row.get('override', 0)), torque=row['observed_torque'])
    engine_rows, _ = engine_witnesses(image)
    for row in engine_rows:
        if row['name'] == 'descriptor-constructor-prefix':
            add('descriptor %s %s' % (row['idle'], row['redline']),
                descriptor=(row['descriptor'][1], row['descriptor'][3]))
        else:
            add('load ' + ' '.join(map(str, row['initial_resource'])), row['initial_resource'])
            add('bridge %s %.17g %s' % (row['prior_active'], row['prior_seconds'], row['clock']),
                row['resource'], engine=(row['active'], row['seconds']))
    run = subprocess.run([str(probe), '--oracle'], input='\n'.join(commands) + '\n',
                         text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if run.returncode != 0 or run.stderr or not run.stdout:
        raise RuntimeError('portable probe failed: exit=%d stderr=%r stdout=%r' %
                           (run.returncode, run.stderr, run.stdout[:200]))
    lines = run.stdout.splitlines()
    if len(lines) != len(expected):
        raise AssertionError('fresh output count %d versus %d' % (len(lines), len(expected)))
    for line, want in zip(lines, expected):
        if want['descriptor'] is not None:
            actual = tuple(float(value) for value in line.split())
            if tuple(f32(value) for value in actual) != want['descriptor']:
                raise AssertionError('portable constructor descriptor differs: ' + line)
        elif want['engine'] is not None:
            words = line.split()
            if len(words) != 6 or list(map(int, words[:4])) != want['state'] or (
                    int(words[4]), f32(float(words[5]))) != want['engine']:
                raise AssertionError('portable engine bridge differs: ' + line)
        elif want['torque'] is not None:
            if abs(float(line) - want['torque']) > 2e-6:
                raise AssertionError('portable torque %s: %s versus %s' % (want['command'], line, want['torque']))
        else:
            actual = list(map(int, line.split()))
            if len(actual) != 5 or actual[:4] != want['state'] or (
                    want['result'] is not None and actual[4] != want['result']):
                raise AssertionError('portable resource %s: %s versus %r' % (want['command'], line, want))
    return dict(resource_witnesses=len(rows), dispatcher_witnesses=dispatch_count,
                resource_sequence_operations=sequence_count, torque_witnesses=len(torque_rows),
                engine_witnesses=len(engine_rows),
                fresh_probe_lines=len(lines), stdout_sha256=hashlib.sha256(run.stdout.encode('ascii')).hexdigest().upper())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root, out, probe = args.root.resolve(), args.out.resolve(), args.probe.resolve()
    if out == root or root in out.parents or out in root.parents:
        parser.error('output must be outside input root')
    out.mkdir(parents=True, exist_ok=False)
    before = manifest(root)
    (out / 'before.json').write_bytes(before)
    try:
        if digest_file(root / 'speed.exe') != EXE_SHA:
            raise ValueError('unsupported executable SHA-256')
        import unicorn
        if unicorn.__version__ != '2.1.4': raise ValueError('expected Unicorn 2.1.4')
        result = compare(Image((root / 'speed.exe').read_bytes()), probe)
        (out / 'compare.json').write_text(json.dumps(result, indent=2) + '\n', encoding='ascii')
        print('PASS: portable resource/dispatcher/sequence/torque compared with original ' + json.dumps(result, sort_keys=True))
    finally:
        after = manifest(root)
        (out / 'after.json').write_bytes(after)
        if after != before: raise RuntimeError('retail input tree changed')
        print('PASS: retail manifests identical SHA256 ' + hashlib.sha256(before).hexdigest().upper())


if __name__ == '__main__':
    main()

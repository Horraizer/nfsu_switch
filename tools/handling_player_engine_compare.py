"""Compare the portable selector1 engine primitives to an original witness."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path.cwd()/'tools'))
from world_body_trace import manifest
from handling_brake_compare import EXE_SHA, BUN_SHA


def compare(root, native, out):
    data = native.read_bytes()
    report = json.loads(data)
    if (report['executable_sha256'] != EXE_SHA or report['globalb_sha256'] != BUN_SHA
            or report['external_hooks'] != [] or len(report['rows']) != 216):
        raise ValueError('expected authenticated no-hook original engine216 witness')
    for path, expected in ((root/'speed.exe', EXE_SHA), (root/'Global/GLOBALB.BUN', BUN_SHA)):
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError('unsupported retail input')
    binary = out/'handling251-player-engine-compare.exe'
    command = ['g++', '-std=c++17', '-O2', '-ffp-contract=off', '-fno-fast-math',
               '-I', 'include', 'tools/handling_player_engine_compare.cpp',
               'src/player_powertrain.cpp', 'src/vehicle.cpp', 'src/sha256.cpp',
               '-o', str(binary)]
    build = subprocess.run(command, capture_output=True, text=True)
    (out/'build.log').write_text(build.stdout+build.stderr, encoding='ascii')
    if build.returncode != 0:
        raise RuntimeError('portable bridge failed to build')
    rows = report['rows']
    text = ''.join('%d %d %.9g %.9g %.9g\n' %
                   (r['gear'], r['gearbox_mode'], r['clutch'], r['throttle'], r['fixed_wheel_spin'])
                   for r in rows)
    run = subprocess.run([str(binary)], input=text, capture_output=True, text=True)
    (out/'portable.csv').write_text(run.stdout, encoding='ascii')
    if run.returncode != 0 or run.stderr or len(run.stdout.splitlines()) != len(rows):
        raise RuntimeError('portable bridge did not produce all fresh rows')
    keys = ['initial_engine_rad_s', 'wheel_equivalent_engine_rad_s', 'engine_feedback_torque',
            'engine_angular_acceleration', 'completed_engine_rad_s', 'exported_actor_rpm',
            'feedback_after_component_update']
    differences = []
    for index, (row, line) in enumerate(zip(rows, run.stdout.splitlines())):
        values = [float(value) for value in line.split(',')]
        if len(values) != 8: raise RuntimeError('invalid portable row')
        for key, actual in zip(keys, values):
            if actual != row[key]: differences.append([index, key, row[key], actual])
        if values[7] != row['wheel_drive_torque'][2] or values[7] != row['wheel_drive_torque'][3]:
            differences.append([index, 'wheel_drive_torque', row['wheel_drive_torque'], values[7]])
    record = subprocess.run([str(binary), '--record'], capture_output=True, text=True)
    if record.returncode != 0 or record.stderr or not record.stdout:
        raise RuntimeError('portable record output failed')
    encoded = [(int(line.split(':')[0],16), int(line.split(':')[1],16))
               for line in record.stdout.splitlines()]
    original = (root/'Global/GLOBALB.BUN').read_bytes()[1736752:1736752+3216]
    if any(original[offset] != value for offset,value in encoded):
        raise RuntimeError('portable stock field bytes differ from authenticated retail record')
    if len(encoded) != 344:
        raise RuntimeError('unexpected portable field coverage')
    result = dict(native_report_sha256=hashlib.sha256(data).hexdigest(),
                  native_source_head=report['source_head'],
                  native_source_tool_sha256=report['source_tool_sha256'],
                  portable_head=subprocess.check_output(['git','rev-parse','HEAD']).decode('ascii').strip(),
                  portable_sources={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in
                    map(Path, ['src/player_powertrain.cpp','include/nfsu/player_powertrain.h',
                               'src/vehicle.cpp','include/nfsu/vehicle.h'])},
                  binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  rows=len(rows), differences=differences,
                  stock_record_field_bytes=len(encoded),
                  stock_record_fields_sha256=hashlib.sha256(bytes(v for o,v in encoded)).hexdigest(),
                  boundary='Original engine-only fixed-wheel matrix comparison; no whole gearbox, wheel/body integration, live cadence or gameplay acceptance.')
    (out/'report.json').write_text(json.dumps(result, ensure_ascii=True, indent=2)+'\n', encoding='ascii')
    if differences: raise RuntimeError('portable engine differs: see report.json')
    print('PASS: portable/original engine216 exact rows and344 authenticated stock field bytes')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--native-report',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args = parser.parse_args()
    root,out = args.root.resolve(),args.out.resolve()
    relative = out.relative_to(Path.cwd())
    if not relative.parts[0].startswith('build') or not str(relative).isascii() or root in out.parents:
        parser.error('fresh ASCII workspace build output outside retail required')
    out.mkdir(parents=True,exist_ok=False)
    before = manifest(root); (out/'before.json').write_bytes(before)
    try: compare(root,args.native_report.resolve(),out)
    finally:
        after = manifest(root); (out/'after.json').write_bytes(after)
        if before != after: raise RuntimeError('retail manifest changed')
        print('PASS: complete retail content/metadata manifest unchanged')


if __name__ == '__main__': main()

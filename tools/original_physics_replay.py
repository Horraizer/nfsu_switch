"""Replay captured human rigid steps with authenticated original instructions."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys

from original_recorder import EXE_SHA256, digest, make_directory, write_json
from original_physics_recorder import validate_record
from world_body_trace import Image
import contact256_native as native
from physics256_native import bits
from unicorn.x86_const import UC_X86_REG_FPCW, UC_X86_REG_MXCSR

# Semantic float fields, retaining original units and pre-normalization epoch.
FIELDS = [(name + str(k), offset + k * 4) for name, offset, count in (
    ('position', 0x20, 3), ('axes', 0x30, 12), ('momentum', 0x70, 3),
    ('angular_momentum', 0x80, 3), ('adaptive_damping', 0x90, 1),
    ('velocity', 0xa0, 3), ('angular_velocity', 0xc0, 3),
    ('snapshot_axes', 0xd0, 12), ('force', 0x110, 3), ('torque', 0x120, 3)) for k in range(count)]


def floats(blob, offset, count=3):
    return list(struct.unpack_from('<' + 'f' * count, blob, offset))


def native_step(image, record):
    validate_record(record, record['seq'])
    m = native.fixture(image, (4, 0, 0), (0, 0, 0), (0, 0, 0))
    # Use fresh memory and authentic instruction bytes on every invocation.
    def store(address, blob):
        for page in range(address & ~4095, (address + len(blob) + 4095) & ~4095, 4096):
            if not any(start <= page and page + 4095 <= end for start, end, _ in m.uc.mem_regions()):
                m.uc.mem_map(page, 4096)
        m.uc.mem_write(address, blob)
    rigid, properties = record['rigid'], record['properties']
    pre = bytes.fromhex(record['pre_hex'])
    store(rigid, pre)
    store(properties, bytes.fromhex(record['properties_hex']))
    for address, content in record['globals']:
        store(address, bytes.fromhex(content))
    system = 0x2200000
    while any(start <= system + 0x1fff and end >= system for start, end, _ in m.uc.mem_regions()):
        system += 0x2000
    table = system + 0x1000
    store(system, bytes(0x100)); store(table, bytes(0x100))
    m.put(system, table); m.put(table + 4, 0x46abd0)
    # Isolate this body's component list, without replacing any arithmetic.
    m.put(system + 0xc, rigid + 4)
    m.put(rigid + 4, system + 0xc); m.put(rigid + 8, system + 0xc)
    m.uc.reg_write(UC_X86_REG_FPCW, record['fpcw'])
    m.uc.reg_write(UC_X86_REG_MXCSR, record['mxcsr'])
    m.run(0x46aa40, (system, record['dt_bits']), budget=3000000)
    if m.events:
        raise RuntimeError('External semantic stubs are forbidden')
    actual = bytes(m.uc.mem_read(rigid, 0x200))
    expected = bytes.fromhex(record['post_hex'])
    # List links differ by construction; every other body byte must match.
    differences = [i for i in range(len(actual)) if not 4 <= i < 12 and actual[i] != expected[i]]
    return actual, differences, len(m.executed)


def port_input(record):
    b, p = bytes.fromhex(record['pre_hex']), bytes.fromhex(record['properties_hex'])
    if record['fpcw'] != 0x7f:
        return None, 'Port arithmetic models007F only'
    if record['mxcsr'] & 0xffc0 != 0x1f80:
        return None, 'Non-default SSE rounding/denormal policy is outside the port API'
    if floats(b, 0x1e4, 1)[0] != 0:
        return None, 'Drive-force control1E4 is outside the common port API'
    inertia = [floats(p, 0x10 + k * 0x14, 1)[0] for k in range(3)]
    if any(floats(p, 0x10 + c * 16 + r * 4, 1)[0] != 0
           for c in range(3) for r in range(3) if c != r):
        return None, 'Off-diagonal local inertia is outside the port API'
    mass, scale = floats(p, 0, 1)[0], floats(p, 0x50, 1)[0]
    values = [struct.unpack('<f', struct.pack('<I', record['dt_bits']))[0], mass, *inertia, scale]
    for offset in (0x20, 0x70, 0x80, 0xa0, 0xc0, 0x110, 0x120):
        values += floats(b, offset)
    for k in range(3):
        values += floats(b, 0x30 + k * 16)
    values += [*floats(b, 0x90, 1), *floats(b, 0x1e0, 1)]
    values += [struct.unpack_from('<I', b, offset)[0] for offset in (0x130, 0x134, 0x138, 0x140)]
    if not all(math.isfinite(v) for v in values) or mass <= 0 or min(inertia) <= 0:
        return None, 'Invalid physical values'
    return values, None


PORT_SOURCE = r'''#include "nfsu/player_powertrain.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace nfsu;
int main(){try{unsigned count=0; float dt;
std::cout<<std::setprecision(17);
while(std::cin>>dt){VehicleHandling h; PlayerRigidBodyState b;
std::cin>>h.mass;for(auto& x:h.inertia)std::cin>>x;std::cin>>h.alternate_yaw_scale;
for(auto* v:{&b.position,&b.momentum,&b.angular_momentum,&b.velocity,&b.angular_velocity,&b.force,&b.torque})for(auto& x:*v)std::cin>>x;
for(auto& a:b.axes)for(auto& x:a)std::cin>>x;
unsigned linear,angular,alternate,gravity;
std::cin>>b.adaptive_damping>>b.yaw_torque_limit>>linear>>angular>>alternate>>gravity;
if(!std::cin)throw std::runtime_error("Truncated input");
b.linear_damping=linear!=0;b.angular_damping=angular!=0;b.alternate_yaw=alternate!=0;b.gravity=gravity!=0;
player_rigid_body_integrate(h,b,dt);
bool first=true;auto emit=[&](float x){if(!first)std::cout<<' ';first=false;std::cout<<x;};
for(auto* v:{&b.position,&b.momentum,&b.angular_momentum,&b.velocity,&b.angular_velocity,&b.force,&b.torque})for(auto x:*v)emit(x);
for(auto& a:b.axes)for(auto x:a)emit(x);emit(b.adaptive_damping);std::cout<<'\n';++count;}
if(!count)throw std::runtime_error("No cases");return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
'''


def compare_port(records, out, build, compiler):
    rows, selected, unsupported = [], [], []
    for record in records:
        values, reason = port_input(record)
        if values is None:
            unsupported.append({'seq': record['seq'], 'reason': reason})
        else:
            rows.append(' '.join(format(v, '.17g') for v in values))
            selected.append(record)
    if not selected:
        return {'compared': 0, 'unsupported': unsupported, 'passed': False}
    source, exe = out / 'port-probe.cpp', out / 'port-probe.exe'
    source.write_text(PORT_SOURCE, encoding='ascii')
    repo = Path(__file__).resolve().parents[1]
    build = build.resolve()
    cache = build / 'CMakeCache.txt'
    homes = [line.split('=', 1)[1] for line in cache.read_text('utf-8').splitlines()
             if line.startswith('CMAKE_HOME_DIRECTORY:INTERNAL=')]
    if len(homes) != 1 or Path(homes[0]).resolve() != repo:
        raise RuntimeError('Build directory belongs to a different source checkout')
    result = subprocess.run(['cmake', '--build', str(build), '--target', 'nfsu_vehicle'], capture_output=True, text=True)
    (out / 'port-library-build.log').write_text(result.stdout + result.stderr, encoding='ascii', errors='backslashreplace')
    if result.returncode:
        raise RuntimeError('Current port library build failed; see port-library-build.log')
    command = [compiler, '-std=c++20', '-ffp-contract=off', '-I', str(repo / 'include'), str(source),
               '-Wl,--start-group', *[str(build / ('libnfsu_' + name + '.a'))
                 for name in ('vehicle', 'drift', 'sha256')], '-Wl,--end-group', '-o', str(exe)]
    result = subprocess.run(command, capture_output=True, text=True)
    (out / 'port-build.log').write_text(result.stdout + result.stderr, encoding='ascii', errors='backslashreplace')
    if result.returncode or not exe.is_file():
        raise RuntimeError('Port probe build failed; see port-build.log')
    result = subprocess.run([str(exe)], input='\n'.join(rows) + '\n', capture_output=True, text=True)
    (out / 'port-output.txt').write_text(result.stdout + result.stderr, encoding='ascii', errors='backslashreplace')
    if result.returncode or result.stderr or len(result.stdout.splitlines()) != len(selected):
        raise RuntimeError('Port probe did not produce fresh complete successful output')
    failures, worst, compared = [], 0.0, 0
    for record, line in zip(selected, result.stdout.splitlines()):
        actual = [float(x) for x in line.split()]
        post = bytes.fromhex(record['post_hex'])
        expected = sum((floats(post, at) for at in (0x20, 0x70, 0x80, 0xa0, 0xc0, 0x110, 0x120)), [])
        expected += sum((floats(post, 0x30 + k * 16) for k in range(3)), [])
        expected += floats(post, 0x90, 1)
        if len(actual) != len(expected) or not all(math.isfinite(v) for v in actual + expected):
            raise RuntimeError('Malformed/nonfinite port result')
        different = []
        for i, (a, b) in enumerate(zip(actual, expected)):
            difference = abs(a - b); worst = max(worst, difference)
            # Explicit absolute+relative tolerance, not a substituted native answer.
            if difference > 2e-5 + 2e-6 * abs(b):
                different.append({'field': i, 'port': a, 'original': b})
        if different:
            failures.append({'seq': record['seq'], 'differences': different})
        compared += 1
    return {'compared': compared, 'unsupported': unsupported, 'failures': failures,
            'max_abs_difference': worst, 'passed': not failures and not unsupported,
            'port_library_sha256': digest(build / 'libnfsu_vehicle.a'),
            'cmake_cache_sha256': digest(cache),
            'port_sources': {name: digest(repo / name) for name in (
                'include/nfsu/vehicle.h', 'include/nfsu/player_powertrain.h',
                'src/player_powertrain.cpp', 'src/vehicle.cpp')},
            'probe_sha256': digest(exe), 'tolerance': '2e-5 + 2e-6*abs(original)'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--build', type=Path)
    parser.add_argument('--compiler', default='c++')
    args = parser.parse_args()
    if args.out.exists() or args.out.resolve().is_relative_to(args.retail.resolve()):
        raise ValueError('Fresh output outside retail root required')
    make_directory(args.out, exclusive=True)
    source = args.retail / 'speed.exe'
    if digest(source) != EXE_SHA256:
        raise ValueError('Unsupported original executable')
    manifest = json.loads((args.session / 'manifest.json').read_text('ascii'))
    for name in ('metadata.json', 'steps.jsonl', 'summary.json'):
        pin = manifest[name]; path = args.session / name
        if path.stat().st_size != pin['bytes'] or digest(path) != pin['sha256']:
            raise ValueError('Capture integrity failed: ' + name)
    metadata = json.loads((args.session / 'metadata.json').read_text('ascii'))
    if metadata['schema'] != 'nfsu-rigid-steps-1' or metadata['source']['sha256'] != EXE_SHA256:
        raise ValueError('Unsupported capture identity/schema')
    records = [json.loads(line) for line in (args.session / 'steps.jsonl').read_text('ascii').splitlines()]
    summary = json.loads((args.session / 'summary.json').read_text('ascii'))
    if summary.get('steps') != len(records):
        raise ValueError('Summary step count differs')
    if not records:
        raise ValueError('No physics steps; empty agreement is forbidden')
    image, results = Image(source.read_bytes()), []
    for i, record in enumerate(records):
        validate_record(record, i)
        _, differences, instructions = native_step(image, record)
        results.append({'seq': i, 'different_byte_offsets': differences, 'instructions': instructions})
    report = {'scope': 'Isolated rigid component, forces supplied, before outer40BD40 normalization',
              'source_sha256': digest(source), 'capture_manifest_sha256': digest(args.session / 'manifest.json'),
              'steps': len(results), 'session_complete_stop': summary.get('complete_stop', False),
              'synthetic_fixture': bool(metadata.get('synthetic_fixture')),
              'native_byte_exact': all(not r['different_byte_offsets'] for r in results),
              'native': results}
    if args.build:
        report['port'] = compare_port(records, args.out, args.build, args.compiler)
    if digest(source) != EXE_SHA256:
        raise RuntimeError('Original executable changed during replay')
    write_json(args.out / 'report.json', report)
    passed = report['native_byte_exact'] and (not args.build or report['port']['passed'])
    print(('PASS' if passed else 'FAIL') + ' rigid replay: ' + str(len(results)) + ' steps; see report.json')
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':
    main()

"""Check the actual port frame clock against the authenticated PC1.4 clock.

No game is started. The clock oracle starts after the external timer wait,
with a supplied measured interval. The physics oracle stops before Move's
inner loop. The 144Hz variant changes one constant only in synthetic memory;
it documents the installed FPS patch and is never a gameplay correction.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from world_body_trace import Image
from physics256_native import Machine, STACK
from launch_traction_boundary import PC_HASH


CPP = r'''#include "nfsu/race_run.h"
#include <cstdint>
#include <cstring>
#include <iostream>
int main(){
 nfsu::RaceLiveClock clock;
 std::uint64_t us;
 while(std::cin>>us){
  const double dt=clock.advance(us);
  const unsigned count=unsigned(float(dt)/.004399999976158142F)+1;
  const float seconds=float(dt),slice=float(dt/count);
  std::uint32_t a=0,b=0;std::memcpy(&a,&seconds,4);std::memcpy(&b,&slice,4);
  std::cout<<a<<' '<<count<<' '<<b<<'\n';
 }
 return std::cin.eof()?0:2;
}
'''


def sha(blob):
    return hashlib.sha256(blob).hexdigest()


def native(image, intervals, hz):
    machine = Machine(image)
    machine.allowed = [(0x585C50, 0x585D69), (0x477D56, 0x477D7A),
                       (0x674898, 0x67490D)]
    machine.put(0x7345A0, 0)
    machine.put(0x6F0880, 1)
    if hz != 60:
        machine.put(0x6CC7B0, 1.0 / hz, 'f')
    assert machine.get(0x6F0894, 'f') == 3.0
    machine.put(0x734570, 0.0, 'f')
    rows = []
    for us in intervals:
        machine.put(STACK + 0x28, us / 1000000.0, 'f')
        machine.run(0x585C50, end=0x585D69, edi=0x730AD8)
        seconds = machine.get(0x73AD38, 'f')
        word = machine.get(0x73AD38)
        remainder = machine.get(0x734570, 'f')
        units = machine.get(0x734568)
        machine.put(STACK + 4, seconds, 'f')
        machine.run(0x477D56, end=0x477D7A, ebp=STACK - 4)
        rows.append(dict(microseconds=us, units=units, seconds=seconds,
                         remainder=remainder, seconds_bits=word,
                         solver_slices=machine.get(STACK + 0x48),
                         solver_step_bits=machine.get(STACK + 0x2C)))
    assert not machine.events
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--compiler', default='c++')
    parser.add_argument('--build', type=Path, default=Path('build-release'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.out.resolve()
    try:
        relative = output.relative_to(root)
    except ValueError:
        parser.error('output must stay inside this checkout')
    if (not relative.parts or not all(part.isascii() for part in relative.parts)
            or output.exists()):
        parser.error('fresh ASCII output directory required')
    retail = args.retail.resolve()
    if retail == output or retail in output.parents:
        parser.error('output must stay outside retail')
    data = (retail / 'speed.exe').read_bytes()
    if sha(data) != PC_HASH:
        parser.error('unsupported PC executable')
    image = Image(data)
    build = args.build.resolve()
    if not (build/'generated/nfsu/shader_manifest.h').is_file():
        parser.error('configured build with generated shader manifest required')
    if struct.unpack('<f', image.read(0x6CC7B0, 4))[0] != struct.unpack('<f', struct.pack('<f', 1/60))[0]:
        parser.error('unexpected original clock period')
    intervals = [6944, 6944, 16667, 20000, 33333, 50000, 90000, 6944, 20200]
    state = 0x12345678
    for _ in range(240):
        state = (1664525 * state + 1013904223) & 0xFFFFFFFF
        intervals.append(6000 + state % 70000)
    original = native(image, intervals, 60)
    patched = native(image, intervals, 144)
    assert original[0]['units'] == 60 and patched[0]['units'] == 25
    output.mkdir(parents=True)
    source = output / 'clock.cpp'
    source.write_bytes(CPP.encode('ascii'))
    exe = output / 'clock.exe'
    command = [args.compiler, '-std=c++17', '-O2', '-ffp-contract=off',
               '-ffunction-sections', '-fdata-sections', '-I', root/'include',
               '-I', root/'src', '-I', build/'generated', source, root/'src/race_run.cpp',
               '-Wl,--gc-sections', '-o', exe, '-Wl,--start-group',
               *sorted(build.glob('libnfsu_*.a')), '-Wl,--end-group']
    result = subprocess.run(command, cwd=root, capture_output=True)
    (output/'build.log').write_bytes(result.stdout + result.stderr)
    result.check_returncode()
    run = subprocess.run([exe], input=''.join(str(us)+'\n' for us in intervals).encode('ascii'),
                         capture_output=True, check=True)
    assert not run.stderr
    port = [list(map(int, line.split())) for line in run.stdout.splitlines()]
    assert len(port) == len(original)
    differences = []
    for i, (expected, actual) in enumerate(zip(original, port)):
        row = [expected['seconds_bits'], expected['solver_slices'], expected['solver_step_bits']]
        if row != actual:
            differences.append(dict(index=i, expected=row, actual=actual))
    report = dict(executable_sha256=PC_HASH, tool_sha256=sha(Path(__file__).read_bytes()),
                  port_sources={name: sha((root/name).read_bytes()) for name in
                                ('src/race_run.cpp', 'include/nfsu/race_run.h', 'src/race_session.cpp')},
                  probe_sha256=sha(exe.read_bytes()), clock_rows=original,
                  diagnostic_144hz_rows=patched, port=port, differences=differences,
                  source_ranges=[dict(start=hex(a), end=hex(b), sha256=sha(image.read(a, b-a)))
                                 for a, b in ((0x585C50, 0x585D69), (0x477D56, 0x477D7A))],
                  boundary='Original clock after external wait and original slice selection, '
                           'supplied elapsed microseconds, original x87 single precision, no hooks. '
                           'Port executes actual RaceLiveClock with target display_hz=60; '
                           'slice expression is the RaceSession expression. '
                           '144Hz changes 6CC7B0 in synthetic memory only. '
                           'No full vehicle, running retail game, GC or Switch hardware claim.')
    (output/'comparison.json').write_bytes((json.dumps(report, indent=2)+'\n').encode('ascii'))
    print('PC clock/solver rows:', len(original), 'differences:', len(differences))
    if differences:
        raise RuntimeError('native clock or solver mismatch: '+repr(differences[:3]))


if __name__ == '__main__':
    main()

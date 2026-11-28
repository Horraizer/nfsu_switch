"""Compare original PC/GC limiter writers with the actual port, offline.

Supplied angular speed, gear, clock and timer only. This checks the timer
writer, not a whole launch or the producer of pre-GO engine phase. No game
is started and no callback result is substituted in gameplay.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from launch_start_state import decode
from launch_traction_boundary import PC_HASH
from physics256_native import Machine
from world_body_trace import Image
from unicorn import Uc, UC_ARCH_PPC, UC_MODE_32, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.ppc_const import (UC_CPU_PPC32_750_V3_1, UC_PPC_REG_MSR,
                               UC_PPC_REG_1, UC_PPC_REG_3, UC_PPC_REG_13, UC_PPC_REG_PC,
                               UC_PPC_REG_FPR0)


GC_HASH = 'dffc823164d98094e3f32a76f61ad22c7fc2b6375f8b99df5df78e718a70a4fd'
CPP = r'''#include "nfsu/player_powertrain.h"
#include "nfsu/vehicle.h"
#include <cstdint>
#include <cstring>
#include <iostream>
float value(std::uint32_t word){float x;std::memcpy(&x,&word,4);return x;}
int main(){unsigned rpm,gear,angular,clock,timer;
 while(std::cin>>rpm>>gear>>angular>>clock>>timer){
  nfsu::VehicleHandling h;h.max_rpm=float(rpm);
  nfsu::PlayerEngineState e;e.angular_speed=value(angular);e.cutoff_until=value(timer);
  nfsu::player_engine_cutoff_update(h,int(gear),e,float(clock)*.00025F);
  std::uint32_t word;std::memcpy(&word,&e.cutoff_until,4);std::cout<<word<<'\n';
 }return std::cin.eof()?0:2;}
'''


def word(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def real(value):
    return struct.unpack('<f', struct.pack('<I', value))[0]


def elf_read(blob, address, length):
    if blob[:6] != b'\x7fELF\x01\x02':
        raise ValueError('unsupported GC ELF')
    offset = struct.unpack_from('>I', blob, 28)[0]
    stride, count = struct.unpack_from('>HH', blob, 42)
    for i in range(count):
        kind, at, start, _, size, _, _, _ = struct.unpack_from('>8I', blob, offset+i*stride)
        if kind == 1 and start <= address <= address+length <= start+size:
            return blob[at+address-start:at+address-start+length]
    raise ValueError('GC ELF range absent')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--gc-elf', type=Path, required=True)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--compiler', default='c++')
    parser.add_argument('--build', type=Path, default=Path('build-release'))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    out = args.out.resolve()
    try:
        relative = out.relative_to(repo)
    except ValueError:
        parser.error('output must stay inside this checkout')
    if not relative.parts or not relative.as_posix().isascii() or out.exists():
        parser.error('fresh ASCII output directory required')
    pe, elf = (args.retail/'speed.exe').read_bytes(), args.gc_elf.read_bytes()
    if hashlib.sha256(pe).hexdigest() != PC_HASH or hashlib.sha256(elf).hexdigest() != GC_HASH:
        parser.error('unsupported original executable')
    state = decode(args.state)
    import zlib
    cache = args.state.read_bytes()
    size = int.from_bytes(cache[:4], 'little')
    raw = zlib.decompress(cache[4+size:])
    origin = state['mem1_offset']
    mem = raw[origin:origin+0x1800000]
    first, last = 0x800760F4, 0x800761B8
    assert mem[first-0x80000000:last-0x80000000] == elf_read(elf, first, last-first)
    gc = Uc(UC_ARCH_PPC, UC_MODE_32 | UC_MODE_BIG_ENDIAN)
    gc.ctl_set_cpu_model(UC_CPU_PPC32_750_V3_1)
    gc.mem_map(0x80000000, 0x1800000)
    gc.mem_write(0x80000000, mem)
    gc.mem_map(0x90000000, 0x20000)
    gc.reg_write(UC_PPC_REG_MSR, 0x2000)
    gc.reg_write(UC_PPC_REG_13, 0x803C86C0)

    def guard(uc, address, size, unused):
        if address == last:
            uc.emu_stop()
        elif not first <= address < last:
            raise RuntimeError('GC writer escaped its declared slice')

    gc.hook_add(UC_HOOK_CODE, guard)
    engine, setup, box = 0x1100000, 0x1101000, 0x1102000
    pc = Machine(Image(pe))
    pc.allowed = [(0x46CA90, 0x46CCC0)]
    pc.put(engine+0x18, setup)
    pc.put(engine+0x20, box)
    pc.put(setup, 0, 'f')
    pc.put(0x78A344, 0, 'B')
    pc.put(0x7361A8, 0)
    ge, gs, gb = 0x90010000, 0x90011000, 0x90012000
    gc.mem_write(ge+0x18, struct.pack('>I', gs))
    gc.mem_write(ge+0x20, struct.pack('>I', gb))
    rows, expected, gekko = [], [], []
    for maximum in (8000, 9000):
        threshold = (maximum-50)/9.549304008483887
        for angular in (threshold-.25, threshold, threshold+.25,
                        924.5777587890625, 916.5277099609375) if maximum == 9000 else (threshold-.25, threshold, threshold+.25):
            for gear in (1, 2, 3, 7):
                for clock in (4000, 50900, 56193):
                    seconds = real(word(clock*.00025))
                    for timer in (0, seconds, real(word(seconds+.04)), real(word(seconds-.00025))):
                        a, before = word(angular), word(timer)
                        pc.put(setup+8, maximum, 'f')
                        pc.put(engine+0x14, real(a), 'f')
                        pc.put(engine+0x9C, real(before), 'f')
                        pc.put(box+0x3C, gear)
                        pc.put(0x73AD34, clock)
                        pc.run(0x46CA90, end=0x46CCC0, ecx=engine)
                        expected.append(pc.get(engine+0x9C))
                        gc.mem_write(gs+8, struct.pack('>f', maximum))
                        gc.mem_write(ge+0x9C, struct.pack('>I', before))
                        gc.mem_write(gb+0x3C, struct.pack('>I', gear))
                        gc.mem_write(0x803C31B8, struct.pack('>I', clock))
                        gc.reg_write(UC_PPC_REG_1, 0x90001000)
                        gc.reg_write(UC_PPC_REG_3, ge)
                        rpm = real(word(real(a)*9.549304008483887))
                        gc.reg_write(UC_PPC_REG_FPR0+9, struct.unpack('>Q', struct.pack('>d', rpm))[0])
                        gc.emu_start(first, 0, count=200)
                        assert gc.reg_read(UC_PPC_REG_PC) == last
                        gekko.append(struct.unpack('>I', gc.mem_read(ge+0x9C, 4))[0])
                        rows.append([maximum, gear, a, clock, before])
    assert not pc.events
    out.mkdir(parents=True)
    source, exe = out/'limiter.cpp', out/'limiter.exe'
    source.write_bytes(CPP.encode('ascii'))
    build = args.build.resolve()
    command = [args.compiler, '-std=c++17', '-O2', '-ffp-contract=off',
               '-I', repo/'include', source, '-o', exe, '-Wl,--start-group',
               *sorted(build.glob('libnfsu_*.a')), '-Wl,--end-group', '-lwinmm']
    result = subprocess.run(command, cwd=repo, capture_output=True)
    (out/'build.log').write_bytes(result.stdout+result.stderr)
    result.check_returncode()
    result = subprocess.run([exe], input=''.join(' '.join(map(str, r))+'\n' for r in rows).encode('ascii'),
                            capture_output=True, check=True)
    assert not result.stderr
    actual = [int(line) for line in result.stdout.splitlines()]
    assert len(actual) == len(rows)
    differences = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
    gc_deltas = [abs(a-b) for a, b in zip(expected, gekko)]
    report = dict(pc_sha256=PC_HASH, gc_elf_sha256=GC_HASH,
                  source_sha256={name: hashlib.sha256((repo/name).read_bytes()).hexdigest()
                                 for name in ('src/player_powertrain.cpp', 'include/nfsu/player_powertrain.h')},
                  cache_sha256=state['cache_sha256'], rows=rows,
                  pc=expected, gc=gekko, port=actual, differences=differences,
                  max_gc_pc_ulp=max(gc_deltas),
                  tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  port_binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  boundary=__doc__)
    (out/'comparison.json').write_bytes((json.dumps(report, indent=2)+'\n').encode('ascii'))
    assert not differences and max(gc_deltas) <= 1
    print('PASS original limiter writers:', len(rows), 'rows; PC/port exact; GC max ULP', max(gc_deltas))


if __name__ == '__main__':
    main()

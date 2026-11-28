"""Execute the original name hash and pre-owner draw gate for every stock row."""
import hashlib
import json
import struct
import sys
from pathlib import Path

import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--retail-root',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args()
data=(args.retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(data).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
from hud254_trace import PE
from physics256_native import Machine,STACK
image=PE(data)
bank=(args.retail_root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(bank).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
args.out.mkdir(parents=True,exist_ok=False)
m = Machine(image)
m.allowed = [(0x4041f0, 0x40421a), (0x404240, 0x404264)]
m.run(0x4041f0, end=0x40421a)
key = m.get(STACK+0x90)
assert key == 0xccf7bd61, hex(key)
node, preset = 0x1100000, 0x1101000
m.put(node+8, preset)
result = []
for index in range(92):
    at=min(index,87) if index>=90 else index
    raw=bytearray(bank[0x2034a0+at*544:0x2034a0+(at+1)*544])
    if index>=90:struct.pack_into('<I',raw,8,key+(-1 if index==90 else 1))
    m.uc.mem_write(preset, bytes(raw))
    observed = []
    def boundary(eligible):
        def stop(machine):
            observed.append(eligible)
            machine.finished = True
            machine.uc.emu_stop()
        return stop
    m.hook(0x404987, 'observed-suppressed', callback=boundary(False))
    m.hook(0x404264, 'observed-pass-to-owner-check', callback=boundary(True))
    m.put(STACK+0x90, key)
    m.run(0x404240, edi=node)
    assert len(observed) == 1
    result.append(dict(index=index, key=struct.unpack_from('<I', raw, 8)[0],
                       name=raw[0x180:].split(b'\0', 1)[0].decode('ascii'),
                       passes_preset_gate=observed[0]))
assert [r['name'] for r in result if not r['passes_preset_gate']] == ['XETT_RoadSDr_SprayA']
out=args.out
(out/'draw-gate-native.json').write_bytes((json.dumps(result, indent=2)+'\n').encode('ascii'))
lines = ['// PC4041F0..40421A hash;404240..40425E gate; owner/life/texture checks excluded.',
         'static const struct { std::uint32_t key; bool passes; } kNativeParticleDrawGate[] = {']
lines += ['    {0x%08xU,%s},' % (r['key'], 'true' if r['passes_preset_gate'] else 'false') for r in result]
lines += ['};', '']
(out/'particle_draw_native.inc').write_bytes('\n'.join(lines).encode('ascii'))
print('PASS original draw admission90 stock rows and2 adjacent keys:1 suppressed before owner/depth checks', hex(key))

"""Read-only bounded witness for the retail solid DEFAULTALPHA substitution."""
from pathlib import Path
import argparse
import hashlib
import struct

import capstone
import unicorn
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ESI, UC_X86_REG_EBX

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('root', type=Path)
args = parser.parse_args()
path = args.root / 'speed.exe'
data = path.read_bytes()
expected = 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
assert hashlib.sha256(data).hexdigest() == expected, 'unsupported speed.exe'
print('sha256', expected, 'Capstone', capstone.__version__, 'Unicorn', unicorn.__version__)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
# This authenticated executable maps .text raw offsets to VA - 0x400000.
for start, end in [(0x53f9b0, 0x53f9e5), (0x53f9f0, 0x53fb03),
                   (0x545998, 0x5459b0), (0x546227, 0x54623e)]:
    for inst in md.disasm(data[start-0x400000:end-0x400000], start):
        print('%08X %-8s %s' % (inst.address, inst.mnemonic, inst.op_str))
for resolved in [0, 0x910000]:
    u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    u.mem_map(0x400000, 0x400000)
    u.mem_write(0x401000, data[0x1000:0x297000])
    u.mem_map(0x900000, 0x20000)
    u.mem_map(0xa00000, 0x10000)
    u.mem_write(0x73551c, struct.pack('<I', 0x900000))
    # Isolate the fixup: global lookup is the only stub, returning a miss or hit.
    u.mem_write(0x5461c0, b'\xb8' + struct.pack('<I', resolved) + b'\xc3')
    u.reg_write(UC_X86_REG_ESP, 0xa08000)
    u.reg_write(UC_X86_REG_ESI, 0x828dcf0b)
    u.reg_write(UC_X86_REG_EBX, 0x900100)
    u.emu_start(0x53fac8, 0x53fb03, count=100)
    actual = struct.unpack('<I', u.mem_read(0x900104, 4))[0]
    assert actual == (resolved or 0x900000)
    print('PASS lookup=%08X stored=%08X' % (resolved, actual))
assert path.read_bytes() == data, 'retail executable changed'
print('PASS retail executable unchanged')

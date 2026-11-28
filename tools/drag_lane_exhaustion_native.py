"""Original manual drag stage when its retained exclusion covers every lane.

The pose, raw road and lane state come from the event80 Celica refusal.
Retail code and raw road bytes run unchanged in isolated memory. The stage
stops before the common context update; this does not certify a race outcome.
No function result or geometry callback is hooked.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys

sys.dont_write_bytecode = True
from world_body_trace import Image, chunks, jdlz
from physics256_native import ACTOR, Machine, bits
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_FPCW

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('retail', type=Path)
parser.add_argument('--out', type=Path)
parser.add_argument('--xbox', type=Path, help='Optional original Xbox game root')
args = parser.parse_args()
executable = (args.retail / 'speed.exe').read_bytes()
assert hashlib.sha256(executable).hexdigest() == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
track_raw = (args.retail / 'Tracks/TRACKB1214.lzc').read_bytes()
assert hashlib.sha256(track_raw).hexdigest() == '4b33662e4281bfb0623e15a6b08c04b3fde0116a06a654d6cfbe4be93faf75b8'
track = jdlz(track_raw)
routes = {}
for tag, at, end in chunks(track):
    if tag != 0x34121:
        continue
    offset = at + 8
    while offset < end:
        index = struct.unpack_from('<h', track, offset + 0xA)[0]
        count = struct.unpack_from('<h', track, offset + 0x10)[0]
        size = 0x27C + count * 0x88
        assert count >= 0 and offset + size <= end and index not in routes
        routes[index] = track[offset:offset + size]
        offset += size
    assert offset == end

m = Machine(Image(executable))
road, controller, descriptor, chain = 0x1110000, 0x1130000, 0x1140000, 0x1150000
m.uc.mem_write(road, routes[27])
m.put(0x77AF30 + 27 * 4, road)
point = road + 0x27C + 20 * 0x88
position = [-250.698822, 779.973206, 13.9051514]
for k, value in enumerate(position):
    m.put(ACTOR + 0x60 + 4 * k, value, 'f')
m.put(controller, 0x6C8D20)
m.put(controller + 4, ACTOR)
m.put(ACTOR + 0x2C, controller)
m.put(controller + 0x3D8, descriptor)
m.put(controller + 0x3E4, chain)
m.put(chain + 4, 1)  # Nonempty retained chain; this branch never reads a cell.
m.put(descriptor, road)
m.put(descriptor + 4, point)
m.put(descriptor + 8, 14)
m.put(controller + 0x500, 2)
m.put(controller + 0x504, 11)
m.put(controller + 0x508, 14)
m.put(0x78A2FC, 0)  # Actual drag direction virtual45D640.
m.allowed = [(0x45D640, 0x45D646), (0x45D800, 0x45D969), (0x456E90, 0x457060)]
m.run(0x456E90, (ACTOR, 0, 255, 255), eax=point, ecx=ACTOR + 0x60)
nearest = m.reg('eax')
assert 11 <= nearest <= 14
m.run(0x45D800, (bits(1 / 60),), ecx=controller, end=0x45D908)
actual = [m.get(descriptor + 8, 'i'), m.get(controller + 0x500),
          m.get(controller + 0x504), m.get(controller + 0x508)]
assert actual == [-1, 2, 11, 14], actual
assert m.get(controller + 0x4BC, 'B') == 1
assert 0x45D963 in m.visited and 0x45D908 in m.visited
assert 0x5A6700 not in m.visited and not m.events
record = dict(scope=__doc__, executable_sha256=hashlib.sha256(executable).hexdigest(),
              track_sha256=hashlib.sha256(track_raw).hexdigest(), raw=[27, 20],
              position=position, before=[14, 2, 11, 14], nearest=nearest,
              after=actual, dirty=True, reached_common_update=True,
              semantic_hooks=m.events)

if args.xbox:
    class Xbe:
        def __init__(self, path):
            self.data = path.read_bytes()
            assert hashlib.sha256(self.data).hexdigest() == 'cdd562ffcc70906b3fecd841db4dc10fd463664b05e03f896a8fb38b9a7d878a'
            assert self.data[:4] == b'XBEH'
            base = struct.unpack_from('<I', self.data, 0x104)[0]
            offset = struct.unpack_from('<I', self.data, 0x120)[0] - base
            count = struct.unpack_from('<I', self.data, 0x11C)[0]
            self.sections = [struct.unpack_from('<4I', self.data, offset + 56 * i + 4) for i in range(count)]

        def read(self, address, size):
            for va, _, offset, count in self.sections:
                if va <= address and address + size <= va + count:
                    start = offset + address - va
                    return self.data[start:start + size]
            raise ValueError('XBE address outside sections')

    class XboxMachine(Machine):
        def __init__(self, image):
            self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
            self.uc.mem_map(0x10000, 0x300000)
            self.uc.mem_map(0x1000000, 0x200000)
            for va, _, offset, count in image.sections:
                self.uc.mem_write(va, image.data[offset:offset + count])
            self.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
            self.hooks, self.events, self.allowed = {}, [], []
            self.visited = set()
            self.uc.hook_add(UC_HOOK_CODE, self.instruction)

    xbox_raw = (args.xbox / 'TRACKS/TRACKB1214.LZC').read_bytes()
    assert hashlib.sha256(xbox_raw).hexdigest() == '9912e5e846de6f1f3f21418232402908a8c6dc7bc9e4f614fd59edacad7afdcb'
    xbox_track = jdlz(xbox_raw)
    xbox_road = None
    for tag, at, end in chunks(xbox_track):
        if tag != 0x34121:
            continue
        offset = at + 8
        while offset < end:
            index = struct.unpack_from('<h', xbox_track, offset + 0xA)[0]
            count = struct.unpack_from('<h', xbox_track, offset + 0x10)[0]
            size = 0x27C + count * 0x88
            assert count >= 0 and offset + size <= end
            if index == 27:
                assert xbox_road is None
                xbox_road = xbox_track[offset:offset + size]
            offset += size
        assert offset == end
    assert xbox_road == routes[27], 'PC/Xbox supplied road differs'
    image = Xbe(args.xbox / 'default.xbe')
    x = XboxMachine(image)
    x.uc.mem_write(road, xbox_road)
    x.put(0x273A20 + 27 * 4, road)
    for k, value in enumerate(position):
        x.put(ACTOR + 0x60 + 4 * k, value, 'f')
    x.put(controller, 0x1F74B8)
    x.put(controller + 4, ACTOR)
    x.put(ACTOR + 0x2C, controller)
    x.put(controller + 0x3D8, descriptor)
    x.put(controller + 0x3E4, chain)
    x.put(chain + 4, 1)
    x.put(descriptor, road)
    x.put(descriptor + 4, point)
    x.put(descriptor + 8, 14)
    x.put(controller + 0x500, 2)
    x.put(controller + 0x504, 11)
    x.put(controller + 0x508, 14)
    direction = image.read(0x62D40, 6)
    assert direction[0] == 0xA1 and direction[-1] == 0xC3
    x.put(struct.unpack_from('<I', direction, 1)[0], 0)
    x.allowed = [(0x63040, 0x63160), (0x62DF0, 0x62E48), (0x5B900, 0x5BBF0),
                 (0x155D20, 0x155D3A), (0x62D40, 0x62D46), (0x50B00, 0x50B08)]
    x.run(0x63040, (bits(1 / 60),), ecx=controller, end=0x63154)
    xbox_actual = [x.get(descriptor + 8, 'i'), x.get(controller + 0x500),
                   x.get(controller + 0x504), x.get(controller + 0x508)]
    assert xbox_actual == actual, xbox_actual
    assert x.get(controller + 0x4BC, 'B') == 1
    assert 0x155D24 in x.visited and 0x63154 in x.visited
    assert 0x160700 not in x.visited and not x.events
    record['xbox'] = dict(executable_sha256=hashlib.sha256(image.data).hexdigest(),
                          track_sha256=hashlib.sha256(xbox_raw).hexdigest(), after=xbox_actual,
                          dirty=True, reached_common_update=True, semantic_hooks=x.events)
    print('PASS actual Xbox63040/62DF0 stores lane=-1 and reaches66E30; no hooks')
if args.out:
    assert args.out.as_posix().isascii() and not args.out.exists()
    args.out.write_text(json.dumps(record, indent=2) + '\n', 'ascii')
print('PASS actual45D800/45D920 stores lane=-1 and reaches462430; no hooks')

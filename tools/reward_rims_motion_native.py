"""Bounded original PC unique-mode and ordinary rim target setters, no hooks."""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path.cwd() / 'tools'))
from world_body_trace import Image
from physics256_native import ACTOR, Machine


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    args = parser.parse_args()
    raw = (args.root / 'speed.exe').read_bytes()
    assert hashlib.sha256(raw).hexdigest() == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    image = Image(raw)
    # Unique visual constructor: EDI=4 survives the UI lookup/message calls
    # (callee-saved register), then 4B2480 forwards EDI to 4B14E0 in EAX.
    assert image.read(0x4c7eff, 5) == bytes.fromhex('bf04000000')
    assert image.read(0x4c7f3d, 5) == bytes.fromhex('e83ea5feff')
    machine = Machine(image)
    machine.allowed = [(0x4c7eff, 0x4c7f04), (0x4b248f, 0x4b2496),
                       (0x4b14e0, 0x4b14fc), (0x50cd34, 0x50cd3e)]
    machine.run(0x4c7eff, end=0x4c7f04, edi=99)
    mode = machine.reg('edi')
    assert mode == 4
    machine.put(0x746de0, 0)
    machine.run(0x4b248f, end=0x4b14fc, ecx=ACTOR, edi=mode)
    unique = machine.get(0x746de0)
    assert machine.get(ACTOR + 0x124) == mode
    assert unique == struct.unpack('<I', struct.pack('<f', 20.0))[0]
    machine.run(0x50cd34, end=0x50cd3e)
    rim = machine.get(0x746de0)
    assert rim == struct.unpack('<I', struct.pack('<f', 1.0))[0]
    assert not machine.events
    print(json.dumps({'unique_mode': mode, 'unique_target_kmh': 20,
                      'normal_rim_target_kmh': 1, 'hooks': machine.events,
                      'boundary': 'Original setter fragments; UI lookup, allocation, scheduler and rendering excluded.'}))
    print('PASS original unique visual mode20 and normal rim inspection1, no hooks')


if __name__ == '__main__':
    main()

"""Verify original57A530/45FAF0 path lifetime without semantic hooks.

Synthetic node, actor and Driver storage; execute the actual copy/publish
functions from the authenticated PC executable. This is a publication test,
not a whole-world simulation or a trajectory parity claim.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from physics256_native import Machine
from world_body_trace import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, required=True)
    ap.add_argument('--evidence', type=Path, required=True)
    args = ap.parse_args()
    executable = (args.root / 'speed.exe').read_bytes()
    identity = 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(executable).hexdigest() != identity:
        raise ValueError('unsupported executable')
    image = Image(executable)
    rows = []
    for owner in (2, 3, 4):
        m = Machine(image)
        m.allowed = [(0x57a530, 0x57a5e9), (0x647b70, 0x647b81),
                     (0x45faf0, 0x45fb27), (0x459980, 0x459aba)]
        node, ref, actor, driver, incoming = [0x1100000+j*4096 for j in range(5)]
        for at, value in ((node+0x1d4, ref), (ref+0x24, 6),
                          (ref+0x90, actor), (actor+0x24, owner), (actor+0x30, driver)):
            m.put(at, value)
        old = bytearray(0x1c0)
        new = bytearray(0x1c0)
        old[0] = 1
        new[0] = 2
        struct.pack_into('<3f', old, 0x10, 15, 4, 2)
        struct.pack_into('<3f', new, 0x10, 25, -8, 3)
        struct.pack_into('<3f', new, 0x20, 50, -12, 4)
        m.uc.mem_write(driver+0x10, bytes(old))
        m.uc.mem_write(incoming, bytes(new))
        m.run(0x57a530, ebx=node)
        before = bytes(m.uc.mem_read(node+0x10, 0x1c0))
        m.run(0x45faf0, (incoming,), ecx=driver)
        same_epoch = bytes(m.uc.mem_read(node+0x10, 0x1c0))
        driver_after = bytes(m.uc.mem_read(driver+0x10, 0x1c0))
        m.run(0x57a530, ebx=node)
        next_epoch = bytes(m.uc.mem_read(node+0x10, 0x1c0))
        assert before == old and same_epoch == old
        assert driver_after == new and next_epoch == new and not m.events
        rows.append(dict(owner=owner, before=before[0], same_epoch=same_epoch[0],
                         driver_after=driver_after[0], next_epoch=next_epoch[0]))
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,
        semantic_hooks=0, cases=rows), indent=2)+'\n', encoding='ascii')
    print('PASS original path epoch: 3 owner types, Driver changes now, neighbor copy next epoch; semantic hooks: 0')


if __name__ == '__main__':
    main()

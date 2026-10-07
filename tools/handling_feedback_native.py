"""Issue251 authenticated original engine feedback producer witness.

Execute original0x00475D00 on synthetic flat, supported wheels. The PE's
matrix dispatch pointer is explicitly bound to its original scalar matrix
implementation; the live CPU dispatch selection is unverified. No original
force, trig or math function is replaced. This is not live-game telemetry.
"""
import argparse
import hashlib
import json
from pathlib import Path

from world_body_trace import Image, manifest
from physics256_native import Machine
from handling_brake_compare import EXE_SHA, BUN_SHA


def witness(root, out):
    data = (root/'speed.exe').read_bytes()
    bun = (root/'Global/GLOBALB.BUN').read_bytes()
    if hashlib.sha256(data).hexdigest() != EXE_SHA or hashlib.sha256(bun).hexdigest() != BUN_SHA:
        raise ValueError('unsupported retail inputs')
    image = Image(data)
    rows = []
    visited = set()
    allowed = [(0x475D00, 0x47623F), (0x564CD0, 0x564F20),
               (0x564B10, 0x564C00), (0x674898, 0x674920),
               (0x5ABDAF, 0x5ABF00), (0x584CD0, 0x584E00)]
    # Declared helper ranges include padding, which is never invoked; the
    # report's visited instruction addresses define the actual execution.
    for speed in (0.0, 8.0, 20.0, 35.0):
        for slip_mps in (-4.0, -1.0, 0.0, 1.0, 4.0):
            for locked in (0, 1):
                m = Machine(image); m.allowed = allowed
                engine, setup, body = 0x1100000, 0x1101000, 0x1102000
                m.put(0x6D4AC4, 0x5ABDB5)
                m.put(engine+0x18, setup); m.put(engine+0x14, body)
                # Setup layout is the original record+0x110 subobject.
                record = bun[1736752:1736752+3216]
                m.uc.mem_write(setup, record[0x110:])
                for k in range(4):
                    radius = m.get(setup+0x20+k*0x30, 'f')
                    m.put(engine+0x64+k*4, 1)  # synthetic road support
                    m.put(engine+0x54+k*4, 3.0, 'f')  # load kN
                    m.put(engine+0x24+k*4, (speed-slip_mps)/radius, 'f')
                    m.put(engine+0xB8+k*4, locked)
                for index, value in enumerate((1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)):
                    m.put(body+0x30+index*4, float(value), 'f')
                m.put(body+0xA0, speed, 'f')
                m.put(body+0x58, 1.0, 'f')
                m.put(engine+0x138, 1.0, 'f')  # no boost
                m.put(engine+0x13C, 1.0, 'f')  # scale
                m.put(engine+0x140, 1.0, 'f')  # low-slip coefficient
                m.put(engine+0x1E0, 0); m.put(0x73AD34, 0)
                m.run(0x475D00, ecx=engine)
                if m.events:
                    raise RuntimeError('producer unexpectedly needed a hook')
                visited.update(m.visited)
                rows.append(dict(body_speed_mps=speed, requested_long_slip_mps=slip_mps,
                    brake_locked=locked, load_kn=3.0,
                    measured_slip=[m.get(engine+0x94+k*8, 'f') for k in range(4)],
                    feedback=[m.get(engine+0x74+k*8, 'f') for k in range(4)],
                    feedback_other_axis=[m.get(engine+0x78+k*8, 'f') for k in range(4)],
                    body_force=[m.get(body+0x110+k*4, 'f') for k in range(3)]))
    report = dict(executable_sha256=EXE_SHA, globalb_sha256=BUN_SHA,
        boundary='Isolated original feedback producer, synthetic supported flat wheels, stock geometry, identity rotation, zero yaw/steer, fixed load3kN, no boost, no live body integration',
        dispatch_binding='Synthetic0x006D4AC4 -> original scalar0x005ABDB5; live implementation unknown',
        x87_control_word='0x007F synthetic; live environment unverified',
        producer='0x00475D00..0x0047623F', stores=['0x004761BC', '0x004761C2'],
        consumer='0x0047576B..0x00475781 reads engine+0x74+8k',
        allowed=[['%08X'%a, '%08X'%b] for a,b in allowed],
        visited=['%08X'%a for a in sorted(visited)], external_hooks=[], rows=rows)
    (out/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n',encoding='ascii')
    print('PASS: authenticated original feedback producer %d synthetic cases, no external hooks' % len(rows))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    relative = out.relative_to(Path.cwd())
    if not relative.parts[0].startswith('build') or not str(relative).isascii() or root == out or root in out.parents:
        parser.error('output must be a fresh ASCII build directory in the assigned workspace, outside retail')
    out.mkdir(parents=True,exist_ok=False)
    before = manifest(root); (out/'before.json').write_bytes(before)
    try:
        witness(root,out)
    finally:
        after = manifest(root); (out/'after.json').write_bytes(after)
        if before != after:
            raise RuntimeError('retail manifest changed')
        print('PASS: complete retail content/metadata manifest unchanged')


if __name__ == '__main__':
    main()

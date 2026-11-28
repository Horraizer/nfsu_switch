"""Show native CalcBurnout sampling and timer cadence on a declared test signal.

The continuous test slip is not a vehicle trajectory or a gameplay adapter.
Each original Move has one policy observation before its inner solver loop.
This test supplies that observation directly, including its global world dt.
It never starts a game or replaces recorded values in production.
"""
import argparse
import hashlib
import json
from pathlib import Path

from launch_traction_boundary import PC_HASH, authenticated, bits, host
from physics256_native import ACTOR, PHYSICS, RIDE, STATUS, Machine
from world_body_trace import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
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
    image = Image(authenticated(args.retail/'speed.exe', PC_HASH))
    ranges = [(0x479640, 0x479C30), (0x4280A0, 0x4280E3),
              (0x454F30, 0x454FE4), (0x674898, 0x67490D),
              (0x464ED0, 0x464F11), (0x564DB0, 0x564EF6),
              (0x564CD0, 0x564DA8), (0x564B10, 0x564BA6)]
    inputs, expected, epochs, activation = [], [], [], []
    for hz in (24, 30, 60, 120):
        m = Machine(image)
        m.allowed = ranges
        owner, setup, box = ACTOR+0x40, 0x1110000, 0x1111000
        m.put(PHYSICS+0x5C, ACTOR)
        m.put(ACTOR+0x24, 2)
        m.put(ACTOR+0x3D0, 1, 'f')
        m.put(ACTOR+0x10, RIDE)
        m.put(RIDE+0x424, 1, 'B')
        m.put(PHYSICS+0x58, owner)
        m.put(PHYSICS+0x54, setup)
        m.put(setup+0x1C0, 1, 'f')
        m.put(PHYSICS+0x4C, box)
        m.put(0x73619C, STATUS)
        m.put(STATUS, 4)
        m.put(0x73457C, 1/hz, 'f')
        m.put(owner+0x30, 1, 'f')
        m.put(owner+0x34, 0, 'f')
        m.put(owner+0x374, 0, 'H')
        m.put(owner+0x3D0, 1, 'f')
        for wheel in range(4):
            m.put(PHYSICS+0x38+4*wheel, 0x1112000+wheel*0x1000)
        for index in range(int(2.1*hz)+1):
            time = index/hz
            slip = 40*time if time < .12 else 3 if time < .8 else 0
            loads, slips, grips = [3]*4, [0, 0, slip, 0], [.85]*4
            before = [m.get(owner+0x3C8), *[m.get(owner+offset, 'f')
                      for offset in (0x3CC, 0x3D0, 0x3D4)]]
            inputs.append([1, 1, *before, 1, 1/hz, *loads, *slips, *grips])
            for wheel in range(4):
                address = 0x1112000+wheel*0x1000
                for offset, value in ((0x158, loads[wheel]), (0x160, slips[wheel]),
                                      (0x178, grips[wheel])):
                    m.put(address+offset, value, 'f')
            m.run(0x479640, (PHYSICS,))
            after = [m.get(owner+0x3C8), *[m.get(owner+offset, 'f')
                     for offset in (0x3CC, 0x3D0, 0x3D4)],
                     *[m.get(0x1112000+wheel*0x1000+0x178, 'f') for wheel in range(4)]]
            expected.append(after)
            epochs.append([hz, time])
            if before[0] == 0 and after[0] != 0:
                activation.append(dict(hz=hz, seconds=time, slip=slip, state=after[:4]))
        assert not m.events
    out.mkdir(parents=True)
    actual = host(repo, args.build.resolve(), out, inputs)
    differences = [i for i, (a, b) in enumerate(zip(actual, expected))
                   if int(a[0]) != int(b[0]) or any(bits(x) != bits(y) for x, y in zip(a[1:], b[1:]))]
    report = dict(executable_sha256=PC_HASH, epochs=epochs, inputs=inputs,
                  pc=expected, port=actual, differences=differences, first_activation=activation,
                  source_ranges=[dict(start=hex(a), end=hex(b),
                                      sha256=hashlib.sha256(image.read(a, b-a)).hexdigest())
                                 for a, b in ranges],
                  tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  boundary=__doc__)
    (out/'comparison.json').write_bytes((json.dumps(report, indent=2)+'\n').encode('ascii'))
    if differences:
        raise RuntimeError('native policy mismatch: '+str(differences[:5]))
    print('PASS original owner policy:', len(expected), 'calls; exact state/grip at24/30/60/120Hz')
    print('First activation:', json.dumps(activation, separators=(',', ':')))


if __name__ == '__main__':
    main()

"""Verify native late Driver, field restore, path publication and brake wiring.

Controlled restored states are not claimed as ordinary race activations.
The executable is authenticated and read-only; no game process is used.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
from unicorn.x86_const import UC_X86_REG_EIP

from world_body_trace import Image
from physics256_native import Machine, ACTOR, DRIVER, PHYSICS, bits
from controller293_native import fixture
from ai_controller_cadence_vectors import publish


def value(word):
    return struct.unpack('<f', struct.pack('<I', word))[0]


def restore(m, offset, data):
    stream = 0x1170000
    m.allowed.extend([(0x571c20, 0x571d89), (0x571ba0, 0x571c16)])
    m.put(stream + 0x3d, 1, 'B')
    m.put(stream + 0x40, 0, 'B')
    m.put(stream + 0x150, 0)
    m.put(stream + 0x154, len(data))
    m.uc.mem_write(stream + 0x158, data)
    m.run(0x571c20, (stream, DRIVER + offset, len(data), 0xffffffff, 0))
    assert bytes(m.uc.mem_read(DRIVER + offset, len(data))) == data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--write-fixture', action='store_true')
    args = parser.parse_args()
    work = Path.cwd().resolve()
    relative = args.out.resolve().relative_to(work)
    assert relative.as_posix().isascii() and relative.parts[0].startswith('build')
    args.out.mkdir(parents=True, exist_ok=False)
    raw = (args.root / 'speed.exe').read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    assert digest == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    image = Image(raw)
    points = [(10., -1., 0.), (35., 5., 0.), (55., -6., 0.), (100., 30., 0.)]
    threshold = struct.unpack('<I', image.read(0x6ccb80, 4))[0]
    dt = value(bits(1 / 60))
    rows = []
    boundary = [value(threshold + offset) for offset in (-1, 0, 1)]
    velocities = [(speed, abs(speed), 0.) for speed in (*boundary, -70., 70.)]
    # Keep longitudinal and total speed independent. Use physical lateral
    # motion at every exact/adjacent total-speed boundary, in both directions.
    for forward in (-8., 0., 8.):
        for magnitude in (*boundary, 38.):
            lateral = value(bits(math.sqrt(magnitude * magnitude - forward * forward)))
            velocities.append((forward, magnitude, lateral))
    velocities.append((0., 0., 0.))
    for selector, drag in ((1, False), (3, False), (4, False), (1, True)):
        for speed, magnitude, lateral in velocities:
            for timer in (0., dt, value(bits(.1))):
                for forced in (False, True):
                    for nos in (False, True):
                        for enabled in (False, True):
                            m = fixture(image, points, speed=speed, category=1, heading=1500)
                            m.put(ACTOR + 0x74, lateral, 'f')
                            m.put(ACTOR + 0x3d4, magnitude, 'f')
                            m.put(ACTOR + 0x28, selector)
                            m.put(0x7361a8, int(drag))
                            m.put(ACTOR + 0x3c, 0x1180000) # valid empty contact-pair owner
                            m.put(ACTOR + 0x40, m.get(ACTOR + 0x10))
                            m.put(PHYSICS + 0x48, 0x1155000)
                            for wheel in range(4):
                                m.put(ACTOR + 0x350 + wheel, 1, 'B')
                            m.put(DRIVER + 0x1fc, .75, 'f')
                            m.put(DRIVER + 0x22c, 40., 'f')
                            m.put(ACTOR + 0x3fc, 100000 - 67 if nos else 0)
                            m.put(ACTOR + 0x3f8, 8000)
                            restore(m, 0x210, struct.pack('<f', timer))
                            restore(m, 0x220, bytes([forced]))
                            m.run(0x45e180, (bits(40.), 0, 0, int(enabled)), ecx=DRIVER)
                            try:
                                m.run(0x45e970, (bits(dt),), ecx=DRIVER)
                            except Exception:
                                print('NATIVE_SETUP_FAILURE', selector, speed, magnitude, lateral, timer, forced, nos, enabled,
                                      hex(m.uc.reg_read(UC_X86_REG_EIP)))
                                raise
                            expected = [m.get(DRIVER + offset) for offset in (0x1dc, 0x1e4, 0x1e0, 0x210)]
                            expected += [m.get(DRIVER + 0x1d0, 'H'), m.get(DRIVER + 0x1d4, 'i')]
                            assert not m.events
                            rows.append([selector, drag, bits(speed), bits(magnitude), bits(lateral), bits(timer),
                                         forced, nos, enabled, expected])
    text = '// Original45E970 after genuine571C20 field restore; controlled state, not ordinary activation.\n'
    for selector, drag, speed, magnitude, lateral, timer, forced, nos, enabled, expected in rows:
        text += '{%dU,%s,0x%08xU,0x%08xU,0x%08xU,0x%08xU,%s,%s,%s,{' % (
            selector, str(drag).lower(), speed, magnitude, lateral, timer,
            str(forced).lower(), str(nos).lower(), str(enabled).lower())
        text += ','.join('0x%08xU' % word for word in expected[:4])
        text += ',%dU,%d}},\n' % (expected[4], expected[5])
    outputs = {'tests/ai_driver_late_native.inc': text}

    path_rows = []
    for raw_points in ([(32., 32., 10.), (33., 32., 10.), (33., 43., 10.)],
                       [(32., 32., 10.), (43., 26., 10.), (72., 27., 10.),
                        (76., 41., 10.), (101., 63., 10.), (132., 64., 10.)]):
        first = publish(image, raw_points)
        first_points = [[value(word) for word in point] for point in first['points']]
        second = publish(image, first_points)
        second_points = [[value(word) for word in point] for point in second['points']]
        third = publish(image, second_points)
        path_rows.append({'first': first, 'second': second, 'cold': third})
    text = '// Original459980 double controller publication, then a cold mover rebuild; lengths precede redistribution.\n'
    for row in path_rows:
        text += '{%dU,{' % len(row['first']['points'])
        for which in ('first', 'second', 'cold'):
            if which != 'first':
                text += '},{'
            text += ','.join('{' + ','.join('0x%08xU' % word for word in point) + '}'
                             for point in row[which]['points'])
        text += '},0x%08xU,0x%08xU},\n' % (row['second']['length'], row['cold']['length'])
    outputs['tests/ai_driver_wiring_path_native.inc'] = text

    for name, text in outputs.items():
        data = text.encode('ascii')
        path = work / name
        if args.write_fixture:
            path.write_bytes(data)
        else:
            assert path.read_bytes() == data, name
    report = {'exe_sha256': digest, 'driver_cases': len(rows), 'path_cases': len(path_rows),
              'threshold_bits': threshold, 'field_restore': 'Actual571C20/571BA0 reading branch; no hooks',
              'late_columns': ['selector', 'drag', 'forward_bits', 'magnitude_bits', 'lateral_bits',
                               'timer_bits', 'forced', 'nos', 'enabled', 'expected'],
              'late': rows, 'paths': path_rows,
              'scope': 'Controlled original states restored through serialization. No ordinary positive trigger is inferred.'}
    (args.out / 'oracle.json').write_bytes(
        (json.dumps(report, indent=2, ensure_ascii=True) + '\n').encode('ascii'))
    for row in rows:
        selector, drag, forward, magnitude, lateral, timer, forced, nos, enabled, expected = row
        if selector == 3 and not drag and value(timer) == value(bits(.1)) and not forced and not nos and enabled:
            print('NATIVE_SPEED forward=%g magnitude=%.9g lateral=%.9g hb=%g gas=%g brake=%g' % (
                value(forward), value(magnitude), value(lateral), value(expected[2]),
                value(expected[0]), value(expected[1])))
    print('PASS native Driver cases', len(rows), 'controller/cold path cases', len(path_rows))


if __name__ == '__main__':
    main()

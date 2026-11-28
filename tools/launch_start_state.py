"""Read launch provenance from an existing GNDE69 grid-cache image, offline.

The sound/global/world random fields are current saved states, not claims
about their initialization epoch. Only RaceParameters+148 is the saved
before-settings seed. This tool never imports Dolphin or changes a capture.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def decode(path):
    cache = path.read_bytes()
    if len(cache) < 5:
        raise ValueError('truncated cache')
    size = int.from_bytes(cache[:4], 'little')
    if not 0 < size < min(len(cache)-4, 65536):
        raise ValueError('invalid cache header')
    metadata = json.loads(cache[4:4+size])
    raw = zlib.decompress(cache[4+size:])
    digest = hashlib.sha256(raw).hexdigest()
    if digest != metadata['sha256']:
        raise ValueError('cache checksum mismatch')
    origin = raw.find(b'GNDE69\0\0')
    if origin < 0:
        raise ValueError('supported MEM1 origin absent')

    def memory(address, length):
        if not 0x80000000 <= address <= address+length <= 0x81800000:
            raise ValueError('invalid MEM1 address')
        offset = origin + address - 0x80000000
        result = raw[offset:offset+length]
        if len(result) != length:
            raise ValueError('truncated MEM1 range')
        return result

    def u(address):
        return struct.unpack('>I', memory(address, 4))[0]

    def f(address):
        return struct.unpack('>f', memory(address, 4))[0]

    if u(0x80000020) != 0x0D15EA5E or u(0x80000028) != 0x01800000:
        raise ValueError('unsupported MEM1 header')
    world = u(0x803C0B70)
    race = u(0x803C0C70)
    count = u(world+0x20)
    if not 0 < count <= 64:
        raise ValueError('invalid racing car table')
    mapper = u(0x803C108C)
    playlists = []
    for i in range(12):
        length = u(mapper+4*i)
        if length > 64:
            raise ValueError('invalid NIS playlist')
        pointer = u(mapper+0x30+4*i)
        playlists.append([u(pointer+4*j) for j in range(length)])
    emitters = []
    emitter_table, emitter_count = u(0x803C0CC8), u(0x803C0CCC)
    if emitter_count > 20:
        raise ValueError('invalid emitter table')
    for i in range(emitter_count):
        emitter = emitter_table + 0x108*i
        emitters.append(dict(index=i, road=u(emitter),
                             assigned=memory(emitter+0x100, 1)[0],
                             base=[f(emitter+0x30+4*j) for j in range(8)],
                             spread=[f(emitter+0x50+4*j) for j in range(8)],
                             samples=[f(emitter+0xE0+4*j) for j in range(8)]))
    actors = []
    for i in range(count):
        car = u(world+0x28+4*i)
        mover = u(car+0x34)
        selector = u(car+0x28)
        actor = struct.unpack('>h', memory(car+10, 2))[0]
        row = dict(actor=actor, selector=selector, car=car, mover=mover)
        # A saved instruction boundary can fall between publishing selector1
        # and publishing the new mover pointer. Do not fabricate its state.
        row['mover_available'] = mover != 0
        if selector == 1 and mover != 0:
            engine, gearbox, body = [u(mover+o) for o in (0x48, 0x4C, 0x20)]
            row.update(engine=dict(angular=f(engine+0x14), throttle=f(engine+0x78),
                                   feedback=f(engine+0x7C), multiplier=f(engine+0x80),
                                   cutoff_until=f(engine+0x9C)),
                       gearbox=dict(clutch=f(gearbox+0x14), requested=u(gearbox+0x38),
                                    physical=u(gearbox+0x3C), mode=u(gearbox+0x44)),
                       position=[f(body+0x18+4*j) for j in range(3)], wheels=[])
            for j in range(4):
                wheel = u(mover+0x38+4*j)
                row['wheels'].append(dict(index=j, spin=f(wheel+0x28),
                                          load=f(wheel+0x158), slip=f(wheel+0x160),
                                          owner_grip=f(wheel+0x178)))
        actors.append(row)
    return dict(schema='nfsu-gn-de69-start-state-1', cache_file=path.name,
                cache_sha256=hashlib.sha256(cache).hexdigest(),
                state_sha256=digest, case=metadata['case'], mem1_offset=origin,
                parameters_seed=u(0x802F56D0+0x148),
                observed_setup_seed=metadata['observed']['setup_rng'],
                clock=u(0x803C31B8), world=world,
                current_rng=dict(global_seed=u(0x803C19BC),
                                 sound_seed=u(0x803C15E0), world_seed=u(world+0x2D0)),
                current_time=dict(real_units=u(0x803C1E1C),
                                  real_error=f(0x803C1E24)),
                race_timer=dict(go=u(race+4), countdown_start=u(race+8),
                                countdown_interval=f(race+12)),
                # GC constructor800866C0 stores the vtable at manager+D8.
                nis=dict(vtable=u(0x80307FBC+0xD8), scene=u(0x80307FBC),
                         current=u(0x80307FBC+8),
                         next=u(0x80307FBC+12), playlists=playlists),
                emitters=emitters, actors=actors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.out.resolve()
    try:
        relative = output.relative_to(root)
    except ValueError:
        parser.error('output must stay inside this checkout')
    if not relative.parts or not all(part.isascii() for part in relative.parts) or output.exists():
        parser.error('fresh ASCII output path required')
    report = decode(args.state)
    if report['parameters_seed'] != report['observed_setup_seed']:
        raise ValueError('saved settings seed does not match the observed phase')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes((json.dumps(report, indent=2, allow_nan=False)+'\n').encode('ascii'))
    print('PASS saved launch:', report['case']['event'], report['case']['car'],
          'scene', report['nis']['current'], 'settings seed', report['parameters_seed'])


if __name__ == '__main__':
    main()

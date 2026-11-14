"""Prepare an independent all-unlocks test profile for PC Underground 1.4 USA."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import zlib

sys.dont_write_bytecode = True
from physics256_native import Machine
from world_body_trace import Image, chunks, jdlz

SAVE_BODY_SIZE = 0xF660
CAREER_OFFSET = 0x6634
EVENT_STATUS = 0x34C
PROFILE = 0x1100000
SECOND_PROFILE = 0x1120000
SOURCE = 0x1001000
TARGET = 0x10A0000
FRONT = 0x1020000
STYLE = 0x10E0000


def sha(data):
    return hashlib.sha256(data).hexdigest()


def hash_files(folder):
    return {p.name: sha(p.read_bytes()) for p in folder.iterdir() if p.is_file()}


def verify_closed():
    result = subprocess.run(
        ['powershell', '-NoProfile', '-Command',
         'if (Get-Process -Name speed -ErrorAction SilentlyContinue) { exit 1 }'],
        check=False,
    )
    if result.returncode:
        raise RuntimeError('Close the original game before installing a profile')


def profile_names(registry):
    count, = struct.unpack_from('<I', registry)
    assert len(registry) == 4 + 32 * count
    return [registry[4 + 32*i:36 + 32*i].split(b'\0')[0].decode('ascii')
            for i in range(count)]


def body(data, offset, size):
    assert len(data) == offset + size + 4
    assert struct.unpack_from('<I', data)[0] == 19
    payload = data[offset:-4]
    assert struct.unpack_from('<I', data, len(data)-4)[0] == zlib.adler32(payload)
    return payload


def read_retail(retail):
    exe = (retail / 'speed.exe').read_bytes()
    front = jdlz((retail / 'FrontEnd/FrontB.lzc').read_bytes())
    global_data = (retail / 'Global/GLOBALB.BUN').read_bytes()
    assert sha(exe) == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    assert sha(front) == '94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77'
    assert sha(global_data) == '39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
    return exe, front, global_data


def native_inputs(retail):
    exe, front, global_data = read_retail(retail)
    machine = Machine(Image(exe))
    machine.allowed = [
        (0x4AB120, 0x4AB330), (0x41DD10, 0x41DD80), (0x4AAC70, 0x4AACA7),
        (0x5A0060, 0x5A012C), (0x5A0350, 0x5A03D0), (0x5A1550, 0x5A17D0),
        (0x4AA970, 0x4AA9C7), (0x4AA9D0, 0x4AAAB1), (0x5A3630, 0x5A366D),
        (0x5A0FC0, 0x5A1100),
    ]
    start, end = next((a, b) for t, a, b in chunks(front) if t == 0x80034A00)
    machine.uc.mem_write(FRONT, front[start:end])
    machine.run(0x5A00A0, (FRONT,))
    assert machine.reg('eax') == 1
    events = {i: machine.get(0x733FF8 + 4*i) for i in range(256)}
    events = {i: ptr for i, ptr in events.items() if ptr and i <= 113}
    assert len(events) == 113 and 0 in events and 113 in events
    start, end = next((a, b) for t, a, b in chunks(global_data) if t == 0x34A08)
    start = (start + 23) & ~15
    records = global_data[start:end]
    assert len(records) % 32 == 0 and 0 < len(records) // 32 <= 128
    style = list(struct.iter_unpack('<8I', records))
    machine.uc.mem_write(STYLE, records)
    machine.put(0x7343F8, STYLE)
    machine.put(0x7343FC, len(style))
    return machine, events, style


def read_profile(machine, save_data, option_data, profile):
    machine.uc.mem_write(SOURCE, option_data)
    machine.run(0x4AAC70, eax=SOURCE, ecx=len(option_data), ebx=profile, end=0x4AACA7)
    machine.uc.mem_write(SOURCE, save_data)
    machine.run(0x4AB200, eax=SOURCE, ecx=len(save_data), edx=profile)


def read_bytes(machine, address, size):
    return bytes(machine.uc.mem_read(address, size))


def encode(machine, header, payload):
    machine.uc.mem_write(TARGET, payload)
    machine.run(0x41DD10, (0, TARGET), ebx=len(payload), budget=3000000)
    checksum = machine.reg('eax')
    assert checksum == zlib.adler32(payload)
    return header + payload + struct.pack('<I', checksum)


def complete_profile(machine, events, style, profile):
    career = profile + CAREER_OFFSET
    assert machine.get(career + 8) != 0
    assert machine.get(career + 0x88B0) == 0, 'Choose a base save from outside a race'
    for event in events:
        machine.put(career + EVENT_STATUS + event, 2, 'B')
    machine.put(career + 0x1374, 1_000_000)
    points = max(record[0] for record in style)
    machine.put(profile + 0x210, points)
    machine.run(0x4AA970, (profile, points), ebx=profile + 0x214)
    assert read_bytes(machine, profile + 0x214, len(style)) == b'\x01' * len(style)

    # Give the owned parts table each unlocked standard performance package.
    # The current installed configuration is inherited from the template.
    for field in range(9):
        machine.run(0x5A0350, eax=field)
        category = machine.reg('eax')
        row = career + 0x450 + category * 0x90
        for level in range(1, 4):
            machine.run(0x5A3630, (career + 0x44C,), eax=category, edx=level)
            if not machine.reg('eax') & 255:
                slot = next(i for i in range(36) if machine.get(row + 4*i) == 0)
                machine.put(row + 4*slot, level)

    # Native magazine records hold an earned byte and a car configuration.
    # Use the template's career ride for the synthetic cover photos.
    ride = read_bytes(machine, career, 0x338)
    for i in range(27):
        entry = career + 0x13A8 + i * 0x454
        if machine.get(entry, 'B') == 0:
            machine.uc.mem_write(entry + 4, ride)
            machine.put(entry, 1, 'B')
        machine.put(entry + 1, 1, 'B')
        machine.put(career + 0x8884 + i, 1, 'B')


def verify_unlocks(machine, events, style, profile):
    career = profile + CAREER_OFFSET
    counts = {'campaign_wins': len(events), 'part_rewards': 0, 'car_rewards': 0,
              'track_rewards': 0, 'style_rewards': len(style), 'bonus_cars': 0,
              'magazines': 27}
    rewards = set()
    for event, ptr in events.items():
        assert machine.get(career + EVENT_STATUS + event, 'B') == 2
        machine.run(0x5A0FC0, ecx=ptr, edx=career)
        assert not machine.reg('eax') & 255
        count = machine.get(ptr + 0x74)
        assert count <= 5
        rewards.update(struct.unpack('<3I', read_bytes(machine, ptr + 0x38 + i*12, 12))
                       for i in range(count))
    for kind, key, variant in sorted(rewards):
        if kind == 0:
            machine.run(0x5A1550, (career, key), edi=variant)
            counts['part_rewards'] += 1
        elif kind == 2:
            machine.run(0x5A1630, ebx=career, edi=key)
            counts['car_rewards'] += 1
        elif kind == 3:
            machine.run(0x5A16F0, (career,), ebx=key, edi=variant)
            counts['track_rewards'] += 1
        else:
            continue
        assert machine.reg('eax') & 255, (kind, key, variant)
    for i, (_, packed_kind, key, *_) in enumerate(style):
        kind = packed_kind & 0xFFFF
        assert kind in (0, 2, 3)
        machine.run(0x4AA9D0, (profile, kind, key))
        assert machine.reg('eax') & 255, (i, kind, key)
        counts['bonus_cars'] += kind == 3
    for field in range(9):
        machine.run(0x5A0350, eax=field)
        category = machine.reg('eax')
        for level in range(1, 4):
            machine.run(0x5A3630, (career + 0x44C,), eax=category, edx=level)
            assert machine.reg('eax') & 255, (category, level)
    for i in range(27):
        assert read_bytes(machine, career + 0x13A8 + i * 0x454, 2) == b'\x01\x01'
    assert machine.get(career + 0x1374) == 1_000_000
    assert not machine.events
    return counts


def prepare(retail, saved, base, name, output):
    names = [name + ext for ext in ('.cfg', '.opt', '_autosave.ugd')]
    before = hash_files(saved)
    original_registry = (saved / 'Profiles.usr').read_bytes()
    registry_names = profile_names(original_registry)
    assert base in registry_names and name.casefold() not in (n.casefold() for n in registry_names)
    assert not any((saved / n).exists() for n in names)

    source_save = (saved / (base + '_autosave.ugd')).read_bytes()
    source_opt = (saved / (base + '.opt')).read_bytes()
    cfg = (saved / (base + '.cfg')).read_bytes()
    old_save = body(source_save, 47, SAVE_BODY_SIZE)
    old_opt = body(source_opt, 4, 0x2E5)
    assert old_opt[4:17].split(b'\0')[0] == base.encode('ascii')
    profile_name = name.encode('ascii').ljust(13, b'\0')
    new_opt = old_opt[:4] + profile_name + old_opt[17:]

    machine, events, style = native_inputs(retail)
    read_profile(machine, old_save, new_opt, PROFILE)
    assert read_bytes(machine, PROFILE, 13) == profile_name
    machine.run(0x4AB120, (PROFILE, SAVE_BODY_SIZE), eax=TARGET)
    assert read_bytes(machine, TARGET, SAVE_BODY_SIZE) == old_save

    complete_profile(machine, events, style, PROFILE)
    machine.run(0x4AB120, (PROFILE, SAVE_BODY_SIZE), eax=TARGET)
    payload = read_bytes(machine, TARGET, SAVE_BODY_SIZE)
    save = encode(machine, source_save[:47], payload)
    opt = encode(machine, source_opt[:4], new_opt)

    read_profile(machine, body(save, 47, SAVE_BODY_SIZE), body(opt, 4, 0x2E5), SECOND_PROFILE)
    assert read_bytes(machine, SECOND_PROFILE, 13) == profile_name
    machine.run(0x4AB120, (SECOND_PROFILE, SAVE_BODY_SIZE), eax=TARGET)
    assert read_bytes(machine, TARGET, SAVE_BODY_SIZE) == payload
    counts = verify_unlocks(machine, events, style, SECOND_PROFILE)
    new_registry = (struct.pack('<I', len(registry_names) + 1)
                    + original_registry[4:] + name.encode('ascii').ljust(32, b'\0'))
    assert profile_names(new_registry) == registry_names + [name]

    assert hash_files(saved) == before, 'Source saves changed during preparation'
    output.mkdir(parents=True, exist_ok=False)
    files = dict(zip(names, (cfg, opt, save)))
    files['Profiles.usr'] = new_registry
    for filename, data in files.items():
        (output / filename).write_bytes(data)
    record = dict(profile=name, before=before, staged=hash_files(output), unlocks=counts)
    (output / 'manifest.json').write_text(json.dumps(record, indent=2) + '\n', encoding='ascii')
    print(json.dumps(dict(profile=name, unlocks=counts), indent=2))


def write_new(path, data):
    stream = path.open('xb')
    try:
        with stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
    except Exception:
        path.unlink(missing_ok=True)
        raise


def install(saved, output):
    verify_closed()
    manifest = json.loads((output / 'manifest.json').read_text('ascii'))
    name = manifest['profile']
    assert name.isascii() and name.isalnum() and len(name) <= 12
    names = [name + ext for ext in ('.cfg', '.opt', '_autosave.ugd')]
    assert set(manifest['staged']) == {*names, 'Profiles.usr'}
    staged = {n: (output / n).read_bytes() for n in manifest['staged']}
    assert {n: sha(data) for n, data in staged.items()} == manifest['staged']
    assert hash_files(saved) == manifest['before'], 'Source saves changed since preparation'
    registry = saved / 'Profiles.usr'
    old_registry = registry.read_bytes()
    assert profile_names(staged['Profiles.usr']) == profile_names(old_registry) + [name]
    (output / 'Profiles.before.usr').write_bytes(old_registry)
    temporary = saved / (name + '-registry-install.tmp')
    created = []
    try:
        for filename in names:
            path = saved / filename
            write_new(path, staged[filename])
            created.append(path)
        write_new(temporary, staged['Profiles.usr'])
        created.append(temporary)
        verify_closed()
        os.replace(temporary, registry)
    except Exception:
        for path in reversed(created):
            path.unlink(missing_ok=True)
        raise
    expected = manifest['before'] | manifest['staged']
    assert hash_files(saved) == expected
    print('Installed', name, '- prior profile files unchanged')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--retail', type=Path)
    parser.add_argument('--base', default='1')
    parser.add_argument('--name', default='test')
    parser.add_argument('--install', action='store_true')
    args = parser.parse_args()
    saved = Path(os.environ['PROGRAMDATA']) / 'NFS Underground'
    output = args.out.resolve()
    if args.install:
        install(saved, output)
    else:
        assert args.retail is not None
        assert all(n.isascii() and n.isalnum() and 1 <= len(n) <= 12
                   for n in (args.base, args.name))
        assert not output.is_relative_to(saved.resolve())
        assert not output.is_relative_to(args.retail.resolve())
        prepare(args.retail, saved, args.base, args.name, output)


if __name__ == '__main__':
    main()

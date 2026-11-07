"""Read-only issue258 source research; Python3.8+, Capstone5.0.7, Unicorn2.1.4.

No executable is launched or copied. Outputs are disassembly, provenance,
full manifests and bounded synthetic-memory experiment results, not assets.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct

from event0_trace import (Image, INPUTS, manifest, jdlz,
                          preset_records, event_records, grid_records)


# Exclusive instruction boundaries. Names describe scope, not recovered symbols.
# Slices are explicitly named; they are not alleged complete routines.
RANGES = [
    ('role_partition', 0x41FE50, 0x41FF47),
    ('id_lookup', 0x41FF50, 0x41FF77),
    ('world_update', 0x420B40, 0x421163),
    ('grid_instantiation', 0x422A70, 0x422B70),
    ('car_factory', 0x422410, 0x42257D),
    ('car_construct', 0x425350, 0x42598A),
    ('car_destroy', 0x425990, 0x425AE3),
    ('remove_all', 0x422B70, 0x422BA8),
    ('controller_replace', 0x425FE0, 0x4260F6),
    ('physics_replace', 0x426110, 0x4262A6),
    ('physics_select', 0x425150, 0x425195),
    ('controller_base', 0x458A50, 0x458ABB),
    ('ai_driver_construct', 0x4620C0, 0x462228),
    ('ai_controller_init_slice', 0x44A330, 0x44A3F5),
    ('ai_route_init', 0x44CD30, 0x44CDD5),
    ('real_controller_construct', 0x59BFC0, 0x59C077),
    ('ai_car_driver_init_slice', 0x45DBB0, 0x45DBF0),
    ('real_car_driver_init_slice', 0x460E70, 0x460E91),
    ('physics_vtable_slice', 0x4740D0, 0x474117),
    ('driver_update_slice', 0x4615E0, 0x461773),
    ('traffic_construct_dispatch', 0x598BF0, 0x598C72),
    ('traffic_create', 0x5992F0, 0x5995D8),
    ('traffic_update', 0x598600, 0x5988B6),
    ('traffic_suspend', 0x59B710, 0x59B78C),
    ('traffic_resume', 0x59B790, 0x59B837),
    ('active_setter', 0x432D30, 0x432D3D),
    ('status_register', 0x434A80, 0x434B46),
    ('lap_init', 0x4333C0, 0x43349A),
    ('lap_update', 0x4338C0, 0x433E1A),
    ('status_update', 0x435570, 0x435A64),
    ('rank_change_notices', 0x434F80, 0x4350C2),
    ('rank', 0x434ED0, 0x434F7F),
    ('finish', 0x4350D0, 0x4353DE),
    ('first_unused_place', 0x435BD0, 0x435C0B),
    ('stage', 0x434B50, 0x434D0A),
    ('status_init', 0x434590, 0x434637),
    ('gap_table_construct', 0x432E80, 0x43333D),
    ('track_projection', 0x443A10, 0x443C88),
    ('post_contact_projection_slice', 0x426A95, 0x426B03),
    ('session_teardown_slice', 0x4221B0, 0x42221A),
]

# Separately specified claims at exact instruction boundaries, not a count
# of whatever the decoder happened to find. The executable hash pins all bytes.
EXPECTED = {
    0x425598: ('movsx', 'cx, byte ptr [eax + 1]'),
    0x42559D: ('mov', 'word ptr [ebx + 0xa], cx'),
    0x41FF22: ('mov', 'word ptr [edx + 8], ax'),
    0x4257F7: ('mov', 'byte ptr [ebx + 0x4d0], 1'),
    0x4258C6: ('mov', 'byte ptr [ebx + 0x8a9], 1'),
    0x420CBA: ('call', 'dword ptr [edx + 0xc]'),
    0x420CFA: ('call', 'dword ptr [eax + 0xc]'),
    0x420D39: ('call', 'dword ptr [edx + 0xc]'),
    0x420DA9: ('mov', 'ecx, dword ptr [ebx + 0x30]'),
    0x420DB2: ('call', 'dword ptr [eax + 4]'),
    0x420E3D: ('call', '0x426930'),
    0x421155: ('call', '0x435570'),
    0x4260E4: ('mov', 'dword ptr [esi + 0x30], eax'),
    0x426235: ('mov', 'dword ptr [esi + 0x34], eax'),
    0x434AB0: ('call', '0x4333c0'),
    0x434B05: ('call', '0x4333c0'),
    0x43560A: ('call', '0x4338c0'),
    0x433DB1: ('fst', 'dword ptr [edi + 0x20]'),
    0x4356D1: ('call', '0x434ed0'),
    0x435A4F: ('call', '0x4350d0'),
    0x43535D: ('mov', 'byte ptr [esi + 0x8a9], 0'),
    0x434615: ('call', '0x432e80'),
    0x434622: ('mov', 'dword ptr [esi + 0x564], eax'),
    0x4330C6: ('movzx', 'edx, byte ptr [ecx + edi + 0x6a]'),
    0x426AFE: ('call', '0x443a10'),
    0x443C7F: ('fstp', 'dword ptr [esi + 0x10]'),
    0x599463: ('call', '0x425350'),
    0x599499: ('call', '0x425fe0'),
    0x599565: ('call', '0x432d30'),
    0x422B8B: ('call', '0x425990'),
    0x4221B1: ('call', '0x422b70'),
}


def authenticate(root):
    result, blobs = {}, {}
    for name, expected in INPUTS.items():
        data = (root / name).read_bytes()
        actual = hashlib.sha256(data).hexdigest().upper()
        if actual != expected:
            raise ValueError('unsupported input SHA-256: ' + name)
        blobs[name] = data
        result[name] = dict(bytes=len(data), sha256=actual)
    return result, blobs


def output_path(root, out, workspace=None):
    workspace = (workspace or Path.cwd()).resolve()
    root, out = root.resolve(), out.resolve()
    if not root.is_dir():
        raise ValueError('retail root must exist')
    if out == root or root in out.parents or out in root.parents:
        raise ValueError('output overlaps retail root')
    if workspace not in out.parents:
        raise ValueError('output must be inside the current workspace')
    if any(p.as_posix().lower() == 'e:/games' for p in [out] + list(out.parents)):
        raise ValueError('output is in protected game directory')
    for path in [out] + list(out.parents):
        if path.exists():
            break
        if not path.name.isascii() or any(ord(c) < 32 for c in path.name):
            raise ValueError('new path must be ASCII')
    if out.exists():
        raise ValueError('output must be fresh')
    return out


def full_manifest(root):
    # Python3.8 Windows junctions are not all recognized as symlinks.
    for path in [root] + list(root.rglob('*')):
        if getattr(path.lstat(), 'st_file_attributes', 0) & 0x400:
            raise ValueError('retail reparse point is unsupported')
    return manifest(root)


def source_report(image):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    lines, spans, seen = [], [], {}
    for name, start, end in RANGES:
        data = image.read(start, end - start)
        instructions = list(md.disasm(data, start))
        if sum(i.size for i in instructions) != len(data):
            raise ValueError('incomplete source range: ' + name)
        spans.append(dict(name=name, start=hex(start), end=hex(end),
                          sha256=hashlib.sha256(data).hexdigest().upper()))
        lines.append('\n%s %08X..%08X' % (name, start, end))
        for i in instructions:
            seen[i.address] = (i.mnemonic, i.op_str)
            lines.append('%08X %-8s %s' % (i.address, i.mnemonic, i.op_str))
    for address, expected in EXPECTED.items():
        if seen.get(address) != expected:
            raise ValueError('source assertion failed at %08X: %r' % (address, seen.get(address)))
    tables = {}
    for name, address, count in [('controller_jump', 0x4260F8, 5),
                                  ('physics_jump', 0x4262A8, 5),
                                  ('ai_controller_vtable', 0x6C89F0, 4),
                                  ('ai_driver_vtable', 0x6C8C18, 4),
                                  ('real_driver_vtable', 0x6C8AD0, 4),
                                  ('physics_vtable', 0x6C8468, 4)]:
        tables[name] = [hex(x) for x in struct.unpack('<' + 'I' * count, image.read(address, count * 4))]
    names = {hex(a): image.string(a) for a in (0x6CAD04, 0x6CACE8, 0x6CACD4, 0x6CAD34, 0x6C9990)}
    constants = {hex(a): struct.unpack('<f', image.read(a, 4))[0]
                 for a in (0x6CC83C, 0x6CC85C, 0x6CC8F0, 0x6CCD40)}
    return dict(ranges=spans, source_assertions=len(EXPECTED), tables=tables,
                serialization_names=names, gap_table_constants=constants), '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    out = output_path(root, args.out)
    out.mkdir(parents=True, exist_ok=False)
    before = full_manifest(root)
    (out / 'before.json').write_bytes(before)
    try:
        provenance, blobs = authenticate(root)
        image = Image(blobs['speed.exe'])
        source, listing = source_report(image)
        front = jdlz(blobs['FrontEnd/FrontB.lzc'])
        presets = preset_records(front)
        setup = event_records(front, presets)[0]
        grid = grid_records(jdlz(blobs['Tracks/TRACKB1003.lzc']))
        from actors258_native import suite
        import capstone
        import unicorn
        report = dict(provenance=provenance, versions=dict(capstone=capstone.__version__, unicorn=unicorn.__version__),
                      source=source, event0=setup, grid=grid, native=suite(image),
                      original_runtime='unperformed; no game process launched or attached')
        (out / 'source.txt').write_text(listing, encoding='ascii')
        (out / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=True) + '\n', encoding='ascii')
    finally:
        after = full_manifest(root)
        (out / 'after.json').write_bytes(after)
        if after != before:
            raise ValueError('retail manifest changed')
        print('PASS: full retail manifest unchanged SHA256 ' + hashlib.sha256(after).hexdigest().upper())
    print('PASS: source assertions and bounded x86 experiments; fresh output ' + out.name)


if __name__ == '__main__':
    main()

"""Read-only bounded minimap research. Python 3.8+, Capstone 5, Unicorn 2.

Only ASCII metadata, disassembly and measurements are written, never images
or extracted texture bytes. Output must be fresh and outside input roots.
"""
import argparse
import json
import math
from pathlib import Path
import re
import struct

from event0_trace import chunks, grid_records, name_hash, table_span
from hud254_trace import PE, jdlz, manifest, output_path, sha


INPUTS = {
    'speed.exe': 'C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7',
    'Global/GLOBALB.BUN': '39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9',
    'Global/INGAMEB.BUN': '044724AB4C167831587E80F29961A1E7D8404C9DD5266533F3563C555A997299',
    'Tracks/TEX1003TRACK.BIN': '081B4D966C154D7B9823E70DD5031269A8D3293F8CD13ED515EF644F09573ED5',
    'Tracks/TRACKB1003.lzc': 'ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA',
}
# Exclusive ends. Context spans are explicitly search context, not functions.
RANGES = (
    ('actor_partitions', 0x41FE50, 0x41FF47),
    ('style_select', 0x42E4F0, 0x42E61B),
    ('visibility_mask', 0x42E620, 0x42EB98),
    ('map_lifetime', 0x42EC10, 0x42ED3C),
    ('map_constructor', 0x58E540, 0x58E80A),
    ('map_draw', 0x58E810, 0x58EFA7),
    ('orientation_update', 0x58EFB0, 0x58F087),
    ('route_quad', 0x58F090, 0x58F63F),
    ('marker_quad', 0x58F640, 0x58FA49),
    ('frame_quad', 0x58FA50, 0x58FB03),
    ('clip_and_submit', 0x58FB10, 0x590AC8),
    ('world_to_uv', 0x590AD0, 0x590B48),
    ('uv_rect', 0x590B50, 0x590DC4),
    ('screen_project', 0x590DD0, 0x5910ED),
    ('camera_direction', 0x5910F0, 0x5912A1),
    ('style_init', 0x5912B0, 0x5919A8),
    ('orientation_filter', 0x5919D0, 0x591A31),
    ('rotate_z', 0x40B930, 0x40B989),
    ('rotate_x_context', 0x40B990, 0x40BAF0),
    ('sin_u16_context', 0x564B10, 0x564BB0),
    ('angle_context', 0x564DB0, 0x564F20),
    ('profile_map_context', 0x4B47A0, 0x4B4820),
    ('career_options_context', 0x4B562D, 0x4B56CF),
    ('map_render_dispatch_context', 0x409EC2, 0x409EFF),
    ('scalar_vector_context', 0x5ABDAF, 0x5ABE60),
    ('scalar_matrix_context', 0x5AC69C, 0x5AC990),
)
TEXTURES = {'TRACK1003_MAP', 'CAR_MARKER', 'CAR_MARKERB',
            'TRACKMAPFRAME', 'MAPBACK', 'START_LINE', 'BKGBMP'}


def unpack(fmt, data, offset=0):
    size = struct.calcsize(fmt)
    if offset < 0 or offset + size > len(data):
        raise ValueError('read outside record')
    return struct.unpack_from(fmt, data, offset)


def raw_textures(data):
    """Bounded raw pack metadata, matching texture.h's observed record layout."""
    result = []
    for tag, root, end in chunks(data):
        if tag != 0xB3300000:
            continue
        children = list(chunks(data, root + 8, end))
        def one(wanted):
            found = [(at + 8, stop) for kind, at, stop in children if kind == wanted]
            if len(found) != 1:
                raise ValueError('missing or ambiguous raw texture table')
            return found[0]
        if any(kind == 0x33310003 for kind, _, _ in children):
            raise ValueError('compressed texture storage outside this probe')
        first, last = one(0x33310004)
        formats, formats_end = one(0x33310005)
        hashes, hashes_end = one(0x33310002)
        payload, payload_end = one(0x33320002)
        count, remainder = divmod(last - first, 124)
        if remainder or formats_end-formats != count*32 or hashes_end-hashes != count*8:
            raise ValueError('invalid texture table counts')
        base = (payload + 127) & ~127
        if base > payload_end or any(b != 0x11 for b in data[payload:base]):
            raise ValueError('invalid texture data alignment')
        for index in range(count):
            at = first + index*124
            name_bytes = data[at+12:at+36]
            if b'\0' not in name_bytes:
                continue  # Other resources may have a full/truncated name field.
            name = name_bytes.split(b'\0', 1)[0].decode('ascii')
            if name not in TEXTURES:
                continue
            identity = unpack('<I', data, at+36)[0]
            if unpack('<I', data, hashes+index*8)[0] != identity:
                raise ValueError('texture hash table disagrees')
            offset, _, length, _, top = unpack('<5I', data, at+48)
            width, height = unpack('<2H', data, at+68)
            code, mips = data[at+74], data[at+78]
            format_code = unpack('<I', data, formats+index*32+20)[0]
            begin, stop = base+offset, base+offset+length
            if offset % 128 or not width or not height or top > length or stop > payload_end:
                raise ValueError('texture range/dimensions invalid')
            row = dict(name=name, hash='%08X' % identity, root_offset=root,
                       info_offset=at, data_offset=begin, bytes=length,
                       width=width, height=height, format_code=code,
                       d3d_format=format_code, mip_count=mips,
                       alpha_test=data[at+85], alpha_blend=data[at+86],
                       data_sha256=sha(data[begin:stop]))
            if name == 'TRACK1003_MAP':
                if (code, format_code, length, top) != (0x20, 21, width*height*4, width*height*4):
                    raise ValueError('unsupported map pixel layout')
                alpha = data[begin+3:stop:4]
                row['alpha_histogram'] = {str(a): alpha.count(a) for a in sorted(set(alpha))}
                # Retain statistics only. No pixel array or image is exported.
            result.append(row)
    if len({row['name'] for row in result}) != len(result):
        raise ValueError('ambiguous selected texture name')
    return result


def placeholder(package):
    found = []
    for match in re.finditer(rb'FOb\xea', package):
        start = match.start()
        length, = unpack('<I', package, start+4)
        obj_size, = unpack('<I', package, start+12)
        end = start+16+obj_size
        if package[start+8:start+12] != b'ObjD' or end > start+8+length or start+8+length > len(package):
            raise ValueError('invalid FEng object enclosure')
        at, fields = start+16, {}
        while at < end:
            size, = unpack('<H', package, at+2)
            if at+4+size > end:
                raise ValueError('FEng field outside object')
            key = package[at:at+2]
            if key in fields:
                raise ValueError('duplicate FEng field')
            fields[key] = (at, package[at+4:at+4+size])
            at += 4+size
        if fields.get(b'On', (None, b''))[1].split(b'\0')[0] != b'track_map_placeholder':
            continue
        if b'SA' not in fields:
            raise ValueError('placeholder without shape')
        shape_at, shape = fields[b'SA']
        center = unpack('<3f', shape, 0x1C)
        size = unpack('<2f', shape, 0x38)
        if not all(math.isfinite(x) for x in center+size) or min(size) <= 0:
            raise ValueError('invalid placeholder geometry')
        found.append(dict(offset=start, shape_offset=shape_at, center_depth=center,
                          size=size, bgra=unpack('<4I', shape),
                          native_center=[center[0]+320, center[1]+240]))
    if len(found) != 1:
        raise ValueError('expected one map placeholder')
    return found[0]


def track_map(data):
    first, last = table_span(data, 0x34201, 252)
    found = []
    for at in range(first, last, 252):
        # Track number is a signed 16-bit field at +0x6c.
        if unpack('<h', data, at+0x6C)[0] == 1003:
            values = unpack('<3f3H', data, at+0x8C)
            if not all(math.isfinite(x) for x in values[:3]) or values[2] <= 0:
                raise ValueError('invalid track map scale/offset')
            found.append(dict(offset=at, offset_x=values[0], offset_y=values[1],
                              world_span=values[2], rotation_u16=values[3],
                              start_angle_u16=values[4], finish_angle_u16=values[5]))
    if len(found) != 1:
        raise ValueError('missing or duplicate track1003 record')
    return found[0]


def route_points(data):
    first, last = table_span(data, 0x34149, 20)
    rows = []
    for at in range(first, last, 20):
        x, y = unpack('<2f', data, at)
        distance, = unpack('<f', data, at+16)
        if not all(math.isfinite(v) for v in (x, y, distance)):
            raise ValueError('nonfinite route point')
        rows.append(dict(index=len(rows), offset=at, xy=[x, y], distance=distance))
    return rows


def lines(data):
    first,last = table_span(data,0x34146,48,16)
    wanted = {name_hash(name,False):name for name in ('ForwardStartLine','BackwardStartLine')}
    result = []
    for at in range(first,last,48):
        key, = unpack('<I',data,at+8)
        if key in wanted:
            position = unpack('<3f',data,at+16)
            if not all(math.isfinite(value) for value in position):
                raise ValueError('nonfinite line marker')
            result.append(dict(name=wanted[key],offset=at,position=position,
                               angle_u16=unpack('<H',data,at+32)[0]))
    if sorted(row['name'] for row in result) != sorted(wanted.values()):
        raise ValueError('missing or duplicate start lines')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--source', required=True)
    args = parser.parse_args()
    if not re.fullmatch('[0-9a-f]{40}', args.source):
        parser.error('source must be a full lowercase commit SHA')
    roots = [args.root.resolve()]
    if args.reference:
        roots.append(args.reference.resolve())
    if any(not root.is_dir() for root in roots):
        parser.error('input directory missing')
    out = output_path(args.out, roots)
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    from minimap266_native import measure
    out.mkdir(parents=True)
    before = [manifest(root) for root in roots]
    for index, value in enumerate(before):
        (out/('before-%d.json' % index)).write_bytes(value)
    try:
        blobs = {name: (roots[0]/name).read_bytes() for name in INPUTS}
        for name, value in blobs.items():
            if sha(value) != INPUTS[name]:
                raise ValueError('unsupported input: '+name)
        pe = PE(blobs['speed.exe'])
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        instructions, ranges = [], []
        for label, start, end in RANGES:
            data = pe.read(start, end-start)
            context = label.endswith('_context')
            md.skipdata = context
            decoded = list(md.disasm(data, start))
            if not context and sum(item.size for item in decoded) != len(data):
                raise ValueError('incomplete function disassembly: '+label)
            ranges.append(dict(name=label, start='%08X' % start, end='%08X' % end,
                               context_only=context, sha256=sha(data)))
            instructions.append('\n%s %08X..%08X SHA256 %s' % (label,start,end,sha(data)))
            instructions.extend('%08X %-8s %s' % (i.address,i.mnemonic,i.op_str) for i in decoded)
        (out/'instructions.txt').write_text('\n'.join(instructions)+'\n', encoding='ascii')
        bundle = blobs['Global/INGAMEB.BUN']
        package = jdlz(bundle[0x16F40C:])
        if b'HUD_SingleRace.fng' not in package[:256]:
            raise ValueError('wrong HUD package')
        track = jdlz(blobs['Tracks/TRACKB1003.lzc'])
        textures = []
        for name in ('Global/INGAMEB.BUN', 'Tracks/TEX1003TRACK.BIN'):
            textures.extend(dict(source=name, **row) for row in raw_textures(blobs[name]))
        if {row['name'] for row in textures} != TEXTURES:
            raise ValueError('missing selected minimap resources')
        metadata = dict(source_commit=args.source, provenance={name:dict(bytes=len(value),sha256=sha(value))
                         for name,value in blobs.items()}, ranges=ranges, textures=textures,
                         package_sha256=sha(package), placeholder=placeholder(package),
                         track=track_map(blobs['Global/GLOBALB.BUN']),
                         grid=grid_records(track), route_nodes=route_points(track),lines=lines(track))
        native = measure(pe, metadata)
        native['source_commit'] = args.source
        native['tool_hashes'] = {name:sha((Path(__file__).parent/name).read_bytes()) for name in
                                ('minimap266_trace.py','minimap266_native.py','hud254_native.py',
                                 'hud254_trace.py','event0_trace.py')}
        for name, value in (('data.json',metadata),('native.json',native)):
            (out/name).write_text(json.dumps(value,indent=2,ensure_ascii=True,allow_nan=False)+'\n',encoding='ascii')
    finally:
        after = [manifest(root) for root in roots]
        for index, value in enumerate(after):
            (out/('after-%d.json' % index)).write_bytes(value)
        if after != before:
            raise RuntimeError('input files/directories changed')
    print('PASS: minimap source/native trace; full input manifests unchanged')


if __name__ == '__main__':
    main()

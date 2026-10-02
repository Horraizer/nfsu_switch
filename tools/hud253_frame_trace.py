"""Read-only, bounded issue 253 frame trace (Capstone 5 required).

Writes ASCII evidence, never images or retail assets. Composition rows are
models of current port source, not observations of original/GPU execution.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
from hud253_trace import jdlz
from handling_measure import manifest
from race_replay_check import retail_manifest


def checked(data, sha, name):
    if hashlib.sha256(data).hexdigest().upper() != sha:
        raise ValueError('unsupported ' + name)
    return data


def dxt3(data, width, height):
    """Decode explicit alpha and four-colour BC2 blocks independently."""
    if width % 4 or height % 4 or len(data) != width * height:
        raise ValueError('invalid DXT3 dimensions/length')
    pixels = [None] * (width * height)
    for block in range(len(data) // 16):
        alpha, c0, c1, indices = struct.unpack_from('<QHHI', data, block * 16)
        def rgb(c):
            r, g, b = c >> 11, (c >> 5) & 63, c & 31
            return ((r << 3) | (r >> 2), (g << 2) | (g >> 4),
                    (b << 3) | (b >> 2))
        colors = [rgb(c0), rgb(c1)]
        colors += [tuple((2*a+b)//3 for a, b in zip(*colors)),
                   tuple((a+2*b)//3 for a, b in zip(*colors))]
        bx, by = block % (width // 4) * 4, block // (width // 4) * 4
        for i in range(16):
            pixels[(by+i//4)*width+bx+i%4] = (
                *colors[(indices >> (2*i)) & 3], ((alpha >> (4*i)) & 15)*17)
    return pixels


def trace(root, out):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    exe = checked((root/'speed.exe').read_bytes(),
        'C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7',
        'speed.exe')
    bun = checked((root/'Global/INGAMEB.BUN').read_bytes(),
        '044724AB4C167831587E80F29961A1E7D8404C9DD5266533F3563C555A997299',
        'INGAMEB.BUN')
    package = checked(jdlz(bun[0x16F40C:]),
        '652DD66715CC8EFF180A64C59377098385B3896B584A1430C014D7476DEA2E63',
        'HUD_SingleRace.fng')
    pe = struct.unpack_from('<I', exe, 60)[0]
    if exe[pe:pe+4] != b'PE\0\0':
        raise ValueError('not a PE')
    count, optsize = (struct.unpack_from('<H', exe, pe+off)[0] for off in (6, 20))
    base = struct.unpack_from('<I', exe, pe+52)[0]
    sections = [struct.unpack_from('<IIII', exe, pe+24+optsize+i*40+8)
                for i in range(count)]
    def read(va, size):
        for _, rva, rawsize, raw in sections:
            if base+rva <= va and va+size <= base+rva+rawsize:
                return exe[raw+va-base-rva:raw+va-base-rva+size]
        raise ValueError('VA outside raw sections: %08X' % va)
    ranges = [
        ('HUD-constructor', 0x4A03D0, 0x4A0E98),
        ('third-person-lines-red-selector', 0x432A30, 0x432C49),
        ('FEng-parent-colour', 0x4F1810, 0x4F1D76),
        ('FEng-sprite', 0x4F2000, 0x4F2278),
        ('quad-submission', 0x514B90, 0x514BE7),
        ('quad-vertices-and-material', 0x40BEF0, 0x40C2F4),
        ('texture-material-state', 0x40F3D0, 0x40F6DB),
        ('blend-state', 0x410550, 0x410650),
    ]
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    lines = ['speed.exe SHA-256 ' + hashlib.sha256(exe).hexdigest().upper(),
             'End addresses are exclusive.',
             'No skip-data; full instruction coverage required.',
             'Static evidence does not establish executed shader/state.']
    for name, start, end in ranges:
        blob = read(start, end-start)
        instructions = list(md.disasm(blob, start))
        if sum(i.size for i in instructions) != len(blob):
            raise ValueError('incomplete instruction range: ' + name)
        lines.append('\n%s %08X..%08X' % (name, start, end))
        lines.extend('%08X %-8s %s' % (i.address, i.mnemonic, i.op_str)
                     for i in instructions)
    (out/'instructions.txt').write_text('\n'.join(lines)+'\n', encoding='ascii')
    textures = {}
    for name, info, raw, identity in (
            ('RPM_FILL', 36172, 77568, 0x0148AED4),
            ('3RDPERSON_7000FILL', 47208, 800896, 0x9107116C)):
        if bun[info+12:info+36].split(b'\0')[0].decode('ascii') != name:
            raise ValueError('texture name moved')
        if struct.unpack_from('<I', bun, info+36)[0] != identity:
            raise ValueError('texture identity moved')
        width, height = struct.unpack_from('<HH', bun, info+68)
        size = struct.unpack_from('<I', bun, info+56)[0]
        if bun[info+74] != 0x24 or bun[info+78] != 1:
            raise ValueError('expected one-mip DXT3')
        pixels = dxt3(bun[raw:raw+size], width, height)
        textures[name] = dict(hash='%08X' % identity, info_offset=info,
            data_offset=raw, width=width, height=height,
            alpha_test=bun[info+85], alpha_blend=bun[info+86],
            layer_order=bun[info+81], layer_group=bun[info+84],
            alpha_histogram=dict(sorted(Counter(p[3] for p in pixels).items())),
            nontransparent_rgb=sorted(set(p[:3] for p in pixels if p[3])))
        if name == '3RDPERSON_7000FILL':
            textures[name]['samples'] = [dict(x=x, y=y, rgba=pixels[y*width+x])
                for x, y in ((69, 30), (69, 70), (120, 64), (200, 100))]
    if package[0x4030:0x4034] != b'OP\x10\0':
        raise ValueError('fill OP moved')
    fields = dict(fill_texture_hash='%08X' % struct.unpack_from('<I', package, 0x4038)[0],
        fill_bgra=struct.unpack_from('<4I', package, 0x4058),
        group_bgra=struct.unpack_from('<4I', package, 0x3F9C))
    project = Path(__file__).resolve().parent.parent
    source_hashes = {name: hashlib.sha256((project/name).read_bytes()).hexdigest().upper()
        for name in ('src/race_hud.cpp', 'switch/deko_renderer.cpp',
                     'shaders/textured_opaque.frag.glsl', 'tools/hud253_frame_trace.py')}
    report = dict(textures=textures, feng=fields, bounded_binding_gap=True,
        bun_sha256=hashlib.sha256(bun).hexdigest().upper(),
        feng_sha256=hashlib.sha256(package).hexdigest().upper(),
        port_source_sha256=source_hashes,
        caveat='Default OP is not a measurement of live stock sprite binding.')
    (out/'fields.json').write_text(json.dumps(report, indent=2, sort_keys=True)+'\n',
                                   encoding='ascii')
    # Source models only. GPU fixed-function quantization is not measured here.
    rows = ['model only; not original pixels or a GPU execution test',
            'texture_alpha,vertex_alpha,background,cpu_source_alpha,shader_source_alpha,cpu_rgb,shader_blend_model_rgb']
    for a in (0, 153):
        for bg in (32, 224):
            cpu = (a*150+127)//255
            rows.append('%d,150,%d,%d,%d,%d,%d' % (a, bg, cpu, a,
                (bg*(255-cpu)+127)//255, (bg*(255-a)+127)//255))
    (out/'port-model.csv').write_text('\n'.join(rows)+'\n', encoding='ascii')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    if out == root or root in out.parents:
        parser.error('output must be outside retail')
    # resolve() follows existing junction ancestors before containment testing.
    out.mkdir(parents=True, exist_ok=False)
    before = manifest(root), retail_manifest(root)
    (out/'before.json').write_bytes(before[0])
    try:
        trace(root, out)
    finally:
        after = manifest(root), retail_manifest(root)
        (out/'after.json').write_bytes(after[0])
        if after != before:
            raise RuntimeError('retail files or directories changed')
    print('PASS: bounded frame trace; port-only models; retail unchanged')


if __name__ == '__main__':
    main()

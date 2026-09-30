"""Read-only, bounded retail evidence for issue 188; requires Capstone."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path
import struct


def file_manifest(root):
    entries = []
    for path in sorted(root.rglob('*')):
        if path.is_file():
            stat = path.stat()
            entries.append((path.relative_to(root).as_posix(), stat.st_size,
                            stat.st_mtime_ns,
                            hashlib.sha256(path.read_bytes()).hexdigest()))
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path,
                        help='Retail root, read-only')
    parser.add_argument('--out', type=Path, default=Path('out/188-codex-s8-trace'),
                        help='Manifest directory, outside the retail root')
    args = parser.parse_args()
    root = args.root.resolve()
    output = args.out.resolve()
    if output == root or root in output.parents:
        parser.error('--out must be outside the retail installation')
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    output.mkdir(parents=True, exist_ok=True)
    cmp_path = shutil.which('cmp')
    if cmp_path is None:
        git_path = shutil.which('git')
        if git_path:
            candidate = Path(git_path).resolve().parent.parent / 'usr/bin/cmp.exe'
            if candidate.is_file():
                cmp_path = str(candidate)
    if cmp_path is None:
        parser.error('cmp is required on PATH (or beside Git for Windows)')
    before = file_manifest(root)
    before_bytes = json.dumps(before, ensure_ascii=True).encode('ascii')
    (output / '188-codex-s8-before.json').write_bytes(before_bytes)
    try:
        data = (root / 'speed.exe').read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest != 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':
            raise ValueError('Unsupported speed.exe SHA-256: ' + digest)
        print('speed.exe', len(data), digest)
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        # Each range begins at a verified instruction boundary.
        ranges = [
            (0x56ABEF, 0x56AC84), (0x56B08A, 0x56B0D4),
            (0x58161F, 0x58165D), (0x56B33C, 0x56B43E),
            (0x56DECC, 0x56DEE8), (0x56E330, 0x56E455),
            (0x56EB0F, 0x56F475), (0x440A40, 0x440ACE),
            (0x410970, 0x4109B3), (0x40F0F0, 0x40F165),
            (0x40E1F1, 0x40E214), (0x408B20, 0x408B59),
            (0x40F436, 0x40F549), (0x40EFB4, 0x40F026),
            (0x40ED50, 0x40EE30), (0x40F0AC, 0x40F0D0),
        ]
        for start, end in ranges:
            print('RANGE 0x%08x..0x%08x' % (start, end))
            for insn in md.disasm(data[start - 0x400000:end - 0x400000], start):
                print('0x%08x %-9s %s' %
                      (insn.address, insn.mnemonic, insn.op_str))
        for va in [0x6B5D80, 0x6B5D84, 0x6B5D88, 0x6B5DB4,
                   0x6B5DB8, 0x6B5DC8, 0x6B5DCC, 0x6CCA58]:
            print('F32 0x%08x %.9g' %
                  (va, struct.unpack_from('<f', data, va - 0x400000)[0]))
        for relative in ['Cars/240SX/TEXTURES.BIN', 'fx/world.efx']:
            payload = (root / relative).read_bytes()
            print(relative, len(payload), hashlib.sha256(payload).hexdigest())
            if relative.endswith('TEXTURES.BIN'):
                for name in [b'240SX_SHADOWIG', b'240SX_SHADOWFE']:
                    name_at = payload.index(name + b'\0')
                    info = name_at - 12
                    raw = payload[info:info + 124]
                    width, height = struct.unpack_from('<HH', raw, 68)
                    print('TEXTURE', name.decode('ascii'),
                          'info=0x%x hash=0x%08x dimensions=%dx%d' %
                          (info, struct.unpack_from('<I', raw, 36)[0], width, height),
                          'row/group/test/blend=', raw[81], raw[84], raw[85], raw[86])
            else:
                # Source excerpts go only to stdout, never to a tracked asset.
                source = bytes(value ^ 0xAA for value in payload)
                for number, line in enumerate(source.splitlines(), 1):
                    if any(token in line for token in [b'tex2D(', b'ZEnable',
                            b'ZWrite', b'ZFunc', b'CullMode', b'Modulate2x',
                            b'AlphaTestEnable =', b'AlphaBlendEnable =',
                            b'SrcBlend =', b'DestBlend =']):
                        print('world.efx:%d' % number,
                              line.decode('ascii').strip())
        print('PASS: bounded shadow evidence read')
    finally:
        after = file_manifest(root)
        after_bytes = json.dumps(after, ensure_ascii=True).encode('ascii')
        (output / '188-codex-s8-after.json').write_bytes(after_bytes)
        if before_bytes != after_bytes:
            raise RuntimeError('RETAIL MANIFEST CHANGED')
        subprocess.run([cmp_path, str(output / '188-codex-s8-before.json'),
                        str(output / '188-codex-s8-after.json')], check=True)
        print('RETAIL MANIFEST EQUAL', len(after), 'cmp exit 0')


if __name__ == '__main__':
    main()

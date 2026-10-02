"""Issue 222: bounded read-only collision/audio trace of the pinned PC retail.

Requires Python 3.8+ and Capstone. Run from the checkout; use a fresh ignored
build/out directory. Reports are ASCII; no executable, bank or PCM is copied.
The full retail tree is hashed before and after, including the root metadata.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

EXE_SHA = 'C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7'
BANK_SHA = {
    'IMPACT_MB.abk': 'C1EE8237C292E2934E296CAF10E7A78B25BDAE8326C517D1AA034379362CC6D5',
    'SCRAPE_MB.abk': 'F8064C70C59BB0B6E61596DE7F1A6856EA4AC48ACE3A48D50849C2B4E9FECB50',
}
RANGES = [
    ('impulse-record-prefix', 0x465830, 0x465B09),
    ('record-constructor-fragment', 0x465640, 0x4656AA),
    ('unit-vector', 0x567370, 0x5673C8),
    ('audio-class-selector', 0x52F6F0, 0x52F748),
    ('audio-record-attach', 0x530840, 0x530A49),
    ('audio-record-lifetime', 0x530A50, 0x530B2D),
    ('scrape-stop', 0x5307E0, 0x530838),
    ('impact-intensity', 0x530F80, 0x53102A),
    ('scrape-intensity', 0x531030, 0x53107E),
    ('scrape-start', 0x531080, 0x53114E),
    ('scrape-predicate', 0x531170, 0x53118F),
    ('impact-start', 0x531350, 0x531455),
    ('world-scrape-selector', 0x531470, 0x531484),
    ('world-impact-classifier', 0x531DD0, 0x531F4E),
    ('world-impact-variants', 0x531F60, 0x531FD7),
    ('world-impact-admission', 0x531FF0, 0x532270),
    ('impact-parameter-consumer', 0x5259E0, 0x525A9F),
    ('scrape-parameter-consumer', 0x52F380, 0x52F439),
    ('bank-relocation-fragment', 0x64DF30, 0x64DFA1),
    ('instance-template-fragment', 0x64DC20, 0x64DC9A),
    ('integer-lookup', 0x64CDD0, 0x64CEEF),
    ('random-range', 0x64CA50, 0x64CA74),
    ('scaled-product', 0x64D0B0, 0x64D0CF),
    ('integer-sum', 0x64D0D0, 0x64D0EC),
    ('player-selection', 0x64D860, 0x64D95D),
    ('curve-interpolation', 0x532840, 0x5328AB),
    ('scrape-deck', 0x64CA80, 0x64CB25),
    ('scrape-edge', 0x64CB90, 0x64CBCB),
    ('scrape-oscillator', 0x64D960, 0x64DA70),
    ('scrape-ramp', 0x64DA70, 0x64DB13),
    ('graph-clock-fragment', 0x651E84, 0x651EFA),
    ('pair-constructor', 0x465640, 0x4656F4),
    ('pair-delete', 0x465700, 0x465764),
    ('scrape-speed-fragment', 0x46C0A8, 0x46C0DC),
    ('impulse-audio-call-fragment', 0x46C3DB, 0x46C3F3),
]


def digest(path):
    sha = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            sha.update(block)
    return sha.hexdigest().upper()


def manifest(root):
    rows = []
    for path in [root] + sorted(root.rglob('*')):
        stat = path.stat()
        rows.append([path.relative_to(root).as_posix(),
                     'dir' if path.is_dir() else stat.st_size,
                     stat.st_mtime_ns, digest(path) if path.is_file() else None])
    return (json.dumps(rows, ensure_ascii=True, indent=2) + '\n').encode('ascii')


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=True, indent=2) + '\n',
                    encoding='ascii')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    # Anchor BEFORE resolve: Python 3.8 on Windows can leave a nonexistent
    # relative output relative even after Path.resolve().
    workspace = Path.cwd().resolve()
    root = (workspace / args.root).resolve()
    out = (workspace / args.out).resolve()
    if root == out or root in out.parents:
        parser.error('output must be outside retail')
    games = Path('E:/Games').resolve()
    if out == games or games in out.parents:
        parser.error('output must be outside E:/Games')
    if out == workspace:
        parser.error('output cannot be the checkout root')
    if workspace in out.parents:
        first = out.relative_to(workspace).parts[0]
        if not (first.startswith('build') or first in ('out',) or first.startswith('cmake-build-')):
            parser.error('checkout output must use an ignored build/out directory')
    new_parts = [p for p in [out] + list(out.parents) if not p.exists()]
    if any(any(ord(c) < 32 or ord(c) > 126 for c in p.name) for p in new_parts):
        parser.error('new output names must be ASCII')
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    out.mkdir(parents=True, exist_ok=False)
    before = manifest(root)
    (out / 'before.json').write_bytes(before)
    try:
        data = (root / 'speed.exe').read_bytes()
        if hashlib.sha256(data).hexdigest().upper() != EXE_SHA:
            raise ValueError('unsupported speed.exe SHA256')
        pe = struct.unpack_from('<I', data, 60)[0]
        if data[pe:pe+4] != b'PE\0\0':
            raise ValueError('not a PE')
        count, optsize = struct.unpack_from('<H', data, pe+6)[0], struct.unpack_from('<H', data, pe+20)[0]
        base = struct.unpack_from('<I', data, pe+52)[0]
        sections = []
        for k in range(count):
            at = pe+24+optsize+40*k
            vs, va, rawsize, raw = struct.unpack_from('<IIII', data, at+8)
            sections.append((base+va, rawsize, raw))

        def read(va, size):
            for start, length, raw in sections:
                if start <= va and va+size <= start+length:
                    return data[raw+va-start:raw+va-start+size]
            raise ValueError('unmapped VA: %08X' % va)

        lines = ['speed.exe SHA256 ' + EXE_SHA]
        for name, start, end in RANGES:
            blob = read(start, end-start)
            ins = list(md.disasm(blob, start))
            if sum(i.size for i in ins) != len(blob):
                raise ValueError('invalid instruction boundary: ' + name)
            lines.append('\n%s %08X..%08X SHA256 %s' %
                         (name, start, end, hashlib.sha256(blob).hexdigest().upper()))
            lines.extend('%08X %-8s %s' % (i.address, i.mnemonic, i.op_str) for i in ins)
        (out / 'instructions.txt').write_text('\n'.join(lines)+'\n', encoding='ascii')
        constants = {}
        for va in (0x6CC7A4, 0x6CC7BC, 0x6CCB34, 0x6CCC3C, 0x6CCC68,
                   0x6CCA38, 0x7000D8, 0x6B7288, 0x6B728C,
                   0x6CCA94, 0x6CC9F8, 0x6CCB4C, 0x6CC7C8):
            b = read(va, 4)
            constants[hex(va)] = dict(u32=struct.unpack('<I', b)[0], f32=struct.unpack('<f', b)[0])
        constants['0x6ccc70'] = struct.unpack('<d', read(0x6CCC70, 8))[0]
        tables = {}
        for va, n in ((0x6B6450, 7), (0x6F4F08, 13), (0x531FD8, 5),
                      (0x6F8170, 8), (0x6E9AE8, 40), (0x6BE520, 29)):
            tables[hex(va)] = struct.unpack('<'+'I'*n, read(va, 4*n))
        write_json(out / 'tables.json', dict(constants=constants, tables=tables))
        for filename, wanted in BANK_SHA.items():
            path = root / 'Sound/IG_GLOBAL' / filename
            bank = path.read_bytes()
            actual = hashlib.sha256(bank).hexdigest().upper()
            if actual != wanted:
                raise ValueError('unsupported bank SHA256: ' + filename)
            u32 = lambda at: struct.unpack_from('<I', bank, at)[0]
            modules = []
            module = u32(28)
            init_lines = []
            for ix in range(struct.unpack_from('<H', bank, 10)[0]):
                players, controllers = bank[module+36], bank[module+39]
                start, template = u32(module+40), u32(module+44)
                entries = []
                for j in range(players):
                    player = template + u32(module+60+4*j)
                    table = u32(player+4)
                    entries.append(dict(player=hex(player), table=hex(table),
                        values=[struct.unpack_from('<III', bank, table+4+12*k) for k in range(u32(table))]))
                init_lines.append('\nmodule %d init %X; instance base template+0x18=%X' % (ix,start,template+24))
                delta = 0
                calls = []
                ended = False
                for i in md.disasm(bank[start:template], start):
                    if i.mnemonic == 'add' and i.op_str.startswith('esi,'):
                        delta += int(i.op_str.split(',')[1],0)
                    suffix = ''
                    if i.mnemonic == 'call':
                        ordinal = struct.unpack_from('<i', bank, i.address+1)[0]
                        if not 0 <= ordinal < 40:
                            raise ValueError('unknown callback ordinal')
                        calls.append(dict(address=hex(i.address),ordinal=ordinal,node=hex(template+24+delta)))
                        suffix = ' ; UNRELOCATED ordinal=%d node=%X' % (ordinal,template+24+delta)
                    init_lines.append('%08X %-8s %s%s' % (i.address,i.mnemonic,i.op_str,suffix))
                    if i.mnemonic == 'ret':
                        ended = True
                        break
                if not ended:
                    raise ValueError('module initializer lacks RET')
                modules.append(dict(module=hex(module),template=hex(template),entries=entries,calls=calls))
                module += 60+4*(players+controllers)
            exports = []
            table = u32(56)
            for k in range(u32(table)):
                at = table+4+12*k
                name = u32(at+4)
                end = bank.index(0,name+4)
                exports.append(dict(module_field=hex(u32(at)),event_id=hex(u32(name)),name=bank[name+4:end].decode('ascii')))
            write_json(out / (filename+'.json'),dict(sha256=actual,modules=modules,exports=exports))
            (out / (filename+'.init.txt')).write_text('\n'.join(init_lines)+'\n',encoding='ascii')
            regions = [(0,u32(32)),(u32(48),len(bank))]
            (out / (filename+'.metadata.txt')).write_text('\n'.join(
                '%06X %s' % (a, bank[a:min(a+16,end)].hex(' '))
                for start,end in regions for a in range(start,end,16))+'\n',encoding='ascii')
            curves = []
            addresses = (0x70C,0x81C,0x92C,0xA3C,0xB4C) if filename=='IMPACT_MB.abk' else (0x38C,0x49C,0x5AC,0x5D0)
            for address in addresses:
                typ,count,low,high,scale = struct.unpack_from('<HHIIf',bank,address)
                if typ not in (1,2,4) or not 1 <= count <= 1024:
                    raise ValueError('unexpected curve header')
                fmt = {1:'b',2:'h',4:'i'}[typ]
                values = struct.unpack_from('<'+fmt*count,bank,address+16)
                curves.append(dict(address=hex(address),type=typ,count=count,low=low,high=high,scale=scale,values=values))
            write_json(out / (filename+'.curves.json'),curves)
        print('PASS: exact source identities, bounded decode, tables and bank graph reports')
    finally:
        after = manifest(root)
        (out / 'after.json').write_bytes(after)
        if before != after:
            raise ValueError('retail manifest changed')
        print('PASS: complete retail SHA256/size/mtime/directory manifests unchanged')


if __name__ == '__main__':
    main()
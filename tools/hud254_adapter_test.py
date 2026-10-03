"""Compare compiled presentation plumbing to fresh original x86 submissions.

Run hud254_presentation.py and its independent tests first. Expected geometry
comes from original instructions, not from a Python copy of the C++ adapter.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess

from hud254_trace import LANGUAGE_SHA, sha


def run(probe, commands):
    result = subprocess.run([str(probe.resolve()), '--oracle-stream'],
                            input='\n'.join(commands)+'\n', text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode or result.stderr or not result.stdout.strip():
        raise AssertionError('compiled oracle failed after %d commands: %s %s' %
                             (len(result.stdout.splitlines()),result.returncode,result.stderr))
    lines = result.stdout.splitlines()
    if len(lines) != len(commands) or any('FAIL' in s or 'error' in s.lower() for s in lines):
        raise AssertionError('compiled oracle output incomplete or failed')
    return lines


def close(actual, expected):
    if len(actual) != len(expected) or any(abs(a-b) > .0001 for a,b in zip(actual,expected)):
        raise AssertionError('geometry mismatch: %r != %r' % (actual,expected))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--source', required=True)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--probe', type=Path, required=True)
    args = p.parse_args()
    # Revalidate source identity, tool hashes, native assertions and manifests.
    subprocess.run([__import__('sys').executable, '-B', str(Path(__file__).with_name(
        'hud254_presentation_test.py')), '--report', str(args.report), '--source', args.source], check=True)
    report = json.loads((args.report/'presentation.json').read_text(encoding='ascii'))
    language_path = args.root/'Languages/English.bin'
    before = language_path.stat()
    language = language_path.read_bytes()
    if sha(language) != LANGUAGE_SHA:
        raise AssertionError('unsupported English font input')
    objects = {o['name']: {f['tag']: f for f in o['fields']} for o in report['objects']}
    metadata = {f['name'].upper(): f for f in report['font_metadata']}
    commands = []
    for row in report['cases']:
        fields = objects[row['name']]
        sa = fields['SA']
        parent = row.get('parent') or dict(matrix=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],color=[256]*4)
        tex = row['texture']
        raw = bytes.fromhex(sa['raw'])
        uv = struct.unpack_from('<4f',raw,68) if 'font' not in row else [0]*4
        sj = row['sj_override'] if row.get('sj_override') is not None else fields.get('Sj',{}).get('justification',0)
        sl, sw = [int.from_bytes(bytes.fromhex(fields[tag]['raw']),'little') if tag in fields else 0
                  for tag in ('Sl','Sw')]
        values = [int(tex['hash'],16),tex['width'],tex['height'],*sa['position_depth'],
                  *sa['size_scale'],*sa['bgra'],*struct.unpack_from('<4f',raw,40),
                  *parent['matrix'],*parent['color'],sj,sl,sw,*uv]
        command = ('g' if 'font' in row else 'b')+' '+' '.join(map(str,values))
        if 'font' in row:
            font = row['font']; at = metadata[font['name'].upper()]['fntf_offset']
            count = struct.unpack_from('<H',language,at+10)[0]
            fntf = language[at:at+128+16*count]
            command += ' %s %s %s %s %s' % (font['line_height'],font['baseline'],
                font['line_spacing_scale'],fntf.hex(),row['text'].encode('ascii').hex())
        commands.append(command)
    lines = run(args.probe,commands)
    vertices = 0
    for row,line in zip(report['cases'],lines):
        values = line.split(); known, ready, bucket, count = map(int,values[:4])
        if (known,ready,count) != (1,0,len(row['output']['local']['quads'])):
            raise AssertionError('geometry count/gate mismatch for '+row['name'])
        if len(values) != 4+32*count:
            raise AssertionError('incomplete vertex output')
        depth = objects[row['name']]['SA']['position_depth'][2]
        depth += row['parent']['matrix'][14] if row.get('parent') else 0
        if bucket != max(0,min(255,int(depth))):
            raise AssertionError('effective bucket mismatch')
        cursor = 4
        for quad,draw in zip(row['output']['local']['quads'],row['output']['draws']):
            for i,native in enumerate(draw['vertices']):
                v = list(map(float,values[cursor:cursor+7])); color = int(values[cursor+7]); cursor += 8
                close(v[:3],quad['positions'][i]); close(v[3:5],quad['uv'][i])
                close(v[5:7],native['uv'])
                if color != int(native['color'],16):
                    raise AssertionError('packed color mismatch')
                vertices += 1
    language_commands = ['l 1 %d' % r['selected'] for r in report['languages']]
    language_lines = run(args.probe,language_commands)
    if language_lines != ['1\t'+r['text'] for r in report['languages']]:
        raise AssertionError('language-to-format differs from original setter/formatter')
    if run(args.probe,['l 0 0','l 1 -1','l 1 7']) != ['0\t']*3:
        raise AssertionError('unknown language guessed a style')
    permission_commands = ['p %d %d' % (r['initial'],r['option']) for r in report['permissions']]
    for r,line in zip(report['permissions'],run(args.probe,permission_commands)):
        values = list(map(int,line.split()))
        # Original profile copy also applies unrelated bits 0x650. The bounded
        # lap adapter receives those other options already resolved separately.
        expected = [r['settings'] & ~0x650, int(r['mask']['lap_enabled'])]
        expected[0] |= r['initial'] & 0x650
        for t in r['toggles']:
            for key in ('view','settings'):
                expected.append((t[key] & ~0x650) | (r['initial'] & 0x650))
            expected.append(t['profile'])
        if values != expected:
            raise AssertionError('lap permission propagation differs from native')
    queue_commands = ['q %d %s' % (len(r['depths']),' '.join(map(str,r['depths']))) for r in report['queue']]
    for r,line in zip(report['queue'],run(args.probe,queue_commands)):
        if list(map(int,line.split())) != r['submission_indices']:
            raise AssertionError('queue ordering differs from original')
    after = language_path.stat()
    if before.st_size != after.st_size or before.st_mtime_ns != after.st_mtime_ns or sha(language_path.read_bytes()) != LANGUAGE_SHA:
        raise AssertionError('font input changed')
    print('PASS: C++ adapter matches %d original geometry cases, %d vertices, language/permission/queue cases; production gate closed' %
          (len(lines),vertices))


if __name__ == '__main__':
    main()

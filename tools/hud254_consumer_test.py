"""Compare the compiled lap HUD consumer to the pinned native research report.

No input asset is modified or exported. The report must first pass all guards
in hud254_trace_test.py; these comparisons cover synthetic consumer inputs,
not event0 lifecycle, screen placement or hardware acceptance.
"""
import argparse
import json
from pathlib import Path
import subprocess


def require(value, message):
    if not value:
        raise RuntimeError(message)


def run(probe, option, data=None):
    result = subprocess.run([str(probe), option], input=data, text=True,
                            capture_output=True, timeout=30)
    require(result.returncode == 0, 'consumer failed: ' + result.stderr)
    require(bool(result.stdout) and not result.stderr, 'empty consumer output or stderr')
    return result.stdout.splitlines()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    # Re-run the original report guards rather than treating JSON as authority.
    import sys
    guards = subprocess.run([sys.executable, '-B', str(Path(__file__).with_name('hud254_trace_test.py')),
                             '--report', str(args.report)], capture_output=True, text=True, timeout=30)
    require(guards.returncode == 0 and 'OK' in guards.stderr and 'skipped' not in guards.stderr,
            'native report guards failed or skipped: ' + guards.stderr)
    native = json.loads((args.report/'native.json').read_text(encoding='ascii'))
    requests, expected = [], []
    for row in native['format_cases']:
        requests.append('f %d %d' % (row['ticks'], row['locale'] or 7))
        expected.append(row['text'])
    for row in native['lap_cases']:
        values = row['input']
        if values.get('enabled') is False:
            continue  # No guessed production mask consumer in this partial implementation.
        now = values['now']
        since = now if values.get('pulse') else values.get('held_since', 0)
        held = values.get('completed', 0) if values.get('pulse') else values.get('held_ticks', 0)
        requests.append('p %d %d %d %d %d %d %d %d %d' % (
            now, values['start'], values.get('lap', 0), values.get('laps', 2),
            values.get('best', 0x7FFFFFFF), int(bool(since)), since, held, values.get('locale', 5)))
        calls = {c['target']: c for c in row['output']['calls'] if 'target' in c}
        fields = []
        for name in ('lap_number', 'lap_total', 'best', 'current'):
            call = calls[name]
            fields.append(call['text'] if 'text' in call else '#' + str(int(call['localized_hash'], 16)))
        expected.append('\t'.join(fields))
    actual = run(args.probe, '--oracle-stream', '\n'.join(requests)+'\n')
    require(len(actual) == len(expected), 'wrong consumer response count')
    for request, wanted, got in zip(requests, expected, actual):
        require(got == wanted, 'native mismatch for %s: %r != %r' % (request, got, wanted))
    package = json.loads((args.report/'data.json').read_text(encoding='ascii'))
    objects = {row['name']: row for row in package['objects']}
    rows = run(args.probe, '--bindings')
    require(len(rows) == 7, 'wrong binding count')
    for line in rows:
        name, resource, index, x, y, z, scale, sj, label, *rgba = line.split('\t')
        obj = objects[name]
        fields = {f['tag']: f for f in obj['fields']}
        require(resource == obj['default_resource']['name'].split('\\')[-1] and
                int(index) == obj['default_resource']['index'], name + ': wrong font request')
        require([float(x), float(y), float(z)] == fields['SA']['position_depth'], name + ': wrong local position')
        require(abs(float(scale)-fields['SA']['size_scale'][0]) < 1e-8, name + ': wrong scale')
        bgra = fields['SA']['bgra']
        require(list(map(int, rgba)) == [bgra[2], bgra[1], bgra[0], bgra[3]], name + ': wrong color')
        require(int(sj) == fields['Sj']['justification'], name + ': wrong raw justification')
        if name.endswith('Text'):
            require(int(label) == int(fields['SH']['string_hash'],16), name + ': wrong localization hash')
    print('PASS: %d native formatter/lap comparisons and %d package bindings; report guards passed' %
          (len(expected), len(rows)))


if __name__ == '__main__':
    main()

"""Render authenticated hull_features_native.py output as exact C++ fixtures."""
import json
import sys
from pathlib import Path

if len(sys.argv) != 3:
    raise SystemExit('usage: hull_query_pins.py feature-json fresh-output-inc')
source, output = map(Path, sys.argv[1:])
if output.exists():
    raise ValueError('output must be fresh')
rows = json.loads(source.read_text())
def f(v):
    return float(v).hex() + 'F'
lines = ['// Original472950/46EA50; retail240SX CV, COM(.25,0,.28).',
         '// Generated from authenticated feature traces by hull_query_pins.py.']
for row in rows:
    q = row['query']
    values = row['input'] + [q['scalar']] + q['witness_a'] + q['witness_b']
    if len(values) != 31:
        raise ValueError('unexpected query schema')
    lines.append('{' + ','.join(map(f, values)) + '},')
output.write_text('\n'.join(lines) + '\n', encoding='ascii')
print('Wrote exact original query fixtures:', len(rows))

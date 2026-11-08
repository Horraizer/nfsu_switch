"""Regenerate earned bonus-car pins from authenticated PC1.4 instructions.

Preset parts use frontend66_preset_native.py's declared zero-slot constructor
and formatter boundaries.004BC830 executes its stores;0057F2B0 is an explicit
recalculation boundary, not a claim to emulate the whole running game.
Outputs must be fresh and outside the read-only retail root.
"""
from pathlib import Path
import argparse, hashlib, json, struct, subprocess, sys
sys.dont_write_bytecode = True
from event0_trace import Image, chunks, jdlz, name_hash, preset_records
from physics256_native import Machine

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('retail_root', type=Path)
ap.add_argument('output', type=Path)
a = ap.parse_args()
if a.output.exists() or a.output.resolve().is_relative_to(a.retail_root.resolve()):
    raise ValueError('fresh output outside retail required')
raw = (a.retail_root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest() == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
global_b = (a.retail_root/'Global/GLOBALB.BUN').read_bytes()
assert hashlib.sha256(global_b).hexdigest() == '39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
front = jdlz((a.retail_root/'FrontEnd/FrontB.lzc').read_bytes())
presets = preset_records(front)
_, start, end = next(row for row in chunks(global_b) if row[0] == 0x34a08)
start = (start+8+15)&~15
rows = []
for at in range(start, end, 32):
    points, kind, key = struct.unpack_from('<III', global_b, at)
    if kind&65535 == 3:
        rows.append((points, presets[key]['name']))
assert len(rows) == 10
lines = ['// Original PC1.4 USA pins. Regenerate with tools/bonus_cars_native.py.',
         'struct BonusCarWitness {const char* preset;unsigned points;std::array<unsigned,9> performance;std::array<int,120> parts;};',
         'static const BonusCarWitness bonus_car_witnesses[]={']
for points, preset in rows:
    run = subprocess.run([sys.executable, '-B', str(Path(__file__).with_name('frontend66_preset_native.py')),
                          str(a.retail_root), '--preset', preset], capture_output=True, text=True)
    if run.returncode or 'Traceback' in run.stderr:
        raise RuntimeError(preset+' native witness failed: '+run.stdout+run.stderr)
    result = json.loads(run.stdout.splitlines()[-1])
    index = result['calls'][0]['stock_car']
    image = Image(raw)
    image.sections = [(x, y, z) for x, y, z, _ in image.sections]
    m = Machine(image)
    m.allowed = [(0x4bc830, 0x4bcb40)]
    ride = 0x1100000
    m.hook(0x57f2b0, 'performance recalculation boundary', pop=4)
    m.run(0x4bc830, (index,), eax=ride)
    levels = [m.get(ride+8+i*4) for i in range(9)]
    parts = [-1 if value is None else value for value in result['parts']]
    assert len(parts) == 120
    lines.append('    {"%s",%dU,{{%s}},{{%s}}},' % (preset, points,
        ','.join(map(str, levels)), ','.join(map(str, parts))))
    print('PASS original bonus', preset, 'style', points, 'performance', levels, flush=True)
lines.append('};')
a.output.write_text('\n'.join(lines)+'\n', encoding='ascii')
print('PASS original bonus-car witnesses', len(rows))

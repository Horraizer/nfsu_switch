"""Compare compiled engine graph with original instructions on supplied controls."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import subprocess
import struct

from hud254_trace import PE, chunks
from audio312_native import BankGraph, engine_parameters, Machine
from unicorn import x86_const as x

p = argparse.ArgumentParser()
p.add_argument('--root', type=Path, required=True)
p.add_argument('--probe', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--all', action='store_true')
p.add_argument('--tuned', action='store_true')
p.add_argument('--gains',type=int,nargs=2)
p.add_argument('--pan',type=int,default=32768)
a = p.parse_args()
assert not a.out.exists()
a.out.mkdir(parents=True)
exe = (a.root/'speed.exe').read_bytes()
assert hashlib.sha256(exe).hexdigest() == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
image = PE(exe)
gain_a,gain_b = a.gains if a.gains else (32767,32767)
if a.tuned:
    assert a.gains is None,'use either selected tuning or explicit input gains'
    bundle = (a.root/'Global/GLOBALB.BUN').read_bytes()
    assert hashlib.sha256(bundle).hexdigest() == '39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
    begin = next(start for kind,start,end in chunks(bundle) if kind == 0x34600)
    record = (begin+15)//16*16+14*0xc90
    assert bundle[record:record+8] == b'INTEGRA\0'
    tune = bundle[record+0x9c0+0x64:record+0x9c0+0xc8]
    m = Machine(image, [(0x529cd0,0x529d36)])
    owner, table = 0x1100000, 0x1104000
    m.uc.mem_write(table,tune)
    m.wi(owner+0x1f0c,table)
    m.callbacks[0x529d36] = lambda cpu: cpu.finish()
    m.run(0x529cd0,regs={x.UC_X86_REG_ECX:owner})
    gain_a, gain_b = m.ri(owner+0x238), m.ri(owner+0x23c)
    assert (gain_a,gain_b) == tuple((v*32767)>>15 for v in struct.unpack_from('<2h',tune,10))
    print('original selected tuning gain pair',gain_a,gain_b,flush=True)
report = []
for group in (range(4) if a.all else (2,)):
    for upgrade in (range(4) if a.all else (1,)):
        for kind in ('ENG', 'EXH'):
            name = f'C{group}U{upgrade}_M0_{kind}_MB'
            path = a.root/f'Sound/CAR_{group}/ENG_UG_{upgrade}/{name}.abk'
            bank = path.read_bytes()
            result = subprocess.run([str(a.probe.resolve()), str(path), str(group), str(upgrade),str(gain_a),str(gain_b),str(a.pan)], capture_output=True, text=True)
            (a.out/(name+'-port.csv')).write_text(result.stdout, encoding='ascii')
            assert result.returncode == 0, result.stderr
            rows = list(csv.DictReader(io.StringIO(result.stdout)))
            assert len(rows) == 960
            graph = BankGraph(image, bank)
            differences = []
            native = []
            for tick in range(240):
                subset = rows[tick*4:tick*4+4]
                rpm, load = float(subset[0]['rpm']), float(subset[0]['load'])
                params = engine_parameters(image, rpm, load, group=group, game_producer=True, pan=a.pan)
                # Engine event record +0C carries the selected upgrade.
                params[2] = upgrade
                params[8],params[9] = gain_a,gain_b
                voices = graph.tick(params)
                native.append(voices)
                for actual, expected in zip(subset, voices):
                    for key, field in [('sample','sample'),('state','state'),('pitch','pitch_q12'),('gain','gain_q15'),('pan','pan')]:
                        if int(actual[key]) != expected[field]:
                            differences.append([tick, expected['sample'], key, int(actual[key]), expected[field]])
            (a.out/(name+'-native.json')).write_text(json.dumps(native,indent=2)+'\n',encoding='ascii')
            record = dict(bank=name, sha256=hashlib.sha256(bank).hexdigest(), controls=960, differences=differences)
            report.append(record)
            print(name, 'differences', len(differences), differences[:3], flush=True)
(a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='ascii')
raise SystemExit(1 if any(r['differences'] for r in report) else 0)

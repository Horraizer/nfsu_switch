"""Check portable player constraints against a fresh authenticated original fixture."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path.cwd()/'tools'))
from world_body_trace import manifest
from handling_brake_compare import EXE_SHA, BUN_SHA


def compare(root, native, out):
    data=native.read_bytes(); report=json.loads(data)
    rows=report['rows']
    if (report['executable_sha256']!=EXE_SHA or report['globalb_sha256']!=BUN_SHA
            or report['external_hooks'] or len(rows)!=216
            or 'rng_reset_per_case' not in report):
        raise ValueError('authenticated isolated original216 fixture required')
    for p,sha in ((root/'speed.exe',EXE_SHA),(root/'Global/GLOBALB.BUN',BUN_SHA)):
        if hashlib.sha256(p.read_bytes()).hexdigest()!=sha:raise ValueError('retail provenance mismatch')
    binary=out/'handling251-player-coupled-compare.exe'
    sources=['tools/handling_player_coupled_compare.cpp','src/player_powertrain.cpp',
             'src/vehicle.cpp','src/sha256.cpp']
    command=['g++','-std=c++17','-O2','-ffp-contract=off','-fno-fast-math','-I','include',*sources,'-o',str(binary)]
    build=subprocess.run(command,capture_output=True,text=True)
    (out/'build.log').write_text(build.stdout+build.stderr,encoding='ascii')
    if build.returncode:raise RuntimeError('portable build failed')
    inputs=''.join('%d %d %.9g %.9g %.9g\n'%(r['gear'],r['gearbox_mode'],r['clutch'],r['throttle'],r['initial_wheel_spin']) for r in rows)
    run=subprocess.run([str(binary),str(report['owner_speed']),str(report.get('lateral_speed',0.0)),
                        str(report.get('initial_history_torque',0.0)),str(report.get('longitudinal_speed',8.316501835256739)),
                        str(report.get('initial_rpm',5000.0)),str(report.get('service_axis',1.0)),
                        str(report.get('owner_magnitude',0.0)),str(report.get('rear_spin_spread',0.0))],input=inputs,capture_output=True,text=True)
    (out/'portable.csv').write_text(run.stdout,encoding='ascii')
    if run.returncode or run.stderr or len(run.stdout.splitlines())!=len(rows):raise RuntimeError('fresh portable rows missing')
    differences=[]
    for i,(row,line) in enumerate(zip(rows,run.stdout.splitlines())):
        actual=[float(x) for x in line.split(',')]
        names=['engine_feedback_torque','engine_angular_acceleration','completed_engine_rad_s','exported_actor_rpm']
        expected=[row[k] for k in names]
        for k in range(4):
            for field in ['wheel_total_torque','tyre_reaction','tyre_lateral','contact_slip','completed_wheel']:
                names.append('%s[%d]'%(field,k));expected.append(row[field][k])
        for field in ['completed_clutch','completed_gearbox_mode','post_update_wheel_equivalent_engine_rad_s',
                      'completed_up_offset','completed_down_offset','completed_rng']:
            names.append(field);expected.append(row[field])
        if len(actual)!=len(expected):raise RuntimeError('portable schema mismatch')
        for field,a,b in zip(names,actual,expected):
            if a!=b:differences.append([i,field,b,a])
    source_paths=[Path(p) for p in sources+['include/nfsu/player_powertrain.h','include/nfsu/vehicle.h']]
    result=dict(native_report_sha256=hashlib.sha256(data).hexdigest(),
                native_source_head=report['source_head'],native_source_tool_sha256=report['source_tool_sha256'],
                portable_head=subprocess.check_output(['git','rev-parse','HEAD']).decode('ascii').strip(),
                sources={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_paths},
                binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                owner_speed=report['owner_speed'],rows=len(rows),differences=differences,
                boundary=report['boundary'])
    (out/'report.json').write_text(json.dumps(result,ensure_ascii=True,indent=2)+'\n',encoding='ascii')
    if differences:raise RuntimeError('portable differences: see report.json')
    print('PASS: original/portable216 exact constraint, wheel, engine and gearbox state rows')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True);p.add_argument('--native-report',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();root=a.root.resolve();out=a.out.resolve();relative=out.relative_to(Path.cwd())
    if not relative.parts[0].startswith('build') or not str(relative).isascii() or root in out.parents:
        p.error('fresh ASCII workspace build output outside retail required')
    out.mkdir(parents=True,exist_ok=False);before=manifest(root);(out/'before.json').write_bytes(before)
    try:compare(root,a.native_report.resolve(),out)
    finally:
        after=manifest(root);(out/'after.json').write_bytes(after)
        if before!=after:raise RuntimeError('retail manifest changed')
        print('PASS: complete retail content/metadata manifest unchanged')


if __name__=='__main__':main()

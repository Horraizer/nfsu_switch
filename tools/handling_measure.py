"""Issue 248: reproduce unpinned handling measurements without changing the model.

Run from the repository root with the desktop toolchain on PATH. Output must
be a new directory outside the read-only retail installation. Logs and retail
derived outputs stay there; only this script belongs in source control.
"""
import argparse
import csv
import json
from pathlib import Path
import re
import shutil
import statistics
import subprocess


def manifest(root):
    rows = []
    for path in sorted(root.rglob('*')):
        info = path.stat()
        rows.append([path.relative_to(root).as_posix(),
                     'dir' if path.is_dir() else info.st_size, info.st_mtime_ns])
    return json.dumps(rows, ensure_ascii=True).encode('ascii')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('root', 'probe', 'race', 'out'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    forbidden = Path('E:/Games').resolve()
    if out == root or root in out.parents or out == forbidden or forbidden in out.parents:
        parser.error('output must be outside retail root and E:/Games')
    cmp = shutil.which('cmp')
    if not cmp:
        parser.error('cmp must be on PATH')
    out.mkdir(parents=True, exist_ok=False)
    summary = {}

    def run(name, command):
        before, after = out / (name + '-before.json'), out / (name + '-after.json')
        (out / (name + '-command.json')).write_text(
            json.dumps([str(x) for x in command], ensure_ascii=True) + '\n', encoding='ascii')
        before.write_bytes(manifest(root))
        try:
            result = subprocess.run([str(x) for x in command], stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, check=False)
            (out / (name + '.log')).write_bytes(result.stdout)
            text = result.stdout.decode('utf-8')
            if result.returncode or not text.strip() or 'error:' in text.lower():
                raise RuntimeError(name + ' failed; see log')
            return text
        finally:
            after.write_bytes(manifest(root))
            subprocess.run([cmp, str(before), str(after)], check=True)

    pattern = re.compile(
        r'(?:at|final) step (\d+) t ([\d.]+) pos ([-\d.]+) ([-\d.]+) ([-\d.]+)'
        r' kmh ([-\d.]+) lateral ([-\d.]+) yaw-rate ([-\d.]+).*'
        r' steer-angle ([-\d.]+) load ([-\d. ]+) sliding ([01]{4})')
    def probe(name, extra):
        text = run(name, [args.probe.resolve(), 'vehicle-sim'] + extra +
                   ['--every', '1', '--handling', root / 'Global/GLOBALB.BUN'])
        rows = []
        for match in pattern.finditer(text):
            g = match.groups()
            rows.append(dict(step=int(g[0]), time=int(g[0])/120, x=float(g[2]),
                             kmh=float(g[5]), lateral=float(g[6]), yaw=float(g[7]),
                             angle=float(g[8]), sliding=g[10]))
        if not rows or 'telemetry: records' not in text:
            raise RuntimeError(name + ' missing telemetry')
        return rows

    accel = probe('accelerate', ['accelerate'])
    hit = next(r for r in accel if r['kmh'] >= 100)
    summary['acceleration'] = {'first_100_step': hit['step'],
                               'seconds_after_throttle': (hit['step'] - 60)/120}
    brake = probe('brake', ['brake'])
    stop = next(r for r in brake if abs(r['kmh']) <= 0.1)
    summary['brake'] = {'threshold_kmh': 0.1, 'time_s': stop['time'],
                        'distance_m': stop['x']}
    top = probe('top-speed', ['top-speed'])
    summary['top_speed'] = {'at_120_s_kmh': top[-1]['kmh'],
                            'last_10_s_range_kmh': [min(r['kmh'] for r in top[-1200:]),
                                                   max(r['kmh'] for r in top[-1200:])]}
    for name, speed, throttle, steer in [('corner-long', 20, 0.5, 0.2),
                                         ('step-steer', 15, 0.4, 0.3)]:
        script = out / (name + '.txt')
        script.write_text('speed %s\n7200 %s 0 %s 0 none\n' %
                          (speed, throttle, steer), encoding='ascii')
        rows = probe(name, ['--script', script])
        tail = rows[-1200:]
        summary[name] = {
            'last_10_s_mean_kmh': statistics.mean(r['kmh'] for r in tail),
            'last_10_s_range_kmh': [min(r['kmh'] for r in tail), max(r['kmh'] for r in tail)],
            'last_10_s_mean_yaw_rad_s': statistics.mean(r['yaw'] for r in tail),
            'last_10_s_mean_angle_rad': statistics.mean(r['angle'] for r in tail),
            'last_10_s_sliding_steps': sum(r['sliding'] != '0000' for r in tail),
            'early': [rows[k-1] for k in (1, 12, 30, 60, 120)]}
    for name, extra in [('stock-race', []), ('tuned-race', ['--tuned'])]:
        target = out / name
        text = run(name, [args.race.resolve(), '--root', root, '--input',
                         Path.cwd()/'tests/recordings/event1.txt', '--out', target] + extra)
        with (target / 'trajectory.csv').open() as stream:
            rows = list(csv.DictReader(stream))
        if 'outcome finished' not in text or not rows:
            raise RuntimeError('race did not finish')
        speeds = [float(r['speed'])*3.6 for r in rows]
        summary[name] = {'maximum_kmh': max(speeds), 'mean_kmh': statistics.mean(speeds),
                         'samples': len(rows),
                         'contact_samples': sum(int(r['contacts']) > 0 for r in rows),
                         'under_four_road_wheels': sum(int(r['road_wheels']) < 4 for r in rows)}
    for name in ('states.txt', 'trajectory.csv'):
        subprocess.run([cmp, str(out/'stock-race'/name), str(out/'tuned-race'/name)], check=True)
    summary['stock_tuned_states_and_trajectory'] = 'byte-identical'
    result = json.dumps(summary, indent=2, sort_keys=True)
    (out/'summary.json').write_text(result + '\n', encoding='ascii')
    print(result)


if __name__ == '__main__':
    main()

"""Compare the C++ descriptor against a fresh authenticated event0_trace report.

Reports contain metadata only and stay untracked. The check preserves a full
retail manifest around the C++ load/guards. Python 3.8+, no Capstone needed.
"""
import argparse
import json
from pathlib import Path
import subprocess
import struct
from event0_trace import INPUTS, digest_file, manifest


def check(driver, root, trace):
    report = json.loads(trace.read_text(encoding='ascii'))
    for name, expected in INPUTS.items():
        if digest_file(root / name) != expected:
            raise ValueError('retail input identity mismatch: ' + name)
        if report['provenance'][name]['sha256'] != expected:
            raise ValueError('trace provenance mismatch: ' + name)
    before = manifest(root)
    try:
        run = subprocess.run([str(driver.resolve()), str(root.resolve()), '--dump'],
                             capture_output=True, text=True, check=True)
        actual = json.loads(run.stdout)
        tables = report['tables']
        events = tables['FrontEnd/FrontB.lzc']['events']
        event = next(row for row in events if row['number'] == 0)
        race = event['races'][0]
        track = tables['Global/GLOBALB.BUN']['track1003']
        assert actual['event'] == event['number']
        assert actual['event_offset'] == event['offset']
        assert actual['track_offset'] == track['offset']
        for key in ('track', 'laps', 'direction'):
            assert actual[key] == race[key], key
        words = [int(word, 16) for word in event['words']]
        assert actual['words'] == words
        assert actual['desired_grid'] == words[0x138 // 4]
        assert actual['difficulty'] == 1 and actual['ai_scalar'] == 1.0
        markers = {row['marker_index']: row for row in tables['Tracks/TRACKB1003.lzc']['forward_grid']}
        initial = [4, 1, 2, 3]
        final = [2, 1, 4, 3]
        for index, actor in enumerate(actual['actors']):
            preset = event['presets'][index]
            assert actor['id'] == ([0, 100, 101, 102][index])
            assert actor['role'] == (1 if index == 0 else 2)
            for key in ('name', 'car', 'pairs'):
                assert actor[key] == preset[key], (index, key)
            assert actor['hash'] == int(preset['hash'], 16)
            assert actor['preset_offset'] == preset['offset']
            assert actor['initial_grid'] == initial[index] and actor['final_grid'] == final[index]
            marker = markers[final[index] - 1]
            assert actor['marker_index'] == marker['marker_index']
            assert actor['marker_offset'] == marker['offset']
            assert struct.pack('<3f', *actor['position']) == struct.pack('<3f', *marker['raw_position'])
            assert actor['angle'] == marker['angle_u16']
            if index:
                rival = event['rivals'][index - 1]
                assert actor['source_offset'] == rival['offset']
                assert actor['label'] == rival['name'] and not actor['profile_label']
                assert actor['context'] == rival['difficulty_values']
                assert actor['selected_context'] == rival['difficulty_values'][1]
                assert actor['tuning_selector'] == rival['tuning_selector']
                assert actor['ai_byte'] == rival['ai_byte'] and actor['performance'] is None
            else:
                assert actor['source_offset'] == event['offset'] + 0xD0
                assert actor['profile_label'] and actor['label'] == ''
                named = dict(token.split('=') for token in actor['performance'].split(','))
                assert len(named) == 11 and set(named.values()) == {'1'}
        assert actual['traffic_policy'] == track['traffic_counts']
        assert actual['traffic_request'] == track['event0_traffic_configs']
        assert actual['traffic_live_count'] is None
        assert actual['authenticated'] and not actual['runtime_ready']
        assert actual['hashes'] == [tables[name]['sha256'] for name in
                                    ('FrontEnd/FrontB.lzc', 'Global/GLOBALB.BUN', 'Tracks/TRACKB1003.lzc')]
    finally:
        if manifest(root) != before:
            raise ValueError('retail manifest changed during descriptor check')
    print('PASS: authenticated trace/descriptor comparison; all 480 ordered pairs, actor/grid/policy fields; retail manifest unchanged')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--trace', type=Path, required=True)
    args = parser.parse_args()
    check(args.driver, args.root, args.trace)

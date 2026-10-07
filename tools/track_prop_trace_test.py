"""Synthetic guards and optional assertions over a fresh static retail report.

Python 3.8+. No original executable is launched. No gameplay pass is claimed.
Run -B tools/track_prop_trace_test.py [--report build/fresh/270-props.json].
"""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

import track_prop_trace as p


class ResearchGuards(unittest.TestCase):
    def test_append_unload_reload_keeps_record_identity(self):
        ops = [('load', 'prior-load', 'cone'), ('load', 'track-first', 'cone'),
               ('load', 'track-second', 'cone'), ('find', 'cone'),
               ('unload', 'prior-load'), ('find', 'cone'),
               ('unload', 'track-first'), ('find', 'cone'),
               ('load', 'track-first-reload', 'cone'), ('find', 'cone')]
        self.assertEqual(p.list_replay(ops), dict(
            order=['track-second', 'track-first-reload'],
            queries=['prior-load', 'track-first', 'track-second', 'track-second']))

    def test_empty_missing_and_invalid_list_nodes(self):
        self.assertEqual(p.list_replay([('find', 'missing')])['queries'], [None])
        self.assertEqual(p.list_replay([('load', 'x', 'k'), ('unload', 'x')])['order'], [])
        for ops in ([('load', None, 'k')], [('unload', 'x')],
                    [('load', 'x', 'k'), ('load', 'x', 'k')]):
            with self.assertRaises(ValueError):
                p.list_replay(ops)

    def test_demotion_is_strict_and_requires_proxy_prerequisites(self):
        decide = p.lifecycle_decision
        for stamp in (0, 0x7FFFFFFF):
            self.assertEqual(decide(True, stamp, False, 2, 0, 0, 2), 'retain')
            self.assertEqual(decide(True, stamp, False, 2.01, 0, 0, 2),
                             'attempt-proxy-then-delete-body')
            self.assertEqual(decide(False, stamp, False, 3, 0, 0, 2), 'retain')
            self.assertEqual(decide(True, stamp, True, 3, 0, 0, 2), 'retain')

    def test_settled_is_not_unactivated(self):
        # An activation clock survives deactivation. Zero exported lifetime
        # keeps an activated body out of BOTH lifetime deletion branches.
        self.assertEqual(p.lifecycle_decision(True, 4000, False, 999, 999, 0, 2), 'retain')

    def test_lifetime_is_nonzero_not_positive_and_expires_at_equality(self):
        decide = p.lifecycle_decision
        self.assertEqual(decide(True, 4000, False, 3, 1.99, 2, 2), 'retain')
        self.assertEqual(decide(True, 4000, False, 3, 2, 2, 2), 'callback-7c-then-delete-body')
        self.assertEqual(decide(False, 4000, False, 3, 0, -1, 2), 'callback-7c-then-delete-body')

    def test_settling_clock_and_speed_boundaries(self):
        c = dict(zip(('006B5A7C', '006B5A78', '006B5A70', '006B5A74'),
                     (0.1, 0.025, 0.1, 0.1)))
        self.assertTrue(p.settle_decision(1, True, 1, 0, 0, c))
        for values in ((0.1, True, 1, 0, 0), (1, False, 1, 0, 0),
                       (1, True, 0.025, 0, 0), (1, True, 1, 0.1, 0),
                       (1, True, 1, 0, 0.1)):
            self.assertFalse(p.settle_decision(*values, c))

    def test_nonfinite_synthetic_inputs_fail(self):
        with self.assertRaises(ValueError):
            p.lifecycle_decision(True, 4000, False, math.nan, 0, 0, 2)

    def test_chunk_and_record_bounds(self):
        with self.assertRaises(ValueError):
            list(p.chunks(struct.pack('<II', 1, 8)))
        with self.assertRaises(ValueError):
            list(p.chunks(b'', -1, 0))
        with self.assertRaises(ValueError):
            p.record_start(bytes(16), 0, 16, 256)
        with self.assertRaises(ValueError):
            p.identity(b'ab', 0, 1, 2)

    def test_jdlz_refuses_truncation_and_invalid_backreference(self):
        for tail, size in ((b'', 1), (b'\x01\x01\x00\x00', 3)):
            blob = b'JDLZ\x02\x10\0\0' + struct.pack('<II', size, 16 + len(tail)) + tail
            with self.assertRaises(ValueError):
                p.jdlz(blob)

    def test_pe_refuses_short_headers(self):
        with self.assertRaises(ValueError):
            p.Image(b'MZ')

    def test_stream_identity_does_not_accept_section_name_alone(self):
        record = bytearray(56)
        struct.pack_into('<h', record, 8, 1204)
        struct.pack_into('<III', record, 16, 1, 100, 48)
        track = struct.pack('<II', 0x34107, 56) + record
        placements = [dict(section=1204, offset=800)]
        with self.assertRaises(ValueError):
            p.stream_bindings(track, placements, [dict(placement_offset=800, scenery_offset=101)])
        matched = p.stream_bindings(track, placements, [dict(placement_offset=800, scenery_offset=100)])
        self.assertEqual(matched[0]['live_route_presence'], 'unavailable')
        self.assertFalse(matched[0]['static_track_byte_request'])

    def test_output_guards_actual_workspace(self):
        workspace = Path.cwd()
        parent = workspace / 'build'
        parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='270-astra-test-', dir=str(parent)) as tmp:
            base = Path(tmp)
            retail = base / 'retail'
            retail.mkdir()
            for out in (retail, retail / 'output', base, workspace.parent / 'outside',
                        workspace / 'docs/270-denied', base / ('bad-' + chr(0xE9))):
                with self.assertRaises(ValueError):
                    p.output_path(workspace, retail, out)
            _, allowed = p.output_path(workspace, retail, base / 'new-parent/new-output')
            self.assertTrue(allowed.is_absolute())
            self.assertFalse(allowed.exists())

    def test_cli_refusal_precedes_input_read(self):
        parent = Path.cwd() / 'build'
        parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='270-astra-cli-', dir=str(parent)) as tmp:
            base = Path(tmp)
            retail = base / 'retail'
            retail.mkdir()
            existing = base / 'existing'
            existing.mkdir()
            marker = existing / 'marker.txt'
            marker.write_bytes(b'preserve\n')
            for target, error in ((existing, b'output directory must be fresh'),
                                  (retail / 'denied', b'output overlaps retail')):
                run = subprocess.run([sys.executable, '-B', 'tools/track_prop_trace.py',
                    '--root', str(retail), '--out', str(target)],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.assertEqual(run.returncode, 2)
                self.assertIn(error, run.stderr)
            self.assertEqual(marker.read_bytes(), b'preserve\n')
            self.assertEqual(list(retail.iterdir()), [])


def verify_report(path):
    report = json.loads(path.read_text(encoding='ascii'))
    assert report['evidence'] == 'static-original-files'
    assert report['inputs']['speed.exe']['sha256'] == p.INPUTS['speed.exe']
    assert report['checked_instructions'] == len(p.INSTRUCTIONS)
    assert len(report['definitions']) == 77
    assert len(report['placements']) == len(report['scenery_links']) == len(report['stream_bindings']) == 178
    assert all(r['static_track_byte_request'] for r in report['stream_bindings'])
    assert len({r['offset'] for r in report['placements']}) == 178
    definitions = {r['offset']: r for r in report['definitions']}
    placements = {r['offset']: r for r in report['placements']}
    for at, section, instance, key in ((810656, 1204, 49, '5087DB01'),
                                     (793520, 1001, 2, '629B3B7D'),
                                     (794192, 1003, 63, '662259F6')):
        row = placements[at]
        assert (row['section'], row['instance'], row['hash']) == (section, instance, key)
    assert placements[810656]['definition_offsets'] == [257504, 257760]
    assert definitions[257504]['descriptor_sha256'] != definitions[257760]['descriptor_sha256']
    assert len([h for h in report['hull_records'] if h['hash'] == '5087DB01']) == 2
    assert definitions[247776]['activation_field'] == definitions[248032]['activation_field'] > 0
    assert definitions[253152]['activation_field'] > 0
    used = {at for row in report['placements'] for at in row['definition_offsets']}
    assert all(definitions[at]['effect_hashes'] == ['00000000', '00000000'] for at in used)
    assert all(definitions[at]['child_count'] == definitions[at]['break_budget_field'] ==
               definitions[at]['lifetime_field'] == 0 for at in used)
    assert report['race_message_targets']['8'] == '00422BD4'
    assert report['vtables']['006B8B18'][0x7C // 4] == '0040A880'
    assert report['synthetic']['list_order']['queries'] == ['257504']
    assert (path.parent / '270-before.json').read_bytes() == (path.parent / '270-after.json').read_bytes()
    assert (path.parent / '270-instructions.txt').stat().st_size > 0
    print('PASS: static report identity, named thresholds, duplicate hulls, streaming and reset assertions')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ResearchGuards))
    if not result.wasSuccessful():
        raise SystemExit(1)
    if args.report:
        verify_report(args.report)

"""Assert a fresh physics256 probe report plus fail-closed harness guards.

All numbers here are synthetic case expectations, not original runtime pins.
"""

import argparse
import json
from pathlib import Path
import struct
import sys
import unittest

from physics256_native import Machine, STOP, PHYSICS
from world_body_trace import INPUTS, digest_file

REPORT = None


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


class ProbeTests(unittest.TestCase):
    def test_identity_and_fresh_tool_hashes(self):
        size, sha = INPUTS['speed.exe']
        self.assertEqual(REPORT['input'], dict(size=size, sha256=sha))
        self.assertEqual(REPORT['versions'], dict(capstone='5.0.7', unicorn='2.1.4'))
        for name in ('physics256_trace.py', 'physics256_native.py'):
            self.assertEqual(REPORT['tool_hashes'][name], digest_file(Path(__file__).with_name(name)))

    def test_source_ownership_and_distinct_selectors(self):
        self.assertIn('004260E4', REPORT['ownership'])
        tables = REPORT['source']['tables']
        self.assertEqual(tables['physics1-step']['values'], ['004774A0'])
        self.assertEqual(tables['physics3-step']['values'], ['00474A90'])
        self.assertEqual(tables['body-step']['values'], ['0046A030'])
        self.assertEqual(REPORT['source']['constants'], {
            '006FFE14': f32(0.0044), '006FFF10': f32(0.0084),
            '006EF2C8': f32(0.008), '006CCAB0': f32(0.0001)})
        for field in ('original_runtime', 'original_cadence', 'original_contact_matrix'):
            self.assertIsNone(REPORT['source'][field])
        self.assertEqual(REPORT['negative_source_guards'], [
            'source assertion failed: 004264EB', 'source table failed: physics1-step'])

    def test_dt_subdivision_and_publication_after_slices(self):
        rows = REPORT['native']['physics']
        self.assertEqual(len(rows), 12)
        for row in rows:
            counts = {1: [1, 4, 7], 3: [1, 2, 4]}[row['selector']]
            index = [0.0, 1.0 / 60, 0.03].index(row['dt'])
            self.assertEqual(len(row['slices']), counts[index])
            for step in row['slices']:
                self.assertEqual(step['system'], PHYSICS + 8)
                self.assertEqual(step['dt'], step['global_dt'])
                self.assertAlmostEqual(step['dt'], f32(row['dt']) / counts[index], places=8)
            self.assertEqual(row['exports'], [f32(row['dt'])])
            hooks = [x['hook'] for x in row['hooks']]
            last_slice = max(i for i, x in enumerate(hooks) if x == 'integration_capture_no_dynamics')
            export = next(i for i, x in enumerate(hooks) if 'publication_capture' in x)
            self.assertGreater(export, last_slice)

    def test_threshold_is_strict_subdivision_boundary(self):
        rows = REPORT['native']['thresholds']
        self.assertEqual(len(rows), 6)
        self.assertEqual([len(x['slices']) for x in rows], [1, 2, 2, 1, 2, 2])

    def test_setup_skip_does_not_skip_integration(self):
        for row in REPORT['native']['physics']:
            self.assertEqual(row['setup_executed'], row['selector'] == 3 or row['scheduler'] == 0)
            self.assertTrue(row['slices'])
        for row in REPORT['native']['state_gates']:
            expected = [0, 0, 0] if row['state'] == 1 else [1, 1, 0]
            self.assertEqual(row['rigid_flags'], expected)
            self.assertTrue(row['slices'])

    def test_body_chain_gates_and_scheduler_dt(self):
        rows = REPORT['native']['dispatch']
        self.assertEqual(len(rows), 8)
        for row in rows:
            self.assertEqual(row['body_called'], row['active'])
            self.assertEqual(row['actor_called'], row['active'])
            self.assertEqual(row['bounds_called'], row['active'] and row['scheduler'] == 0)
            if not row['active']:
                self.assertEqual(row['slices'], [])
                self.assertEqual(row['exports'], [])
                continue
            expected_dt = f32(0.02 if row['scheduler'] == 0 else 0.008)
            self.assertEqual(row['exports'], [expected_dt])
            expected_count = {(1, 0): 5, (1, 1): 2, (3, 0): 3, (3, 1): 1}
            self.assertEqual(len(row['slices']), expected_count[row['selector'], row['scheduler']])
        wrong = REPORT['native']['wrong_group']
        self.assertFalse(wrong['actor_called'])
        self.assertEqual(wrong['slices'], [])

    def test_group_deduplicates_by_id_and_skips_marked_pair(self):
        self.assertEqual(REPORT['native']['unique_group']['visits'], [[0, f32(0.004)], [2, f32(0.004)]])
        self.assertEqual(REPORT['native']['marked_group']['visits'], [[0, f32(0.004)]])

    def test_group_interval_then_contact_not_fixed_rate(self):
        rows = REPORT['native']['intervals']
        self.assertEqual(len(rows), 4)
        for row, interval in zip(rows, (0.012, 0.008, 0.004, 0.008)):
            self.assertEqual([x[0] for x in row['calls']], ['advance', 'contact'])
            self.assertAlmostEqual(row['calls'][0][1], interval, places=7)
            self.assertAlmostEqual(row['calls'][1][1], row['current'] + interval, places=7)

    def test_actual_publication_stores_and_skip(self):
        rows = REPORT['native']['publication']
        self.assertEqual(len(rows), 4)
        for row in rows:
            self.assertEqual(row['position'], [0.0] * 3 if row['disabled'] else [10.0, 20.0, 30.0])
            self.assertEqual(row['velocity'], [0.0] * 3 if row['disabled'] else [3.0, 4.0, 0.0])
            self.assertEqual(row['angular'], [0.0] * 3 if row['disabled'] else [0.25, 0.5, 0.75])

    def test_driver_query_is_not_integration(self):
        human, ai = REPORT['native']['drivers']
        self.assertTrue(human['human'])
        self.assertFalse(ai['human'])
        self.assertFalse(human['physics_integration'])
        self.assertFalse(ai['physics_integration'])
        self.assertTrue(ai['physics_query'])
        self.assertFalse(human['physics_query'])
        self.assertTrue(human['hooks'])
        self.assertTrue(ai['hooks'])

    def test_pre_post_snapshot_and_zero_dt_guard(self):
        zero, positive = REPORT['native']['snapshots']
        self.assertEqual(zero['saved_position'], [0.0, 0.0, 0.0])
        self.assertEqual(zero['linear_delta_rate'], [-99.0] * 3)
        self.assertEqual(zero['angular_delta_rate'], [-99.0] * 3)
        self.assertEqual(positive['saved_position'], [10.0, 20.0, 30.0])
        self.assertEqual(positive['saved_velocity'], [1.0, 2.0, 3.0])
        self.assertEqual(positive['linear_delta_rate'], [25.0, 50.0, 75.0])
        self.assertEqual(positive['angular_delta_rate'], [25.0, 50.0, 75.0])


class HarnessTests(unittest.TestCase):
    @staticmethod
    def machine():
        class EmptyImage:
            sections = []
            data = b''
        return Machine(EmptyImage())

    def test_unknown_instruction_is_not_success(self):
        m = self.machine()
        m.uc.mem_write(STOP + 0x400, b'\xc3')
        with self.assertRaisesRegex(RuntimeError, 'undeclared execution'):
            m.run(STOP + 0x400)

    def test_instruction_limit_is_not_success(self):
        m = self.machine()
        m.uc.mem_write(STOP + 0x400, b'\xeb\xfe')
        m.allowed.append((STOP + 0x400, STOP + 0x402))
        with self.assertRaisesRegex(RuntimeError, 'budget exhausted'):
            m.run(STOP + 0x400, budget=20)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    REPORT = json.loads(args.report.read_text(encoding='ascii'))
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__]))
    sys.exit(0 if result.wasSuccessful() else 1)

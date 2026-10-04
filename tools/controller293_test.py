"""Issue293 synthetic original-x86 expectations, not live event0 frame pins."""

import argparse
import ast
import json
from pathlib import Path
import struct
import sys
import unittest

from controller293_trace import TOOL_FILES
from physics256_native import Machine, STOP
from world_body_trace import INPUTS, digest_file

REPORT = None
REPORT_PATH = None


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


class EvidenceTests(unittest.TestCase):
    def row(self, name):
        return next(r for r in REPORT['native']['cases'] if r['name'] == name)

    def test_identity_freshness_and_manifest(self):
        size, sha = INPUTS['speed.exe']
        self.assertEqual(REPORT['input'], dict(size=size, sha256=sha))
        self.assertEqual(REPORT['versions'], dict(capstone='5.0.7', unicorn='2.1.4'))
        for name in TOOL_FILES:
            path = Path(__file__).with_name(name)
            self.assertEqual(REPORT['tool_hashes'][name], digest_file(path))
        self.assertEqual((REPORT_PATH.parent / 'before.json').read_bytes(),
                         (REPORT_PATH.parent / 'after.json').read_bytes())
        self.assertIsNone(REPORT['original_runtime'])
        self.assertIsNone(REPORT['native']['original_runtime'])
        self.assertFalse(REPORT['inherited_cpp_validated'])

    def test_source_receivers_and_negative_branches(self):
        tables = REPORT['source']['tables']
        self.assertEqual(tables['driver-update'], ['006C8C1C', '0045E970'])
        self.assertEqual(tables['controller-update'], ['006C89FC', '00462430'])
        self.assertEqual(tables['input-setter'], ['006C8C50', '0045E180'])
        self.assertEqual(REPORT['negative_source_guards'], [
            'source assertion failed: 0045EB7E', 'source assertion failed: 0044C7CB',
            'source assertion failed: 004623EF'])
        self.assertEqual(REPORT['source']['assertions']['0044C731'],
                         ['fmul', 'dword ptr [ebx + 0x4b8]'])

    def test_complete_driver_has_no_hooks_and_reaches_real_helpers(self):
        rows = REPORT['native']['cases']
        self.assertEqual(len(rows), 9)
        for row in rows:
            self.assertEqual(row['hooks'], [])
            self.assertEqual(len(row['frames']), 6)
            for address in ('0045E970', '0045E370', '0045FB30', '004590A0',
                            '00564DB0', '00583EA0', '0045FAE9', '00689FD0', '00689FF0'):
                self.assertIn(address, row['visited'])
            self.assertNotIn('004774A0', row['visited'])
            self.assertNotIn('00474A90', row['visited'])
            for frame in row['frames']:
                for field in ('throttle', 'brake', 'handbrake'):
                    self.assertGreaterEqual(frame[field], 0.0)
                    self.assertLessEqual(frame[field], 1.0)

    def test_straight_acceleration_history(self):
        row = self.row('straight-accelerate')
        self.assertEqual([x['steering_word'] for x in row['frames']], [0] * 6)
        self.assertEqual([x['brake'] for x in row['frames']], [0.0] * 6)
        self.assertEqual(row['frames'][0]['throttle'], 0.46875)
        self.assertEqual(row['frames'][-1]['throttle'], 1.0)
        self.assertEqual(row['frames'][0]['speed_error'], 10.0)
        self.assertAlmostEqual(row['frames'][0]['goal'][0], 11.362499237060547, places=6)

    def test_signed_corners_and_category_dependence(self):
        positive = self.row('corner-positive')['frames']
        negative = self.row('corner-negative')['frames']
        category = self.row('corner-category3')['frames']
        self.assertEqual([positive[0]['steering_word'], positive[-1]['steering_word']], [1228, 2652])
        self.assertEqual([negative[0]['steering_word'], negative[-1]['steering_word']], [64308, 62884])
        self.assertEqual(category[-1]['steering_word'], 2883)
        self.assertLess(positive[0]['steering_error'], 0.0)
        self.assertGreater(negative[0]['steering_error'], 0.0)
        self.assertGreater(positive[0]['goal'][1], 0.0)
        self.assertLess(negative[0]['goal'][1], 0.0)

    def test_braking_is_not_throttle_or_hold(self):
        row = self.row('braking')
        self.assertIn('0045FC90', row['visited'])
        self.assertIn('0045FF75', row['visited'])
        self.assertEqual(row['frames'][0]['brake'], 0.328125)
        self.assertEqual(row['frames'][-1]['brake'], 1.0)
        self.assertEqual([x['throttle'] for x in row['frames']], [0.0] * 6)
        self.assertEqual([x['handbrake'] for x in row['frames']], [0.0] * 6)
        self.assertEqual(row['frames'][0]['speed_error'], -20.0)

    def test_input_gate_and_airborne_take_different_branches(self):
        disabled, airborne = self.row('disabled'), self.row('airborne')
        self.assertIn('0045FC90', disabled['visited'])
        self.assertNotIn('0045FC90', airborne['visited'])
        self.assertIn('0045F2B4', airborne['visited'])
        self.assertNotIn('0045F2B4', disabled['visited'])
        for row in (disabled, airborne):
            self.assertEqual([x['throttle'] for x in row['frames']], [0.0] * 6)
            self.assertEqual([x['brake'] for x in row['frames']], [0.0] * 6)

    def test_reverse_and_special_mode_are_bounded_inputs(self):
        reverse, special = self.row('reverse-command'), self.row('special-flag')
        self.assertEqual([x['gear_command'] for x in reverse['frames']], [0] * 6)
        self.assertEqual([x['throttle'] for x in reverse['frames']], [1.0] * 6)
        self.assertIn('0045FA72', special['visited'])
        for frame in special['frames']:
            self.assertEqual((frame['throttle'], frame['brake'], frame['handbrake']), (0.0, 1.0, 1.0))

    def test_actual_action_exit_clears_special_state_before_next_driver(self):
        row = REPORT['native']['action_exit']
        self.assertEqual(row['hooks'], [])
        self.assertEqual(row['state'], dict(flag=0, actor_flag=0, timers=[-1.0, -1.0]))
        self.assertEqual(row['before']['handbrake'], 1.0)
        self.assertEqual(row['after']['handbrake'], 0.0)
        self.assertEqual(row['after']['brake'], 0.0)
        self.assertEqual(row['after']['throttle'], 0.0)
        self.assertGreater(row['later']['throttle'], 0.0)

    def test_history_rebase_preserves_published_controls(self):
        reset = REPORT['native']['reset']
        self.assertEqual(reset['hooks'], [])
        for field in ('steering_word', 'throttle', 'brake', 'handbrake', 'goal'):
            self.assertEqual(reset['before'][field], reset['after'][field])
        for row in reset['history']:
            self.assertEqual(row['current'], 0.0)
            self.assertEqual(row['counts'], [1, 0, 0])
            self.assertEqual(row['buffers'][0][0], f32(0.02))
            self.assertEqual(row['buffers'][1], [row['previous']] * len(row['buffers'][1]))
            self.assertEqual(row['buffers'][2], [row['previous']] * len(row['buffers'][2]))
        self.assertAlmostEqual(reset['history'][0]['previous'], -0.7853927612304688)
        self.assertEqual(reset['history'][3]['previous'], 10.0)

    def test_action_route_and_strategy_cap_producers(self):
        rows = REPORT['native']['producers']
        self.assertEqual([x['desired_speed'] for x in rows], [25.0, 12.0, 20.0, 12.0, 0.0])
        self.assertEqual([x['duration'] for x in rows], [0.75, 0.75, 0.0, 0.0, 0.0])
        self.assertEqual([x['auxiliary'] for x in rows], [0.125, 0.125, 0.125, 0.125, 0.0])
        for row in rows:
            self.assertEqual(row['enabled'], 1)
            self.assertTrue(row['stopped_before_controller_tail'])
            self.assertEqual(row['hooks'], [dict(hook='synthetic_strategy_speed_cap', address='01000300')])


    def test_real_strategy_selection_and_first_action_construction(self):
        rows = REPORT['strategy']['selection']
        self.assertEqual([r['initial']['strategy_kind'] for r in rows], [1, 2, 3, 2, 2, 1, 1])
        self.assertEqual([r['initial']['action_kind'] for r in rows], [7, 8, 8, 8, 8, 7, 7])
        for row in rows:
            initial = row['initial']
            self.assertEqual(row['hooks'], [])
            self.assertEqual(initial, row['repeated'])
            self.assertEqual(initial['pool_counts'], [1, 1])
            self.assertEqual(initial['flags'], [1, 1, 1, 0])
            self.assertEqual(initial['path_count'], 0)
            self.assertEqual(initial['input_fields'], [0.0, 0.0, 0.0])
            self.assertEqual(initial['action_priority'], 0)
            self.assertEqual(initial['action_duration'], -1.0)
            self.assertEqual(initial['strategy_duration'], -1.0)
            self.assertEqual(initial['strategy_timed'], 0)
            for address in ('0044B831', '004623D0', '0044AA10', '00567160',
                            '0044A080', '0044E2B0', '00458F10', '0044B857'):
                self.assertIn(address, row['visited'])
            self.assertEqual(initial['action_table'],
                             '006C9374' if initial['action_kind'] == 7 else '006C920C')

    def test_real_cap_removes_injected_scalar_and_binds(self):
        rows = REPORT['strategy']['publication']
        self.assertEqual([r['published']['desired'] for r in rows], [20.0, 1000000000.0])
        for row in rows:
            self.assertEqual(row['hooks'], [])
            self.assertEqual(row['published']['duration'], 0.0)
            self.assertEqual(row['published']['auxiliary'], 0.0)
            self.assertEqual(row['published']['enabled'], 1)
            self.assertEqual(row['published']['path_count'], 6)
            for address in ('0044A1F0', '0044C550', '0045FAF0', '00459980',
                            '00459870', '0045E180'):
                self.assertIn(address, row['visited'])
            self.assertNotIn('01000300', row['visited'])

    def test_changing_synthetic_pose_speed_and_long_path(self):
        positive, negative = REPORT['strategy']['evolving']
        pins = [1228, 2456, 2315, 2570, 2201, 2268]
        self.assertEqual([r['controls']['steering_word'] for r in positive['frames']], pins)
        self.assertEqual([r['controls']['steering_word'] for r in negative['frames']],
                         [(-n) & 65535 for n in pins])
        for row in (positive, negative):
            self.assertFalse(row['physical_integration'])
            self.assertFalse(row['observed_runtime_inputs'])
            self.assertEqual(row['hooks'], [])
            self.assertIn('00459870', row['visited'])
            self.assertIn('00459980', row['visited'])
            self.assertEqual([f['cache_tick'] for f in row['frames']],
                             [f['inputs']['world_tick'] for f in row['frames']])
            self.assertEqual([f['controls']['speed_error'] for f in row['frames']],
                             [10.0, 10.0, -8.0, -8.0, 10.0, 10.0])
            self.assertEqual(len({tuple(f['controls']['goal']) for f in row['frames']}), 6)
            self.assertEqual(row['frames'][3]['controls']['brake'], 0.46875)
            self.assertGreater(row['frames'][5]['controls']['throttle'], 0.7)
            for frame in row['frames']:
                for name in ('throttle', 'brake', 'handbrake'):
                    self.assertGreaterEqual(frame['controls'][name], 0.0)
                    self.assertLessEqual(frame['controls'][name], 1.0)
        for a, b in zip(positive['frames'], negative['frames']):
            self.assertEqual(a['controls']['goal'][0], b['controls']['goal'][0])
            self.assertEqual(a['controls']['goal'][1], -b['controls']['goal'][1])

    def test_original_action_reaches_precise_unexecuted_planner_boundary(self):
        report = REPORT['strategy']
        row = report['planner_boundary']
        self.assertFalse(report['planner_executed'])
        self.assertIsNone(report['original_runtime'])
        self.assertEqual(row['hooks'], [])
        self.assertEqual(row['action_kind'], 7)
        self.assertEqual(row['stopped_at'], '0045B050')
        self.assertEqual(row['action_path_count'], 0)
        self.assertIn('004502D7', row['visited'])
        self.assertNotIn('00450321', row['visited'])


class HarnessTests(unittest.TestCase):
    @staticmethod
    def machine():
        class EmptyImage:
            sections = []
            data = b''
        return Machine(EmptyImage())

    def test_unknown_execution_fails(self):
        m = self.machine()
        m.uc.mem_write(STOP + 0x400, b'\xc3')
        with self.assertRaisesRegex(RuntimeError, 'undeclared execution'):
            m.run(STOP + 0x400)

    def test_budget_exhaustion_fails(self):
        m = self.machine()
        m.uc.mem_write(STOP + 0x400, b'\xeb\xfe')
        m.allowed.append((STOP + 0x400, STOP + 0x402))
        with self.assertRaisesRegex(RuntimeError, 'budget exhausted'):
            m.run(STOP + 0x400, budget=10)

    def test_python38_grammar_and_ascii_tools(self):
        for name in TOOL_FILES:
            data = Path(__file__).with_name(name).read_bytes()
            self.assertTrue(all(b in (9, 10, 13) or 32 <= b <= 126 for b in data), name)
            ast.parse(data.decode('ascii'), filename=name, feature_version=8)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    REPORT_PATH = args.report
    REPORT = json.loads(args.report.read_text(encoding='ascii'))
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__]))
    sys.exit(0 if result.wasSuccessful() else 1)

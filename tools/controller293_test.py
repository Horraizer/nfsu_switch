"""Issue293 synthetic original-x86 expectations, not live event0 frame pins."""

import argparse
import ast
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest

from controller293_trace import TOOL_FILES
from physics256_native import Machine, STOP
from controller293_planner import PlannerMachine, RETAIL, low_memory
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
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
            'source assertion failed: 004623EF', 'source assertion failed: 0057A318'])
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


class PlannerTests(unittest.TestCase):
    def test_original_producers_and_external_services(self):
        self.assertEqual(REPORT['contract'], 'controller293-v5')
        required = ('00442040', '004484F0', '00462070', '00417600',
                    '0057D160', '0057F2B0', '00464860', '0057A1B0',
                    '0057A380', '00472B00', '00469B00', '004620C0',
                    '0045DBB0', '0044CF30', '00450210', '0045B050',
                    '0045032D', '0044C550', '0045FAF0', '0045E180',
                    '0044A1F0', '00426A5C', '0045E970')
        for case in REPORT['planner']['cases']:
            self.assertEqual(case['input_identities'], {k: list(v) for k, v in RETAIL.items()})
            self.assertEqual(case['initial'], dict(action_count=0, driver_count=0, marker=-1))
            self.assertEqual(case['shape_kind'], 1)
            self.assertEqual(case['category'], 3)
            self.assertEqual(case['body_width'], 1.7545678615570068)
            self.assertFalse(case['physical_integration'])
            self.assertIsNone(case['original_runtime'])
            for at in required:
                self.assertIn(at, case['visited'])
            self.assertEqual({row['hook'] for row in case['hooks']},
                             {'external_heap_allocate', 'external_heap_release',
                              'external_QueryPerformanceCounter'})

    def test_generated_paths_and_complete_frame_pins(self):
        hashes = ('f6b53ef5df0cd8cf8d5a66d707da4675ca7a23459345d8988cf2186ad188cc26',
                  '9af0f5303bac43732ad6418ef2f1e86bb0a6c3377249772c6f4d85adb1059563',
                  '48ceca873c166b6edf06c75f07dcfa90d36655b378f0c15818f1e618655be3e1')
        for case, expected in zip(REPORT['planner']['cases'], hashes):
            raw = json.dumps(case['frames'], sort_keys=True, separators=(',', ':'),
                             allow_nan=False).encode('ascii')
            self.assertEqual(hashlib.sha256(raw).hexdigest(), expected)
            for i, frame in enumerate(case['frames']):
                self.assertEqual(frame['selected_self'], 0)
                self.assertGreaterEqual(frame['context_count'], 1)
                self.assertGreaterEqual(len(frame['path']), 2)
                self.assertGreater(frame['driver_count'], 0)
                self.assertEqual(frame['inputs']['world_tick'], 100000 + (i + 1) * 80)
                self.assertEqual(frame['inputs']['dt'], 80 / 4000)
                self.assertGreater(frame['desired_speed'], 0)
                self.assertEqual(frame['catchup_scale'], 1)
                for name in ('throttle', 'brake', 'handbrake'):
                    self.assertGreaterEqual(frame['controls'][name], 0)
                    self.assertLessEqual(frame['controls'][name], 1)

    def test_retained_driver_braking_and_action_refresh(self):
        normal, fast, lifecycle = REPORT['planner']['cases']
        self.assertEqual([len(c['frames']) for c in (normal, fast, lifecycle)], [24, 24, 160])
        rows = normal['frames']
        self.assertEqual(rows[0]['path'], rows[1]['path'])
        self.assertLess(rows[0]['controls']['throttle'], rows[1]['controls']['throttle'])
        self.assertNotEqual(rows[0]['path'], rows[-1]['path'])
        self.assertGreater(rows[-1]['controls']['steering_word'], 0)
        self.assertEqual([f['controls']['brake'] for f in fast['frames'][:5]],
                         [f32(v) for v in (0.2, 0.4, 0.6, 0.8, 1.0)])
        self.assertGreater(fast['frames'][-1]['controls']['throttle'], 0)
        self.assertEqual({f['action_tick'] for f in lifecycle['frames']}, {100080, 112080})
        self.assertEqual(lifecycle['frames'][149]['action_tick'], 100080)
        self.assertEqual(lifecycle['frames'][150]['action_tick'], 112080)

    def test_clock_matches_dt_in_supplied_path_legacy_fixture(self):
        for case in REPORT['strategy']['evolving']:
            rows = case['frames']
            for a, b in zip(rows, rows[1:]):
                self.assertEqual(b['inputs']['world_tick'] - a['inputs']['world_tick'],
                                 b['inputs']['dt'] * 4000)


class SceneTests(unittest.TestCase):
    def test_original_event_metadata_and_grid_construction(self):
        event = REPORT['scene']['initial']['event']
        self.assertEqual([event[k] for k in ('track', 'mode', 'difficulty', 'laps', 'humans', 'rivals')],
                         [1003, 1, 1, 2, 1, 3])
        fields = ('id', 'role', 'grid', 'context', 'mode', 'controller_selector')
        self.assertEqual([[c[k] for k in fields] for c in event['configs']],
                         [[0, 1, 2, -2, 0, 1], [100, 2, 1, 0, 1, 3],
                          [101, 2, 4, 0, 1, 3], [102, 2, 3, 1, 1, 3]])
        self.assertEqual(REPORT['scene']['initial']['physical_selectors'], [1, 3])
        self.assertEqual(event['input_seed'], event['settings_seed'])
        self.assertEqual(event['settings_next_seed'], event['world_seed'])
        self.assertEqual([event[k] for k in ('input_seed', 'world_seed', 'next_seed', 'initial_tick')],
                         [12345, 418134235, 808147100, 4000])

    def test_original_paired_producers_are_executed(self):
        scene = REPORT['scene']
        for at in ('005A00A0', '005A0CE0', '00431DF0', '004B5E36', '005809C0',
                   '004B6300', '00425150', '0041F889', '00434270', '00434A80',
                   '00435D30', '00441ED0', '0041FE50', '004338C0', '00434ED0',
                   '0059BFC0', '0059C210', '00430758', '00584270', '004257F7',
                   '0057A300', '0057A910', '0044BD10', '0044C010'):
            self.assertIn(at, scene['visited'])
        self.assertEqual({h['hook'] for h in scene['hooks']},
                         {'external_heap_allocate', 'external_heap_release', 'external_QueryPerformanceCounter'})
        self.assertFalse(scene['physical_integration'])
        self.assertFalse(scene['actual_event0_roster'])
        self.assertIsNone(scene['original_runtime'])

    def test_paired_context_history_catchup_and_refresh(self):
        frames = REPORT['scene']['frames']
        self.assertEqual(len(frames), 160)
        self.assertEqual([frames[i]['result']['context_count'] for i in (0, 39, 40, 159)], [1, 1, 2, 2])
        self.assertEqual([frames[i]['history_count'] for i in (0, 50, 51, 101, 102, 159)], [1, 1, 2, 2, 3, 4])
        self.assertEqual([frames[i]['history_timer'] for i in (0, 50, 51)], [4000, 0, 4000])
        self.assertLess(frames[0]['progress'][0], frames[0]['progress'][1])
        self.assertEqual(frames[40]['progress'][0], frames[40]['progress'][1])
        self.assertGreater(frames[80]['progress'][0], frames[80]['progress'][1])
        self.assertEqual({f['result']['action_tick'] for f in frames}, {500080, 512080})
        self.assertEqual(len({f['result']['catchup_scale'] for f in frames}), 3)
        for i, frame in enumerate(frames):
            result = frame['result']
            self.assertEqual(result['inputs']['world_tick'], 500000+(i+1)*80)
            self.assertEqual(frame['elapsed_ticks'], 240000+(i+1)*80)
            self.assertEqual(frame['reference'], 0x11A0000)
            self.assertEqual(frame['action_catchup'], 1)
            self.assertGreater(len(result['path']), 1)
            self.assertGreater(result['desired_speed'], 0)
            self.assertGreater(result['catchup_scale'], 0)
            self.assertLess(result['catchup_scale'], 1)
        raw = json.dumps(frames, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('ascii')
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         'f734eb93e4753fb251ca728db31f80eae0596fd9cb7ed440dc7f55df93a27c31')


class EventRideTests(unittest.TestCase):
    def test_native_event_presets_and_adaptive_tuning(self):
        rides = REPORT['event_scene']['initial']['event']['rides']
        self.assertEqual([r['car_type'] for r in rides], [14, 7, 9, 6])
        self.assertEqual([r['levels'] for r in rides], [
            [1]*11, [3, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1],
            [2, 1, 2, 1, 1, 0, 1, 1, 1, 1, 1], [0, 1, 0, 0, 1, 3, 1, 1, 1, 0, 0]])
        self.assertEqual([r['items'] for r in rides], [[0]*11]*4)
        self.assertEqual([r['category'] for r in rides], [2]*4)
        self.assertEqual([r['bound_slots'] for r in rides], [48, 46, 39, 37])
        self.assertEqual([r['physical_sha256'] for r in rides], [
            '98eb651cfdb1ab25d69022ba74554b84ce7875767ebf36ad91489c369f13cd9c',
            '32fdc0de42865696be29d8c49440e0b01bef55993c10361d5fc7fc49af3d63c1',
            '14abcaf92a808ca3ed0928afaffd93e9c4658e291f260d36542579443f078fc4',
            'fa3a2126dfa59a8e1e6cfc337b45060e8cae3f18f9c8cd3a01627bcd31ffdc14'])
        self.assertEqual([r['slot_binding_sha256'] for r in rides], [
            'cbbc5a52e6404a8f971ab8f346e3c4a06b59d2569761a94bc6519940145c41da',
            '9da5f8a07571c2288c38279340b7861b2f216ee279958f5ffaa4ec08bbdc4f5e',
            '474cfc1ce83cc9b2baf209c7df1dbe33039045b50e5e6c2463eb90968f53f445',
            '1583e9454276290df63bf83f28f950ac8e49949d34bb5907ba2aa04f7ac9301e'])
        for i, ride in enumerate(rides):
            self.assertEqual(ride['visual_seeds'], [0x530B82B0+i, 0xBD97DD90+i])
            self.assertLess(abs(ride['rating']-rides[0]['rating']), 5.0)

    def test_original_actor_ride_binding_and_consumption(self):
        scene = REPORT['event_scene']
        self.assertTrue(scene['resolved_event_rides'])
        self.assertFalse(scene['actual_event0_roster'])
        self.assertFalse(scene['physical_integration'])
        for binding, car, identity in zip(scene['initial']['actor_bindings'], (14, 7), (0, 100)):
            self.assertEqual(binding['id'], identity)
            self.assertEqual(binding['ride'], binding['config']+0x10)
            self.assertEqual(binding['physical'], binding['ride']+0x60)
            self.assertEqual(binding['resolved']['car_type'], car)
            self.assertEqual(binding['resolved']['category'], 2)
            self.assertGreater(binding['bounds'][1], binding['bounds'][0])
        self.assertEqual([b['resolved']['levels'] for b in scene['initial']['actor_bindings']],
                         [r['levels'] for r in scene['initial']['event']['rides'][:2]])
        for at in ('004B25B0', '0057C8B0', '004B2640', '004B26F0', '004B6100',
                   '004B40A0', '0057F700', '0057F768', '005809C0', '004323F0',
                   '00425598', '004255AB', '004255C8', '00472B00', '00469B00',
                   '0045B050', '0045E970'):
            self.assertIn(at, scene['visited'])
        frames = scene['frames']
        raw = json.dumps(frames, sort_keys=True, separators=(',', ':'), allow_nan=False).encode('ascii')
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         '12a1d98c6dd6372b1d28434f88d6966ba3aada211d052272b6a8af7f1063eabe')
        self.assertNotEqual(frames[0]['result']['desired_speed'],
                            REPORT['scene']['frames'][0]['result']['desired_speed'])

    def test_external_pointer_services_are_disclosed_and_bounded(self):
        hooks = REPORT['event_scene']['hooks']
        self.assertEqual({h['hook'] for h in hooks},
            {'external_heap_allocate', 'external_heap_release', 'external_QueryPerformanceCounter',
             'external_IsBadReadPtr'})
        probes = [h for h in hooks if h['hook'] == 'external_IsBadReadPtr']
        self.assertTrue(probes)
        for row in probes:
            self.assertEqual(row['bytes'], 1)
            self.assertFalse(row['bad_pointer'])
            self.assertGreaterEqual(row['pointer'], 4096)

    def test_pointer_service_rejects_null_unmapped_and_readonly_writes(self):
        from controller293_event_rides import pointer_probe
        m = HarnessTests.machine()
        m.uc.mem_map(0x2000000, 4096, 1)
        for pointer, write, expected in ((0, False, 1), (0xDEADBEEF, False, 1),
                                         (0x2000000, True, 1), (0x2000000, False, 0),
                                         (STOP+0x100, True, 0)):
            m.reg('esp', 0x11F0000)
            m.put(0x11F0000, STOP)
            m.put(0x11F0004, pointer)
            m.put(0x11F0008, 1)
            m.events.append({})
            pointer_probe(m, write)
            self.assertEqual(m.reg('eax'), expected)
            self.assertEqual(m.events[-1]['bad_pointer'], bool(expected))


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

    def test_null_page_only_permits_explicit_fs_chain(self):
        m = self.machine()
        m.uc.mem_map(0, 4096)
        m.uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, low_memory, begin=0, end=4095)
        m.uc.mem_write(STOP + 0x400, bytes.fromhex('64a100000000c3'))
        m.allowed.append((STOP + 0x400, STOP + 0x407))
        m.run(STOP + 0x400)
        m.uc.mem_write(STOP + 0x400, bytes.fromhex('a100000000c3'))
        with self.assertRaisesRegex(RuntimeError, 'NULL memory access'):
            m.run(STOP + 0x400)

    def test_planner_exact_boundary_rejects_unknown_and_wrong_size(self):
        class FakeUC:
            def emu_stop(self):
                raise AssertionError('not a stop address')
        machine = object.__new__(PlannerMachine)
        machine.visited, machine.hooks = set(), {}
        machine.end, machine.instructions = STOP, {0x401000: 5}
        machine.instruction(FakeUC(), 0x401000, 5, None)
        for address, size in ((0x401001, 4), (0x401000, 4)):
            with self.assertRaisesRegex(RuntimeError, 'undeclared planner execution'):
                machine.instruction(FakeUC(), address, size, None)

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

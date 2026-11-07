"""Parser/output guards plus independent assertions on a fresh research report.

Run with --report after actors258_trace.py to validate the original-byte
experiments. Without --report, only synthetic guard tests run.
"""

import argparse
import json
from pathlib import Path
import tempfile
import unittest

from actors258_trace import Image, output_path
from event0_trace import INPUTS


class Guards(unittest.TestCase):
    def test_reject_non_pe(self):
        for blob in (b'', b'MZ', b'MZ' + b'\0' * 100):
            with self.assertRaises(ValueError):
                Image(blob)

    def test_output_scope(self):
        with tempfile.TemporaryDirectory(prefix='actors258-tests-') as name:
            base = Path(name)
            work, retail = base / 'work', base / 'retail'
            work.mkdir()
            retail.mkdir()
            for target in (retail, retail / 'output', base, base / 'elsewhere', work):
                with self.assertRaises(ValueError):
                    output_path(retail, target, work)
            good = work / 'build' / 'fresh'
            self.assertEqual(output_path(retail, good, work), good.resolve())
            good.mkdir(parents=True)
            with self.assertRaises(ValueError):
                output_path(retail, good, work)
            # Never create this non-ASCII name: refusal must precede mutation.
            bad = work / ('bad-' + chr(0xE9)) / 'out'
            with self.assertRaises(ValueError):
                output_path(retail, bad, work)
            self.assertFalse(bad.parent.exists())


class Report(unittest.TestCase):
    report_path = None

    @classmethod
    def setUpClass(cls):
        if cls.report_path is None:
            raise unittest.SkipTest('no report supplied; original-byte experiments not checked')
        cls.data = json.loads(cls.report_path.read_text(encoding='ascii'))
        cls.native = cls.data['native']

    def test_provenance_and_manifests(self):
        self.assertEqual({k: v['sha256'] for k, v in self.data['provenance'].items()}, INPUTS)
        self.assertEqual(self.data['versions'], dict(capstone='5.0.7', unicorn='2.1.4'))
        before = (self.report_path.parent / 'before.json').read_bytes()
        after = (self.report_path.parent / 'after.json').read_bytes()
        self.assertEqual(before, after)
        self.assertGreater(len(json.loads(before)), 1)
        self.assertGreater(len((self.report_path.parent / 'source.txt').read_bytes()), 1000)
        self.assertTrue(self.data['original_runtime'].startswith('unperformed'))
        self.assertGreaterEqual(self.data['source']['source_assertions'], 30)

    def test_grid_selection(self):
        grid = self.native['grid']
        self.assertEqual(grid['created'], [
            dict(id=100, marker=0, controller=3),
            dict(id=0, marker=1, controller=1),
            dict(id=102, marker=2, controller=3),
            dict(id=101, marker=3, controller=3)])
        self.assertEqual(grid['table'], [0, 100, 102, 101])
        self.assertEqual(grid['indices'], [0, 1, 2, 3])
        self.assertEqual(self.native['grid_skipped']['table'], [0, 102, 101])

    def test_event_input_is_intro(self):
        setup = self.data['event0']
        self.assertEqual(setup['number'], 0)
        self.assertEqual(setup['races'], [dict(track=1003, laps=2, direction=0)])
        self.assertEqual(setup['player_preset_hash'], '0xffaad92a')
        self.assertEqual([r['name'] for r in setup['rivals']], ['BRIAN', 'BRETT', 'LOLA'])
        self.assertEqual([r['tuning_selector'] for r in setup['rivals']], [-1, -1, -1])
        self.assertEqual([g['marker_index'] for g in self.data['grid'][:4]], [0, 1, 2, 3])

    def test_registration_and_reindex(self):
        result = self.native['registration']
        self.assertEqual(result['registered'], [0, 100, 102, 101])
        self.assertEqual(result['ranked'], [0, 100, 102, 101])
        self.assertEqual(result['table'], [0, 100, 102, 101, 50])
        self.assertEqual(result['rank_pairs'], [[1, 1], [2, 2], [3, 3], [4, 4]])
        self.assertEqual(result['backlinks'], [True] * 4)
        self.assertEqual(result['sentinel'], [0x7FFFFFFF] * 4)
        self.assertEqual(result['after_insert']['ids'], [1, 0, 100, 102, 101, 50])
        self.assertEqual(result['after_insert']['indices'], [1, 2, 3, 4, 5])

    def test_status_order_and_ties(self):
        a = self.native['status_reorder']
        self.assertEqual(a['lap_visits'], [0, 100, 102, 101])
        self.assertEqual([x['id'] for x in a['finish_dispatch']], [100, 102, 101, 0])
        self.assertTrue(all(x['args'] == [0, 0, 1] for x in a['finish_dispatch']))
        b = self.native['status_ties']
        self.assertEqual([x['id'] for x in b['finish_dispatch']], [102, 0, 101, 100])
        self.assertEqual(self.native['status_finished']['lap_visits'], [100])
        self.assertEqual(self.native['places'], [1, 2, 1, 5])

    def test_scheduler_phases(self):
        all_actors = [0, 100, 102, 101, 7, 50]
        prefix = [['traffic_manager']] + [['controller', i] for i in [100, 102, 101, 50, 0]]
        prefix += [['driver', i] for i in all_actors]
        prefix += [['pre_contact', i] for i in all_actors] + [['props']]
        suffix = [['post_contact', i] for i in all_actors]
        self.assertEqual(self.native['scheduler_zero'], prefix + [['collision_zero_prepare'], ['collision_zero_solve']] + suffix)
        self.assertEqual(self.native['scheduler_nonzero'], prefix + [['collision_nonzero']] + suffix)
        self.assertEqual(self.native['scheduler_zero_dt'], self.native['scheduler_zero'])

    def test_gate_and_lifetime(self):
        self.assertEqual(self.native['gate_keep'], dict(gate=1, other=0, stamp=0))
        self.assertEqual(self.native['gate_hide'], dict(gate=0, other=1, stamp=12345))
        self.assertEqual(self.native['teardown'], dict(empty=True, events=[
            ['destroy', 100], ['free', 100], ['destroy', 0], ['free', 0],
            ['destroy', 50], ['free', 50]]))
        self.assertEqual(self.native['physics'], [1, 3, 3, 3, 3, 1])

    def test_controller_driver_physics_are_distinct(self):
        tables = self.data['source']['tables']
        self.assertEqual(tables['ai_controller_vtable'][3], '0x462430')
        self.assertEqual(tables['ai_driver_vtable'][1], '0x45e970')
        self.assertEqual(tables['real_driver_vtable'][1], '0x4615e0')
        self.assertEqual(tables['physics_vtable'][1], '0x474a90')
        self.assertNotEqual(tables['ai_driver_vtable'], tables['physics_vtable'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    Report.report_path = args.report
    unittest.main(argv=['actors258_test.py'], verbosity=2)

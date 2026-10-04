"""Dependency-free numeric rejection regressions for the minimap oracle."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import minimap266_diff as diff


def quad():
    return dict(screen=[[22, 176], [150, 176], [150, 304], [22, 304]],
                uvq=[[0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]],
                colors=[[128, 128, 128, 128] for _ in range(4)])


def driver_case():
    q = quad()
    case = dict(center=[86, 240], boundary=copy.deepcopy(q['screen']),
                route_clipped=[q], markers=[])
    values = [1, 86, 240] + [v for xy in case['boundary'] for v in xy]
    vertex_values = [value for i in range(4)
                     for value in q['screen'][i]+q['uvq'][i]+q['colors'][i]]
    output = ['P 0.5 0.5', 'G '+' '.join(map(str, values)),
              'Q 1 0 14 '+' '.join(map(str, vertex_values)), 'PASS: minimap checks']
    return output, [dict(name='point', uv=[0.5, 0.5])], [case]


class DifferentialChecks(unittest.TestCase):
    def test_genuine_finite_comparison(self):
        maxima = dict(screen=0, uvq=0, color=0)
        self.assertGreater(diff.compare([quad()], [quad()], 'finite', maxima), 0)
        self.assertEqual(maxima, dict(screen=0, uvq=0, color=0))

    def test_nonfinite_native_and_port_vertices_rejected(self):
        for side in (0, 1):
            for field in ('screen', 'uvq', 'colors'):
                for value in (float('nan'), float('inf'), -float('inf')):
                    with self.subTest(side=side, field=field, value=value):
                        pair = [[quad()], [quad()]]
                        pair[side][0][field][0][0] = value
                        with self.assertRaises(AssertionError):
                            diff.compare(*pair, 'nonfinite', dict(screen=0, uvq=0, color=0))

    def test_invalid_degenerate_and_uncovered_vertices_rejected(self):
        actual = quad()
        actual['screen'] = [[0, 0] for _ in range(4)]
        actual['uvq'][3][2] = float('nan')
        with self.assertRaises(AssertionError):
            diff.compare([], [actual], 'uncovered', dict(screen=0, uvq=0, color=0))

    def test_finite_driver_protocol(self):
        lines, rows, cases = driver_case()
        maxima, samples = diff.check_driver_output('\n'.join(lines)+'\n', rows, cases)
        self.assertGreater(samples, 0)
        self.assertTrue(all(value == 0 for value in maxima.values()))

    def test_corrupted_driver_numeric_rows_rejected(self):
        # Exercise the parser used by the full native differential, including
        # attributes that a coverage comparison might never sample.
        lines, rows, cases = driver_case()
        for line, first in ((0, 1), (1, 2), (2, 4)):
            for column in range(first, len(lines[line].split())):
                for value in ('nan', 'inf', '-inf'):
                    with self.subTest(line=line, column=column, value=value):
                        corrupt = lines.copy()
                        tokens = corrupt[line].split()
                        tokens[column] = value
                        corrupt[line] = ' '.join(tokens)
                        with self.assertRaises(AssertionError):
                            diff.check_driver_output('\n'.join(corrupt)+'\n', rows, cases)

    def test_nonfinite_unmatched_driver_piece_rejected(self):
        lines, rows, cases = driver_case()
        tokens = lines[2].split()
        tokens[1:3] = ['0', '999']
        tokens[6] = 'nan'
        lines[2] = ' '.join(tokens)
        with self.assertRaises(AssertionError):
            diff.check_driver_output('\n'.join(lines), rows, cases)

    def test_malformed_driver_protocol_rejected(self):
        lines, rows, cases = driver_case()
        for line in range(3):
            with self.subTest(line=line):
                corrupt = lines.copy()
                corrupt[line] = ' '.join(corrupt[line].split()[:-1])
                with self.assertRaises(AssertionError):
                    diff.check_driver_output('\n'.join(corrupt), rows, cases)
        for corrupt in (lines[:-1], lines+['PASS: minimap checks']):
            with self.assertRaises(AssertionError):
                diff.check_driver_output('\n'.join(corrupt), rows, cases)

    def test_finite_attribute_mismatch_still_rejected(self):
        lines, rows, cases = driver_case()
        tokens = lines[2].split()
        tokens[6] = '0.01'
        lines[2] = ' '.join(tokens)
        with self.assertRaises(AssertionError):
            diff.check_driver_output('\n'.join(lines), rows, cases)


if __name__ == '__main__':
    unittest.main()

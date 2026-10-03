"""Synthetic guard tests only. These do not execute original game code."""

import math
from pathlib import Path
import struct
import tempfile
import unittest

from collision256_trace import aligned, append_replay, output_path, queue_replay, shape_rows


def chunk(tag, data):
    return struct.pack('<II', tag, len(data)) + data


def synthetic_shapes():
    # Two distinct headers with the same lookup hash and different point data.
    payload = b''
    for value in (1.0, 2.0):
        at = 8 + len(payload)
        padding = (-(at + 8)) % 16
        head = bytearray(88)
        struct.pack_into('<I', head, 0x18, 0x5087DB01)
        payload += chunk(0x39200, b'\x11' * padding + head)
        at = 8 + len(payload)
        padding = (-(at + 8)) % 16
        point = bytearray(48)
        struct.pack_into('<f', point, 16, value)
        payload += chunk(0x39201, b'\x11' * padding + point)
        at = 8 + len(payload)
        padding = (-(at + 8)) % 16
        payload += chunk(0x39202, b'\x11' * padding + b'\0\0')
    return chunk(0x80034020, payload)


class CollisionEvidenceTests(unittest.TestCase):
    def test_duplicate_identity_survives_both_loaders(self):
        rows = shape_rows(synthetic_shapes(), 0x5087DB01)
        self.assertEqual(len(rows), 2)
        self.assertNotEqual(rows[0]['header_record'], rows[1]['header_record'])
        self.assertNotEqual(rows[0]['sha256'], rows[1]['sha256'])
        identities = [r['header_record'] for r in rows]
        self.assertEqual(append_replay(identities), identities)
        self.assertEqual([r['point_records'] for r in rows], [1, 1])

    def test_truncated_shape_is_failure_not_empty_evidence(self):
        with self.assertRaises(ValueError):
            shape_rows(synthetic_shapes()[:-1], 0x5087DB01)
        with self.assertRaises(ValueError):
            aligned(0, 49, 48)

    def test_bad_array_tag_is_rejected(self):
        data = synthetic_shapes().replace(struct.pack('<I', 0x39201), struct.pack('<I', 0x39203), 1)
        with self.assertRaisesRegex(ValueError, 'array tags'):
            shape_rows(data, 0x5087DB01)

    def test_first_match_is_not_last_duplicate(self):
        # Explicitly synthetic identities, not selected original runtime pointers.
        records = [257504, 257760]
        self.assertEqual(append_replay(records)[0], records[0])
        with self.assertRaises(ValueError):
            append_replay([records[0], records[0]])

    def test_queue_is_descending_and_equal_keys_keep_insertion_order(self):
        jobs = [('a', 0.002), ('b', 0.007), ('c', 0.002), ('d', 0.004)]
        self.assertEqual([j[0] for j in queue_replay(jobs, -5.0001)], ['b', 'd', 'a', 'c'])
        for threshold in (0, 1, math.nan):
            with self.assertRaises(ValueError):
                queue_replay(jobs, threshold)
        with self.assertRaises(ValueError):
            queue_replay([('bad', math.inf)], -5)

    def test_output_guards_from_actual_workspace(self):
        workspace = Path.cwd().resolve()
        retail = Path('E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground').resolve()
        self.assertEqual(output_path(workspace, retail, Path('build/256-test-unused-output')),
                         (workspace/'build/256-test-unused-output').resolve())
        for path in (retail, retail/'256-denied', retail.parent, workspace.parent, workspace):
            with self.assertRaises(ValueError):
                output_path(workspace, retail, path)
        with self.assertRaises(ValueError):
            output_path(workspace, retail, Path('build') / chr(0xE9))
        # TemporaryDirectory owns exactly this newly created subtree.
        with tempfile.TemporaryDirectory(prefix='256-test-', dir=str(workspace/'build')) as directory:
            with self.assertRaisesRegex(ValueError, 'fresh'):
                output_path(workspace, retail, Path(directory))


if __name__ == '__main__':
    unittest.main()

"""Synthetic malformed-input and read-only-output guards for event0_trace."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

import event0_trace as trace


def chunk(tag, data):
    return struct.pack('<II', tag, len(data)) + data


def preset(name='CAR_PRESET'):
    row = bytearray(0x408)
    row[8:12] = b'CAR\0'
    text = name.encode('ascii') + b'\0'
    row[0x28:0x28 + len(text)] = text
    for index in range(120):
        struct.pack_into('<II', row, 0x48 + index * 8, 0xFFFFFFFF, 0)
    return row


def event(number, key=0):
    row = bytearray(332)
    struct.pack_into('<I', row, 0, number)
    struct.pack_into('<hbb', row, 0x78, 1003, 2, 0)
    struct.pack_into('<I', row, 0x98, 1)
    struct.pack_into('<I', row, 0xD0, key)
    return row


def grids():
    payload = bytearray(8 + 48 * 4)  # payload VA/file offset 8 aligns to 16
    for index in range(4):
        pos = 8 + 48 * index
        struct.pack_into('<II3f', payload, pos + 8,
                         trace.name_hash('ForwardStartingPosition', fold=False), index,
                         float(index), 2.0, 3.0)
        struct.pack_into('<H', payload, pos + 0x20, 16384)
    return chunk(0x34146, payload)


def pe_image():
    data = bytearray(512)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3C, 64)
    data[64:68] = b'PE\0\0'
    struct.pack_into('<H', data, 70, 1)
    struct.pack_into('<H', data, 84, 32)
    struct.pack_into('<H', data, 88, 0x10B)
    struct.pack_into('<I', data, 116, 0x400000)
    data[120:128] = b'.text\0\0\0'
    struct.pack_into('<IIII', data, 128, 16, 0x2000, 16, 256)
    data[256:260] = b'ABC\0'
    return data


class ParserGuards(unittest.TestCase):
    def test_pe_nonidentity_mapping(self):
        image = trace.Image(pe_image())
        self.assertEqual(image.read(0x402000, 4), b'ABC\0')
        self.assertEqual(image.string(0x402000), 'ABC')
        for address, size in ((0x40200F, 2), (0x401000, 1), (0x402000, -1)):
            with self.assertRaises(ValueError):
                image.read(address, size)

    def test_pe_truncation(self):
        for size in (0, 63, 87, 159, 271):
            with self.assertRaises(ValueError):
                trace.Image(pe_image()[:size])
        data = pe_image()
        struct.pack_into('<H', data, 88, 0x20B)
        with self.assertRaises(ValueError):
            trace.Image(data)

    def test_pe_unterminated_string(self):
        data = pe_image()
        data[256:272] = b'A' * 16
        with self.assertRaises(ValueError):
            trace.Image(data).string(0x402000)

    def test_jdlz_literal_and_overlapping_copy(self):
        tokens = b'\x02\x01A\x00\x00'  # A then three copies at distance one
        packed = b'JDLZ\x02\x10\0\0' + struct.pack('<II', 4, 16 + len(tokens)) + tokens
        self.assertEqual(trace.jdlz(packed), b'AAAA')

    def test_jdlz_bad_tokens_and_size(self):
        for target, tokens in ((1, b''), (3, b'\x01\x01\x00\x00'),
                               (2, b'\x02\x01A\x00\x00'), (33 * 1024 * 1024, b'')):
            packed = b'JDLZ\x02\x10\0\0' + struct.pack('<II', target, 16 + len(tokens)) + tokens
            with self.assertRaises(ValueError):
                trace.jdlz(packed)
        with self.assertRaises(ValueError):
            trace.jdlz(b'JDLZ\x02\x10\0\0' + struct.pack('<II', 0, 17))

    def test_chunks_parent_and_depth(self):
        good = chunk(0x80000001, chunk(2, b'abc'))
        self.assertEqual(len(list(trace.chunks(good))), 2)
        for data in (b'abc', struct.pack('<II', 1, 5),
                     chunk(0x80000001, struct.pack('<II', 2, 1))):
            with self.assertRaises(ValueError):
                list(trace.chunks(data))
        for _ in range(34):
            good = chunk(0x80000001, good)
        with self.assertRaises(ValueError):
            list(trace.chunks(good))

    def test_preset_exact_pairs_and_case_hash(self):
        row = preset()
        struct.pack_into('<II', row, 0x48, 17, 0x12345678)
        parsed = trace.preset_records(chunk(0x30220, row))
        self.assertEqual(parsed[trace.name_hash('car_preset')]['pairs'][0], (17, 0x12345678))

    def test_preset_partial_duplicate_slot_and_string(self):
        badslot, badstring = preset(), preset()
        struct.pack_into('<I', badslot, 0x48, 121)
        badstring[8:40] = b'A' * 32
        for payload in (preset()[:-1], preset() + preset(), badslot, badstring):
            with self.assertRaises(ValueError):
                trace.preset_records(chunk(0x30220, payload))

    def test_event_nonzero_preset_resolves_or_fails(self):
        key = trace.name_hash('CAR_PRESET')
        presets = trace.preset_records(chunk(0x30220, preset()))
        data = chunk(0x34A02, event(0, key) + event(1))
        rows = trace.event_records(data, presets)
        self.assertEqual(rows[0]['presets'][0]['name'], 'CAR_PRESET')
        self.assertEqual(rows[1]['presets'], [None])
        with self.assertRaises(ValueError):
            trace.event_records(data, {})

    def test_events_reject_duplicate_missing_count_and_direction(self):
        count, direction, rivals = event(0), event(0), event(0)
        struct.pack_into('<I', count, 0x98, 9)
        direction[0x7B] = 2
        struct.pack_into('<I', rivals, 0x128, 4)
        for payload in (event(0) + event(0), event(0), event(0)[:-1],
                        event(256) + event(1), count + event(1), direction + event(1), rivals + event(1)):
            with self.assertRaises(ValueError):
                trace.event_records(chunk(0x34A02, payload), {})

    def test_grid_alignment_and_indices(self):
        rows = trace.grid_records(grids())
        self.assertEqual(rows[0]['offset'], 16)
        self.assertEqual(rows[3]['raw_position'], (3.0, 2.0, 3.0))
        for mode in ('duplicate', 'nan', 'missing', 'partial'):
            data = bytearray(grids())
            if mode == 'duplicate':
                struct.pack_into('<I', data, 16 + 48 + 12, 0)
            elif mode == 'nan':
                struct.pack_into('<f', data, 16 + 16, float('nan'))
            elif mode == 'missing':
                struct.pack_into('<I', data, 16 + 8, 0)
            else:
                data = data[:-1]
                struct.pack_into('<I', data, 4, len(data) - 8)
            with self.assertRaises(ValueError):
                trace.grid_records(data)

    def test_track_traffic_index_and_duplicate(self):
        row = bytearray(252)
        struct.pack_into('<h', row, 0x6C, 1003)
        row[0xC2] = 9
        self.assertEqual(trace.track_record(chunk(0x34201, row))['event0_traffic_configs'], 9)
        for payload in (row + row, row[:-1]):
            with self.assertRaises(ValueError):
                trace.track_record(chunk(0x34201, payload))

    def test_catalog_retains_all_candidates_and_bounds(self):
        row = bytearray(48)
        struct.pack_into('<3I', row, 0, 0, 100, 200)
        row[0x10] = 17
        data = chunk(0x34606, b'WHEEL\0') + chunk(0x34604, row + row)
        presets = [dict(pairs=[(17, 200)])]
        self.assertEqual(len(trace.part_catalog(data, presets)['0xc8']), 2)
        struct.pack_into('<I', row, 0, 99)
        with self.assertRaises(ValueError):
            trace.part_catalog(chunk(0x34606, b'WHEEL\0') + chunk(0x34604, row), presets)


class OutputGuards(unittest.TestCase):
    def test_input_overlap_existing_and_ascii_new_names(self):
        with tempfile.TemporaryDirectory(prefix='268-guards-', dir=Path.cwd() / 'build') as directory:
            base = Path(directory)
            root = base / 'retail'
            root.mkdir()
            (root / 'sample').write_bytes(b'abc')
            before = trace.manifest(root)
            for out in (root, root / 'evidence', base):
                with self.assertRaises(ValueError):
                    trace.prepare_output([root], out)
            out = base / 'fresh'
            trace.prepare_output([root], out)
            (out / 'sentinel').write_bytes(b'keep')
            with self.assertRaises(FileExistsError):
                trace.prepare_output([root], out)
            self.assertEqual((out / 'sentinel').read_bytes(), b'keep')
            with self.assertRaises(ValueError):
                trace.prepare_output([root], base / ('bad-' + chr(233)))
            self.assertEqual(trace.manifest(root), before)
            self.assertEqual(json.loads(before)[1][0], 'sample')

    def test_reference_overlap_and_missing_root(self):
        with tempfile.TemporaryDirectory(prefix='268-reference-', dir=Path.cwd() / 'build') as directory:
            base = Path(directory)
            retail, reference = base / 'retail', base / 'reference'
            retail.mkdir()
            reference.mkdir()
            for out in (reference, reference / 'new-output', base):
                with self.assertRaises(ValueError):
                    trace.prepare_output([retail, reference], out)
            with self.assertRaises(ValueError):
                trace.prepare_output([base / 'missing'], base / 'new-output')
            self.assertFalse((base / 'new-output').exists())

    def test_cli_hash_failure_is_not_a_probe_pass(self):
        with tempfile.TemporaryDirectory(prefix='268-cli-', dir=Path.cwd() / 'build') as directory:
            base = Path(directory)
            retail = base / 'retail'
            retail.mkdir()
            (retail / 'speed.exe').write_bytes(b'wrong executable')
            before = trace.manifest(retail)
            out = base / 'evidence'
            result = subprocess.run([sys.executable, '-B', 'tools/event0_trace.py',
                                     '--root', str(retail), '--out', str(out)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('unsupported input SHA-256: speed.exe', result.stderr)
            self.assertNotIn('PASS: fresh evidence', result.stdout)
            self.assertFalse((out / 'data.json').exists())
            self.assertEqual((out / 'before-0.json').read_bytes(), before)
            self.assertEqual((out / 'after-0.json').read_bytes(), before)
            self.assertEqual(trace.manifest(retail), before)


if __name__ == '__main__':
    unittest.main()

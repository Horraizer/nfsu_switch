"""Synthetic malformed-input guards and independent native report expectations.

Run with --report <fresh trace directory> to check actual retail measurements.
No test launches the game or changes retail/reference input trees.
"""
import argparse
import json
from pathlib import Path
import struct
import tempfile
import unittest

from hud254_trace import (BUNDLE_SHA, EXE_SHA, LANGUAGE_SHA, PE, chunks,
                          feng_objects, fonts, jdlz, output_path, resources)

REPORT = None


def pe_fixture():
    data = bytearray(512)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 64)
    data[64:68] = b'PE\0\0'
    struct.pack_into('<H', data, 70, 1)
    struct.pack_into('<H', data, 84, 32)
    struct.pack_into('<H', data, 88, 0x10B)
    struct.pack_into('<I', data, 116, 0x400000)
    struct.pack_into('<IIII', data, 128, 4, 0x1000, 4, 256)
    data[256:260] = b'ABCD'
    return data


def compressed(body=b'\0\0ABC', target=3):
    return b'JDLZ\x02\x10\0\0' + struct.pack('<II', target, 16+len(body)) + body


def field(tag, payload):
    return tag + struct.pack('<H', len(payload)) + payload


def object_fixture():
    fields = field(b'On', b'LapNumber\0') + field(b'SH', struct.pack('<I', 23))
    obj = b'ObjD' + struct.pack('<I', len(fields)) + fields
    return b'FOb\xea' + struct.pack('<I', len(obj)) + obj


def resource_fixture(name_offset=0, count=1):
    names = b'LCD_LET48.ffn\0'
    request = struct.pack('<I6I', count, 7, name_offset, 2, 0, 0, 0)
    body = b'RsNm' + struct.pack('<I', len(names)) + names
    body += b'RsRq' + struct.pack('<I', len(request)) + request
    return b'Res\xcc' + struct.pack('<I', len(body)) + body


class ParserGuards(unittest.TestCase):
    def test_pe_maps_raw_section_and_edges(self):
        image = PE(pe_fixture())
        self.assertEqual(image.read(0x401000, 4), b'ABCD')
        for address, size in ((0x400FFF, 1), (0x401003, 2), (0x401000, -1)):
            with self.subTest(address=address, size=size), self.assertRaises(ValueError):
                image.read(address, size)

    def test_pe_rejects_invalid_and_truncated_headers(self):
        for cut in (0, 63, 90, 159, 259):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                PE(pe_fixture()[:cut])
        value = pe_fixture()
        struct.pack_into('<I', value, 60, 0xFFFFFFF0)
        with self.assertRaises(ValueError):
            PE(value)
        value = pe_fixture()
        struct.pack_into('<H', value, 88, 0x20B)
        with self.assertRaises(ValueError):
            PE(value)

    def test_jdlz_literals_and_overlapping_copy(self):
        self.assertEqual(jdlz(compressed()), b'ABC')
        self.assertEqual(jdlz(compressed(b'\x02\x01A\0\0', 4)), b'AAAA')

    def test_jdlz_refuses_truncation_overflow_and_bad_distance(self):
        for cut in range(len(compressed())):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                jdlz(compressed()[:cut])
        for value in (compressed(b'\x01\x01\0\0', 3),
                      compressed(b'\x02\x01A\0\0', 2), compressed(target=20)):
            with self.subTest(value=value.hex()), self.assertRaises(ValueError):
                jdlz(value)
        with self.assertRaises(ValueError):
            jdlz(compressed(), limit=2)

    def test_chunk_payload_boundaries(self):
        self.assertEqual(list(chunks(struct.pack('<II', 42, 1)+b'X')), [(42, 8, 9)])
        for value in (b'X', struct.pack('<II', 42, 5)+b'X'):
            with self.assertRaises(ValueError):
                list(chunks(value))

    def test_feng_string_hash_is_not_font_hash(self):
        obj = feng_objects(object_fixture())[0]
        self.assertEqual(obj['name'], 'LapNumber')
        self.assertEqual(obj['fields'][1]['string_hash'], '00000017')
        self.assertNotIn('font_hash', obj['fields'][1])

    def test_feng_rejects_bad_enclosures_and_field_sizes(self):
        for cut in (4, 12, len(object_fixture())-1):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                feng_objects(object_fixture()[:cut])
        value = bytearray(object_fixture())
        struct.pack_into('<H', value, 18, 0xFFFF)
        with self.assertRaises(ValueError):
            feng_objects(value)

    def test_feng_requires_one_name(self):
        fields = field(b'On', b'Lap\0') * 2
        obj = b'ObjD'+struct.pack('<I', len(fields))+fields
        with self.assertRaises(ValueError):
            feng_objects(b'FOb\xea'+struct.pack('<I', len(obj))+obj)

    def test_resources_map_count_stride_and_name_offset(self):
        entries = resources(resource_fixture())[0]['chunks'][1]['records']
        self.assertEqual(entries[0]['name'], 'LCD_LET48.ffn')
        self.assertEqual(entries[0]['type'], 2)
        for value in (resource_fixture(name_offset=1), resource_fixture(count=2),
                      resource_fixture()[:-1]):
            with self.assertRaises(ValueError):
                resources(value)

    def test_font_header_and_glyph_bounds(self):
        data = bytearray(656)
        data[:5] = b'test\0'
        data[256:261] = b'test\0'
        data[512:516] = b'FNTF'
        struct.pack_into('<H', data, 522, 1)
        struct.pack_into('<I', data, 524, 0x40000)
        struct.pack_into('<HBBHH', data, 640, ord('1'), 4, 8, 2, 3)
        struct.pack_into('<bb', data, 649, -1, 5)
        struct.pack_into('<h', data, 654, 3)
        pack = struct.pack('<II', 0x30201, len(data)) + data
        glyph = fonts(pack)[0]['glyphs'][0]
        self.assertEqual((glyph['bearing_x'], glyph['bearing_y'], glyph['advance']), (-1, 5, 3))
        struct.pack_into('<H', data, 522, 2)
        with self.assertRaises(ValueError):
            fonts(struct.pack('<II', 0x30201, len(data)) + data)

    def test_output_must_be_fresh_nonoverlapping_ascii(self):
        with tempfile.TemporaryDirectory(prefix='254-guard-') as temp:
            base = Path(temp)
            root = base/'input'
            root.mkdir()
            self.assertEqual(output_path(base/'fresh', [root]), base/'fresh')
            for path in (base, root, root/'new', base/('bad'+chr(0xE9))):
                with self.subTest(path=path.name), self.assertRaises(ValueError):
                    output_path(path, [root])


class NativeReportGuards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if REPORT is None:
            raise unittest.SkipTest('native report not requested')
        cls.data = json.loads((REPORT/'data.json').read_text(encoding='ascii'))
        cls.native = json.loads((REPORT/'native.json').read_text(encoding='ascii'))

    def test_inputs_and_complete_manifests(self):
        for key, expected in (('executable_sha256', EXE_SHA), ('bundle_sha256', BUNDLE_SHA),
                              ('language_sha256', LANGUAGE_SHA)):
            self.assertEqual(self.data[key], expected)
        before = sorted(REPORT.glob('before-*.json'))
        self.assertIn(len(before), (1, 2))
        for path in before:
            self.assertEqual(path.read_bytes(), (REPORT/path.name.replace('before-', 'after-')).read_bytes())
            self.assertGreater(len(json.loads(path.read_bytes())), 30)

    def test_formatter_padding_truncation_locale_and_hours(self):
        values = {(row['locale'], row['ticks']): row['text'] for row in self.native['format_cases']}
        self.assertEqual(len(values), 105)
        expected = {0: ' 0:00.00', 39: ' 0:00.00', 40: ' 0:00.01', 3999: ' 0:00.99',
                    4000: ' 0:01.00', 239999: ' 0:59.99', 240000: ' 1:00.00',
                    14399999: '59:59.99', 14400000: ' 1h:00:00',
                    0x7FFFFFFF: ' 0:00.00', -40: '-0:00.01'}
        for ticks, text in expected.items():
            self.assertEqual(values[(5, ticks)], text)
        self.assertEqual(values[(1, 240000)], ' 1\'00"00')
        self.assertEqual(values[(2, 240000)], ' 1:00:00')

    def test_native_lap_hold_invalid_start_best_and_finish(self):
        values = {row['name']: row['output'] for row in self.native['lap_cases']}
        self.assertEqual(len(values), 13)

        def call(name, target):
            return next(row for row in values[name]['calls'] if row.get('target') == target)

        for name in ('before_start', 'invalid_start_sentinel', 'restart_fresh'):
            self.assertEqual(call(name, 'current')['localized_hash'], '61C9B7A2')
            self.assertEqual(call(name, 'best')['text'], ' 0:00.00')
            self.assertEqual(call(name, 'lap_number')['text'], ' 1')
        self.assertEqual(call('go', 'current')['text'], ' 0:00.00')
        self.assertEqual(call('running', 'current')['text'], ' 0:15.08')
        self.assertEqual(values['paused_same_snapshot'], values['running'])
        self.assertEqual(call('rollover', 'lap_number')['text'], ' 2')
        self.assertEqual(call('rollover', 'lap_total')['text'], '/ 2')
        self.assertEqual(call('hold_before_3s', 'current')['text'], ' 1:00.00')
        self.assertEqual(call('hold_exact_3s', 'current')['text'], ' 0:03.00')
        self.assertEqual(call('hold_after_3s', 'current')['text'], ' 0:03.00')
        self.assertEqual(call('best_zero', 'best')['text'], '--:--:--')
        self.assertEqual(call('finished_record', 'lap_number')['text'], ' 3')
        self.assertEqual(values['component_disabled']['calls'], [])

    def test_mask_requires_permission_and_branch_and_hides_at_finish(self):
        rows = self.native['mask_cases']
        self.assertEqual(len(rows), 34)
        for row in rows:
            expected = row['stage'] == 4 and row['permission'] == 0x100 and row['view_state'] == 0 and not row.get('finished')
            self.assertEqual(row['output']['lap_enabled'], expected)

    def test_resource_binding_uses_actual_native_stride(self):
        for row in self.native['resource_cases']:
            index = row['index']
            self.assertEqual(row['resolved_hash'], 0 if index == 0xFFFF else 0x11220000+index)
            self.assertEqual(row['resolved_handle'], 0 if index == 0xFFFF else 0x33440000+index)

    def test_package_font_requests_and_local_geometry(self):
        rows = {obj['name']: obj for obj in self.data['objects']}
        for name in ('CurrentLapTime', 'CurrentLapTimeText', 'BestLapTime', 'BestLapTimeText'):
            request = rows[name]['default_resource']
            self.assertEqual(request['index'], 3)
            self.assertTrue(request['name'].endswith('ConduitMdITC_TT14i.ffn'))
        self.assertEqual(rows['LapNumber']['default_resource']['index'], 5)
        self.assertEqual(rows['NumberLaps']['default_resource']['index'], 0)
        current = next(row for row in rows['CurrentLapTime']['fields'] if row['tag'] == 'SA')
        self.assertEqual(current['position_depth'], [-147.0, -170.0, 100.0])
        self.assertEqual(current['bgra'], [255, 190, 150, 255])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path)
    args, rest = parser.parse_known_args()
    REPORT = args.report
    unittest.main(argv=[__file__] + rest)

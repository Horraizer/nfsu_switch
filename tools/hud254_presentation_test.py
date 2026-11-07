"""Validate a fresh native report, including independent glyph/transform math.

Both --report and --source are mandatory: no skipped native evidence mode.
"""
import argparse
import json
from pathlib import Path
import struct
import unittest

from hud254_presentation import GLOBAL_SHA, TEXTURES_SHA, RANGES, textures
from hud254_trace import BUNDLE_SHA, EXE_SHA, LANGUAGE_SHA, sha


REPORT = None
SOURCE = None


def fields(obj):
    return {f['tag']: f for f in obj['fields']}


class PresentationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.r = json.loads((REPORT / 'presentation.json').read_text(encoding='ascii'))
        cls.objects = {o['name']: o for o in cls.r['objects']}

    def close(self, left, right):
        self.assertEqual(len(left), len(right))
        for a, b in zip(left, right):
            self.assertAlmostEqual(a, b, delta=0.0001)

    def test_report_identity_and_input_preservation(self):
        self.assertEqual(self.r['source_commit'], SOURCE)
        self.assertEqual(self.r['input_sha256'], {
            'speed.exe': EXE_SHA, 'Global/INGAMEB.BUN': BUNDLE_SHA,
            'Languages/English.bin': LANGUAGE_SHA, 'Global/GLOBALB.BUN': GLOBAL_SHA,
            'Languages/LanguageTextures.bin': TEXTURES_SHA})
        for name, digest in self.r['tools'].items():
            self.assertEqual(sha((Path(__file__).parent / name).read_bytes()), digest)
        for i in range(2):
            before = (REPORT / ('before-%d.json' % i)).read_bytes()
            self.assertEqual(before, (REPORT / ('after-%d.json' % i)).read_bytes())
            rows = json.loads(before)
            self.assertGreater(len(rows), 1)
            self.assertEqual(rows[0][:2], ['.', 'dir'])
            for name, size, mtime, digest in rows:
                self.assertIsInstance(mtime, int)
                if size != 'dir':
                    self.assertRegex(digest, '^[0-9A-F]{64}$')
        self.assertIn('no original runtime capture', self.r['evidence'])
        self.assertGreaterEqual(len(self.r['limits']), 6)

    def test_instruction_ranges_are_complete_and_named(self):
        self.assertEqual([(r['name'], int(r['begin'], 16), int(r['end'], 16))
                          for r in self.r['ranges']], list(RANGES))
        text = (REPORT / 'instructions.txt').read_text(encoding='ascii')
        for r in self.r['ranges']:
            self.assertIn('%s %s..%s SHA256 %s' %
                          (r['name'], r['begin'], r['end'], r['sha256']), text)

    def test_texture_parser_rejects_malformed_records(self):
        def chunk(payload):
            return struct.pack('<II', 0x33310004, len(payload)) + payload
        record = bytearray(124)
        record[12:17] = b'FONT\0'
        struct.pack_into('<I', record, 36, 0x12345678)
        struct.pack_into('<HH', record, 68, 256, 128)
        nested = struct.pack('<II', 0xB3300000, 132) + chunk(record)
        row = textures(nested)[0]
        self.assertEqual((row['name'], row['width'], row['height'], row['offset']),
                         ('FONT', 256, 128, 16))
        for bad in (chunk(record[:-1]), nested[:-1], b'X'):
            with self.subTest(bad=bad[:8]), self.assertRaises(ValueError):
                textures(bad)
        record[12:36] = b'X' * 24
        with self.assertRaises(ValueError):
            textures(chunk(record))

    def test_language_producer_selects_formatter(self):
        self.assertEqual([r['name'] for r in self.r['language_table'][:7]],
                         ['ENGLISH', 'FRENCH', 'GERMAN', 'ITALIAN', 'SPANISH', 'DUTCH', 'SWEDISH'])
        expected = [' 1:00.00', ' 1\'00"00', ' 1:00:00', ' 1\'00"00',
                    ' 1:00:00', ' 1:00.00', ' 1:00:00']
        for i, (row, text) in enumerate(zip(self.r['languages'], expected)):
            self.assertEqual((row['selected'], row['language_global'], row['text']), (i, i, text))
            self.assertEqual(row['stub_calls'], ['0059F7C0', '0051C5F0'])
        self.assertEqual(len(self.r['languages']), len(expected))

    def test_profile_copy_mask_and_toggle_round_trip(self):
        self.assertEqual(len(self.r['permissions']), 9)
        for row in self.r['permissions']:
            with self.subTest(option=row['option'], initial=row['initial']):
                enabled = row['option'] == 1
                self.assertEqual((row['initialized'], row['view_state']), (0x755, 0))
                expected = (row['initial'] | 0x650) & ~0x100
                if enabled:
                    expected |= 0x100
                self.assertEqual(row['settings'], expected)
                self.assertEqual(row['view'], expected)
                self.assertEqual(row['mask']['lap_enabled'], enabled)
                self.assertTrue(row['mask']['stub_calls'])
                for step, toggle in enumerate(row['toggles']):
                    flags = expected ^ (0x100 if step == 0 else 0)
                    self.assertEqual(toggle, dict(view=flags, settings=flags, profile=(flags >> 8) & 1))
                self.assertEqual(len(row['toggles']), 2)
                self.assertEqual(len(row['toggle_stubs']), 2)

    def test_font_loader_and_native_execution_boundaries(self):
        defaults = {'ConduitMdITC_TT14i': 20, 'LCD_LET48': 40, 'Font_ConduitMdITCTT38bi': 47}
        required = {'004F44C0', '004F43D0', '00501AC0', '004F1810', '004F25F0',
                    '004F48B0', '004F4790', '00514530', '00514B90', '0040BEF0', '0040F3D0'}
        for row in self.r['cases']:
            if 'font' not in row:
                continue
            font = row['font']
            self.assertEqual(font['line_height'], defaults[font['name']])
            self.assertEqual((font['baseline'], font['line_spacing_scale']), (0, 1))
            self.assertEqual(font['texture_hash'], row['texture']['hash'])
            self.assertTrue(required <= set(row['executed_entries']))
            self.assertFalse(any('font lookup' in s for s in row['stubs']))
            self.assertGreater(row['executed_instructions'], 100)
        self.assertEqual([r['hash'] for r in self.r['font_overrides']],
                         ['DCA5485A', '833A8678', 'F88A75F9', '71C777D7'])

    def test_numeric_glyph_geometry_and_Sj_from_metadata(self):
        metadata = next(f for f in self.r['font_metadata'] if f['name'] == 'conduitmditc_tt14i')
        glyphs = {g['code']: g for g in metadata['glyphs']}
        tested = 0
        for row in self.r['cases']:
            if row['name'] not in ('CurrentLapTime', 'BestLapTime') or row.get('parent'):
                continue
            tested += 1
            fs = fields(self.objects[row['name']])
            sa = fs['SA']
            sx, sy = sa['size_scale']
            tx, ty, _ = sa['position_depth']
            sj = row['sj_override'] if row['sj_override'] is not None else fs['Sj']['justification']
            width = sum(glyphs[ord(c)]['advance'] for c in row['text'])
            x = -width / 2 if sj & 1 else -width if sj & 2 else 0
            y = -row['font']['line_height'] / 2 if sj & 4 else -row['font']['line_height'] if sj & 8 else 0
            quads = row['output']['local']['quads']
            self.assertEqual(len(quads), len(row['text']))
            for c, quad in zip(row['text'], quads):
                g = glyphs[ord(c)]
                left, top = x + g['bearing_x'], y + g['bearing_y']
                right, bottom = left + max(4, g['width']), top + g['height']
                for actual, (px, py) in zip(quad['positions'],
                        ((left, top), (right, top), (right, bottom), (left, bottom))):
                    self.close(actual, [px*sx+tx+320, py*sy+ty+240, 1])
                tw, th = row['texture']['width'], row['texture']['height']
                u, v = (g['x']-1)/tw, (g['y']-1)/th
                u1, v1 = (g['x']+g['width']+1)/tw, (g['y']+g['height']+1)/th
                for actual, expected in zip(quad['uv'], ((u,v),(u1,v),(u1,v1),(u,v1))):
                    self.close(actual, expected)
                x += g['advance']
        self.assertEqual(tested, 9)

    def test_parent_transform_precedes_canvas_translation(self):
        rows = [r for r in self.r['cases'] if r['name']=='CurrentLapTime' and r['sj_override'] is None]
        self.assertEqual(len(rows), 2)
        base, parent = rows
        for a, b in zip(base['output']['local']['quads'], parent['output']['local']['quads']):
            for p, q in zip(a['positions'], b['positions']):
                self.close(q, [(p[0]-320)*2+11+320, (p[1]-240)*3+17+240, 1])

    def test_sprite_geometry_uses_centered_unit_quad(self):
        for row in self.r['cases']:
            if 'font' in row:
                continue
            sa = fields(self.objects[row['name']])['SA']
            sx, sy = sa['size_scale']
            tx, ty, _ = sa['position_depth']
            quads = row['output']['local']['quads']
            self.assertEqual(len(quads), 1)
            for actual, (x,y) in zip(quads[0]['positions'], ((-.5,-.5),(.5,-.5),(.5,.5),(-.5,.5))):
                self.close(actual, [x*sx+tx+320, y*sy+ty+240, 1])

    def test_final_vertices_material_and_half_texel_inset(self):
        self.assertEqual(len(self.r['cases']), 18)
        for row in self.r['cases']:
            output = row['output']
            quads, draws = output['local']['quads'], output['draws']
            self.assertEqual(len(draws), len(quads))
            self.assertGreater(len(draws), 0)
            self.assertEqual(output['material'], [dict(at='00410650',esi=0,edi=0),
                             dict(at='00410550',esi=6,edi=5)] * len(draws))
            bgra = fields(self.objects[row['name']])['SA']['bgra']
            if row.get('parent'):
                bgra = [(a*b+128) >> 8 for a,b in zip(bgra,row['parent']['color'])]
            b,g,r,a = [int(c/2) for c in bgra]
            color = '%08X' % ((a<<24)|(r<<16)|(g<<8)|b)
            for quad, draw in zip(quads, draws):
                self.assertEqual((draw['primitive'],draw['count'],draw['stride']), (6,2,32))
                for i, v in enumerate(draw['vertices']):
                    p = quad['positions'][i]
                    self.close(v['position'], [p[0]/320-1, 1-p[1]/240, 1])
                    self.assertEqual(v['color'], color)
                    uv = quad['uv'][i]
                    self.close(v['uv'], [uv[0]+(1 if i in (0,3) else -1)*.5/row['texture']['width'],
                                         uv[1]+(1 if i in (0,1) else -1)*.5/row['texture']['height']])

    def test_queue_descending_clamped_truncated_and_stable(self):
        expected = [[1,2,0,3], [2,3,0,1], [2,0,1]]
        self.assertEqual([r['submission_indices'] for r in self.r['queue']], expected)
        for row in self.r['queue']:
            self.assertEqual(len(row['stubs']), 2)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--source', required=True)
    args = parser.parse_args()
    REPORT, SOURCE = args.report, args.source
    unittest.main(argv=['hud254_presentation_test.py'], verbosity=2)

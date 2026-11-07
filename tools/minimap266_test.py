"""Parser guards and pinned assertions for a freshly generated minimap report.

--report is required: native checks must not silently skip without retail.
"""
import argparse
import json
from pathlib import Path
import struct
import tempfile
import unittest

import minimap266_trace as trace


def chunk(kind,data):
    return struct.pack('<II',kind,len(data))+data


def texture_fixture():
    info = bytearray(124)
    name = b'TRACK1003_MAP\0'
    info[12:12+len(name)] = name
    struct.pack_into('<I',info,36,0x1234)
    struct.pack_into('<5I',info,48,0,0,4,0,4)
    struct.pack_into('<2H',info,68,1,1)
    info[74],info[78] = 0x20,1
    fmt = bytearray(32)
    struct.pack_into('<I',fmt,20,21)
    tables = chunk(0x33310002,struct.pack('<II',0x1234,0))
    tables += chunk(0x33310004,info)+chunk(0x33310005,fmt)
    # Include the root header and this data header in absolute alignment.
    padding = b'\x11' * (-(8+len(tables)+8) % 128)
    return chunk(0xB3300000,tables+chunk(0x33320002,padding+b'\x00\x00\x00\xff'))


class Guards(unittest.TestCase):
    def test_texture_valid_and_no_pixels_exported(self):
        rows = trace.raw_textures(texture_fixture())
        self.assertEqual(rows[0]['alpha_histogram'],{'255':1})
        self.assertNotIn('pixels',rows[0])

    def test_texture_rejects_truncation_and_bad_counts(self):
        blob=texture_fixture()
        with self.assertRaises(ValueError):
            trace.raw_textures(blob[:-1])
        damaged=bytearray(blob)
        struct.pack_into('<I',damaged,4,len(blob)-8+1)
        with self.assertRaises(ValueError):
            trace.raw_textures(damaged)

    def test_texture_rejects_hash_or_data_escape(self):
        blob=texture_fixture()
        at=blob.index(b'TRACK1003_MAP')-12
        for offset,value in ((36,0x5678),(48,128),(56,0xFFFFFFFF),(64,8)):
            damaged=bytearray(blob)
            struct.pack_into('<I',damaged,at+offset,value)
            with self.subTest(offset=offset),self.assertRaises(ValueError):
                trace.raw_textures(damaged)

    def test_track_rejects_duplicate_missing_or_bad_scale(self):
        row=bytearray(252)
        struct.pack_into('<h',row,0x6C,1003)
        struct.pack_into('<3f',row,0x8C,100,200,1202)
        self.assertEqual(trace.track_map(chunk(0x34201,row))['world_span'],1202)
        for payload in (row+row,b'',row[:-1]):
            with self.assertRaises(ValueError):
                trace.track_map(chunk(0x34201,payload))
        for scale in (0,-1,float('nan'),float('inf')):
            struct.pack_into('<f',row,0x94,scale)
            with self.assertRaises(ValueError):
                trace.track_map(chunk(0x34201,row))

    def test_package_and_route_bounds(self):
        for blob in (b'',b'FOb\xea',b'FOb\xea'+struct.pack('<I',100)+b'ObjD'+struct.pack('<I',99)):
            with self.assertRaises(ValueError):
                trace.placeholder(blob)
        with self.assertRaises(ValueError):
            trace.route_points(chunk(0x34149,b'\0'*19))
        row=bytearray(20)
        struct.pack_into('<f',row,0,float('nan'))
        with self.assertRaises(ValueError):
            trace.route_points(chunk(0x34149,row))

    def test_output_refuses_overlap_and_existing(self):
        with tempfile.TemporaryDirectory(prefix='266-s4-test-',dir=Path.cwd()/'build') as directory:
            root=Path(directory)
            inputs=root/'input'
            inputs.mkdir()
            for out in (inputs,inputs/'new',root):
                with self.assertRaises(ValueError):
                    trace.output_path(out,[inputs])
            self.assertEqual(trace.output_path(root/'fresh',[inputs]),root/'fresh')


class NativeEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data=json.loads((REPORT/'data.json').read_text(encoding='ascii'))
        cls.native=json.loads((REPORT/'native.json').read_text(encoding='ascii'))

    def test_fresh_tools_provenance_and_unchanged_manifests(self):
        for name,digest in self.native['tool_hashes'].items():
            self.assertEqual(trace.sha((Path(__file__).parent/name).read_bytes()),digest)
        self.assertEqual(set(self.data['provenance']),set(trace.INPUTS))
        for name,identity in self.data['provenance'].items():
            self.assertEqual(identity['sha256'],trace.INPUTS[name])
        self.assertEqual(self.data['source_commit'],self.native['source_commit'])
        self.assertRegex(self.data['source_commit'],r'^[0-9a-f]{40}$')
        self.assertTrue((REPORT/'before-0.json').is_file())
        for before in REPORT.glob('before-*.json'):
            self.assertEqual(before.read_bytes(),before.with_name(before.name.replace('before-','after-')).read_bytes())

    def test_source_resource_and_native_placeholder(self):
        textures={t['name']:t for t in self.data['textures']}
        self.assertEqual(set(textures),trace.TEXTURES)
        texture=textures['TRACK1003_MAP']
        self.assertEqual((texture['width'],texture['height'],texture['data_offset']),(256,256,819968))
        self.assertEqual(texture['hash'],'A4F2F595')
        self.assertEqual(texture['data_sha256'],'CEC7716675F0A80B62EA18DA25CEE3C8D2E9D05967EB38A8284DF7A247166F21')
        self.assertEqual(sum(texture['alpha_histogram'].values()),256*256)
        self.assertEqual(self.data['placeholder']['native_center'],[86,240])
        self.assertEqual(self.data['placeholder']['size'],[128,128])

    def test_world_projection_scalar_precision_and_source_points(self):
        rows={row['name']:row for row in self.native['projections']}
        self.assertEqual(len(rows),39)
        self.assertEqual(rows['node_11']['source_offset'],265972)
        self.assertEqual(rows['node_11']['uv'],[0.8671801090240479,0.4392877519130707])
        self.assertEqual(rows['grid_1']['uv'],[0.8695126175880432,0.4154343605041504])
        # Retail's sine approximation is observably not an exact identity.
        self.assertEqual(self.native['world_matrix'][0],1.0000035762786865)
        self.assertNotEqual(rows['node_0']['uv'],rows['node_30']['uv'])

    def test_geometry_clips_and_culls_outside(self):
        self.assertEqual(len(self.native['geometry']),8)
        for case in self.native['geometry']:
            all_quads=list(case['route_clipped'])
            for marker in case['markers']:
                all_quads.extend(marker['clipped'])
                if marker['name']=='outside_map':
                    self.assertEqual(marker['submitted'],[])
                    self.assertEqual(marker['clipped'],[])
            if case['style']==6:
                self.assertEqual(all_quads,[])
                self.assertEqual(case['route'],[])
            else:
                self.assertTrue(case['route_clipped'])
            for quad in all_quads:
                for x,y in quad['screen']:
                    self.assertGreaterEqual(x,22-0.00001)
                    self.assertLessEqual(x,150+0.00001)
                    self.assertGreaterEqual(y,176-0.00001)
                    self.assertLessEqual(y,304+0.00001)

    def test_separation_rotates_with_map_and_heading(self):
        normal,turned=self.native['geometry'][1:3]
        def delta(case):
            a,b=[marker['submitted'][0]['quad']['screen'][0] for marker in case['markers'][:2]]
            return b[0]-a[0],b[1]-a[1]
        dx,dy=delta(normal)
        self.assertGreater(dx,2)
        self.assertAlmostEqual(dy,0,places=5)
        dx,dy=delta(turned)
        self.assertAlmostEqual(dx,0,places=4)
        self.assertGreater(dy,2)
        self.assertNotEqual(normal['markers'][0]['submitted'],turned['markers'][0]['submitted'])

    def test_seam_and_reset_are_pose_projections_not_progress_wraps(self):
        before,after,reset=self.native['geometry'][5:]
        self.assertEqual(before['markers'][0]['name'],'lap_before')
        self.assertEqual(after['markers'][0]['name'],'lap_after')
        self.assertEqual(reset['markers'][0]['name'],'reset_to_player_grid')
        self.assertEqual(reset['player_xy'],self.data['grid'][1]['raw_position'][:2])
        self.assertLess(abs(before['center'][1]-after['center'][1]),0.001)
        for case in (before,after,reset):
            vertices=case['markers'][0]['submitted'][0]['quad']['screen']
            self.assertAlmostEqual(sum(v[0] for v in vertices)/4,86,places=3)
            self.assertAlmostEqual(sum(v[1] for v in vertices)/4,240,places=3)

    def test_native_partitions_opponents_and_no_traffic_marker(self):
        base,first_off,second_off,remote=self.native['selection']
        self.assertEqual(base['partitions'],[6,1,4,5])
        ids=lambda row:[call['fixture_id'] for call in row['calls'] if call['fixture_id'] is not None]
        self.assertEqual(ids(base),[100,101,102,7,0])
        self.assertEqual(ids(first_off),[101,102,7,0])
        self.assertEqual(ids(second_off),[101,102,7,0])
        self.assertEqual(ids(remote),[100,101,102,7,0,0])
        for row in self.native['selection']:
            self.assertNotIn(50,ids(row))
        self.assertTrue(all(c['color_address']=='007313DD' for c in base['calls'] if c['fixture_id'] in (100,101,102)))

    def test_mask_style_branch_and_finish(self):
        rows=self.native['masks']
        self.assertEqual(len(rows),48)
        for row in rows:
            expected=(row['stage']==4 and not row['finished'] and
                      (row['view_state']==1 or (row['view_state']==0 and row['map_style']!=6)))
            self.assertEqual(row['map_enabled'],expected,row)

    def test_native_orientation_pause_and_filter(self):
        rows=self.native['orientation']['rows']
        self.assertEqual([r['angle'] for r in rows],[49152,49145,49145,49125,49098])
        self.assertEqual(rows[1]['filter_state'],rows[2]['filter_state'])
        self.assertNotEqual(rows[0]['filter_state'],rows[1]['filter_state'])
        self.assertNotEqual(rows[3]['filter_state'],rows[4]['filter_state'])


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path,required=True)
    args,rest=parser.parse_known_args()
    REPORT=args.report
    unittest.main(argv=[__file__]+rest)

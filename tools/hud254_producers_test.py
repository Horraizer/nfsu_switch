"""Independent report guards; a fresh authenticated native report is required."""
import argparse
import json
from pathlib import Path
import struct
import unittest

from hud254_producers import (EXE_SHA,BUNDLE_SHA,LANGUAGE_SHA,SHADER_SHA,TOOLS,
                             RANGES,package_objects,sha)

LAP = ('LapCounterBacking_1','LapCounterBacking_2','LapCounterBacking_3',
       'CurrentLapTime','CurrentLapTimeText','BestLapTime','BestLapTimeText',
       'LapNumber','NumberLaps','LapText')


def validate(report,source):
    data=json.loads((report/'producers.json').read_text(encoding='ascii'))
    if data['source_commit']!=source:
        raise ValueError('source mismatch')
    for name in TOOLS:
        if data['tools'].get(name)!=sha((Path(__file__).parent/name).read_bytes()):
            raise ValueError('stale tool: '+name)
    for index in (0,1):
        before=(report/('before-%d.json'%index)).read_bytes()
        after=(report/('after-%d.json'%index)).read_bytes()
        if before!=after or not json.loads(before):
            raise ValueError('changed or empty input manifest')
    return data


class WitnessTests(unittest.TestCase):
    def test_authentication_and_boundaries(self):
        self.assertEqual(DATA['input_sha256'],{'speed.exe':EXE_SHA,'Global/INGAMEB.BUN':BUNDLE_SHA,
            'Languages/English.bin':LANGUAGE_SHA,'fx/world.efx':SHADER_SHA})
        self.assertEqual([(r['name'],int(r['begin'],16),int(r['end'],16)) for r in DATA['ranges']],list(RANGES))
        for row in DATA['ranges']:
            self.assertRegex(row['sha256'],r'^[0-9A-F]{64}$')
        self.assertFalse(DATA['presentation_ready'])
        self.assertIn('no original runtime capture',DATA['evidence'])
        self.assertEqual(len(DATA['blockers']),5)
        with self.assertRaisesRegex(ValueError,'source mismatch'):
            validate(REPORT,'0'*40)

    def test_constructor_then_loaded_profile_overrides(self):
        for case in DATA['profiles']:
            self.assertEqual(case['initialized_option'],1)
            expected=1 if case['loaded_option'] is None else case['loaded_option']
            self.assertEqual(case['selected_option'],expected)
            self.assertEqual(bool(case['view_permission']&0x100),expected==1)
            self.assertEqual(case['stubs'],[])
            self.assertGreater(case['executed_instructions'],100)
        self.assertEqual([c['loaded_option'] for c in DATA['profiles']],[None,0,1,2])

    def test_viewport_uses_target_without_invented_safe_margin(self):
        self.assertEqual([c['supplied_size'] for c in DATA['viewports']],[[640,480],[1280,720],[1920,1080]])
        for case in DATA['viewports']:
            self.assertEqual(case['calls'][-1]['viewport'],[0,0]+case['supplied_size']+[0.,1.])
            self.assertEqual([c['boundary'] for c in case['calls']],['0040ED50','device+94','device+9C','device+BC'])
            self.assertTrue(case['active_target_matches'])
            self.assertEqual(case['shader_context'],0)
            self.assertEqual(len(case['stubs']),4)

    def test_package_hierarchy_is_serialized_provenance(self):
        self.assertEqual(len(DATA['objects']),85)
        objects={r['id']:r for r in DATA['objects']}
        selected={r['name']:r for r in DATA['objects'] if r['name'] in LAP}
        self.assertEqual(set(selected),set(LAP))
        for row in selected.values():
            self.assertIsNone(row['parent'])
            self.assertFalse(row['flags']&0x100000)
        for row in objects.values():
            if row['parent'] is not None:
                self.assertEqual(objects[row['parent']]['kind'],5)
                self.assertLess(objects[row['parent']]['offset'],row['offset'])
        # Nested groups exercise the parent-cache path as well as root lap leaves.
        turbo=next(r for r in objects.values() if r['name']=='TurboPSI')
        self.assertEqual(objects[turbo['parent']]['name'],'Turbo_Group')
        self.assertEqual(objects[objects[turbo['parent']]['parent']]['name'],'GaugeCluster')

    def test_native_resource_hash_and_font_crosswalk(self):
        def name_hash(name):
            basename=name.replace('\\','/').split('/')[-1].rsplit('.',1)[0].upper()
            value=0xFFFFFFFF
            for c in basename:
                value=(value*33+ord(c))&0xFFFFFFFF
            return '%08X'%value
        requests=DATA['resources']
        self.assertEqual(len(requests),29)
        for request in requests:
            self.assertEqual(request['hash'],name_hash(request['name']))
            self.assertEqual(request['handle'],0)
        fonts={name_hash(row['name']):row for row in DATA['fonts']}
        for binding in DATA['bindings']:
            request=requests[binding['index']]
            self.assertEqual(binding['bound_hash'],request['hash'])
            self.assertEqual(binding['bound_handle'],0)
            if binding['name'] in LAP and not binding['name'].startswith('LapCounterBacking'):
                self.assertIn(binding['bound_hash'],fonts)
        self.assertEqual({b['name'] for b in DATA['bindings']},set(LAP))

    def test_native_upstream_sort_is_stable_before_reverse_visit(self):
        case=DATA['traversal'][0]
        expected=sorted(case['sort_input'],key=lambda r:r['depth'])
        self.assertEqual(case['sorted_order'],[r['name'] for r in expected])
        self.assertEqual(case['executed_entries'],['004F0390','004F04E0','004F0660',
            '004F16D0','004F5090','004F5560','004F2E30','004F5640','004F2FD0','004F2EB0'])

    def test_lap_order_is_reverse_package_ties_then_depth_buckets(self):
        rows=[r for r in DATA['objects'] if r['name'] in LAP]
        # Stable ascending sort, reverse iteration; all selected objects are
        # roots with integral depth, so parent round-off cannot change buckets.
        expected=list(reversed(sorted(rows,key=lambda r:struct.unpack_from('<f',bytes.fromhex(r['sa']),36)[0])))
        submitted=[r for r in DATA['traversal'][0]['submissions'] if r['name'] in LAP]
        self.assertEqual([r['name'] for r in submitted],[r['name'] for r in expected])
        self.assertEqual([r['name'] for r in submitted[:3]],
            ['LapCounterBacking_2','LapCounterBacking_3','LapCounterBacking_1'])
        self.assertTrue(all(r['parent_index']==0 for r in submitted))

    def test_visibility_inputs_are_not_assumed_live(self):
        default,hidden,inactive=DATA['traversal']
        self.assertEqual(hidden['hidden'],['LapNumber','LapCounterBacking_2'])
        expected=[r for r in default['submissions'] if r['name'] not in hidden['hidden']]
        self.assertEqual(hidden['submissions'],expected)
        self.assertFalse(inactive['package_active'])
        self.assertEqual(inactive['submissions'],[])
        for case in DATA['traversal']:
            self.assertEqual(len(case['stubs']),3)

    def test_shader_provenance_includes_alpha_times_two(self):
        shader=DATA['shader']
        self.assertEqual(shader['path'],'fx\\world.efx')
        self.assertEqual(shader['techniques'],['world','world_fixed'])
        self.assertEqual(shader['terminal_byte'],255)
        self.assertEqual(shader['effect_context_index'],0)
        self.assertTrue(shader['native_decode'])
        self.assertIn('programmable_rgba_modulate2',shader['clauses'])
        self.assertIn('fixed_alpha_modulate2',shader['clauses'])
        self.assertIn('no technique validation or GPU execution',shader['evidence'])
        self.assertEqual(len(shader['unresolved']),3)

    def test_callback_vtable_is_actual_executable_data(self):
        table=DATA['callback_table']
        for index,value in {8:'004F56A0',9:'004F5730',10:'004F5560',12:'004F5090',
                            13:'004F5640',15:'004F5660'}.items():
            self.assertEqual(table[index],value)

    def test_parser_rejects_truncation_and_unresolved_parent(self):
        def chunk(tag,payload):
            return tag+struct.pack('<I',len(payload))+payload
        def field(tag,payload):
            return tag+struct.pack('<H',len(payload))+payload
        payload=b''.join((field(b'Ot',struct.pack('<I',2)),field(b'On',b'LapNumber\0'),
            field(b'OP',struct.pack('<4I',1,2,0,0)),field(b'SA',bytes(68))))
        def wrap(p):
            return chunk(b'FEn\xe7',chunk(b'Obj\xcc',chunk(b'FOb\xea',chunk(b'ObjD',p))))
        self.assertEqual(package_objects(wrap(payload))[0]['name'],'LapNumber')
        with self.assertRaises(ValueError):
            package_objects(wrap(payload)[:-1])
        with self.assertRaisesRegex(ValueError,'parent absent'):
            package_objects(wrap(payload+field(b'PA',struct.pack('<I',99))))
        with self.assertRaisesRegex(ValueError,'duplicate'):
            package_objects(wrap(payload+field(b'On',b'duplicate\0')))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--source',required=True)
    args=parser.parse_args()
    REPORT=args.report
    DATA=validate(REPORT,args.source)
    unittest.main(argv=['hud254_producers_test.py'],verbosity=2)

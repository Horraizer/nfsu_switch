"""AI dependency closure, output verification, refusal and file integrity."""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import original_ai_recorder as host
from original_ai_replay import shadow,replay,compare,validate_entry
import original_ai_port_compare as port_compare
from original_recorder import digest,EXE_SHA256


def case():
    fx = bytearray(512)
    struct.pack_into('<H',fx,0,0x7f);struct.pack_into('<I',fx,24,0x1f80)
    r = {'seq':0,'invocation':5,'thread':17,'actor':0x920000,'actor_id':100,
         'kind':'driver','entry':0x45e970,'return_address':0x401000,'teb':0x930000,
         'stack_limit':0x910000,'stack_base':0x920000,
         'regs':dict(eax=0,ebx=1,ecx=0x900000,edx=2,esi=3,edi=4,ebp=5,esp=0x910800,eflags=0x202),
         'fxsave':fx.hex()}
    code = bytearray(4096)
    # Increment retained history and produce a float output; both must be
    # observed, not replaced with a recorded answer during replay.
    body = bytes.fromhex('ff4104d94108d84108d9590cc20400')
    code[0x970:0x970+len(body)] = body
    image = SimpleNamespace(pages={0x45e000:bytes(code),0x401000:bytes(4096)})
    actor,stack = bytearray(4096),bytearray(4096)
    struct.pack_into('<I',actor,4,7);struct.pack_into('<f',actor,8,1.25)
    struct.pack_into('<II',stack,0x800,0x401000,0x3ca3d70a)
    pages = {0x900000:bytes(actor),0x910000:bytes(stack)}
    return r,image,pages


def complete():
    r,code,pages = case()
    result = shadow(code,r,lambda a:pages[a])
    assert result['supported'],result
    r['post'] = result['expected'];r['post_regs'] = result['post_regs'];r['post_fxsave'] = r['fxsave']
    r['input_pages'] = result['pages'];r['native_calls']=[c[1] for c in result['calls']];r['native_check'] = compare(result,r)
    return r,code


class Tests(unittest.TestCase):
    def test_observer_reads_do_not_change_replay_or_dependency_closure(self):
        r,code,pages=case();baseline=shadow(code,r,lambda a:pages[a])
        seen=[]
        def observer(uc,address):
            view=port_compare.View(uc,pages,code)
            seen.append(address)
            self.assertEqual(view.get(0x900004),7 if address==r['entry'] else 8)
            before=list(uc.mem_regions())
            with self.assertRaisesRegex(ValueError,'Uncaptured observer dependency'):
                view.read(0x940000,4)
            self.assertEqual(before,list(uc.mem_regions()))
        result=shadow(code,r,lambda a:pages[a],observer=observer)
        self.assertTrue(result['supported'],result)
        self.assertTrue(seen)
        for key in ('expected','pages','watch','calls','post_regs'):
            self.assertEqual(baseline[key],result[key])

    def test_observer_cross_page_reads_saved_bytes_without_mapping(self):
        from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
        uc=Uc(UC_ARCH_X86,UC_MODE_32)
        v=port_compare.View(uc,{0x900000:bytes([3])*4096,0x901000:bytes([4])*4096},SimpleNamespace(pages={}))
        self.assertEqual(v.read(0x900ffe,4),b'\x03\x03\x04\x04')
        self.assertEqual(list(uc.mem_regions()),[])

    def test_native_floating_return_reads_actual_x87_top(self):
        from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
        uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(0x10000,4096)
        uc.mem_write(0x10000,b'\xd9\x05\x10\x00\x01\x00')
        for value in (-1.,0.,12.5,0.03125):
            uc.mem_write(0x10010,struct.pack('<f',value));uc.emu_start(0x10000,0x10006)
            self.assertEqual(port_compare.st0_float(uc),value)
        uc.mem_write(0x10010,struct.pack('<f',float('inf')));uc.emu_start(0x10000,0x10006)
        with self.assertRaisesRegex(ValueError,'Nonfinite'):
            port_compare.st0_float(uc)

    def test_port_consumer_checks_manifest_instead_of_summary_claim(self):
        r,_=complete()
        with tempfile.TemporaryDirectory(prefix='nfsu-ai-port-tests-') as temp:
            root=Path(temp)
            values={'metadata.json':dict(schema='nfsu-ai-decisions-1',source=dict(sha256=EXE_SHA256)),
                    'summary.json':dict(native_reproduced=999)}
            for name,data in values.items():
                (root/name).write_text(json.dumps(data),encoding='ascii')
            (root/'events.jsonl').write_text('',encoding='ascii')
            (root/'steps.jsonl').write_text(json.dumps(r)+'\n',encoding='ascii')
            manifest={p.name:dict(bytes=p.stat().st_size,sha256=digest(p)) for p in root.iterdir()}
            (root/'manifest.json').write_text(json.dumps(manifest),encoding='ascii')
            _,records=port_compare.load_session(root)
            self.assertEqual(len(records),1)
            (root/'steps.jsonl').write_text('{}\n',encoding='ascii')
            with self.assertRaisesRegex(ValueError,'integrity failure'):
                port_compare.load_session(root)

    def test_port_consumer_refuses_foreign_build_before_compiling(self):
        with tempfile.TemporaryDirectory(prefix='nfsu-ai-port-tests-') as temp:
            root=Path(temp);(root/'CMakeCache.txt').write_text('CMAKE_HOME_DIRECTORY:INTERNAL=Q:/foreign\n',encoding='ascii')
            with mock.patch.object(port_compare.subprocess,'run') as run:
                with self.assertRaisesRegex(ValueError,'different checkout'):
                    port_compare.build_probe(root,root,'c++')
                run.assert_not_called()

    def test_port_consumer_never_accepts_duplicate_or_executable_pages(self):
        code=SimpleNamespace(pages={0x450000:bytes(4096)})
        for pages in ([[0x450000,'00'*4096]],[[0x900000,'00'*4096]]*2,
                      [[0x900001,'00'*4096]],[[0x900000,'00'*4095]]):
            with self.assertRaisesRegex(ValueError,'capture page'):
                port_compare.captured_pages(dict(input_pages=pages),code)

    def test_native_outputs_and_retained_state(self):
        r,code = complete()
        self.assertTrue(replay(code,r)['passed'])
        actual = {a:v for a,v in r['post']}
        self.assertEqual(bytes.fromhex(actual[0x900004]),struct.pack('<I',8))
        self.assertEqual(bytes.fromhex(actual[0x90000c]),struct.pack('<f',2.5))

    def test_saved_answer_does_not_feed_emulation(self):
        r,code = complete();r['post'][0][1] = '00000000'
        self.assertTrue(r['native_check']['passed'])
        self.assertFalse(replay(code,r)['passed'])

    def test_missing_dependency_is_not_zero_filled(self):
        r,code = complete();r['input_pages'] = [p for p in r['input_pages'] if p[0] != 0x900000]
        self.assertIn('Uncaptured dependency',replay(code,r)['unsupported'])

    def test_duplicate_page_is_refused(self):
        r,code = complete();r['input_pages'].append(r['input_pages'][0])
        with self.assertRaises(ValueError):replay(code,r)

    def test_external_call_is_unsupported(self):
        r,code,pages = case();b=bytearray(code.pages[0x45e000])
        b[0x970:0x976]=b'\x68'+struct.pack('<I',0x900000)+b'\xc3'
        code.pages[0x45e000]=bytes(b)
        result=shadow(code,r,lambda a:pages[a])
        self.assertFalse(result['supported']);self.assertIn('External/non-retail',result['error'])

    def test_clock_instruction_is_unsupported(self):
        r,code,pages=case();b=bytearray(code.pages[0x45e000]);b[0x970:0x972]=b'\x0f\x31';code.pages[0x45e000]=bytes(b)
        self.assertIn('CPU/time',shadow(code,r,lambda a:pages[a])['error'])

    def test_execution_budget_does_not_pass(self):
        r,code,pages=case();b=bytearray(code.pages[0x45e000]);b[0x970:0x972]=b'\xeb\xfe';code.pages[0x45e000]=bytes(b)
        self.assertFalse(shadow(code,r,lambda a:pages[a],seconds=0)['supported'])

    def test_missing_native_output_is_refused(self):
        r,code=complete();r['post'].pop()
        with self.assertRaises(ValueError):replay(code,r)

    def test_register_damage_fails(self):
        r,code=complete();r['post_regs']['esp']+=4
        self.assertFalse(replay(code,r)['passed'])

    def test_native_producer_order_is_not_substituted(self):
        r,code=complete();r['native_calls'].append(0x4216f0)
        self.assertFalse(replay(code,r)['passed'])

    def test_entry_role_and_boundary(self):
        for k,v in (('kind','human'),('actor_id',0),('entry',0x462430)):
            r,_,_=case();r[k]=v
            with self.assertRaises(ValueError):validate_entry(r)

    def test_missing_register_and_fpu_fail(self):
        r,_,_=case();del r['regs']['ebx']
        with self.assertRaises(ValueError):validate_entry(r)
        r,_,_=case();r['fxsave']='00'
        with self.assertRaises(ValueError):validate_entry(r)

    def test_writer_sequence_and_summary(self):
        r,_=complete()
        with tempfile.TemporaryDirectory(prefix='nfsu-ai-tests-') as temp:
            w=host.Writer(Path(temp));w.batch({'batch':0,'records':[r]})
            self.assertEqual(w.summary_extra['native_reproduced'],1)
            with self.assertRaises(ValueError):w.batch({'batch':1,'records':[r]})
            w.close()

    def test_atomic_byte_ceiling(self):
        r,_=complete()
        with tempfile.TemporaryDirectory(prefix='nfsu-ai-tests-') as temp:
            w=host.Writer(Path(temp))
            with mock.patch.object(host,'MAX_BYTES',1),self.assertRaises(ValueError):
                w.batch({'batch':0,'records':[r]})
            self.assertEqual(w.steps,0);self.assertEqual(w.path.stat().st_size,0);w.close()


if __name__=='__main__':unittest.main()

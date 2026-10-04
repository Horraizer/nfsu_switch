"""Extend pinned original-byte contact fixtures with independent body data.

Requires the exact PR308 six tools extracted to --reference-tools. No live
capture, retail hull, admission, scheduler or final kinematic claim is made.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

# Imports must never create caches in a read-only reference/dependency tree.
sys.dont_write_bytecode = True

PIN = '988bc892f643c463ba02a8c83e33b48918f889c3'
TOOLS = ('contact256_trace.py','contact256_native.py','contact256_test.py',
         'physics256_native.py','world_body_trace.py','collision256_trace.py')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference-tools',type=Path,required=True)
    p.add_argument('--root',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    args=p.parse_args()
    for name in TOOLS:
        original=subprocess.check_output(['git','show',PIN+':tools/'+name])
        if (args.reference_tools/name).read_bytes()!=original:
            raise ValueError('reference tool mismatch: '+name)
    sys.path.insert(0,str(args.reference_tools.resolve()))
    from world_body_trace import Image, INPUTS, digest_file, manifest
    from collision256_trace import output_path
    from contact256_trace import source,execution_source
    import contact256_native as native
    import capstone,unicorn
    if (capstone.__version__,unicorn.__version__)!=('5.0.7','2.1.4'):
        raise ValueError('pinned dependency versions required')
    root=args.root.resolve()
    for path in [root]+list(root.rglob('*')):
        if path.is_symlink() or getattr(path.lstat(),'st_file_attributes',0)&0x400:
            raise ValueError('retail reparse point unsupported')
    out=output_path(Path.cwd().resolve(),root,args.out)
    before=manifest(root)
    out.mkdir(parents=True,exist_ok=False)
    (out/'before.json').write_bytes(before)
    try:
        size,sha=INPUTS['speed.exe']
        if (root/'speed.exe').stat().st_size!=size or digest_file(root/'speed.exe')!=sha:
            raise ValueError('unsupported executable')
        image=Image((root/'speed.exe').read_bytes())
        facts=source(image)
        result=native.suite(image)
        fixture=native.fixture
        def independent(*a,**kw):
            m=fixture(*a,**kw)
            m.put(native.RIGID_B+0x148,0.25,'f')
            for rigid,diag,spin in ((native.RIGID_A,(.7,.3,.9),(.1,.2,.3)),
                                    (native.RIGID_B,(.2,.8,.5),(-.2,.1,-.1))):
                for index,value in zip((0,5,10),diag):
                    m.put(rigid+0x150+index*4,value,'f')
                for index,value in zip((1,4,2,8,6,9),(.04,.04,.08,.08,.03,.03)):
                    m.put(rigid+0x150+index*4,value,'f')
                native.vector(m,rigid+0xC0,spin)
            m.put(native.CONFIG+0x12D0,75)
            return m
        native.fixture=independent
        for name,off,va,vb in (
            ('independent_head',(2.0005,0,0),(10,0,0),(-10,0,0)),
            ('independent_offset',(2.0005,.7,0),(10,0,0),(-10,0,0)),
            ('independent_glancing',(2.0005,.7,.2),(3,4,0),(-2,-1,0)),
            ('independent_separating',(2.0005,0,0),(-10,0,0),(10,0,0))):
            result['cases'].append(native.contact_case(image,name,off,va,vb))
        native.fixture=fixture
        # Exercise the previously unvisited high linear-impulse clamp branch.
        result['cases'].append(native.contact_case(image,'large_already_state1',
            (2.0005,.7,.2),(5000,0,0),(-5000,0,0),state=1))
        last=[]
        def seeded(*a,**kw):
            m=fixture(*a,**kw)
            m.put(native.CONFIG+0x12D0,75)
            last[:]=[m]
            return m
        native.fixture=seeded
        hard=native.contact_case(image,'hard_seed75',(2.0005,.7,.2),(50,0,0),(-50,0,0))
        hard['world_seed_before']=75
        hard['world_seed_after']=last[0].get(native.CONFIG+0x12D0)
        result['cases'].append(hard)
        native.fixture=fixture
        source_text,blocks=execution_source(image,result)
        report=dict(schema='contact256-v1',input=dict(size=size,sha256=sha),
            source=facts,native=result,executed_blocks=blocks,
            evidence='original-x86-emulation-synthetic-memory',runtime_capture=None,
            final_published_kinematics=None,cadence=None,source_sha256=hashlib.sha256(
                source_text.encode('ascii')).hexdigest().upper(),
            reference_tools={name:digest_file(args.reference_tools/name) for name in TOOLS},
            worker_tool_hashes={name:digest_file(Path(__file__).with_name(name))
                for name in ('contact257_trace.py','contact257_pins.py')})
    finally:
        after=manifest(root)
        (out/'after.json').write_bytes(after)
        if before!=after: raise RuntimeError('RETAIL MANIFEST CHANGED')
    (out/'source.txt').write_text(source_text,encoding='ascii')
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=True,allow_nan=False,indent=2)+'\n',encoding='ascii')
    print('PASS: extended independent mass/inertia/spin and clamp original execution')
    print('PASS: retail manifests unchanged; original instructions authenticated')
    print('UNAVAILABLE: real hulls, admission, cadence and final publication')


if __name__=='__main__':
    main()

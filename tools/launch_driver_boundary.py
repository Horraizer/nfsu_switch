"""Execute the authenticated PC throttle callback, including retained history.

This isolates byte/event/gear semantics. It does not emulate the platform
joystick producer and must not turn a captured pressure into a game constant.
"""
import argparse
import hashlib
import json
from pathlib import Path

from launch_traction_boundary import PC_HASH, authenticated
from physics256_native import ACTOR, DRIVER, Machine
from world_body_trace import Image


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail',type=Path,required=True)
    parser.add_argument('--out',type=Path,default=Path('out/launch-driver-boundary'))
    args=parser.parse_args()
    repo=Path(__file__).resolve().parents[1]
    output=args.out.resolve()
    retail=args.retail.resolve()
    try:
        relative=output.relative_to(repo)
    except ValueError:
        parser.error('output must be inside this checkout')
    if (not relative.parts or not all(p.isascii() for p in relative.parts) or
            output==retail or retail in output.parents):
        parser.error('ASCII output outside the retail root required')
    if output.exists():
        parser.error('fresh output directory required')
    source=args.retail/'speed.exe'
    data=authenticated(source,PC_HASH)
    machine=Machine(Image(data))
    machine.allowed=[(0x4618B0,0x4619B9),(0x454F30,0x454FE6),(0x674898,0x67490D)]
    machine.put(DRIVER+0x26C,ACTOR)
    rows=[]
    cases=[(1,0x89,255),(1,0x89,0),(1,0x89,253),(2,None,None),
           (2,0x89,253),(2,0x89,0),(2,0x8B,253),(1,0x8B,253)]
    for gear,event,value in cases:
        machine.put(ACTOR+0x3D8,gear)
        if event is not None:
            machine.run(0x4618B0,(0,event,value,DRIVER))
        rows.append(dict(gear=gear,event=event,value=value,
                         retained_bytes=[machine.get(DRIVER+x,'B') for x in (0x290,0x291,0x292)],
                         output=machine.get(DRIVER+0x294,'f'),output_bits=machine.get(DRIVER+0x294)))
    if machine.events:
        raise AssertionError('semantic hook used')
    expected=[0x3F800000,0,0x3F7A9E78,0x3F7A9E78,0x3F7DFDFF,0,0x3F800000,0x3F7A9E78]
    if [r['output_bits'] for r in rows]!=expected:
        raise AssertionError('original throttle event/history result changed')
    if hashlib.sha256(source.read_bytes()).hexdigest()!=PC_HASH:
        raise AssertionError('retail input changed')
    output.mkdir(parents=True,exist_ok=False)
    result=dict(source_sha256=PC_HASH,callback='004618B0',hooks=[],rows=rows,scope=__doc__)
    (output/'report.json').write_bytes((json.dumps(result,indent=2)+'\n').encode('ascii'))
    print('PASS: eight original throttle events/history boundaries; no semantic hooks')


if __name__=='__main__':
    main()

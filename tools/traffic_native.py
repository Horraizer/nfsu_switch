"""Read-only PC1.4USA traffic primitive witnesses, not full-world emulation.

Usage: python tools/traffic_native.py RETAIL_ROOT OUTPUT.json
Requires Unicorn and Capstone through the existing bounded native helpers.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
from hud254_trace import PE
from audio312_native import Machine, x
from controller293_native import fixture, controls
from physics256_native import ACTOR, DRIVER, RIDE, bits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("retail", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    blob = (args.retail / "speed.exe").read_bytes()
    digest = hashlib.sha256(blob).hexdigest()
    if digest != "c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7":
        raise ValueError("unsupported executable identity")
    if args.output.exists():
        raise ValueError("refusing to overwrite an earlier witness")
    pe = PE(blob)
    mover_rows = []
    for heading, speed, steering, pedals in itertools.product(
            [0., 1.2, -2.4], [0., 12., 24.5], [0, 1000, 64536], [(1., 0.), (0., 1.)]):
        case = [31., -20., heading, speed, 1.25, -1.4, *pedals, 1/60, steering]
        machine = Machine(pe, [(0x5754B0, 0x5756E8), (0x564B10, 0x564BA6),
                              (0x674898, 0x67490D), (0x583EA0, 0x583F18), (0x4387E0, 0x4387F6)])
        mover, actor, record, driver = 0x1100000, 0x1101000, 0x1102000, 0x1103000
        machine.wi(mover+8, actor)
        machine.wi(mover+4, driver)
        machine.wi(actor+0x20, record)
        machine.wi(actor+0x30, driver)
        for address, value in zip([mover+12, mover+16, mover+20, mover+24,
                                   record+0x10, record+0xA0, driver+0x1DC, driver+0x1E4], case[:8]):
            machine.wf(address, value)
        machine.wi(driver+0x1D0, steering)
        machine.callbacks[0x5756E8] = lambda current: current.finish()
        machine.run(0x5754B0, (bits(1/60),), {x.UC_X86_REG_ECX: mover})
        mover_rows.append({"input": case, "native": [machine.rf(mover+a) for a in [12, 16, 20, 24]]})
    driver_rows = []
    for speed, target, lateral in [(0, 15, 0), (12, 20, 2), (12, 20, -2), (20, 5, 0), (12, 0, 0)]:
        machine = fixture(pe, [(10., lateral, 0.), (100., lateral*10, 0.)],
                          speed=speed, target=target, category=2)
        machine.put(RIDE+0x270, 1., "f")
        machine.put(ACTOR+0x24, 4)
        machine.put(ACTOR+0x28, 2)
        machine.put(0x1152004, 3)
        rows = []
        for _ in range(12):
            machine.run(0x45E970, (bits(1/60),), ecx=DRIVER)
            rows.append(controls(machine))
        driver_rows.append({"input": [speed, target, lateral, 12], "rows": rows})
    prediction_rows=[]
    for velocity, heading in [((0,0,0),0),((0,0,0),16000),((0,0,0),45000),
                             ((1,0,0),12000),((1.0001,0,0),12000),((12,3,2),2000),((-20,4,0),30000)]:
        m=Machine(pe,[(0x456DE0,0x456E8A),(0x458F10,0x459096),(0x4594D0,0x4596B0),
                      (0x564BD0,0x564CD0),(0x567370,0x5673E0)])
        position, vector, path=0x1100000,0x1101000,0x1102000
        for k,value in enumerate((10.,20.,3.)):m.wf(position+4*k,value)
        for k,value in enumerate(velocity):m.wf(vector+4*k,value)
        length=max(50.,sum(v*v for v in velocity)**.5*4)
        m.run(0x456DE0,(bits(length),heading),{x.UC_X86_REG_EAX:position,x.UC_X86_REG_EDX:vector,x.UC_X86_REG_ECX:path})
        prediction_rows.append(dict(velocity=velocity,heading=heading,length=length,count=m.ri(path)&255,
                                    point=[m.rf(path+16+4*k) for k in range(3)]))
    policy_rows=[]
    for racer_type,traffic_type in [(2,7),(7,2),(3,2),(2,5),(4,7)]:
        m=Machine(pe,[(0x583BB0,0x583BF6)])
        event=0x1100000;m.uc.mem_write(event+11,bytes((racer_type,traffic_type)))
        m.run(0x583BB0,(event,))
        policy_rows.append(dict(racer=racer_type,traffic=traffic_type,take=m.uc.reg_read(x.UC_X86_REG_EAX)&255))
    report = {"executable_sha256": digest, "mover": mover_rows, "driver": driver_rows, "prediction": prediction_rows, "road_policy": policy_rows,
              "limits": "Supplied actors, selected record, paths and controls. No original live spawn, road owner, collision or full trajectory parity."}
    args.output.write_text(json.dumps(report, indent=2)+"\n", encoding="ascii")
    print("PASS:54 mover witnesses,60 driver snapshots; bounded primitive execution only")


if __name__ == "__main__":
    main()

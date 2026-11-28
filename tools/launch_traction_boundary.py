"""Compare supplied launch histories in production, PC 1.4 and optional Xbox code.

This is a boundary diagnostic, not a whole-race parity test. The captures are
post-frame observations; intervening Move/contact calls are not reconstructed.
Retail inputs are authenticated and read only. No emulator/game is launched.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess

from physics256_native import ACTOR, PHYSICS, RIDE, STATUS, STOP, Machine
from world_body_trace import Image
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_FPCW


PC_HASH = "c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7"
XBOX_HASH = "cdd562ffcc70906b3fecd841db4dc10fd463664b05e03f896a8fb38b9a7d878a"


def authenticated(path, expected):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError("unexpected retail identity: " + path.name)
    return data


class XboxMachine(Machine):
    def __init__(self, data):
        if data[:4] != b"XBEH":
            raise ValueError("expected XBE")
        base = struct.unpack_from("<I", data, 0x104)[0]
        count, table = struct.unpack_from("<II", data, 0x11C)
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(0x10000, 0x400000)
        self.uc.mem_map(STOP, 0x200000)
        for index in range(count):
            _, address, _, raw, size = struct.unpack_from("<5I", data, table-base+index*56)
            self.uc.mem_write(address, data[raw:raw+size])
        self.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
        self.hooks, self.events, self.allowed = {}, [], []
        self.visited = set()
        self.uc.hook_add(UC_HOOK_CODE, self.instruction)


def native(image, loads, slips, drive, category, xbox=False, policy=None,
           speed=1.0, interval=1.0/60, grip=None):
    machine = XboxMachine(image) if xbox else Machine(image)
    machine.allowed = ([(0x7BC50, 0x7C1F7), (0x34640, 0x34681),
                        (0x13CA40, 0x13CA5B), (0x569C0, 0x56BC0),
                        (0x13EA30, 0x13EA55), (0x1B2344, 0x1B23C0),
                        (0x6A0C0, 0x6A0DF)] if xbox else
                       [(0x479640, 0x479C30), (0x4280A0, 0x4280E3),
                        (0x454F30, 0x454FE4), (0x674898, 0x67490D)])
    owner, setup, gearbox = ACTOR+0x40, 0x1110000, 0x1111000
    machine.put(PHYSICS+0x5C, ACTOR)
    machine.put(ACTOR+0x24, 2)
    machine.put(ACTOR+0x3D0, speed, "f")
    machine.put(ACTOR+0x10, RIDE)
    machine.put(RIDE+0x424, category, "B")
    machine.put(PHYSICS+0x58, owner)
    machine.put(PHYSICS+0x54, setup)
    machine.put(setup+0x1C0, drive, "f")
    machine.put(PHYSICS+0x4C, gearbox)
    machine.put(0x273868 if xbox else 0x73619C, STATUS)
    machine.put(STATUS, 4)
    machine.put(0x2CA43C if xbox else 0x73457C, interval, "f")
    policy = policy or [0, 0, 0, 0]
    machine.put(owner+0x3C8, policy[0])
    for offset, value in zip((0x3CC, 0x3D0, 0x3D4), policy[1:]):
        machine.put(owner+offset, value, "f")
    grip = grip or [.85]*4
    for wheel in range(4):
        address = 0x1112000+wheel*0x1000
        machine.put(PHYSICS+0x38+wheel*4, address)
        for offset, value in ((0x158, loads[wheel]), (0x160, slips[wheel]), (0x178, grip[wheel])):
            machine.put(address+offset, value, "f")
    if xbox:
        machine.run(0x7BC50, ecx=PHYSICS)
    else:
        machine.run(0x479640, (PHYSICS,))
    if machine.events:
        raise AssertionError("semantic hook used")
    return [machine.get(owner+0x3C8), *[machine.get(owner+offset, "f")
            for offset in (0x3CC, 0x3D0, 0x3D4)],
            *[machine.get(0x1112000+wheel*0x1000+0x178, "f") for wheel in range(4)]]


HOST = r'''#include "nfsu/player_powertrain.h"
#include <cstdio>
int main() {
    float drive,speed,interval; unsigned category;
    std::array<float,4> loads{},slips{},grip{};
    nfsu::PlayerOwnerTraction state{0,0,0,0};
    while(std::scanf("%f %u %d %f %f %f %f %f",&drive,&category,&state.mode,
                    &state.timer,&state.scale,&state.quiet,&speed,&interval)==8) {
        for(auto& v:loads)if(std::scanf("%f",&v)!=1)return 2;
        for(auto& v:slips)if(std::scanf("%f",&v)!=1)return 2;
        for(auto& v:grip)if(std::scanf("%f",&v)!=1)return 2;
        const auto factor=nfsu::player_owner_traction_update(state,drive,category,
            true,true,speed,0,interval,loads,slips);
        std::printf("%d %.9g %.9g %.9g",state.mode,state.timer,state.scale,state.quiet);
        for(unsigned k=0;k<4;++k)std::printf(" %.9g",grip[k]*factor[k]);
        std::puts("");
    }
    return std::feof(stdin)?0:2;
}
'''


def host(repo, build, out, inputs):
    cache = (build/"CMakeCache.txt").read_text("utf8").splitlines()
    compiler = Path(next(line.split("=", 1)[1] for line in cache
                         if line.startswith("CMAKE_CXX_COMPILER:FILEPATH=")))
    source, executable = out/"boundary.cpp", out/"boundary.exe"
    source.write_bytes(HOST.encode("ascii"))
    env = dict(os.environ, PATH=str(compiler.parent)+os.pathsep+os.environ["PATH"])
    command = [compiler, "-std=c++17", "-O2", "-ffp-contract=off", "-I", repo/"include",
               source, "-o", executable, "-Wl,--start-group", "libnfsu_vehicle.a",
               "libnfsu_sha256.a", "libnfsu_drift.a", "-Wl,--end-group"]
    subprocess.run(command, cwd=build, env=env, check=True, capture_output=True)
    tape = "".join(" ".join(map(str, row))+"\n" for row in inputs)
    result = subprocess.run([executable], input=tape, text=True, encoding="ascii",
                            env=env, capture_output=True, check=True)
    rows = [list(map(float, line.split())) for line in result.stdout.splitlines()]
    if len(rows) != len(inputs) or any(len(row) != 8 for row in rows) or result.stderr:
        raise AssertionError("incomplete production output")
    return rows


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def capture(path, actor):
    rows = []
    for line in (path/"decisions-extra.jsonl").read_bytes().splitlines():
        tick, _, parts = json.loads(line)
        part = next((part for part in parts if part[0] == actor), None)
        if part is None:
            continue
        # The two front wheels have the same base multiplier. A diagonal
        # difference identifies a policy observation, not its call boundary.
        if rows and part[-1][0][1][0] != part[-1][1][1][0]:
            previous_tick, previous = rows[-1]
            loads = [wheel[1][3] for wheel in previous[-1]]
            slips = [wheel[1][6] for wheel in previous[-1]]
            drive = previous[3][4]
            return previous_tick, tick, drive, loads, slips, [wheel[1][0] for wheel in part[-1][:2]]
        rows.append((tick, part))
    raise ValueError("no diagonal policy observation in " + path.name)


def calls(path, limit):
    data = (path/"burnout.jsonl").read_bytes()
    lines = data.splitlines()
    if data and not data.endswith(b"\n"):
        lines = lines[:-1]
    pending = []
    rows = []
    for line in lines:
        row = json.loads(line)
        if row[2] != "call" or row[0] is None or row[0] > limit:
            continue
        if row[3] == 1:
            pending.append(row)
        elif pending:
            before = pending.pop()
            # The LR-store watch sees caller r1; the return read sees the
            # 0x38-byte CalcBurnout frame before its epilogue restores r1.
            if before[5] != row[5]+0x38 or before[0] != row[0]:
                raise ValueError("unpaired CalcBurnout boundary")
            # Without velocity/heading a nonzero mode at speed>=1 has an
            # unobserved yaw-cancellation input. Do not manufacture it.
            if before[18][0] != 0 and before[16] >= 1:
                continue
            rows.append((before, row))
    if not rows:
        raise ValueError("no complete supported calls in " + path.name)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--xbox", type=Path)
    parser.add_argument("--captures", type=Path)
    parser.add_argument("--capture", action="append", default=[])
    parser.add_argument("--cases", type=Path,
                        help="recheck frozen actual_calls from a committed evidence JSON")
    parser.add_argument("--actor", type=int, default=0)
    parser.add_argument("--category", type=int, choices=range(4))
    parser.add_argument("--calls", action="store_true",
                        help="read paired actual CalcBurnout entry/return records")
    parser.add_argument("--until", type=int, default=1500)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--build", type=Path, default=Path("build-release"))
    parser.add_argument("--out", type=Path, default=Path("out/launch-traction-boundary"))
    args = parser.parse_args()
    if args.cases is None and (args.captures is None or not args.capture or args.category is None):
        parser.error("supply --cases, or --captures with --capture and --category")
    if args.cases is not None and not args.calls:
        parser.error("frozen actual call cases require --calls")
    repo = Path(__file__).resolve().parents[1]
    build, out = args.build.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    image = Image(authenticated(args.retail/"speed.exe", PC_HASH))
    xbox = authenticated(args.xbox, XBOX_HASH) if args.xbox else None
    cases, inputs = [], []
    if args.cases is not None:
        frozen = json.loads(args.cases.read_bytes())
        cases = frozen["actual_calls"] if isinstance(frozen, dict) else frozen
        if not cases:
            raise ValueError("empty frozen cases")
        for case in cases:
            inputs.append([case["drive"], case["category"], *case["policy"],
                case["speed"], case["interval"], *case["loads"], *case["slips"], *case["initial_grip"]])
    for name in args.capture:
        path = args.captures/name
        if args.calls:
            for before, after in calls(path, args.until):
                loads = [wheel[0] for wheel in before[19]]
                slips = [wheel[1] for wheel in before[19]]
                grip = [wheel[3] for wheel in before[19]]
                state = before[18]
                values = [before[17], args.category, *state, before[16], before[11], *loads, *slips, *grip]
                if not all(math.isfinite(v) for v in values):
                    raise ValueError("non-finite call input")
                inputs.append(values)
                cases.append(dict(capture=name, before_tick=before[0], after_tick=after[0],
                    drive=before[17], category=args.category, loads=loads, slips=slips, policy=state,
                    speed=before[16], interval=before[11], initial_grip=grip,
                    contacts=before[20], mover_interval=before[10],
                    observed_state_and_grip=[*after[18], *[wheel[3] for wheel in after[19]]],
                    capture_sha256=hashlib.sha256((path/"burnout.jsonl").read_bytes()).hexdigest()))
            continue
        before, after, drive, loads, slips, grip = capture(path, args.actor)
        values = [drive, args.category, 0, 0, 0, 0, 1, 1./60, *loads, *slips, *[.85]*4]
        if not all(math.isfinite(v) for v in values):
            raise ValueError("non-finite capture input")
        inputs.append(values)
        cases.append(dict(capture=name, before_tick=before, after_tick=after, drive=drive, category=args.category,
                          loads=loads, slips=slips, observed_front_grip=grip,
                          capture_sha256=hashlib.sha256((path/"decisions-extra.jsonl").read_bytes()).hexdigest()))
    production = host(repo, build, out, inputs)
    for case, port in zip(cases, production):
        extra = dict(policy=case.get("policy"), speed=case.get("speed", 1),
                     interval=case.get("interval", 1./60), grip=case.get("initial_grip"))
        category = case.get("category", args.category)
        if category not in range(4):
            raise ValueError("invalid performance class")
        pc = native(image, case["loads"], case["slips"], case["drive"], category, **extra)
        if [bits(v) for v in port] != [bits(v) for v in pc]:
            raise AssertionError("production differs from PC at supplied boundary: " + case["capture"])
        xb = native(xbox, case["loads"], case["slips"], case["drive"], category, True, **extra) if xbox else None
        if xb is not None and xb != pc:
            raise AssertionError("Xbox differs from PC at supplied boundary: " + case["capture"])
        expected = case.get("observed_state_and_grip", case.get("observed_front_grip"))
        compared = pc if args.calls else pc[4:6]
        ulps = [abs(bits(a)-bits(b)) for a, b in zip(compared, expected)]
        case.update(pc=pc, xbox=xb, port=port, observed_front_ulp_delta=ulps,
                    observation="compatible" if max(ulps)<=2 else "missing intervening history")
        if args.calls and max(ulps)>2:
            raise AssertionError("actual call return differs: " + case["capture"] + " at " + str(case["before_tick"]))
        if not args.quiet:
            print(json.dumps(case, separators=(",", ":")))
    (out/"results.json").write_bytes(json.dumps(cases, indent=2).encode("ascii"))
    print("Supplied-input agreement checked; whole-race parity and unobserved call history remain separate.")
    print("boundaries", len(cases), "compatible", sum(c["observation"]=="compatible" for c in cases))


if __name__ == "__main__":
    main()

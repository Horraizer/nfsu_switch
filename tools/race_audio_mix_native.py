"""Readonly original race mix and output-consumer differential witness.

Runs original initialization, request/clear, curves and final gain consumers.
Listener0, absent foreground state and absent audio device are supplied inputs.
The stream constructor stops before allocation; no external calls are hooked.
Owner0/categories7/9/10, pre-bank collision gain order and global music1
are asserted by this fixture. Unity spatial gain is a supplied input; natural
contact admission and runtime spatial/global gain are separate boundaries.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from world_body_trace import Image
from physics256_native import Machine
from unicorn.x86_const import UC_X86_REG_EIP


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("retail_root", type=Path)
    parser.add_argument("--write-fixture", action="store_true")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    raw = (args.retail_root / "speed.exe").read_bytes()
    assert hashlib.sha256(raw).hexdigest() == "c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7"
    m = Machine(Image(raw))
    m.uc.mem_map(0, 0x1000)
    # Pure original code, including its CRT float conversion and constructors.
    # Each operation has a declared return/boundary and instruction budget.
    m.allowed = [(0x400000, 0x6a0000)]
    settings, parent, listener, stream, voice, gain = (0x1001000, 0x1006000,
        0x1007000, 0x1008000, 0x1009000, 0x100a100)
    m.run(0x5364e0, (settings,))
    m.put(0x7356a8, parent)
    m.put(0x7356ac, settings)
    m.put(parent + 0xa0, listener)
    m.put(listener + 8, 0)  # Listener index, not elapsed time.
    m.put(0x7356b0, 0)
    m.put(0x7361f0, 0)
    m.run(0x539210, (0, 0, 0, 0, 0, 0, 0), end=0x539313, ecx=stream + 0x60)
    assert m.uc.reg_read(UC_X86_REG_EIP) == 0x539313
    assert m.get(stream + 0x278 + 0x2c) == 32767
    m.put(stream + 0x50, voice)
    m.put(stream + 0x50 + 0x2d0, stream + 0x278)
    m.put(voice, 0)  # Real consumer returns without an audio device call.
    collision_targets=[m.get(0x743ca8+i*0x6c+12+7*4)for i in range(12)]
    collision,node=0x100a200,0x100a300
    m.put(collision+0x50,32768);m.put(collision+0x32c,0)
    profiles = []
    for i in range(12):
        at = 0x743ca8 + i * 0x6c
        profiles.append([m.get(at), m.get(at + 4, 'f'), m.get(at + 8, 'f'),
            m.get(at + 12 + 10 * 4), m.get(at + 12 + 12 * 4)])
    phases = [(1, 3, 60, 1/60), (1, 5, 60, 1/120), (1, 7, 20, 1/60),
        (2, 5, 1, 0), (2, 7, 120, 1/60), (1, 5, 6, 1/60),
        (1, 7, 1, 1/60), (2, 7, 3, 1/60), (2, 5, 120, 1/60),
        (1, 3, 10, 0), (1, 5, 4, 1/30), (2, 5, 120, 1/60)]
    rows = []
    for operation, profile, count, dt in phases:
        if operation == 1:
            m.run(0x5368b0, (settings, profile, 0xbf800000, 0), edx=0)
        else:
            m.run(0x536a70, (settings, profile, 0))
        dt_bits = struct.unpack('<I', struct.pack('<f', dt))[0]
        for step in range(count):
            m.run(0x536800, (settings, dt_bits))
            m.run(0x53ca50, ecx=stream)
            m.put(gain, 32767)
            m.run(0x536c20, (settings,), eax=0, esi=10, ecx=gain)
            engine_gain=m.get(gain)
            m.put(gain,20000);m.run(0x530740,eax=collision,esi=gain);configured=m.get(gain)
            m.put(node,1./32768,'f');m.put(node+4,configured);m.put(node+8,32767)
            m.run(0x64d0b0,(node,));collision_gain=m.reg('eax')
            rows.append([operation if step == 0 else 0, profile, dt_bits,
                m.get(settings + 0x35cc), m.get(settings + 10 * 0x130 + 8),
                m.get(voice + 0xc), engine_gain, m.get(settings + 0x3474),
                m.get(settings + 9 * 0x130 + 8),m.get(settings + 1 * 0x130 + 8),m.get(settings + 3 * 0x130 + 8),m.get(settings + 5 * 0x130 + 8),m.get(settings+0x858),configured,collision_gain])
    assert not m.events
    fixture = ("// Original5364E0,5368B0,536A70,536800,536C20 and53CA50.\n"
        "// Supplied ordinary listener0 and absent output device; no audio IO.\n"
        "static constexpr unsigned kNativeRaceAudioMix[][15] = {\n")
    fixture += ''.join("    {%s},\n" % ','.join(map(str, row)) for row in rows)
    fixture += "};\nstatic constexpr unsigned kNativeCollisionCategoryTargets[]={"+','.join(map(str,collision_targets))+"};\n"
    path = Path(__file__).resolve().parents[1] / "tests/race_audio_mix_native.inc"
    if args.write_fixture:
        path.write_bytes(fixture.encode('ascii'))
    else:
        assert path.read_text(encoding='ascii') == fixture, "native fixture differs"
    if args.out:
        args.out.mkdir(exist_ok=False)
        (args.out / 'oracle.json').write_text(json.dumps(dict(profiles=profiles,
            rows=rows, hooks=m.events, stream_descriptor_default=32767,
            original_instruction_addresses=len(m.visited)), indent=2) + '\n', encoding='ascii')
    print("PASS original race mix: consecutive profiles/curves/category/music; no audio IO or external call hooks")


if __name__ == "__main__":
    main()

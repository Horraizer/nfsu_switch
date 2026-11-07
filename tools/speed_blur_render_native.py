"""Read-only original filter geometry witness; no original GPU execution claim."""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from world_body_trace import Image
from hud254_native import Machine
from unicorn.x86_const import UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--retail-root", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
if args.output.exists():
    raise RuntimeError("Refusing stale output")
raw = (args.retail_root / "speed.exe").read_bytes()
shader_raw = (args.retail_root / "fx/filter.efx").read_bytes()
exe_sha = hashlib.sha256(raw).hexdigest()
shader_sha = hashlib.sha256(shader_raw).hexdigest()
assert exe_sha == "c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7"
assert shader_sha == "fd731c437b4107c00fde2923d69d42d444190335bab7a8f20f2ee4c9b7263988"
shader = bytes(v ^ 0xAA for v in shader_raw).decode("ascii", errors="backslashreplace")
for text in ("OUT.position =p;", "MINFILTER = LINEAR;", "MAGFILTER = LINEAR;", "AddressU = CLAMP;", "AddressV = CLAMP;", "pixel_shader4()"):
    assert text in shader, text
m = Machine(Image(raw))
u = m.uc
rows = []
for offsets in ((0, 0, 0, 0), (.0073125, .0073125, -.0073125, -.0073125),
                (.02, -.03, .04, .05), (-.02, .03, -.04, -.05)):
    for multiplier in (1, 4):
        values = [struct.unpack("<f", struct.pack("<f", x * multiplier))[0] for x in offsets]
        u.mem_write(0x1001000, struct.pack("<4f", *values))
        u.mem_write(m.stack, bytes(0x150))
        u.reg_write(UC_X86_REG_ESP, m.stack)
        u.reg_write(UC_X86_REG_EDI, 0x1001000)
        u.reg_write(UC_X86_REG_EBP, 0)
        # Start after GetDesc/SetRenderTarget; stop before activating D3D effect.
        # The original vertex/UV generation executes without replaced arithmetic.
        u.emu_start(0x40D0C5, 0x40D1AF, count=10000)
        assert u.reg_read(UC_X86_REG_EIP) == 0x40D1AF
        vertices = []
        for index, (x, y, cx, cy) in enumerate(((-1, 1, 0, 0), (1, 1, 1, 0), (1, -1, 1, 1), (-1, -1, 0, 1))):
            v = struct.unpack("<3fI8f", u.mem_read(m.stack + 0x2C + index * 48, 48))
            assert v[:3] == (x, y, 0) and v[3] == 0xFFFFFFFF
            for tap in range(4):
                expected = (cx - tap * values[2 if cx else 0], cy - tap * values[3 if cy else 1])
                assert all(abs(a-b) < 1e-7 for a, b in zip(v[4 + tap*2:6 + tap*2], expected))
            vertices.append(list(v))
        rows.append(dict(offsets=values, vertices=vertices))
# D3D9 viewport maps clip (-1,+1) to integer pixel center (0,0).
# Maxwell gl_FragCoord is half-integer, so each filter pass must subtract 0.5.
source = Path(__file__).resolve().parent.parent
for name in ("speed_blur.frag.glsl", "speed_blur_reduce.frag.glsl", "speed_blur_copy.frag.glsl"):
    text = (source / "switch/postprocess_shaders" / name).read_text(encoding="ascii")
    assert "(gl_FragCoord.xy-vec2(0.5))*params.xy" in text, name
assert hashlib.sha256((args.retail_root / "speed.exe").read_bytes()).hexdigest() == exe_sha
assert hashlib.sha256((args.retail_root / "fx/filter.efx").read_bytes()).hexdigest() == shader_sha
result = dict(status="PASS", executable_sha256=exe_sha, filter_sha256=shader_sha, cases=rows,
              scope="Original x86 radial vertices and authenticated effect contract; D3D calls excluded. Pixel-origin conversion checked in Switch sources. Not hardware image parity.")
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="ascii")
print("PASS original filter contract, %d native vertex cases, D3D9 pixel-origin conversion" % len(rows))

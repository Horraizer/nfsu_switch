"""Execute original FEng Race Over interpolation and quad consumers."""

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

WORK = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(WORK / "tools"))
from world_body_trace import Image
from hud253_trace import jdlz
from hud254_trace import chunks, feng_objects, resources
from hud254_native import Machine
from hud254_presentation_native import Renderer


def tracks(package, obj):
    start = obj["offset"]
    end = start + 8 + struct.unpack_from("<I", package, start + 4)[0]
    result = []
    for kind, begin, finish in chunks(package[start + 8:end]):
        if kind != int.from_bytes(b"Scrp", "little"):
            continue
        payload = package[start + 8 + begin:start + 8 + finish]
        cursor = 0
        state = ""
        channels = []
        while cursor < len(payload):
            tag = payload[cursor:cursor + 2]
            length = struct.unpack_from("<H", payload, cursor + 2)[0]
            value = bytes(payload[cursor + 4:cursor + 4 + length])
            if tag == b"Sn":
                state = value.split(b"\0")[0].decode("ascii")
            elif tag == b"FI":
                channels.append({"fi": value})
            elif tag == b"To":
                channels[-1]["target"] = struct.unpack("<I", value)[0]
            elif tag == b"Kd":
                channels[-1]["kd"] = value
            cursor += 4 + length
        if state.lower() == "animate":
            result.extend(channels)
    return result


def sample(image, channel, tick):
    fi, kd = channel["fi"], channel["kd"]
    width = fi[1]
    stride = width + 4
    assert width in (12, 16) and len(kd) % stride == 0
    machine = Machine(image)
    frame, output = 0x1001000, 0x1002000
    nodes = [0x1003000 + 64 * i for i in range(len(kd) // stride - 1)]
    if not nodes:
        return kd[4:stride]
    machine.uc.mem_write(frame, fi)
    machine.uc.mem_write(frame + 0x18, kd[4:stride])
    machine.write(frame + 0x30, nodes[0])
    machine.write(frame + 0x34, nodes[-1])
    for i, node in enumerate(nodes):
        machine.write(node + 4, nodes[i + 1] if i + 1 < len(nodes) else 0)
        machine.write(node + 8, nodes[i - 1] if i else 0)
        machine.uc.mem_write(node + 0xC, kd[(i + 1) * stride:(i + 2) * stride])
    machine.run(0x4FC100, (frame, tick, output))
    return bytes(machine.uc.mem_read(output, width))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--write-fixture',action='store_true')
    args=parser.parse_args()
    root=args.root
    out=args.out.resolve()
    assert out.relative_to(Path.cwd()).as_posix().isascii()
    assert out.relative_to(Path.cwd()).parts[0].startswith('build')
    out.mkdir(parents=True,exist_ok=False)
    raw = (root / "speed.exe").read_bytes()
    assert hashlib.sha256(raw).hexdigest() == "c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7"
    image = Image(raw)
    bun = (root / "Global/INGAMEB.BUN").read_bytes()
    package = jdlz(bun[0x16F40C:])
    assert hashlib.sha256(package).hexdigest() == "652dd66715cc8eff180a64c59377098385b3896b584a1430c014d7476dea2e63"
    table = resources(package)
    requests = next(child["records"] for child in table[0]["chunks"] if child["tag"] == b"RsRq".hex())
    language = (root / "Languages/English.bin").read_bytes()
    rows = []
    for obj in feng_objects(package, ("raceover",)):
        fields = {field["tag"]: bytes.fromhex(field["raw"]) for field in obj["fields"]}
        index = struct.unpack_from("<H", fields["OP"], 12)[0]
        obj["default_resource"] = requests[index]
        channels = tracks(package, obj)
        print(obj["name"], requests[index]["name"], [(c["fi"].hex(), c["target"], len(c["kd"])) for c in channels])
        for tick in (0, 120, 180, 240, 260, 300, 340, 420, 560, 1200, 2400):
            state = bytearray(fields["SA"])
            for channel in channels:
                value = sample(image, channel, tick)
                at = channel["target"] * 4
                state[at:at + len(value)] = value
            current = {**fields, "SA": bytes(state)}
            native_obj = {**obj, "fields": [{"tag": key, "raw": value.hex()} for key, value in current.items()]}
            renderer = Renderer(image)
            if obj["name"] == "RaceOverMessage":
                renderer.load_font(language, "font_conduitmditctt38bi", (512, 256))
                renderer.object_fields(native_obj, "Race Over")
                result = renderer.render_text()
            else:
                renderer.object_fields(native_obj, "")
                result = renderer.render_image((16, 16))
            rows.append({"name": obj["name"], "tick": tick, "resource": requests[index],
                         "state_hex": state.hex(), "native": result})
        print(obj["name"], "steady-state", list(struct.unpack_from("<4I", state)),
              "position", list(struct.unpack_from("<3f", state, 28)),
              "size", list(struct.unpack_from("<2f", state, 56)))
    (out / "oracle.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="ascii")
    fixture=['// Authenticated4FC100/FEng quad bounds in the original640x480 canvas.',
             '// Fields: tick,left,right,top,bottom,native packed color. The blank space has no pixels.',
             'static constexpr NativeRaceOver kNativeRaceOverMessage[]={']
    for name in ('RaceOverMessage','RaceOverBacking'):
        if name=='RaceOverBacking':fixture+=['};','static constexpr NativeRaceOver kNativeRaceOverBacking[]={']
        for row in rows:
            if row['name']!=name:continue
            quads=row['native']['local']['quads']
            vertices=[v for quad in quads for v in quad['positions']]
            bounds=[min(v[0] for v in vertices),max(v[0] for v in vertices),min(v[1] for v in vertices),max(v[1] for v in vertices)]
            color=int(quads[0]['colors'][0],16)
            fixture.append('    {'+str(row['tick'])+','+','.join(format(v,'.9g')+('' if '.' in format(v,'.9g') or 'e' in format(v,'.9g') else '.')+'F' for v in bounds)+',0x%08XU},'%color)
    fixture+=['};']
    text='\n'.join(fixture)+'\n'
    target=WORK/'tests/race_over_native.inc'
    if args.write_fixture:target.write_text(text,encoding='ascii')
    else:assert target.read_text(encoding='ascii')==text,'Race Over native fixture stale'
    print('PASS original Race Over animation, resource and text/image quad geometry')


if __name__ == "__main__":
    main()

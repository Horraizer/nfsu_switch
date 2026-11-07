"""Read-only retail evidence; outputs must be outside the retail directory."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


def manifest(root):
    rows = []
    for path in sorted(root.rglob("*")):
        if path.is_file():
            stat = path.stat()
            rows.append([path.relative_to(root).as_posix(), stat.st_size,
                         stat.st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest()])
    return (json.dumps(rows, ensure_ascii=True, indent=2) + "\n").encode("ascii")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("mode", choices=["disasm", "run"])
    parser.add_argument("args", nargs=argparse.REMAINDER)
    opt = parser.parse_args()
    root, output = opt.root.resolve(), opt.output.resolve()
    # Never write anywhere under E:/Games, even outside this one install.
    forbidden = Path("E:/Games").resolve()
    if output == forbidden or forbidden in output.parents:
        raise SystemExit("output must be outside E:/Games")
    output.mkdir(parents=True, exist_ok=False)
    before = manifest(root)
    (output / "before.json").write_bytes(before)
    result = 0
    try:
        if opt.mode == "run":
            command = opt.args[1:] if opt.args[:1] == ["--"] else opt.args
            if not command:
                raise ValueError("missing command")
            (output / "command.json").write_text(json.dumps(command, ensure_ascii=True), encoding="ascii")
            with (output / "stdout.txt").open("wb") as stream:
                result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT).returncode
        else:
            import capstone
            data = (root / "speed.exe").read_bytes()
            pe = struct.unpack_from("<I", data, 0x3c)[0]
            count, optional_size = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
            optional = pe + 24
            base = struct.unpack_from("<I", data, optional + 28)[0]
            sections = []
            for i in range(count):
                pos = optional + optional_size + i * 40
                name = data[pos:pos+8].rstrip(b"\0").decode("ascii")
                size, rva, rawsize, raw = struct.unpack_from("<IIII", data, pos+8)
                sections.append((name, base+rva, size, raw, rawsize))
            def offset(va):
                for _, start, _, raw, rawsize in sections:
                    if start <= va < start + rawsize:
                        return raw + va - start
                raise ValueError(hex(va))
            md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
            lines = ["sha256 " + hashlib.sha256(data).hexdigest(), "capstone " + capstone.__version__, repr(sections)]
            for spec in opt.args:
                start, end = [int(s, 0) for s in spec.split(":")]
                lines.append("RANGE " + spec)
                for ins in md.disasm(data[offset(start):offset(end-1)+1], start):
                    lines.append("%08X  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
            for word in [b"d3d9.dll", b"Direct3DCreate9", b"d3d8.dll", b"Direct3DCreate8"]:
                lines.append("string %r offset %d" % (word, data.lower().find(word.lower())))
            imports = struct.unpack_from("<I", data, optional+104)[0]
            pos = offset(base+imports)
            while any(data[pos:pos+20]):
                lookup, _, _, name, _ = struct.unpack_from("<IIIII", data, pos)
                namepos = offset(base+name)
                dll = data[namepos:data.index(b"\0", namepos)].decode("ascii")
                if "d3d" in dll.lower():
                    lines.append("IMPORT " + dll)
                    index = offset(base+lookup)
                    while struct.unpack_from("<I", data, index)[0]:
                        rva = struct.unpack_from("<I", data, index)[0]
                        if not rva & 0x80000000:
                            n = offset(base+rva)+2
                            lines.append("IMPORT_SYMBOL " + data[n:data.index(b"\0",n)].decode("ascii"))
                        index += 4
                pos += 20
            # E8 byte candidates only: inspect a containing function with
            # Capstone before interpreting these as control-flow edges.
            for target in [0x4465b0, 0x446620, 0x446730, 0x40c910, 0x40ed50, 0x40c690]:
                candidates = []
                for _, start, _, raw, rawsize in sections[:1]:
                    for i in range(raw, raw+rawsize-4):
                        if data[i] == 0xe8 and start+i-raw+5+struct.unpack_from("<i",data,i+1)[0] == target:
                            candidates.append(hex(start+i-raw))
                lines.append("E8_CANDIDATES %x %s" % (target, ",".join(candidates)))
            for path in sorted((root / "fx").glob("*.efx")):
                raw = path.read_bytes()
                source = bytes(b ^ 0xaa for b in raw).decode("latin1")
                source = re.sub(r"/\*.*?\*/|//[^\r\n]*", "", source, flags=re.S)
                techniques = list(re.finditer(r"\btechnique\s+(\w+)\s*\{", source))
                for i, match in enumerate(techniques):
                    end = techniques[i+1].start() if i+1 < len(techniques) else len(source)
                    passes = len(re.findall(r"\bpass(?:\s+\w+)?\s*\{", source[match.end():end]))
                    lines.append("FX %s sha256=%s technique=%s passes=%d" %
                                 (path.name, hashlib.sha256(raw).hexdigest(), match[1], passes))
            (output / "disasm.txt").write_text("\n".join(lines)+"\n", encoding="ascii")
    finally:
        after = manifest(root)
        (output / "after.json").write_bytes(after)
        print("manifest files", len(json.loads(before)), "equal", before == after)
        if before != after:
            raise RuntimeError("retail manifest changed")
    if result:
        raise SystemExit(result)


if __name__ == "__main__":
    main()

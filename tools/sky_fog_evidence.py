"""Print bounded, read-only #184 evidence; run inside frame_budget_evidence.py."""
import argparse
import hashlib
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    import capstone
    if capstone.__version__ != "5.0.7":
        raise RuntimeError("use Capstone 5.0.7 for the recorded disassembly")
    data = (args.root / "speed.exe").read_bytes()
    digest = hashlib.sha256(data).hexdigest().upper()
    if digest != "C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7":
        raise RuntimeError("unknown executable layout")
    print("speed.exe", len(data), digest)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    spans = (
        (0x4450AB, 0x4450BF, "separate sky initialization"),
        (0x5715D0, 0x571673, "sky solid and first-instance matrix"),
        (0x445CA0, 0x445D10, "first matching instance lookup"),
        (0x571870, 0x5718B0, "sky view selection and queue"),
        (0x40C910, 0x40CA59, "material texture and queue submit"),
        (0x4098FE, 0x409988, "main sky and scenery flush order"),
        (0x4096A1, 0x409710, "auxiliary depth states before flush"),
        (0x408B84, 0x408BB3, "FOGENABLE initialization"),
        (0x40E458, 0x40E47F, "generic effect-parameter name lookup"),
    )
    for start, end, label in spans:
        print("\nSPAN", hex(start), hex(end), label)
        for ins in decoder.disasm(data[start-0x400000:end-0x400000], start):
            print("%08X: %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
    effects = sorted((args.root / "fx").glob("*.efx"))
    print("\nEFFECT FILES", len(effects))
    for path in effects:
        raw = path.read_bytes()
        decoded = bytes(value ^ 0xAA for value in raw)
        if decoded.endswith(b"\xff"):
            decoded = decoded[:-1]
        source = decoded.decode("ascii")
        print("EFFECT", path.name, len(raw), hashlib.sha256(raw).hexdigest().upper())
        uncommented = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
        fog_lines = [line.strip() for line in uncommented.splitlines() if "fog" in line.lower()]
        print("non-comment fog lines:", repr(fog_lines))
        if path.name.lower() == "world.efx":
            for number, line in enumerate(source.splitlines(), 1):
                if 47 <= number <= 122:
                    print("%d: %s" % (number, line))
    print("Static bounded evidence only; absence of a direct setter is not a runtime proof.")


if __name__ == "__main__":
    main()

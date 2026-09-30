"""Initialize uam's otherwise-zero Mesa dFdy state (issue #147).

Only the exact lowering used by our fragment stages is accepted. This is
compiler data, not an instruction patch; see docs/SHADER_DERIVATIVES.md.
"""

import argparse
from pathlib import Path
import re
import struct


def derivative_offset(blob, tgsi):
    if len(blob) < 24:
        raise ValueError("truncated DKSH header")
    magic, header, control, code, programs, count = struct.unpack_from("<6I", blob)
    if (magic != 0x48534B44 or header != 24 or count != 1 or
            control < 256 or control % 256 or code < 256 or code % 256 or
            control + code != len(blob) or programs < header or
            programs + 64 > control):
        raise ValueError("unsupported DKSH layout")
    stage, entry, _, offset, size, _ = struct.unpack_from("<6I", blob, programs)
    if stage not in (0, 1) or entry + 8 > code:
        raise ValueError("unsupported DKSH stage/entrypoint")
    lines = [line.strip() for line in tgsi.splitlines() if line.strip()]
    if not lines or lines[0] != ("VERT" if stage == 0 else "FRAG"):
        raise ValueError("DKSH/TGSI stage mismatch")
    references = [line for line in lines if "CONST[0]" in line]
    if stage == 0:
        if references or size:
            raise ValueError("unexpected vertex compiler constants")
        return None
    # The unlit sky fragment has no derivatives or compiler constants.
    if not references and size == 0 and not any("DDY" in line for line in lines):
        return None
    if len(references) != 2 or references[0] != "DCL CONST[0][0]":
        raise ValueError("expected only one dFdy compiler-state vector")
    multiply = re.fullmatch(
        r"\d+: MUL TEMP\[(\d+)\], CONST\[0\]\[0\]\.xxxx, IN\[0\]\.xyzz",
        references[1])
    if not multiply:
        raise ValueError("unexpected dFdy compiler-state use")
    pos = lines.index(references[1])
    if pos + 1 >= len(lines) or not re.fullmatch(
            r"\d+: DDY TEMP\[\d+\]\.xyz, TEMP\[" + multiply[1] + r"\]",
            lines[pos + 1]):
        raise ValueError("compiler-state multiply does not feed dFdy")
    # uam places its compiler constbuf after the program, aligned to 256.
    if size != 16 or offset % 256 or offset <= entry or offset + size > code:
        raise ValueError("invalid derivative compiler-constant range")
    start = control + offset
    if blob[start + 4:start + 16] != bytes(12):
        raise ValueError("unexpected derivative compiler-constant lanes")
    if blob[start:start + 4] not in (bytes(4), struct.pack("<f", 1.0)):
        raise ValueError("unexpected derivative multiplier")
    return start


def process(blob, tgsi, check=False):
    offset = derivative_offset(blob, tgsi)
    if offset is None:
        return blob
    one = struct.pack("<f", 1.0)
    if check and blob[offset:offset + 4] != one:
        raise ValueError("dFdy multiplier is zero: flat lighting would be skipped")
    return blob[:offset] + one + blob[offset + 4:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("dksh", type=Path)
    parser.add_argument("tgsi", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    args = parser.parse_args()
    if args.check == (args.output is not None):
        parser.error("use --check with no output, or supply an output to fix")
    try:
        blob = args.dksh.read_bytes()
        result = process(blob, args.tgsi.read_text(encoding="ascii"), args.check)
        if args.output is not None:
            args.output.write_bytes(result)
        print("uam derivatives: " + ("verified" if args.check else "initialized"))
    except (OSError, ValueError) as error:
        parser.exit(1, "uam derivatives: FAIL: " + str(error) + "\n")


if __name__ == "__main__":
    main()

"""Execute retail junction passage, countdown and speed-override admission."""

import argparse
import hashlib
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_FPCW

from physics256_native import Machine, bits
from world_body_trace import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = (args.root / "speed.exe").read_bytes()
    identity = "c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7"
    if hashlib.sha256(data).hexdigest() != identity:
        raise ValueError("unsupported executable")
    machine = Machine(Image(data))
    machine.uc.reg_write(UC_X86_REG_FPCW, 0x007f)  # recorded PC controller entry
    machine.allowed = [(0x462a00, 0x462a52), (0x586640, 0x58668c),
                       (0x421a30, 0x421a46), (0x674898, 0x67490e)]
    controller, events = 0x1160000, 0x1161000
    machine.put(0x736150, events)
    rows = ["// PC 1.4 USA, 462A00 -> 586640 -> 421A30, no semantic hooks.",
            "// speed.exe SHA256 " + identity]
    for marker in (0, 1, 2):
        for selected, timer in ((-1, 0), (3, 42), (4, 10000), (3, 0x7fffffff)):
            for seconds in (0.0, 0.00699999975, 0.01, 1 / 60, 2.5, 2.6):
                event = events + 3 * 32
                machine.put(event + 8, marker, "B")
                machine.put(controller + 0x478, selected, "i")
                machine.put(controller + 0x47c, timer)
                machine.run(0x462a00, (event,), ecx=controller)
                after_pass = machine.get(controller + 0x47c)
                machine.run(0x586640, (bits(seconds),), eax=controller + 0x47c)
                machine.run(0x421a30, eax=controller + 0x47c)
                row = (marker, selected, timer, bits(seconds), after_pass,
                       machine.get(controller + 0x478, "i"),
                       machine.get(controller + 0x47c), machine.reg("eax"))
                rows.append("{%d,%d,%d,%dU,%d,%d,%d,%d}," % row)
    if machine.events:
        raise RuntimeError("unexpected semantic hook")
    args.output.write_text("\n".join(rows) + "\n", encoding="ascii")
    print("Original junction speed window:", len(rows) - 2, "cases")


if __name__ == "__main__":
    main()

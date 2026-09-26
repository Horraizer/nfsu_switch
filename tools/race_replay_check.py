"""Check event 1's retail replay pins, using only fresh output directories."""
import argparse
import hashlib
from pathlib import Path
import subprocess

STEPS = (180, 600, 6000, 22000, 40000, 43564)
FRAMES = (
    "31FFD8366842E6B051112CB103784A875CED47AFA4EA36A1370FC9BC13A900C2",
    "C097A3DFF189EA00B6EB43FC71EAFBD581553D2F7A4BFE80E1B2FB502204AD73",
    "97F1458970990349A44C50225C5239A2E20CB33327CFEE5D988EF8DC3A3B6274",
    "A37F1A53EFDB8E74B85AA0F298D4E8C0FD74B1F1C382B0F4A5D00961F9CCA9C4",
    "2476F4069F955DBCAC084CB575DB491317B1700E29106E7418A7CC83190B80BB",
    "2BF5E5AC93E2A6BB6D14FCF93DAF490C0D178B1CE5ECC49FF62432C08AD7ACCC",
)
RESULT = (
    "event 1 track 1003 laps 2 outcome finished steps 43564 resets 0",
    "lap 1 ticks 1497480 time 6:14.37",
    "lap 2 ticks 1410360 time 5:52.59",
    "total ticks 2907840 time 12:06.96",
    "state-sha256 1FFF1EB3932B6044C81A9D5671FE6D3D45B7D82ACB8B511D2E1441519E127F0D",
    "stream loads 78 releases 0 bytes-read 36471524",
)
STATE_FILE = "440C844BC13D8C502735120708C590B3CB7237C17254EA8A15F52CA3F5E9A6A4"
INPUT = "0308B3B94954F7C47DA6522151D8FB90E2D1F954D65B628C5F967263A834BC82"


def digest(data):
    return hashlib.sha256(data).hexdigest().upper()


def require(okay, reason):
    if not okay:
        raise RuntimeError(reason)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, action="append", required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="new directory outside retail")
    parser.add_argument("--check-driver", action="store_true", help="also regenerate the recording with each build")
    args = parser.parse_args()
    root, output = args.root.resolve(), args.out.resolve()
    require(root.is_dir(), "retail root does not exist")
    require(output != root and root not in output.parents, "output is inside retail")
    output.mkdir()  # No exist_ok: an earlier run cannot masquerade as this run.
    recording = Path(__file__).resolve().parents[1] / "tests/recordings/event1.txt"
    require(digest(recording.read_bytes()) == INPUT, "recording pin changed")
    previous = None
    for index, exe in enumerate(args.exe):
        prefix = output / str(index)
        command = [str(exe.resolve()), "--root", str(root)]
        if args.check_driver:
            generated = output / (str(index) + "-generated.txt")
            result = subprocess.run(command + ["--generate", str(generated)], capture_output=True, text=True)
            (output / (str(index) + "-driver.log")).write_text(result.stdout + result.stderr, encoding="ascii")
            require(result.returncode == 0 and not result.stderr, "driver failed; see log")
            require(generated.read_bytes() == recording.read_bytes(), "scripted driver no longer generates the recording")
        result = subprocess.run(command + ["--input", str(recording), "--out", str(prefix),
                                "--frames", ",".join(map(str, STEPS))], capture_output=True, text=True)
        (output / (str(index) + ".log")).write_text(result.stdout + result.stderr, encoding="ascii")
        require(result.returncode == 0 and not result.stderr, "replay failed; see log")
        lines = result.stdout.splitlines()
        for line in RESULT:
            require(lines.count(line) == 1, "missing result: " + line)
        state = (prefix / "states.txt").read_bytes()
        require(digest(state) == STATE_FILE, "per-step state pin differs")
        rows = state.decode("ascii").splitlines()
        require(len(rows) == STEPS[-1], "wrong state row count")
        for tick, row in enumerate(rows, 1):
            number, value = row.split()
            require(int(number) == tick and len(value) == 64, "invalid state row")
        if previous is not None:
            require(previous == state, "builds differ at a per-step state hash")
        previous = state
        for tick, expected in zip(STEPS, FRAMES):
            frame = prefix / ("frame-" + str(tick) + ".tga")
            require(digest(frame.read_bytes()) == expected, "frame pin differs at " + str(tick))
            require(lines.count("frame " + str(tick) + " tga-sha256 " + expected) == 1,
                    "printed frame pin differs")
        print("PASS", exe.name, "43564 step hashes, result, 6 frames" +
              (", generated recording" if args.check_driver else ""))


if __name__ == "__main__":
    main()

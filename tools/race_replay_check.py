"""Check event 1's retail replay pins, using only fresh output directories."""
import argparse
import csv
import io
import hashlib
from pathlib import Path
import subprocess

STEPS = (1, 180, 600, 6000, 11344, 12459, 13128, 24300, 35396, 36510, 37177, 48336)
# These frame pins were checked on the merged tree. PR #188 adds the ground
# shadow and PR #227 adds sky pixels; both change every sampled CPU frame.
# PR #225's car window/decal fixes remain in the base pixels. PR #221 changes
# GPU draw order, not these CPU reference pixels. The rasterizer depth-write
# guard from 3b1f644 is inherited from base 97e3839.
# Replay steps, state hashes, input recording, and trajectory are unchanged.
FRAMES = (
    "1DB2683AC4AE29AC99E137F7637F7E64050C7EDCFDEC631F1EDD83A0518F7C54",  # 1: #188 shadow, #227 sky
    "60264FBA71A52C1A55FC6EE139DB6CEDDFE251762B01B6A545A6ABA03C97C54B",  # 180: #188 shadow, #227 sky
    "5BD77BD948467DEAD2FC730B3E43559DB37C8EC9E695D515B252754583FFD3AF",  # 600: #188 shadow, #227 sky
    "5883EF7FAB5AD3C7858E1D9961C75ADC02E1C4A24E4C19C7865766CFFDE8EBCD",  # 6000: #188 shadow, #227 sky
    "51DE569D37BAEC803E0D2701362D32332EEE2361DC11D4BFEB3C13A9E9A8D18F",  # 11344: #188 shadow, #227 sky
    "3FE3952828A525512C6B74B69E4DB6BF26EDD2F9D1B16A3AA474CEEC2F48C002",  # 12459: #188 shadow, #227 sky
    "016AD78AC707D504A879EBC91BEB311BEE21E124DA815727A79DA4EBD47E285E",  # 13128: #188 shadow, #227 sky
    "C545FF413B44FFE5FD354FA5CEB731AA767E34DA130D1EE00EB17207376FAFD0",  # 24300: #188 shadow, #227 sky
    "0C105D8384BB46EFBE4DFB9635FDE90A83D050C22AF1F73D461B4E55AD7FF38A",  # 35396: #188 shadow, #227 sky
    "7E348C89F6E1C1ED51E23C213FD4BD49456BC95DEB1002AE3F0954D223F4E112",  # 36510: #188 shadow, #227 sky
    "E87F47D913C557B7F69F3FB387A850AA772E5E3B904CA9B950FA49222E9998CD",  # 37177: #188 shadow, #227 sky
    "C62FC38A8ED12474DB778900113E8FD8B76D020F416A92FAB7DA73D746AA474C",  # 48336: #188 shadow, #227 sky
)
RESULT = (
    "event 1 track 1003 laps 2 outcome finished steps 48336 resets 0",
    "lap 1 ticks 1616400 time 6:44.10",
    "lap 2 ticks 1611200 time 6:42.80",
    "total ticks 3227600 time 13:26.90",
    "state-sha256 79776F6DD3FC2727C99D3F6AD69A16E623BDEEB5CFFD2261458A15FC7F5B3BF9",
    "stream loads 78 releases 0 bytes-read 36471524",
)
STATE_FILE = "E4D4D6E955D59F7FF11C3318BC455D08B7903CA46BBDE8E1FC3F2AC32AEA54E5"
INPUT = "CED29AB7D90F0861AFE2C0B604D0EDDFB4F769BEB067944F8E9C7DB0B892E0AC"
TRAJECTORY = "8203932E99CDA190CEE81C6171705224AC7CC4EE910246805350605304F1D439"


def digest(data):
    return hashlib.sha256(data).hexdigest().upper()


def require(okay, reason):
    if not okay:
        raise RuntimeError(reason)


def retail_manifest(root):
    manifest = {}
    for path in root.rglob("*"):
        if path.is_file():
            stat = path.stat()
            checksum = hashlib.sha256()
            with path.open("rb") as source:
                for chunk in iter(lambda: source.read(1024 * 1024), b""):
                    checksum.update(chunk)
            manifest[path.relative_to(root).as_posix()] = (
                stat.st_size, stat.st_mtime_ns, checksum.hexdigest())
    return manifest


def retail_run(command, root):
    before = retail_manifest(root)
    result = subprocess.run(command, capture_output=True, text=True)
    after = retail_manifest(root)
    require(after == before, "retail manifest changed during " + command[0])
    print("retail-manifest:", len(before), "files unchanged")
    return result


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
            result = retail_run(command + ["--generate", str(generated)], root)
            (output / (str(index) + "-driver.log")).write_text(result.stdout + result.stderr, encoding="ascii")
            require(result.returncode == 0 and not result.stderr, "driver failed; see log")
            require(generated.read_bytes() == recording.read_bytes(), "scripted driver no longer generates the recording")
        result = retail_run(command + ["--input", str(recording), "--out", str(prefix),
                             "--frames", ",".join(map(str, STEPS))], root)
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
        trajectory = (prefix / "trajectory.csv").read_bytes()
        require(digest(trajectory) == TRAJECTORY, "120 Hz trajectory pin differs")
        samples = list(csv.DictReader(io.StringIO(trajectory.decode("ascii"))))
        require(len(samples) == 2*(STEPS[-1]-163), "missing physics samples")
        for physics, sample in enumerate(samples, 1):
            require(int(sample["physics"]) == physics and int(sample["tick"]) == 163+(physics+1)//2,
                    "trajectory does not cover both substeps in order")
            require(int(sample["contacts"]) == 0 and int(sample["resets"]) == 0,
                    "body contact or reset in trajectory")
            require(float(sample["up"]) >= 0.98 and int(sample["road_wheels"]) >= 1,
                    "tilted or airborne car in trajectory")
            if int(sample["tick"]) > 600:
                require(float(sample["speed"]) >= 5, "slow/stalled car after launch")
        for tick, expected in zip(STEPS, FRAMES):
            frame = prefix / ("frame-" + str(tick) + ".tga")
            require(digest(frame.read_bytes()) == expected, "frame pin differs at " + str(tick))
            require(lines.count("frame " + str(tick) + " tga-sha256 " + expected) == 1,
                    "printed frame pin differs")
        print("PASS:", exe.name, str(STEPS[-1]) + " step hashes, result, " + str(len(STEPS)) + " frames, clean 120 Hz trajectory" +
              (", generated recording" if args.check_driver else ""))


if __name__ == "__main__":
    main()

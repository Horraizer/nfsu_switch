"""Check event 1's retail replay pins, using only fresh output directories."""
import argparse
import csv
import io
import hashlib
from pathlib import Path
import subprocess

STEPS = (1, 180, 600, 6000, 11344, 12459, 13128, 24300, 35396, 36510, 37177, 48336)
# PR #225 fixes car window aliases and blank decals; PR #220 adds the ground
# shadow. Each frame below is measured with both changes. These frames also
# depend on the inherited rasterizer depth-write guard from 3b1f644 (already
# in base 97e3839): blended track groups preserve depth.
# Replay steps, state hashes, input recording, and trajectory are unchanged.
FRAMES = (
    "33C7EA1B1C700EF50C89A7D468CAAB012972D4FCFFE36D564B5D056DEC30E1F4",  # 1: visible ground shadow during countdown; no-shadow main is C544B32A3157AB6646E626032685A157CCA456BDB8833B45DF944B2C87AAC2C9
    "ECA21EEA0E33071D8F54FE391F36E08D569FF697F84B561461F419C2EA8912C2",  # 180: car texture fix + shadow at launch lane
    "52CCA0306D140F1CF12819A6547034294FBA83F37073BDE1C4125EFBD96CFD6E",  # 600: car texture fix + shadow at yellow lane
    "0B12D812B7E9E6511BEBF4503FC1E888F57F2E4A7F49AB1F7DCB93A78E444828",  # 6000: car texture fix + shadow at junction
    "57E1194E5ED738A718B55A67580C6AFC8D4883E8F9C25607759AE7CB0EF61EF8",  # 11344: car texture fix + shadow at bend
    "59F874A000ED305B4367A2B6FBAB55E5C0469F5EF17BD05F32B49FAC9750CF60",  # 12459: car texture fix + shadow at straight
    "2AE4CEC5325DDF56113B129A87973B437372D4FF345D11E3E928BFF107729E48",  # 13128: car texture fix + shadow at right edge
    "24939BAB5AECD2969D6B5244ECCD7A654ED79F6E50966CD4B8ACCAA9890D1456",  # 24300: car texture fix + shadow at lap-two launch
    "540739148B7323B6233F0786E6B10C320DF05FC8CAF46BD707835FD2B5CDC15B",  # 35396: car texture fix + shadow at lap-two bend
    "022A558E9483A5486F555834A4CAFDF6A6AC783AB4D1A96BC5683EFB7F0E56DA",  # 36510: car texture fix + shadow at lap-two straight
    "5D387BF5F326021C7F4D4417E7459247539C0EDCB35EFCBFEC8A6750461FC26F",  # 37177: car texture fix + shadow at lap-two right edge
    "08F1B38E8EEA0C75B88CA1AE4BA479ED46A25E22D1323B376D778534830FDAFA",  # 48336: car texture fix + shadow at finish lane
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
        print("PASS", exe.name, str(STEPS[-1]) + " step hashes, result, " + str(len(STEPS)) + " frames, clean 120 Hz trajectory" +
              (", generated recording" if args.check_driver else ""))


if __name__ == "__main__":
    main()

"""Check event 1's retail replay pins, using only fresh output directories."""
import argparse
import csv
import io
import hashlib
from pathlib import Path
import subprocess

STEPS = (180, 600, 5449, 5450, 6000, 11344, 12459, 13128,
         24287, 24288, 24300, 35396, 36510, 37177, 48336)
# 5449/5450 bracket gear-slot 2-to-3 (HUD 1-to-2). 24287/24288 bracket
# lap 1's finish line. 600 is the early straight. The remaining frames
# retain the race scene's earlier material and streaming coverage.
# PR #225 re-pins the car window aliases and blank decals. These frames also
# depend on the inherited rasterizer depth-write guard from 3b1f644 (already
# in base 97e3839): blended track groups preserve depth. Removing that guard
# changes pixels but is not a revert of a change introduced by PR #225.
# PR #243 round 7 changes the gauge's compositing order; every pinned frame
# includes that gauge, so its image hash changes while replay state stays fixed.
# Replay steps, state hashes, input recording, and trajectory are unchanged.
FRAMES = (
    '15D634FADD281CAFE6443B5CCB2390E9EFB904380D6553AE9D6F7B3D8EE35562',
    'E2EF815C0D8F533AFD6A92CDDC065DF9040C81380FD00C8F41305774AB13798F',
    '4671AA5A9900040D142C326E913A46889D97AE25A8AD71F609BFB6334AB0C8EF',
    'A3D12BB0C65687E8ED3F59DC62268A5384C7C7A75DAE01F0C135CAB3DAD1D1C2',
    '3B934EA691AAFA08CFF2A4B1BCB51AE45DFC180AA238A24B6857616C8B4B7FFC',
    '93E028BD08E06214611F350753B4B5468A26CB4A216E2A45743548A09FCB2002',
    '38595E3027EF6317104D8588D513F5815F0646E9CCDE520581781DA353B30558',
    'E9CB32367F2FCCFDD22D0785E22D061F6D41C3C281BD815C94A2CBB9DE3FC121',
    'ABDEF6E87B2C7AAB022DF21AACDAD4CDF02AEBAF20196CFBDA8672B8444C3D79',
    '213EFA4DAA304AE5A28F9FA2A1412A363528DDD6B1DC9FEA8D20EB24874B29D1',
    'D3F13CADDD12DEE13F6335453AC48E0446BFCECED07DEE35E6729DA6B22B9384',
    'A03501C775793FF4635717F7B93CC4E5E83785AA1E5153E45D5B97B3A5EF5699',
    '443B032AD45D26538C369AAC8A21872B77C9CBA494C1FABAE3AFDBFACD2DD64A',
    '48F6F04326FCFA83E6347E075405970760E3411A637D9A233CC2D29230EF80B4',
    'AD7667430D0CAF582F4697F5BCD4F701CCA16AAD2B9BC9741CAE9ECEA75D7DBA',
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

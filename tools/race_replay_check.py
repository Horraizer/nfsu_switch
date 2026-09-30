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
# Replay steps, state hashes, input recording, and trajectory are unchanged.
FRAMES = (
    'C3D68A4C569C75B0F96757D9F5ED78F83D6EB6446187C4593BB2B2F4751F99AA',
    '9F7B93B13A69EF95681A5748220A00A94D1B9CD890638E1C66A8EDE5B519AA63',
    '79368A9E9A6583BAA7F89C2E4B578CE803F0DFCF604F3743A11AFC8FD579D71C',
    '0AF61ACD24D4BCFEFBEA96C7534DDAF16E29BA343A5A8EDA7C9128FAFF8E764E',
    '3908B5476E88591F71514706A1FEC7B602C1CC5075706F92DD66F2DDDF522BE0',
    '5A7CF3B9C3C986652B3991E45B7A14EE195FE515D928BEB8A6DC9FDEB32ADEC8',
    '9D4279A9F718849D6ADA7CEB3E34BC836B700568D15AC502CE095AEAB5C45285',
    '49CB32B1127FB764F8E3CC3DF37D74ECCACFD9CECC534C4A4372D8C317B32BED',
    '8F5CDB94A6C390796A80731B50351BA64FF0F92CBE184620C883FCD8DC9F3F7E',
    '4E1F8FA6ACA389EA18E295E8FAECC93A53D653286ED526E0B74719E7A19C0E13',
    'E2C0C4665DB13FE46070031E6B40249FE5119CAD23FC9CA507E5099F3F144653',
    '02B159E95C0FFD4901C52CC7D0680A5DCEA67F3B967BFD346FEEB37AB8C97987',
    '8BF2DABEE546D49D2DF3EA8F3278298CDAA9958EA6972B4884278FC290016515',
    'AC20A429E902D644A69D4A8BBE1FF093D4B68E9CE5EACFEF25D3B2BF4587D201',
    'DFE07B2B4AB55E4629EA07220AE0DDC275AF6090AAF5FCB218EAE86D77642495',
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

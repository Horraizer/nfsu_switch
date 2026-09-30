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
    '4BE5374595911FE4A0E8A4F843DB8850D3E702896051B9A55A522001DF2491C5',
    '60B4539B738DDF147FD50637C52AB60F5A8A1184CF54F86ADFC5A538398885FC',
    '40D48B228A9478B35A570FBAAC8BE76347F836183B7A0A16976E229901555E6A',
    'ACB8863428F024EDFCD708A79CD11245BF4CC92A19099960F28E25ED3208BD05',
    '82135A121F6A4A90A688860D55075DB3CED87BF15F7C5E95EBDAF01440E04185',
    '3E6854183661231A7A78290991E3DDDCC33049D178E0F5F13C984DE02F244862',
    '729FC122A6A80C6119BCFC5E05AB1B214B4DA32EC6A17AFC783978E2E3B496B6',
    'A221F8CCBB17B7DC183F6F193FF6C650737A0AFCC68803687DFB43BFADDC5326',
    '98DC919C476BD88705EEBAE5C9E9CF0B806B92D1B331CDB8F0C9DBC55A469F76',
    '31B9AFD61D018EB105D355BB5A0E0FD5A4C80CBAF8E9886A7FFA580580E8CAE9',
    '87772B46BB84874ADB1F2A4D70C7DE7C7AE9CCAAA719F2899CA5E385D4659DAD',
    '7CCAF12F0C22ADC1E8425DF3989EF53BAA95D467DFA510E9D23F2E811B487E19',
    'CA4C6E127D6686CEC7ABD8848CEAF70C3F1910361A3EE57CDF7C70BB057051EF',
    'DF966A55611A38BCB0F69891CED6D3D530AC65F93749CC5104763B587907AE75',
    'BB20562AF61FBB8AD85C25B4BF585B5D9B92E8F170A5801E08AF8FCBFE5F96CB',
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

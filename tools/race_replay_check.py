"""Check event 1's retail replay pins, using only fresh output directories."""
import argparse
import csv
import io
import hashlib
from pathlib import Path
import subprocess

STEPS = (180, 600, 6000, 11344, 12459, 13128, 24300, 35396, 36510, 37177, 48336)
FRAMES = (
    "455CA10A332B9C325E6D6CFA8D0FB38F729E49F14E6B61C38E34A673CC0DA801",  # 180: launch lane shadow follows body origin
    "4C62221D36931BE24ECA87DA9ADBCCF3BCB3724FB0D3F38FFE98A0ADAC5196FA",  # 600: yellow lane shadow follows body origin
    "4B1347FAB91DC0BE9AAC54F60B5447854F253811863A7361394D5AC8D0D19DF6",  # 6000: junction shadow follows body origin
    "B5264864905A434E58325905D8F40630A2811AABE7D8F295C90B6B630EE3DFF5",  # 11344: bend shadow follows body origin
    "BF102776D9171FE64D5E62B94512329CD0B4DEA37BA7528262FEC11C4438A098",  # 12459: straight shadow follows body origin
    "39E15F29E3B79CC376B407A619FB691A59F59FED6289E3F6FBCB2911067E1D74",  # 13128: right edge shadow follows body origin
    "BBD2DF1F5D1CC5335B6C8BE2884B54261758EDCF2B3C7FDCBB311D7830CECC69",  # 24300: second-lap launch shadow follows body origin
    "B85E620F11788ABEBB8A1C2A4C5AB63768C8CE705AC2362A2201D1D6FDE64789",  # 35396: second-lap bend shadow follows body origin
    "27169F83958A17AC1B6ABAF5A6F53BAED8E546E61F1991711318ACF881FEA9AB",  # 36510: second-lap straight shadow follows body origin
    "85C77929BDEE4E4BCEF636AC451718FA7EB7C89060B3D55594DE8E63D5315350",  # 37177: second-lap right edge shadow follows body origin
    "83B26E30EA7F4C693CC9697498FCF390D69C1CF5ACCFA4171F785CFE1D3EE4D1",  # 48336: finish-lane shadow follows body origin
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

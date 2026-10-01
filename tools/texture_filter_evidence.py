"""Paired readonly replay images; fresh outputs and identical simulation required."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
sys.dont_write_bytecode = True

from frame_budget_evidence import manifest
from race_replay_check import RESULT, STATE_FILE, TRAJECTORY, STEPS, FRAMES, SHADED_FRAME, digest


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def pixels(path):
    data = path.read_bytes()
    require(len(data) >= 18 and data[2] == 2 and data[16] == 32, "not a 32-bit TGA: " + path.name)
    width, height = struct.unpack_from("<HH", data, 12)
    offset = 18 + data[0]
    require(width and height and len(data) == offset + width*height*4, "truncated TGA: " + path.name)
    return width, height, data[offset:]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--race", required=True, type=Path)
    parser.add_argument("--recording", type=Path, default=Path("tests/recordings/event1.txt"))
    parser.add_argument("--frames", default="180,600,6000")
    parser.add_argument("--motion", action="store_true", help="add 6000..6120 at four replay ticks per image")
    parser.add_argument("--expected", type=Path)
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--references", type=Path)
    args = parser.parse_args()
    root, output = args.root.resolve(), args.output.resolve()
    require(root != output and root not in output.parents, "output is inside retail installation")
    require(Path("E:/Games").resolve() not in output.parents, "output is inside E:/Games")
    steps = sorted(set(map(int, args.frames.split(","))) | (set(range(6000, 6121, 4)) if args.motion else set()))
    require(all(0 < step <= 48336 for step in steps), "frame tick out of range")
    expected = {}
    if args.expected:
        for row in args.expected.read_text(encoding="ascii").splitlines():
            if row and not row.startswith("#"):
                key, value = row.split()
                expected[key] = value
    output.mkdir(parents=True, exist_ok=False)
    before = manifest(root)
    (output/"before.json").write_bytes(before)
    state = trajectory = None
    rows = []
    try:
        for shading in ("off", "on"):
            pair = []
            for mode in ("legacy", "linear"):
                label = mode + "-" + shading
                destination = output/label
                command = [str(args.race.resolve()), "--root", str(root), "--input", str(args.recording.resolve()),
                           "--out", str(destination), "--frames", ",".join(map(str, steps)),
                           "--car-shading", shading, "--texture-filter", mode]
                (output/(label+"-command.json")).write_text(json.dumps(command, ensure_ascii=True), encoding="ascii")
                result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (output/(label+".log")).write_bytes(result.stdout + result.stderr)
                require(result.returncode == 0 and result.stdout and not result.stderr and
                        b"ERROR" not in result.stdout and b"FAIL" not in result.stdout,
                        "replay failed: " + label + "; inspect log")
                lines = result.stdout.decode("ascii").splitlines()
                require(all(lines.count(line) == 1 for line in RESULT), "replay result differs: " + label)
                current_state = (destination/"states.txt").read_bytes()
                current_trajectory = (destination/"trajectory.csv").read_bytes()
                require(digest(current_state) == STATE_FILE, "simulation pin differs: " + label)
                require(digest(current_trajectory) == TRAJECTORY, "trajectory pin differs: " + label)
                if state is None:
                    state, trajectory = current_state, current_trajectory
                require(state == current_state and trajectory == current_trajectory, "filter/shading changed simulation")
                frames = {}
                for step in steps:
                    path = destination/("frame-%d.tga" % step)
                    width, height, frame = pixels(path)
                    require((width,height) == (640,360), "reference viewport changed")
                    image_hash = digest(path.read_bytes())
                    key = label+"/"+str(step)
                    if key in expected:
                        require(image_hash == expected[key], "image pin differs: " + key)
                    if mode == "legacy" and shading == "off" and step in STEPS:
                        require(image_hash == FRAMES[STEPS.index(step)], "legacy image pin changed")
                    if mode == "legacy" and shading == "on" and step == 180:
                        require(image_hash == SHADED_FRAME, "legacy shaded image pin changed")
                    frames[step] = frame
                    rows.append([key,image_hash])
                pair.append(frames)
            for step in steps:
                a,b = pair[0][step],pair[1][step]
                changed = sum(a[i:i+4] != b[i:i+4] for i in range(0,len(a),4))
                require(changed > 0, "linear world did not change any pixel")
                require(a[3::4] == b[3::4], "filter changed frame alpha")
                print("PAIR",shading,step,"changed pixels",changed,"of",len(a)//4)
        require(set(expected).issubset({row[0] for row in rows}), "requested frames omit an expected image")
        if args.compare:
            import re
            require(args.references is not None, "--references required with --compare")
            references = re.findall(r'\{(\d+), TextureFilterMode::(legacy|linear), (true|false), "([A-F0-9]{64})"\}',
                                    args.references.read_text(encoding="ascii"))
            require(len(references) == 12 and len(set(references)) == 12, "reference pin table incomplete")
            for tick, mode, shaded, image_hash in references:
                shade = "on" if shaded == "true" else "off"
                label = "native-"+mode+"-"+shade+"-"+tick
                target = output/(label+".tga")
                command = [str(args.compare.resolve()), "--root", str(root), "--input", str(args.recording.resolve()),
                           "--step", tick, "--texture-filter", mode, "--car-shading", shade, "--out", str(target)]
                result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (output/(label+".log")).write_bytes(result.stdout+result.stderr)
                require(result.returncode == 0 and result.stdout and not result.stderr and
                        b"FAIL" not in result.stdout and b"ERROR" not in result.stdout and target.is_file(),
                        "native reference failed: "+label)
                require(digest(target.read_bytes()) == image_hash, "native reference pin differs: "+label)
                rows.append([label,image_hash])
            print("PASS: twelve native reference images match desktop pins")
        (output/"images.json").write_text(json.dumps(rows,indent=2)+"\n",encoding="ascii")
        print("PASS: four fresh replays; identical pinned states, trajectory and result; paired images",len(rows))
    finally:
        after = manifest(root)
        (output/"after.json").write_bytes(after)
        require(before == after, "retail manifest changed")
        print("PASS: before/after retail file manifests equal")


if __name__ == "__main__":
    main()

"""Execute the six actual retail circuit fixtures and verify pins and behavior."""
import argparse
from pathlib import Path
import re
import subprocess


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--references", type=Path, required=True)
    args = parser.parse_args()
    source = args.references.read_text(encoding="ascii")
    section = source.split("std::vector<VehicleTrackScenario> circuit_scenarios() {", 1)[1]
    section = section.split("} // namespace", 1)[0]
    pins = dict(re.findall(r'scenario\("([a-z-]+)".*?"([A-F0-9]{64})"\)', section, re.S))
    counts = {"grid": 360, "drive": 480, "wall": 480,
              "wall-square": 360, "drop": 360, "leave": 360}
    require(set(pins) == set(counts), "exactly six circuit source fixtures required")
    for name, count in counts.items():
        command = [args.probe, "vehicle-track", str(args.root / "Global/GLOBALB.BUN"),
                   str(args.root / "FrontEnd/FrontB.lzc"),
                   str(args.root / "Tracks/TRACKB1003.lzc"), "1", name, "--every", "0"]
        run = subprocess.run(command, capture_output=True, text=True, timeout=60)
        text = run.stdout.replace("\r\n", "\n")
        require(run.returncode == 0 and not run.stderr and text,
                f"{name}: failed invocation {run.returncode}: {text}{run.stderr}")
        require(not re.search(r"(^|\n)(FAIL|ERROR|error|pinned: DIFFERENT)", text),
                f"{name}: failed producer verdict: {text}")
        record = re.search(r"telemetry: records ([0-9]+) bytes ([0-9]+) sha256 ([A-F0-9]{64})\n", text)
        require(record and int(record[1]) == count and int(record[2]) == count * 1564,
                f"{name}: complete fresh telemetry records required")
        require(record[3] == pins[name] and "pinned: EQUAL" in text,
                f"{name}: actual circuit hash differs from source fixture: {text}")
        summary = re.search(r"summary: steps ([0-9]+) contact-steps ([0-9]+) impulses ([0-9]+) deepest ([0-9.]+) resets ([0-9]+) steps-with-a-wheel-off-road ([0-9]+)\n", text)
        require(summary and int(summary[1]) == count, f"{name}: semantic summary missing")
        contacts, impulses, resets, unsupported = map(int, (summary[2], summary[3], summary[5], summary[6]))
        if name in ("grid", "drive"):
            require(contacts == impulses == resets == unsupported == 0,
                    f"{name}: supported grid/drive must stay clear without resets")
            require("wheels road road road road" in text, f"{name}: final support missing")
        if name == "drive":
            require(re.search(r"race step [0-9]+ crossed-forward index 0 lap 0", text),
                    "drive: start line crossing missing")
        if name in ("wall", "wall-square"):
            require(contacts > 0 and impulses > 0 and float(summary[4]) > 0 and resets == 0,
                    f"{name}: real collision impulse required without concealed reset")
            require(re.search(r"contact step [0-9]+ polygon 9374 wall depth ", text),
                    f"{name}: actual expected retail wall contact missing")
        if name == "drop":
            # Native588AB0 bump stops arrest the five-metre drop before the
            # chassis reaches the road. Preserve the complete state pin and
            # require airborne-to-supported landing without hull penetration.
            require(contacts == impulses == resets == 0 and float(summary[4]) == 0,
                    "drop: native bump-stop landing must avoid chassis penetration/reset")
            require(unsupported > 0 and "wheels road road road road" in text,
                    "drop: airborne-to-supported landing missing")
        if name == "leave":
            require(contacts == impulses == 0 and resets == 1 and unsupported > 0,
                    "leave: one bounds reset required, without hull collision")
            require(re.search(r"bounds step [0-9]+: no collision under the car", text) and
                    re.search(r"reset step [0-9]+ out-of-bounds node [0-9]+ ground ", text),
                    "leave: actual bounds detection and performed reset missing")
        print(f"PASS circuit {name}: fresh records, source hash and contact/reset semantics")
    print("PASS: all six real retail circuit scenarios")


if __name__ == "__main__":
    main()

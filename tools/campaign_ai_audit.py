"""Inventory the PC 1.4 USA campaign AI inputs without starting a race."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess

from event0_trace import ascii_string, chunks, jdlz, name_hash, table_span


FRONT_SHA = "94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77"
GLOBAL_SHA = "39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9"
MODES = ("Circuit", "Drag", "Sprint", "Drift", "Time trial", "Knockout")
CAPS = (0.93, 0.97, 1.0)


def authenticated(blob, digest):
    if hashlib.sha256(blob).hexdigest() != digest:
        raise ValueError("unexpected retail table SHA-256")
    return blob


def offsets(blob, tag, stride):
    first, last = table_span(blob, tag, stride)
    return range(first, last, stride)


def u32(blob, at):
    return struct.unpack_from("<I", blob, at)[0]


def numbers(values):
    return "/".join(format(value, ".4g") for value in values)


def track_features(root, track):
    image = jdlz((root / f"Tracks/TRACKB{track}.lzc").read_bytes())
    at, end = table_span(image, 0x34121)
    roads = 0
    points = 0
    while at < end:
        if end - at < 0x27C:
            raise ValueError(f"truncated route header on {track}")
        count = struct.unpack_from("<h", image, at + 0x10)[0]
        if count < 1:
            raise ValueError(f"invalid route point count on {track}")
        at += 0x27C + 0x88 * count
        points += count
        roads += 1
    if at != end:
        raise ValueError(f"truncated route points on {track}")
    directions = [{}, {}]
    for at in offsets(image, 0x34122, 32):
        route_type = image[at + 11]
        counts = directions[image[at + 13] == 1]
        counts[route_type] = counts.get(route_type, 0) + 1
    def record_count(tag, stride):
        total = 0
        for found, start, end in chunks(image):
            if found == tag:
                count, remainder = divmod(end - start - 8, stride)
                if remainder:
                    raise ValueError(f"partial table {tag:08X} on {track}")
                total += count
        return total

    intersections = record_count(0x34123, 0x170)
    spawns = record_count(0x34124, 0x108)
    return roads, points, directions, intersections, spawns


def census(root, probe):
    front = authenticated(jdlz((root / "FrontEnd/FrontB.lzc").read_bytes()), FRONT_SHA)
    config = authenticated((root / "Global/GLOBALB.BUN").read_bytes(), GLOBAL_SHA)
    presets = {}
    for at in offsets(front, 0x30220, 0x408):
        key = name_hash(ascii_string(front, at + 0x28, 32, True))
        presets[key] = ascii_string(front, at + 8, 32, True)
    tracks = {
        struct.unpack_from("<h", config, at + 0x6C)[0]:
            (config[at + 0x69], struct.unpack_from("<h", config, at + 0xB8)[0])
        for at in offsets(config, 0x34201, 0xFC)
    }
    rows, stages, used_tracks = [], [], set()
    for at in offsets(front, 0x34A02, 0x14C):
        event, kind, mode = struct.unpack_from("<3I", front, at)
        if event > 113 or kind == 8:
            continue
        count = u32(front, at + 0x98)
        opponent_count = u32(front, at + 0x128)
        weight = struct.unpack_from("<f", front, at + 0x12C)[0]
        density, grid = struct.unpack_from("<2I", front, at + 0x134)
        scales = struct.unpack_from("<3f", front, at + 0x13C)
        cap = u32(front, at + 0x148)
        if not (mode < len(MODES) and 1 <= count <= 8 and opponent_count <= 3
                and density <= 3 and cap < len(CAPS)):
            raise ValueError(f"invalid event inputs at {event}")
        rounds = []
        for index in range(count):
            track, laps, reverse = struct.unpack_from("<hbb", front, at + 0x78 + 4 * index)
            if laps < 1 or reverse not in (0, 1):
                raise ValueError(f"invalid course at {event}, round {index + 1}")
            suffix = "R" if reverse else "F"
            lap_bank = "P" if tracks[track][0] else ("L" if laps >= 4 else "S")
            rounds.append(f"{track}{suffix} x{laps} {lap_bank}")
            stages.append((event, index))
            used_tracks.add(track)
        rivals = []
        for index in range(opponent_count):
            actor = at + 0xD4 + index * 0x1C
            label = ascii_string(front, actor, 12)
            key = u32(front, actor + 0xC)
            model = presets[key] if key else "fallback"
            easy, medium, hard, setup, behavior = struct.unpack_from("<4hB", front, actor + 0x10)
            contexts = tuple(value if 0 <= value <= 9 else -1 for value in (easy, medium, hard))
            behavior = behavior if behavior <= 2 else 1
            tuning = "adaptive" if setup == -1 else f"fixed {setup}"
            if mode == 3:
                tuning = f"score row; authored {tuning}"
            rivals.append(f"{label} ({model or 'empty preset'}), {numbers(contexts)}, {tuning}, m{behavior}")
        rows.append((event, MODES[mode], ", ".join(rounds), f"{density}/{grid}",
                     f"{weight:.4g}" if mode in (0, 2, 5) else "-",
                     f"{CAPS[cap]:.2f}" if mode != 3 else "-",
                     numbers(scales) if mode == 3 else "-", "<br>".join(rivals) or "-"))
    run = subprocess.run([str(probe.resolve()), str(root), "--sweep-manifest"],
                         capture_output=True, text=True, check=True, timeout=30)
    manifest = [tuple(map(int, line.split())) for line in run.stdout.splitlines()]
    if not stages or manifest != sorted(stages):
        raise ValueError("retail rounds and production manifest differ")
    features = {track: track_features(root, track) for track in sorted(used_tracks)}
    return sorted(rows), len(stages), config, features, tracks


def report(rows, rounds, config, features, tracks):
    start, end = table_span(config, 0x34B00, 0x82C)
    if end - start != 0x82C:
        raise ValueError("invalid AI configuration size")
    yield "# Career AI inputs by event"
    yield ""
    yield "Generated from authenticated PC 1.4 USA data by [campaign_ai_audit.py](../tools/campaign_ai_audit.py)."
    yield "See [the function map](AI_ORIGINAL_MAP.md) for consumers and parity boundaries."
    yield ""
    yield f"Production round manifest agrees with the retail data for {len(rows)} events and {rounds} rounds."
    yield "Event 85 has no playable round. Tournament rounds appear in authored order."
    yield ""
    yield "- Course suffix F/R is forward/reverse. S/L is the short/long lap curve input. P uses the point-to-point rule."
    yield "- The x count comes from the event slot. Runtime point-to-point races use a single competitive start-to-finish stage."
    yield "- T/G is the authored traffic selector and preferred player grid slot. Traffic density 2 or 3 selects the alternate ordinary catch-up curves."
    yield "- Rank weight scales the rank term in ordinary catch-up. The distance term has its own curve. Drag bypasses this ordinary controller."
    yield "- Pedal cap is the authored moving throttle ceiling from 422523/45FC90. The held grid and GO transition have separate pedal commands."
    yield "- Score scale is the drift multiplier on Easy/Medium/Hard. Drift rows are score producers, not physical competitors."
    yield "- Opponents list authored label, car, Easy/Medium/Hard contexts, tuning selection and behavior mode."
    yield "- Adaptive tuning compares the current human ride with the authored rival car. Fixed tuning uses the given selector."
    yield "- A context of -1 selects row zero for speed configuration and suppresses speed-offset jitter. The raw selector is not a difficulty percentage."
    yield "- Fixed ride selectors occur in events 107, 108, 110, 111, 112 and 113. All other physical opponents use the adaptive branch."
    yield "- Only 1201 forward supplies a type-9 road link in the campaign course data. That course is used in event 20 round 3 and events 21 and 46."
    yield ""
    yield "| Event | Mode | Course/round(s) | T/G | Rank weight | Pedal cap | Score scale | Opponents |"
    yield "| ---: | --- | --- | --- | ---: | ---: | --- | --- |"
    for row in rows:
        yield "| " + " | ".join(str(value).replace("|", r"\|") for value in row) + " |"
    yield ""
    yield "## Course AI data"
    yield ""
    yield "Joins follow the F/R rule and are listed as type:count before flag and actor filtering."
    yield "Type 9 is the obstacle input. Intersections and placement rows are raw authored tables."
    yield ""
    yield "Road gate -1 uses 435CA0 (ordinary racers typically wait 12 seconds). A literal 0 uses the race-clock threshold."
    yield ""
    yield "| Track | Roads | Points | Forward joins | Reverse joins | Intersections | Placement rows | Road gate |"
    yield "| ---: | ---: | ---: | --- | --- | ---: | ---: | ---: |"
    for track, (roads, points, directions, intersections, spawns) in features.items():
        forward, reverse = (", ".join(f"{k}:{v}" for k, v in sorted(counts.items())) or "-"
                            for counts in directions)
        yield f"| {track} | {roads} | {points} | {forward} | {reverse} | {intersections} | {spawns} | {tracks[track][1]} |"
    yield ""
    yield "## Context lookup"
    yield ""
    yield "The table below shows which values change with an opponent's selected context. Units for launch are seconds relative to GO. Shift bounds are RPM offsets."
    yield ""
    yield "| Context | Road weights | Road speed scale | Drag launch | Drag upshift | Drag downshift | Drift score factor | Catch-up grace (s) | Jitter radius |"
    yield "| ---: | --- | ---: | --- | --- | --- | --- | ---: | ---: |"
    for context in range(10):
        row = start + 0x380 + context * 0x54
        get = lambda off, count=1: numbers(struct.unpack_from(f"<{count}f", config, row + off))
        values = (context, get(0, 3), get(0x14), get(0x1C, 2), get(0x2C, 2),
                  get(0x24, 2), get(0x44, 2), get(0x4C), config[row + 0x50])
        yield "| " + " | ".join(map(str, values)) + " |"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("root", "probe", "output"):
        parser.add_argument("--" + option, type=Path, required=True)
    args = parser.parse_args()
    rows, rounds, config, features, tracks = census(args.root, args.probe)
    args.output.write_text("\n".join(report(rows, rounds, config, features, tracks)) + "\n", encoding="ascii")
    print(f"PASS {len(rows)} campaign events, {rounds} production rounds; no races started")


if __name__ == "__main__":
    main()


"""Export one committed source tree as navigable review volumes and original support files."""
import argparse
from collections import defaultdict
from hashlib import sha256
from pathlib import Path, PurePosixPath
import subprocess
from zipfile import ZIP_DEFLATED, ZipFile


def section(path):
    p = PurePosixPath(path)
    top = p.parts[0]
    stem = p.stem
    if top == "cmake" or p.name == "CMakeLists.txt":
        return "15-build"
    if top not in ("src", "include", "shaders", "switch"):
        return None
    if top == "shaders" or "shaders" in str(p.parent):
        return "12-shaders"
    if stem == "kr_logo" or stem in ("player_angle_data", "player_heading_data", "intro_movie_tables"):
        return "14-embedded-tables-and-art"
    if stem in ("main", "race_main", "viewer_main") or stem.endswith("_main"):
        return "01-entry-points"
    if stem.startswith(("lan_", "split_")):
        return "10-multiplayer"
    if "audio" in stem or stem in ("frontend_music", "frontend_sfx", "music_playlist"):
        return "09-audio-and-music"
    if stem.startswith(("ai_", "intro_ai", "quick_rival", "race_adaptive")):
        return "03-opponent-ai"
    if stem.startswith(("race_traffic", "traffic_", "track_contact", "track_collision",
                        "car_collision", "car_contact", "race_contact", "race_crash_road",
                        "race_hull", "world_", "prop_settling", "race_props")):
        return "05-traffic-and-collisions"
    if stem.startswith(("vehicle", "player_", "nitrous", "steering", "drag_input", "drag_shift",
                        "drive_", "gyro", "engine_balance", "skin_inputs", "handling_", "det_sqrt",
                        "input", "race_input", "race_debug_input", "path_follow")) and stem != "input_camera":
        return "04-vehicle-physics-and-controls"
    if stem.startswith(("frontend", "career", "campaign", "profile_pages", "custom_vinyl",
                        "car_parts", "part_placement", "wheel_paint", "screen_text")):
        return "07-frontend-garage-and-career"
    if ("hud" in stem or "camera" in stem or stem.startswith(("minimap", "intro_movie", "intro_player",
            "start_crowd", "eagl_animation", "race_pause", "race_leaderboard", "race_window"))):
        return "08-hud-cameras-and-presentation"
    if stem.startswith(("track_", "racing_line", "ab_route", "freeride", "stock_scene")) or stem == "track":
        return "06-tracks-and-scenery"
    if stem.startswith(("deko_", "gpu_", "draw_", "car_shad", "car_neon", "headlights", "render",
                        "scene", "speed_blur", "light_glow", "race_gpu", "race_particle", "race_skid",
                        "race_effect", "wet_road", "stable_draw", "stock_frame", "stock_view",
                        "texture_filter", "runtime_skin", "body_skin")):
        return "11-rendering-and-effects"
    if stem.startswith(("race_", "event0", "drift", "intro_grid")):
        return "02-race-simulation-and-rules"
    return "13-assets-platform-and-diagnostics"


def git(*args, **kwargs):
    return subprocess.check_output(["git", *args], **kwargs)


def tree(revision):
    records = git("ls-tree", "-rz", revision).split(b"\0")
    paths = {}
    for record in filter(None, records):
        header, name = record.split(b"\t", 1)
        mode, kind, oid = header.decode("ascii").split()
        if kind != "blob" or mode not in ("100644", "100755"):
            raise ValueError(f"unsupported tree entry: {name!r}")
        paths[name.decode("ascii")] = oid
    ids = sorted(set(paths.values()))
    contents = git("cat-file", "--batch", input="".join(i + "\n" for i in ids).encode("ascii"))
    offset = 0
    blobs = {}
    for oid in ids:
        end = contents.index(b"\n", offset)
        got, kind, size = contents[offset:end].split()
        if got.decode("ascii") != oid or kind != b"blob":
            raise ValueError(f"invalid blob response: {oid}")
        offset = end + 1
        blobs[oid] = contents[offset:offset + int(size)]
        offset += int(size)
        if contents[offset:offset + 1] != b"\n":
            raise ValueError(f"incomplete blob: {oid}")
        offset += 1
    if offset != len(contents):
        raise ValueError("unexpected trailing blob data")
    return {path: blobs[oid] for path, oid in paths.items()}


def make_volume(name, revision, paths, source):
    prefix = [f"NFS Underground Switch | {name}", f"Revision: {revision}",
              "Contents (first source line in this volume)", ""]
    ordered = sorted(paths, key=lambda p: (PurePosixPath(p).stem, p))
    content_line = len(prefix) + len(ordered) + 2
    entries = []
    offsets = {}
    body = bytearray()
    for path in ordered:
        raw = source[path]
        banner = f"===== {path} =====\n".encode("ascii")
        body.extend(banner)
        offset = len(body)
        entries.append(f"{content_line + 1:7}  {path}")
        offsets[path] = (offset, content_line + 1)
        body.extend(raw)
        padding = b"\n" if raw.endswith(b"\n") else b"\n\n"
        body.extend(padding)
        content_line += 1 + raw.count(b"\n") + len(padding)
    header = ("\n".join(prefix + entries) + "\n\n").encode("ascii")
    return header + body, {p: (len(header) + start, line) for p, (start, line) in offsets.items()}


def export(revision, output):
    source = tree(revision)
    groups = defaultdict(list)
    files = {}
    locations = {}
    for path, raw in source.items():
        name = section(path)
        if name:
            groups[name].append(path)
        else:
            member = "support/" + path
            files[member] = raw
            locations[path] = (member, 0, 1)
    index = ["NFS Underground Switch | Source review", f"Revision: {revision}", "",
             "Open a numbered text file. Its contents give the first line for each original path.",
             "source-map.tsv maps every committed file to its volume, byte offset, length and SHA-256.",
             "support/ contains tests, tools, documentation and other supporting files at original paths.", ""]
    for name, paths in sorted(groups.items()):
        member = name + ".txt"
        files[member], offsets = make_volume(name, revision, paths, source)
        locations.update({p: (member, start, line) for p, (start, line) in offsets.items()})
        index.append(f"{member:40} {len(paths):4} files")
    index.append(f"\nTotal committed paths: {len(source)}")
    files["00-index.txt"] = ("\n".join(index) + "\n").encode("ascii")
    manifest = ["original_path\tmember\tfirst_line\tbyte_offset\tsize\tsha256"]
    for path, (member, offset, line) in sorted(locations.items()):
        raw = source[path]
        manifest.append(f"{path}\t{member}\t{line}\t{offset}\t{len(raw)}\t{sha256(raw).hexdigest()}")
    files["source-map.tsv"] = ("\n".join(manifest) + "\n").encode("ascii")
    with ZipFile(output, "x", compression=ZIP_DEFLATED) as archive:
        for name, raw in sorted(files.items()):
            archive.writestr(name, raw)
    with ZipFile(output) as archive:
        if archive.testzip() or set(archive.namelist()) != set(files):
            raise ValueError("archive verification failed")
        restored = {}
        seen = set()
        for row in archive.read("source-map.tsv").decode("ascii").splitlines()[1:]:
            path, member, line, start, length, digest = row.split("\t")
            if path in seen:
                raise ValueError(f"duplicate source path: {path}")
            seen.add(path)
            if member not in restored:
                restored[member] = archive.read(member)
            data = restored[member]
            start = int(start)
            fragment = data[start:start + int(length)]
            if fragment != source[path] or sha256(fragment).hexdigest() != digest:
                raise ValueError(f"source mismatch: {path}")
            if int(line) != data.count(b"\n", 0, start) + 1:
                raise ValueError(f"line mismatch: {path}")
        if seen != set(source):
            raise ValueError("missing source paths")
    print(f"{len(groups)} volumes; {sum(map(len, groups.values()))} game/build paths; "
          f"{len(source)} committed paths verified against {revision}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="new ZIP path; run from the source checkout")
    parser.add_argument("--revision", default="HEAD")
    args = parser.parse_args()
    export(git("rev-parse", "--verify", "--end-of-options", args.revision + "^{commit}").decode("ascii").strip(),
           args.output)

"""Check event 1's retail replay pins, using only fresh output directories."""
import argparse
import csv
import io
import hashlib
from pathlib import Path
import subprocess

STEPS = (1, 180, 600, 6000, 11344, 12459, 13128, 24300, 35396, 36510, 37177, 48336)
# The integrated sky, shadow, smoke, car details, glow, and tuned paths change
# sampled CPU frames while preserving the replay state and trajectory.
# Replay steps, state hashes, input recording, and trajectory are unchanged.
FRAMES = (
    "672BF7722883C627042E29355768A1A14BEC7C64D8F47B2459231D14DFD0BF3D",  # 1
    "5F91E6938B2A327D33798BE2064A414760094CAFD65233B62FD00E2275DC3B37",  # 180
    "321D71419E6C33DCC604DEAE5ED3BB45DDA16A7B4C911E158954E4BC957673EE",  # 600
    "8AB08460DA9D9C89F17CAB1FE1F62E83B1A2F0ECF02FCE455CAA93D4F915583F",  # 6000
    "2FE6D7AB5B6A342356E8076C54457BA7A195E3A7051C1EA685FD0E6924EF34D3",  # 11344
    "44D2306D5AF54AA9DBA67A1E35ACE31EED76352FFB45267D9F862EE51E23CC83",  # 12459
    "2CBF4FB21522F6F07BDC64C870CDDBE38C43D0E73447B1A46D9BB9BEC82A420D",  # 13128
    "447D4A133B96F7356F840ADE6EBD6D4FFD4D640B71A8B13FEE7FC9DB85A10D97",  # 24300
    "5F3CA957CD64FD816561E272F3B94F6B9DFAF144565759FCF27333775A1E6DCE",  # 35396
    "850FFAA3CBB9AE45F932802AA839863067F4DAB0670C994E8636BB6466AA37D5",  # 36510
    "36F096C4708179A0430D1DA82660CE011681EC6546F860F864407DA96ED86F1C",  # 37177
    "E812C39D8DA3517E4306934486EB7FDBFDC997663B06A869EC52C9C9A71F6F85",  # 48336
)
# #237: visual-only preset. Same full replay, state/trajectory and input pins.
TUNED_STEPS = (180, 600, 6000, 13128, 48336)
TUNED_FRAMES = (
    # Launch view: red body and the KIT01 rear bumper from the chase camera.
    "E43EB3C3AA5460F1B898BA38E3A28FDEC14F8391493A34DA51DE247B2611F028",  # 180
    # Early chase view: rear of the red car as the replay accelerates.
    "5D00D7BC6E377E27747A00F768444B85D6B4DB2AD9D444766739EBE87BD66E0C",  # 600
    # First-lap driving: tuned assembly follows the moving chase camera.
    "2B681B85E295EE4488A163A5503F767EFEB9CFB853401FB1D4488292799BC18E",  # 6000
    # Braking: the tuned assembly retains the existing lamp animation.
    "07549E0C79C096775FCD5D8B700D13508DAFC49BF3FF8FCBF34345C828DE220D",  # 13128
    # Finish: selection survives both laps and streaming transitions.
    "5EDAEA64B59E1990881192FEBAE93D6DF95F51A3B43854871B30C0BDDA940B44",  # 48336
)
# Glow overlays use the same post-car combined-depth order as Switch.
GLOW_FRAMES = (
    "40E9D2E4AF652F99EDB3B1E0A7DE0404A3579FC83C07AFADEBAD9D6D91E7406C",  # 1
    "1C74EEBB1460A094DE3BB1596DADEB734841FAC8A02D00AEEC46BBD0821FDE81",  # 180
    "EC6ED7684AB502CD381B26CD6DD906C5D2CFACF044D8EE99A42FB1F927FFD991",  # 600
    "A236D3B87AB63EB6A91C635C9F399388587C68F41114552DF48B0C6620C24623",  # 6000
    "59A9A6F9CEB6337AD1EE389776A4A055E9DD1A2FBD027A77A633C569003F7444",  # 11344
    "967AC57F2260BAE52E8AA9B96A9D386E0B54A5CEB80A04EB6554F30DB3CE696C",  # 12459
    "7793851371AF38490EADC8F5C2A693428D1A8D56A22F5F0943EAD29ECD3F9D6A",  # 13128
    "DDB38F77424D8BB9C5C8C54DCA01CCCC267DE41C16133F10B80ADD126F878EA4",  # 24300
    "6333E97D6A060B4E2F73E7269B6BA760431D62CBE2339D192B477F048AD50418",  # 35396
    "670532DDC41CB247D6D3D369D7F218184236DAF9C0D09EE1391E3C53C3E35016",  # 36510
    "E5517AFD2C3E57911B2F85872A54C117CDC8847159AA33C03025B8AC135A8025",  # 37177
    "4833DDF209DE04786194835F9BCAF3C88030FF54E94B0625C876D1802A9A01D6",  # 48336
)
TUNED_GLOW_FRAMES = (
    "36B43A01021F9DF2CABD66BB7566672DA48EA8FEB0149F3D4C1B231530164EF5",  # 180
    "F2ADA1DEBDAC6CBF5379227F2BD211D371C1C7E5F4B34542007109553112DE0A",  # 600
    "544AFCFE67AC51B4617D02FF4002A2749A9B65EF6876797D509318E07710F752",  # 6000
    "C4AA281BDBADE061AF3F3B51A8F9A9182C8B44E56869B9E2DA0C8B591EE99875",  # 13128
    "25C4DF9E2A446D057B12E04471F4876BD7236E39AAF111CEF6F4E221AC24D663",  # 48336
)
# #182 opt-in frame 180 exercises the CARSKIN-to-blue-paint approximation,
# the other recovered car materials, smooth normals and retail alpha/blend.
# Track and simulation pins stay unchanged; the off-state frames include the
# integrated always-on sky, shadow, smoke, and car details.
SHADED_FRAME = "43DFC24146020BEA0381F78A78B728D4247A191FBA3EE34BD477F015D6821C6F"
SHADED_GLOW_FRAME = "DCE6A1CE9C0FDA505797B3F28EB2C22F40ECFAD66DC0C8A18A614F3BA7DC5BE3"
SHADED_TUNED_FRAME = "6666BB2141120826C3A83CE06B73A96A011650239354D683975FB2C8EA047AA5"
SHADED_TUNED_GLOW_FRAME = "964C8E5C44ACB15CEB352320DF705F4F0AF73C41196E626B0961B8D24B9B9832"
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

# Real mirrored-scene pass (#183). These are complete TGA file hashes.
WET_FRAMES = {
    180: "A6F1CD94E6889A2BFF78C42C596C03A698D299A54A80AF92E448629CC47017A1",
    6000: "8B9C0F74497251F9B5427087A16B4ED047FE8AD0454529A3071501275301C6CD",
    11344: "7AEE45719308E589E3B57A3B3769C710EAF17FE867F5C9762773511AF5210BA7",
    11360: "B05CD79EF8C971B004EC3486C7A26DF3B6FEFDA174C383D369F3269179FEBB9B",
    48336: "FF111C75F9E26332D29AE79F5D63012EE3B60E46DB71920DF82CE209B06810D3",
}
# The wet target includes car and glow draws. Each visual option tuple needs
# its own frame pins to exercise effects independently and in combination.
# Keys are (tuned, glows, car_shading), in WET_FRAMES step order.
WET_EFFECT_FRAMES = {
    (False, True, False): (
        "0A30A948307BE18FE1DBC320CB8B79154EEBA984FC565ADB104E3C1167A1006A", "EE88D8CFF6DB88A5578FD2133198199A1664B76A6F872657AD20F87BBF38E6FE", "2611D63C226A3A78E6C3CF31CA5519AB6DB974F2246E55414E26D68481CBBD69",
        "836EB4E991B9F4DE0CA1696F29C6949B4AF868D768C1CA9455BD8C47D9CDD6F4", "46C5F45BFEFDACE98CB1E359A45F5136E05BD937465002F2AD85828796C08EF2"),
    (False, False, True): (
        "591E6830A32C23B6D44B023B3E2D7966300DC0357960D9BAA5E08D1AD37CB931", "3B00FB866AD5794AC2CCB5EF7EFF0BC571CEE2E81E0130FDD1D1550601E2D561", "007C621A33F1996F263A33D3B9395955DC62C77DB53D98B08AD14A422C3D3A7A",
        "E28FBC0658DF2D05BBE9784D10A7845D3C3D5566DDFD83B63A3E9A78A447B94C", "4A9D7EF55839B19AF1C9DA4266D28EE8AAF1A23D36773353DA9496B4324CD5CC"),
    (False, True, True): (
        "AB07B8F923E24B43C8F35E7EC9CADEE6F980B9284016C1A6EB58C11A9D32B532", "A2A5CDB5643D535F67E8DCFC1FA1C82CB577FC7E42E4A3A112C3D3D9137BE00C", "28F220F368724A0DBD0BB36FA40568EB254BE41A598189ECF08E41F6A9E3F85B",
        "86CF9F9D71B922DE2FF9327EC688DC38BFBAD7DE8F5AB74E7F29353E41A3B2A8", "144E384BF03B2C73B452B9763FC356E74CEBB9F4715EBC2AC30C85AA7B041B6B"),
    (True, False, False): (
        "A49ACEF17F43397244BED0142BD60C1EB7F0CBF7B90EEDCC558A475BF1FFE8FE", "356A09135018A3EFEFF9E74EAADF50DD927F6AFC60D4B5DEDB0C7C91DF39CA4B", "BA6676D98525ADDEB3D30C82CC4E28491AC1D2D080F2735C3D3BB64AF6DBD32A",
        "5DD20D5193255B9C3812C59053AEDE2915C7D45ABFB7375FD93973ABF826B539", "990DAB09D0E69C45E302D231272B9132610932CC31828FB0708FA11E80B888AF"),
    (True, True, False): (
        "E098264B45DCA62AAACF4F8BEE20DA300B17F7A71CE6321EC0931A7B73B1B7BB", "E8F881E91D169441186BEAE9024A9F623649ADD2A2F951F0C362BC88F3153E8E", "B59838363512D8E47DFDB9E13B5127EBDE2D11202E1A2062ED8631CEEF7E74E5",
        "70C089CFFEC33CDC9B75CD9F94B278C8AC415299F969E17EBBA41BF925E5F90E", "FD0B347D95F4FE45E0CFB429CD3216C19557E6CCFFA73803F239064162CC1D8C"),
    (True, False, True): (
        "8C7E15B1969F82F8692A673E85F0E826C894475BA04A840B0DBF4E6C491219E4", "F36520B1A494B09238640187D40181CBE8959F0CD6B919EDFF30EC4E3CD3C1C4", "9CAE09AF6F2DC2C28CBEB37DDAA1F5257F3659598F0E1A573EE694C6BF6D16E3",
        "D2E7975C015498114C4BDA36D37F929ACB433A7C6928AD484E065DE3DAAA5921", "1E3CD52F4474177AED0C6EEAD38E1B32400BEFEF14B5401C793B5FBF68AEC81A"),
    (True, True, True): (
        "E68C9E4C8D683B2858DDA800C9C226CCABD17D01494F8100A5BE0FA885CB2F45", "038ABBD5E2BB5097DF72B9F2FC46E6DEE9D2F6C6E093FA87DDAD211E07D075B6", "CD9F04778F60889162041B1009220DB2F8BFEA5365E42DE8CC58A9339658FE6B",
        "7FD6242E73716096DAECFBBDB2188A47EB44358CD627CF09B883E28CC71AFB89", "C65CBFD0AD25374420F1B3A72A3329495EE1B29C77263DB569020D8D5D98EEBD"),
}

PATH_FRAMES = {
    "off": (
        "A8270F7AA0DC1671807992BB24947B5FAD5843370032AE3A0183D90224ADD43C",
        "FB345706C07DDE9DF20F42501922D238181F0C668D6F9B39B05EABD9E4E92507",
        "EF5797014996C2A3813A50148F38982BC92A5840B37F009FDD93099D487381EF"),
    "on": (
        "E10945FADE9F308C953D55F55D5D0849B8B05079C8AF3DAB82DC3B6C376A07B1",
        "CF6FCF5F005ECE1E869D0028B902BFFA69C830C7C9B51915F1E4604B74ABDC08",
        "61BD6A2D6EFC0B32B1336584862055871C03E43F16E553E46495583D572861A2"),
}
PATH_RESULT = (
    "sequence: sha256 F64185A1E34258C03126E86EC80DC021716D3F0FE509933F6A8B0BD4D18CB643 steps 6903",
    "costs: sha256 0AF10104BAB722F0E186CB0861B76BA6F01ED90144363F6D0D394091350C9D5C steps 6903",
)


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
    parser.add_argument("--tuned", action="store_true", help="check the shared tuned visual preset")
    parser.add_argument("--glows", action="store_true", help="check marker-driven night glows")
    parser.add_argument("--car-shading", action="store_true", help="check the opt-in shaded frame and complete simulation")
    parser.add_argument("--probe", type=Path, help="also check the paired wet hand-path frames")
    args = parser.parse_args()
    frame_steps = (180,) if args.car_shading else (TUNED_STEPS if args.tuned else STEPS)
    if args.car_shading:
        shaded = (SHADED_TUNED_GLOW_FRAME if args.glows else SHADED_TUNED_FRAME) if args.tuned else (
            SHADED_GLOW_FRAME if args.glows else SHADED_FRAME)
        frames = (shaded,)
    else:
        frames = (TUNED_GLOW_FRAMES if args.glows else TUNED_FRAMES) if args.tuned else (
            GLOW_FRAMES if args.glows else FRAMES)
    require(len(frames) == len(frame_steps), "frame pin count differs from sampled steps")
    effects = (args.tuned, args.glows, args.car_shading)
    wet_pins = WET_FRAMES if effects == (False, False, False) else dict(
        zip(WET_FRAMES, WET_EFFECT_FRAMES[effects]))
    require(len(wet_pins) == len(WET_FRAMES), "wet frame pin count differs from sampled steps")
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
        if args.tuned:
            command += ["--tuned"]
        if args.car_shading:
            command += ["--car-shading", "on"]
        if args.glows:
            command += ["--glows"]
        if args.check_driver:
            generated = output / (str(index) + "-generated.txt")
            result = retail_run(command + ["--generate", str(generated)], root)
            (output / (str(index) + "-driver.log")).write_text(result.stdout + result.stderr, encoding="ascii")
            require(result.returncode == 0 and not result.stderr, "driver failed; see log")
            require(generated.read_bytes() == recording.read_bytes(), "scripted driver no longer generates the recording")
        result = retail_run(command + ["--input", str(recording), "--out", str(prefix),
                             "--frames", ",".join(map(str, frame_steps))], root)
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
        for tick, expected in zip(frame_steps, frames):
            frame = prefix / ("frame-" + str(tick) + ".tga")
            require(digest(frame.read_bytes()) == expected, "frame pin differs at " + str(tick))
            require(lines.count("frame " + str(tick) + " tga-sha256 " + expected) == 1,
                    "printed frame pin differs")
        wet_prefix = output / (str(index) + "-wet")
        wet = retail_run(command + ["--input", str(recording), "--out", str(wet_prefix),
            "--frames", ",".join(map(str, wet_pins)), "--wet-road-reflections", "on"], root)
        (output / (str(index) + "-wet.log")).write_text(wet.stdout + wet.stderr, encoding="ascii")
        require(wet.returncode == 0 and not wet.stderr, "wet replay failed; see log")
        for line in RESULT:
            require(wet.stdout.splitlines().count(line) == 1, "wet replay changed result: " + line)
        require((wet_prefix / "states.txt").read_bytes() == state, "wet replay changed simulation states")
        require((wet_prefix / "trajectory.csv").read_bytes() == trajectory, "wet replay changed physics trajectory")
        for tick, expected in wet_pins.items():
            require(digest((wet_prefix / ("frame-" + str(tick) + ".tga")).read_bytes()) == expected,
                    "wet race frame pin differs at " + str(tick))
        print("PASS wet replay: paired states/trajectory/result and", len(wet_pins), "frame pins")
        print("PASS", exe.name, str(STEPS[-1]) + " step hashes, result, " + str(len(frame_steps)) + " frames, clean 120 Hz trajectory" +
              (", generated recording" if args.check_driver else ""))

    if args.probe:
        command = [str(args.probe.resolve()), "track-stream-path", str(root / "Tracks/STREAML1RA.BUN"),
            str(root / "Tracks/TRACKB1003.lzc"), "1003", "--hand-path", "1003", "--list", "none",
            "--frame-every", "3000", "--width", "1280", "--height", "720"]
        for name in ("Tracks/TEX1003TRACK.BIN", "Global/INGAMEB.BUN", "Global/GLOBALB.BUN"):
            command += ["--texture", str(root / name)]
        for mode, hashes in PATH_FRAMES.items():
            prefix = output / ("path-" + mode)
            result = retail_run(command + ["--out", str(prefix), "--wet-road-reflections", mode], root)
            (output / ("path-" + mode + ".log")).write_text(result.stdout + result.stderr, encoding="ascii")
            require(result.returncode == 0 and not result.stderr, "hand path failed; see log")
            for line in PATH_RESULT:
                require(result.stdout.splitlines().count(line) == 1, "hand path state/cost pin differs")
            for sample, expected in zip((0, 3000, 6000), hashes):
                require(digest(Path(str(prefix) + "-%06d.tga" % sample).read_bytes()) == expected,
                        "hand path " + mode + " frame pin differs at " + str(sample))
        print("PASS wet hand path: paired sequence/cost and six frame pins")


if __name__ == "__main__":
    main()

"""Check event 1's retail replay pins, using only fresh output directories."""
import argparse
import csv
import io
import hashlib
from pathlib import Path
import subprocess

STEPS = (1, 180, 600, 5449, 5450, 6000, 11344, 12459, 13128, 24287, 24288, 24300, 35396, 36510, 37177, 48336)
# The integrated effects and native HUD both affect CPU frames.
# Issue 253: retail fill, fonts, anchor and FEng registration replace the
# approximate disc/safe-area layout. Only visual pins below change.
# Owner font correction: thin Conduit Italic and native FNTF LCD metrics.
# Owner layer correction: Needle paints above Tach scale numerals.
# Gear-slot and finish-line brackets retain HUD coverage.
# Replay state, trajectory, input and step count are unchanged.
FRAMES = (
    "A773FC78816A5D2463ED1CDEB5AC0551D1A61C1FA55A7622F5EEF629FA8DBE0D",  # 1
    "9DCFA74DF4B1B599F8D3DE506998B30F3F535CBF2F87166D3605FA5C95CBE555",  # 180
    "DDEA0EE6E77341B24C9BF084FBFE2489A4FA3700147261B8DCC96C4E5CE5941B",  # 600
    "A9EA9D90515D22914F8A388EB6FDAF8A877E59E73C2F1E896BD66444142C7747",  # 5449
    "598658FF74A740B668A8229D6DAE2B46A2656E34FC56D653B3E2711AEEF26253",  # 5450
    "79DD59F023DE03EFD0570338A70E87A63C81D37607F9B70F2BF0973BF58F8F71",  # 6000
    "E4BEA5104D39AFD0BA2F94B653FBAFA5E1CEDE4CCD06746316AB61E3BD9503AB",  # 11344
    "9260C22B9C8D271000CB107E88D07FAD5B239F47B1024203DED27160684D863B",  # 12459
    "884574A8AEE54729B1423F4024DC9828844AFAFFA7D413F13363A3CD5C40C7D5",  # 13128
    "CEA5A7232121F745E4071630A329FA11D314E1951F1376982FE1E734A1854D07",  # 24287
    "F5C81EA5B9C336C7F5878C377219476EB5362E7803446067654F52E747895CD0",  # 24288
    "DDEAF593AA6D296E8460D7BB1BB2A4AA568B4F92C782681F56E61CAE196B9B1B",  # 24300
    "4C37538E70CDCB08C75085043A5A1F4BB4C66ADBFBDCDABC16EF57865DAD7EF7",  # 35396
    "A0D9898BE3203533028F2B48F0B63C06F4F98B70CA02310DCC3F1BC6547F0A9C",  # 36510
    "B5A6D25D5539B8B0C48A8D898D8C393755140DE6C2441C3A705E5B9570D52959",  # 37177
    "895A03572F3AC4E5A67A5378535887C18A321C3E4BE8D4AC6BEAC6FCC251F43D",  # 48336
)
# Tuned, glow and shading presets use the same HUD overlay.
TUNED_STEPS = (180, 600, 6000, 13128, 48336)
TUNED_FRAMES = (
    "AAD788E77C6C69F07557832A38AE7C9BF8F6281ECCC3E30C4F0BDE121B8BC60B",  # 180
    "B1528AAF4EAABF0C34B178030114CB60B978279C884EE045EF4A35E7079E9991",  # 600
    "9D8767FB36F3386461BCD0BFA6FD3CECF14B47B5615D83157F6B3EF811F2C9E4",  # 6000
    "C2E4CB3F573CE5A4896BC13534ECF8665BE5714F874D73AE82A2F6C50C5BD1DA",  # 13128
    "C40A60AFB7C6A570FEAF66BD5178F9A426176E0254AF09A6E5E8B19DC88B20BC",  # 48336
)
GLOW_FRAMES = (
    "6DBC44C73CEDD559C955199C95D492E2C7C111DB81A9BB0E20E72BFB3BBC505A",  # 1
    "954325E7308F0B39CCA9111644D6A5EE39B336F945F41D4C27571EF63D4DA1F4",  # 180
    "453CB515B76475F3592AB3642FA3153E16A839D198B0A36B5BCACC480ABB4D66",  # 600
    "FBBD3416A3312E35703D8F1AEA3F8FE7ED13C2C9985BE5A4589A7A69A43CB02A",  # 5449
    "9CBFE0814D6A27F5B6366A8DE3AED4BAA717755E550780D79FFADB1885473D1E",  # 5450
    "805E03E2ADEAC53388F5DD8FFA453C33F46D899B99286F6DCC23E27BCC388EB5",  # 6000
    "1D29FC5D20A644AA742CF6C08608EA43DD1D868276C03474A9B0EE6ABCB10DAD",  # 11344
    "D55B323718473962676D56D6A5BD66F5A84D3E4FA01B6C675E7350BF5F9A5FFE",  # 12459
    "C12B25FD49B6C6EC3EA55884F19C1B9569BB6FAAE9691F95BBB97D2FC577DDE7",  # 13128
    "D7F63C5759B30ACDB571176190A6776D35402E33413CF402442654DC349B2DF0",  # 24287
    "D58ABE5AD87E8F6A134FFCFC24ED75EE287CBFC2CB4139D5E4AB3E378A6EFFD2",  # 24288
    "E5583F49AFBCABDDC60CB461711E6467CE5A711A764AE7ECAF9D76A8095DE8D9",  # 24300
    "C4D5B917F3D678DE5A5753AE40603B0A1314FF065C50064EEE10AB8D16460B96",  # 35396
    "9CD39CD82C4A285DD62CE60C97E39AC2EAC2591B5DA8AA210A90AC2CD14BC0AD",  # 36510
    "1F116BD4AE3991CD3D71E91967CD7F929751EAF3271F180832E3B8DFAD5960F9",  # 37177
    "DF271DE539D7F26890E76C01FA667E7D468B8FF4F1D497BD53F12AAAADAA952B",  # 48336
)
TUNED_GLOW_FRAMES = (
    "1460A652091091FE4E4D0A126E140ADACA16776661FE1AACCFE9FC2FE8B0898F",  # 180
    "236761DF94E36E1E71977942C8A5D485CCE4FC06C88B79D184DFCB0222E31E29",  # 600
    "AC8B71D02F0598D54F54C9C7EAF8DEB275549858200EB15797243EE4E946F8ED",  # 6000
    "7C11DC752DE3FC9DFEFCBA0E5B1084EDE1AE90F7D9899798DA4F71746D857284",  # 13128
    "72AF9023373998BFE869E9770884AD48E62A4A540E0514D10F33E2DAD859BE0F",  # 48336
)
SHADED_FRAME = "E384589FAC920C1086507B0F404C0DB6F1FA8EF61E6CC95C76742AD515F19349"
SHADED_GLOW_FRAME = "F4B8E3804D393554F69150466CBB06B5F5E4583DFDC8AA8944DC0CC07241C625"
SHADED_TUNED_FRAME = "637E6B597F347FAC5106D27A34EF776A8DF6FBF4AF91FEBE2731EFAD146C0624"
SHADED_TUNED_GLOW_FRAME = "4916983646A715900ED171CEEFCDE19A88A850A9D8DC32B031F838C262698440"

# Wet road pins retain all seven optional effect combinations.
WET_FRAMES = {
    180: "F54BB16D93375C73A007ED3AC312A738C598C310124355F868F8CB4A9E57B000",
    6000: "8D73942E52DD254823B5A83A79E7F15AA75B0BAC439F26E45AAB513C572CEDEF",
    11344: "27DA5CE0E5C2F6C3159C0E0D42F05D798FC55A2E26098AA20A881480B2DFC504",
    11360: "4234EF60AE5E67C43666AE9A2947DF0B85D5C4580A4897C9DFB20A67F18C8293",
    48336: "17A8F590B213C190680E5937F0BA1F59FF6EE57638B0E20F7C8F50D671343A44",
}
WET_EFFECT_FRAMES = {
    (False, True, False): (
        "A5F94EB890C3E826B8FC4A354E7B7C6878A594A5A57D44A77CECF7F1CFCE2EC0",  # 180
        "728012F91D46C8100526E674F52CD683B2A1FA709B044403618D6A7572975271",  # 6000
        "F2E205C974A5BFB211CA03797844CF6059270F0EC875EA838310799847F4974D",  # 11344
        "091E5E971F39133EF37B3845E0B8CA8E7260BB371CA15E7A7F78604D6885DF81",  # 11360
        "05E7C631F6355D0EB79215C9F9E561CF34B58B6241DAE0B359FF45CAC830703B",  # 48336
    ),
    (False, False, True): (
        "7108D8DB76D4B0340CF47C2970D796F075393859521C072C5E494BCCB2BB63B0",  # 180
        "D9F7BB6299B0E4C86FC4089883E9C5104AB34F4A45F6146E31E99903D410DB18",  # 6000
        "90A4BC9133345E1667C6EA31D64FAD90B91E4ABCBF2FC027935C07D6A33F668F",  # 11344
        "53E1EAB3CDBEF98B523875DF2CC25D6AAC843F8654C0563B07236AB939F20D46",  # 11360
        "AAE2154C3C35FE53308BD68E42A3513899F670D5D7D631B9B136AE572ADEF15C",  # 48336
    ),
    (False, True, True): (
        "C65BB217702B6AF28C757F1E38F693C2538DD77CEAECC599121C6EE78967F1F6",  # 180
        "E79A80E86FFB2F0FBF5CAAF0916F9094C4B7A69A689978256429593F60FFC327",  # 6000
        "91B680BB8D47129F04F0025D083A3FF995141D17FAA9CE84B53848235D3B4B35",  # 11344
        "0FD2DDEA85A2009494646668F71DE0D14381B529FF22011E223B04389FCDE1D0",  # 11360
        "E5A14706C8834828EA53BC5C627C8CB8D3485DCA9D0934F2C0EA9D244255F38E",  # 48336
    ),
    (True, False, False): (
        "50EBA2CF9BEFF0FD1BC59D4D6FF620A43A42AD7F01690AA80948ECDF80C9B0C4",  # 180
        "C9266E67EEFB6C8FAEA799479567C99EA66937AA0E28A13F1FD8E96001F51B65",  # 6000
        "8ECB7D6500F721AACA83CC115D36CC170105CE4C5CD33F29D4A1D4E1B30C4D40",  # 11344
        "2BAFA666AEE0E4D3ADD193C622DECD72F2FE6FFC4B0CE94F5F7682F8ABC466AC",  # 11360
        "A3EC42B4E437B90E02FD373499214CE5D8831E6E82CD1CD125A4D8FE3A8513A9",  # 48336
    ),
    (True, True, False): (
        "59D4AC60F2703902BCE0CAF02F133B1BA7FD755F40C8BB687AC2A751C0A3F836",  # 180
        "B459619EC574C09EFE36A9D6FB7AB83554A96EA19E39F83608F339BEF7798F32",  # 6000
        "44C8FFBA3A50CEB8716DC6A2F5879B282AA5368C50B6D4FBFA4C3D16C3FA8333",  # 11344
        "3F8BFFAC8C2CEA1D0396231C79F732301643FFE2FD320E69146859B20A7C4428",  # 11360
        "D6DA64F9627243DF04B8D389A56DD0006759F6979508740420965B2B09A27695",  # 48336
    ),
    (True, False, True): (
        "C7EC908BC26C99BF57989815ABBD6E9F7EF5613C6A6E9D38B68F50A63B33846E",  # 180
        "EC3D2EE3469BC98CCC0DB4A26DAA2B237DE76A91B2F3C6A5B1EB42304AFD4AD3",  # 6000
        "76C880B1B76D6FEDADC5313D27FF84E3C595A379C2E8F2A2E3EF4C2A4C7ED6B3",  # 11344
        "DC57F2A731DD8CC582AE760D9682CA94649D1DE83A5FFA0EA2D2514E19DCF8C0",  # 11360
        "F35CED7B98A5BB3486E9BD7FCDA20256076E818C0022A62BD9D28865686953B9",  # 48336
    ),
    (True, True, True): (
        "839896848F32F8753C996B8540AA594DD46045CD53FFB419FD466DB12483272C",  # 180
        "48539A275720BD937E47432935D8AB24E8A94BF7EC83840CB2B7D580C915D00A",  # 6000
        "2329F4A5EE1311981061091FF25C7EB4CB9B2B129A6BDE5D724A8759686B6CD8",  # 11344
        "85B589ECB601138C6849549418A87AD54D9DC9242823883B236E21B4276FC634",  # 11360
        "1F4267C544678E41C09EDE2A5ABFD96CDE2A5D585EDA6A17494395A637BB9F46",  # 48336
    ),
}

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

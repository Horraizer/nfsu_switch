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
# Gear-slot and finish-line brackets retain HUD coverage.
# Replay state, trajectory, input and step count are unchanged.
FRAMES = (
    "B8B254F87C35889282A68B3CF6A56F633C5527BE0974C810496CF596A16363CE",  # 1
    "3B9EC48AB041FB552DB5F025569F933A48965CB7623FB3057E0C92ABCA2F3CEB",  # 180
    "992A29971814D0B36818142F6EFDFEDFE335BC37AC7A6E38CF3238EA18FDCF00",  # 600
    "397249A634A909C565A5E8C22A758AE2E22A3B6602BBC2087E6ADEF2D7DE30F5",  # 5449
    "3AE093B24F217F7FF5CA57BD9EF0DB7F4FE20543EC5272DE2727C9C810D45575",  # 5450
    "2FBF4E1C271A59361DCE765D66638569F095F5D3598BA72191499D34AD299D51",  # 6000
    "D3DFE828C569AA95D8EFA2CDBDFFC8F957D53A2C04117597F73548739475A432",  # 11344
    "24D0B3B80F32A7F68CF189F26DEBDEC9C9176F633FE405D716BDF6BA2F9F4C5B",  # 12459
    "B3E397D75B4C3A8FA4C94236CC973C964C19BE120AD7C1E960BFAEC79C95F206",  # 13128
    "EC4AC64AD4FCA979FAF3CE1A472304196F5395F5CF3B016F82EA7F48C76FDFB3",  # 24287
    "A1051644C771B4E68E5EAEB2457708AD7CD4D4AF3667D9287E46EF993AC38313",  # 24288
    "230BC9A8F25EC5F988DA7B89E09B611BC82AD5C02A80DC70F67778DD0E8BB620",  # 24300
    "041C62AC0F55338391D4CE3752CD7E6D8977A3B53782FEFAEB0A687AE8A38610",  # 35396
    "79A6F0A150CA29C82D394DE7D3AFC85E2B4BB05DAA60D6D35F51105474FA38EA",  # 36510
    "25DFC5A247D925798FD9BD7CD58B297DFC1975EAEF2F36CCA09755FEF2D8B4B0",  # 37177
    "DB9CDBF051986486AA4A8B01A394BF884135C92E35C9DF62DA0B4774AED55750",  # 48336
)
# Tuned, glow and shading presets use the same HUD overlay.
TUNED_STEPS = (180, 600, 6000, 13128, 48336)
TUNED_FRAMES = (
    "01F49FC6166526C062533F1B75760CB558B610C10D28C442590B986B73D06612",  # 180
    "EED735DA8F32CF8A990630713C3F21F95F93CE3ED3561C4673349902D6F00124",  # 600
    "D7BCB70782BA35162BE619F4A8448083A3CFA4EBEECC9720B127BDD171B41EB7",  # 6000
    "A65DF03F52C5FED1ECAD63E7A0DEA53614730313358667295A29D32DCB15A85C",  # 13128
    "3E1E46081C16F9FF67EC96B863AC3E8CCF3FA40AC469EF135B77BE2D9BC88FB7",  # 48336
)
GLOW_FRAMES = (
    "FB4D375AC1F7F115A8FA6A723A62E8DBD2B6ADF10842498A926FB2E7ADA4471E",  # 1
    "4C8796E638B54DFADF32253C35F92973B162317476E09AF1BC59F1976B0CD9C8",  # 180
    "7C5A941DE8BF1A2A12A8A470C2AD9BEE35B7CAF077DD86768AD40E6786CC7126",  # 600
    "F5C1DA4E42947C9251AF489EEBC0892C11092B6B6E63D0D9583F40C440047239",  # 5449
    "671E2DA9623C1F3DD68C7A30953F8520DF16CADD2BF1F75847734089B5567199",  # 5450
    "472CAED44B317B456E78B02A0EB6E2A30F65370A5B66005C8C34D1777076F394",  # 6000
    "BAA75E376F9E793CC38E1CEE45A9CC632DD41E6CF3E7289557222EFBF59D185B",  # 11344
    "21DE40314FAAAB9AC46B64C3D2B39B310E5FDFF8AAB486AE8761B40A6BABA8E1",  # 12459
    "626FC4A6D29ECB97CAE3189594E319C42C244C754861498FDA9F8B63F363F3A2",  # 13128
    "FC3949237F617CF2DA8F587965B41146ED45D1DA06F96025B612FEFF32B830F1",  # 24287
    "624FD6CFB610211711E9C9B074EBB8FEEA5E0F7075E07C9EA5A6A962A631A9FE",  # 24288
    "D44A87136206212C02E9E69EA85D40B621AD439AE62A727BFA5F05052944070B",  # 24300
    "0E3F0DCB81D2E064C695FEC03BF5A77CB520C9B3B9BF3871C7B3AC08ED6A954A",  # 35396
    "1518CCFC92D7AB8FF9FD8386015378E0503D47C31D4BB734F6F1C298A512D485",  # 36510
    "409744FD9C32D5F97C38E0D20113185790852AA6232A92D02DA21B055A8F5675",  # 37177
    "65942605A0C276ABE05DACC44693C18B07422089958BDB6E2EDC84BA07826163",  # 48336
)
TUNED_GLOW_FRAMES = (
    "269FDBB08DAFDD78687C26279328FEDD316AC96410E9E5F99C7998F020269655",  # 180
    "F555C44B10D850CEAB2C46B9530C258055F3672CB96A8739C16A75B58E1BD83D",  # 600
    "992D34F10F5BBCB29E2EFAEE45DDD82FD22C44BDD9F216FB2500EE7D4BB036F3",  # 6000
    "607F3213E6EAC7E01F793E53009B79C3B866792D35810CEFBE894BD6339FA227",  # 13128
    "5D75DA3C6E9AD63CE751580CF77BB91560ECAB8C1B2BAF58ECAB4764B3102049",  # 48336
)
SHADED_FRAME = "C3EF551FA5A22695C004EDF9A58BE68419193975CC0E28EEA28D43772A9A6A8D"
SHADED_GLOW_FRAME = "C541F91ACF61CD16A3C220FA2D05366D1B7FD84A08106FE9918C73FDCE11E94C"
SHADED_TUNED_FRAME = "EE28881FF440E800048DC4A5D1093702FA3925E9998A8848B0CAD2DB77B4F101"
SHADED_TUNED_GLOW_FRAME = "45ED57C48F20697C7D24C6BCE07AA3F6F75E989FDAF62CDDF02A9156D1FC9713"

# Wet road pins retain all seven optional effect combinations.
WET_FRAMES = {
    180: "086D658DDF7AC8BED75E030D355D73B060C40A0A023717D5EBF70FACD53566EB",
    6000: "C5958CD856AEB2814E28E4587D7611785F31EF578D2A2758647FF1C397DD5214",
    11344: "BD87315B923FB9D7F4FEA9BFE504D1E8009853FFD7C5EEAB0621E9D3C084376E",
    11360: "13279CABD0A5F2DCD7D351582DD7B07B3EB5A56AD4AAC49EB92C4A5EB228F035",
    48336: "C5E098F565A29D83C33868A55FE2CE559D6AA8CCAB7C21FC316B3732266DA4FB",
}
WET_EFFECT_FRAMES = {
    (False, True, False): (
        "BFECF01CA82AAFD330D0EB9411EE60ADDECB5C8FD5D601A2A2D2F936C0C5590E",  # 180
        "5D4F90102D55AADE03B2A5102B78120A010590A2144B6F0FA40CC18A8D23E079",  # 6000
        "C5CF616481FBC0705DF69BBFFB8E08233A44A00DE4B0DB7F6BA8948E5F985409",  # 11344
        "8FDAA91F2FACA0E8964247EA927CBD57C188D25FEB876A5255CF89C1C1BCCD64",  # 11360
        "F333ADB6BF092448EC94DDB09003F6977CC3523BA5156165E9D6FFCF9B7FE79E",  # 48336
    ),
    (False, False, True): (
        "D9EE72F6892674558D553DDA907CBA21411D20FA533ECD3864AD80AA042D8FF4",  # 180
        "AEBC6A97D411B9DF321E9FE2423C4FDD5879901292F90D2CB465D535D029E33F",  # 6000
        "33D1EC23648550206030E1C99A4C0FE509812721E3239803FB621F4242959FD9",  # 11344
        "F9475CA6AD9F9F3EC9C90A69D180E5F126410FCBC7F22E4A53EA2F5B754D01C3",  # 11360
        "6DBF0CC07CCEF90FC58B37D07E459154665F0A2C198E21F0CF92208A2C9C4D4E",  # 48336
    ),
    (False, True, True): (
        "2E983B33CECC3A6385E48BA2FAD3738398C611541182B46BE3156070A21CABE4",  # 180
        "08BFA1FC5A2DAE009B54A429038DCF8E07110EB653DADC12F6161038286BCBE5",  # 6000
        "B92CB18404DF0225525D21FB5E59C32466FF39892BAE54EBEB1107DF04A7F1F2",  # 11344
        "A6C1A774F243A64A44ECE2176848544B056A02546999FB90986B783A350EE45E",  # 11360
        "F041A0531B25BF5590B9E7A975E07A8DDD34AB49D8A76164BC85DEC3176D28B7",  # 48336
    ),
    (True, False, False): (
        "E789EFB9F86EA3C42AABAA232FAC1CF4403B1C04EE390643B5D28CF01D8DCF40",  # 180
        "38CB86F24B6A2D5703426DDD61201731C73CB83EC24D61E292A1C308E984A56E",  # 6000
        "691AE5EA2340E12654DEF10169D6C6CBEB0FBD20C1F58574ABED37049F0F228D",  # 11344
        "08F848D23112B5517DBABF88F19ADCBE85468EE7040DD25DE434EF375F02D14D",  # 11360
        "9E0B6C86868A0D88992E63C363CFB35F6518E14C96E14E42E6C9FC0466C1061A",  # 48336
    ),
    (True, True, False): (
        "57B7D9A788FCD08061B218AEFE14570EF07AA0F86F8097CBD8FD9906E9FB9782",  # 180
        "C34C087B5E5B35DA6B40405DDB714C15FF256FAD34B39A56916E46D5EE9795EE",  # 6000
        "550EB3F28C4749A844C576FC716D94BE370F40AAE95C0BB877B3F96F768D5C92",  # 11344
        "26CBF9393DD17B4C6886932D84B7FB532E05D05E0D6209C0E919760D538B2722",  # 11360
        "CC3F0C36D741F625C5A8D25D440FA96430FD3694DA4031A5DDB1935426EA2B36",  # 48336
    ),
    (True, False, True): (
        "A7CC2D14A84CB332A57D5DC96420AF3DA89D3185412B9B102389A7997E71C6C9",  # 180
        "EC3BB9C2EFD7AB0B378B4D833B71F8FD884E2FA547AAD21689A0D783CE8C3373",  # 6000
        "6CF4073FBD9EA5701C07E613F0DF242C5651A8DCF8E79B4C3F49AC5DE73A1245",  # 11344
        "4261A1AAD631DD02869EC95AA9FC6C361A2A93350F34E96E8CAB3A2DCB8581F8",  # 11360
        "6623EBF977B27CB95167E7261E940518AA6A7FED43B73FF899A2F2A1722CC590",  # 48336
    ),
    (True, True, True): (
        "42E1DD1F761720364515E88329B4B46301931FF2E3EA612987E6B31E4DF55910",  # 180
        "8BCEEB34681F751D8E0EE54C98BBE2421F5EF79034772AE2DFBDE34AB6240B8C",  # 6000
        "85CDC285FE69941C4C51A6489EEA1F41C96FC7739D74F18CE20D8733CF40DF1C",  # 11344
        "14FF195772431D525AA6294CAFBD21C6FD6224A4A781228D3B6CF5FBB8BFC897",  # 11360
        "7941F31677F3F28748BDD100177C95EED051D7D66D3D82CEB59CE964E7153D98",  # 48336
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

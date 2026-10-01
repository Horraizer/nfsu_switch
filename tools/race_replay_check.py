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
# Gear-slot and finish-line brackets retain HUD coverage.
# Replay state, trajectory, input and step count are unchanged.
FRAMES = (
    "15F2943EB1CAC816CCA1C5465AD0BF8D1B0F2D22854B53EDF1DC3842D30CE1C8",  # 1
    "6656E08C8676CE2CE35E95608BC83C220C93D9E7EDB116233544EEDF185F2811",  # 180
    "CE622CA14545A30F63497C1290F1863C3AF045CBE01D59DFB93DE8BF5922F614",  # 600
    "39A7F394EFABDD6AE2E4BB395498EB3A87A3385E6DEBB4C848D7C3FCA10A2C19",  # 5449
    "C7121F76CE1E758125D1EC5E19631DF183581B635B33989B892540CBAE7F8156",  # 5450
    "EAAD1E73A399125C889655E4491D955C1C45556283F2BFBBC4170F7F47706CBB",  # 6000
    "EF102C2C91ECA3ABA1AD8B1648ACA34144E534B83AB8EB9A9037336EA33D7088",  # 11344
    "02C5E9533648E0D06FAEB7EC4B375737D6A72013BBEAB2E31529171B97C09189",  # 12459
    "11F005598D393D4692B93B55C3F6D7638D48F1E9052C1DC6747E5273FEFE100C",  # 13128
    "E80E272F0E1D0AC14FD0C5A1FD3392B162097EB8D8C01F6B5CA361FC00EC9F07",  # 24287
    "95ECD4CDD93509636DCF79730485C466F2B771584898BFD459F8E753267496A7",  # 24288
    "CEE37922F97106E2E24EE6CE958FA9024D45FF82EF7DF71F4677104C6E7FC034",  # 24300
    "E1E93D486FBEDC1B26F9BE55C823D435E9F36BE21CF31E0480F2911004A8DF97",  # 35396
    "76A5681078E5E66ADDA09A8EB3E5E802E6671CCE04AC458F7DE5A31031AD838A",  # 36510
    "B6398FB499DA06CC7192C996DCE268F369C528EE85252BD87B7C1D1448E19222",  # 37177
    "A355D23E918639C11C8C7D90A8524588533EE98C935CE8532A7DF8F20D7C4055",  # 48336
)
# Tuned, glow and shading presets use the same HUD overlay.
TUNED_STEPS = (180, 600, 6000, 13128, 48336)
TUNED_FRAMES = (
    "5EDD9D839DE5E10E38B6428ACAA96B0272A032DF0F34D589F934DA1074912DA4",  # 180
    "EE9CC1F3E40F15767E272ABA6AA21FE5DC93A0E86EA41FB6641D1FE76F8A9039",  # 600
    "0CD42D05C69FF47AEDAB8DDD8A5ABD9BDBB79ADB27F02F171975BD507EB1B0DF",  # 6000
    "8F794C2B7A36E722DBFB0B3F0597DFAD4258BB4DCAB12087A3249951288AAD0D",  # 13128
    "A4FDCFA54BDFAAB2C1AA09145234D31C44CA2ACD2E950682F59060613F40F794",  # 48336
)
GLOW_FRAMES = (
    "AC7C1B9C6C77308ADD1D390B044529132607BFBA38D162044CADBE9828A9A4E6",  # 1
    "5D6C27478BB7AC43E69CC2E0412AEBF3E6B89E6F88E643015F1DA92D80979D46",  # 180
    "C48114EF1A6455A1B6324D8C7A8354D8DC8DE0EDC2BC1A994724DEB708469414",  # 600
    "3C7755C8352EF9FCD72CAA4739472FB746A9DC69DA5208515353874881577557",  # 5449
    "5390CCE8499F57CC92E0C41881F47D86B8F978F9333068C71E2F68BB8C5EE14B",  # 5450
    "873449C84A87F986B2201C1872880C09DC5346A0FDB494CFD48438AF703B6F6C",  # 6000
    "BFB6E880AD64E8E18300D1799388E46B75E78A6F9BBFCB4DFDFD381E69D71732",  # 11344
    "0EC2ACBD3F5899DFD04DBE7842BED4B2B2AD8840037F6B53F4F2268320B62BF8",  # 12459
    "FEFB136A4F57DCF585FD4D4081DBF917EF5CA98EFBDA14A075AEC0F10E81DB16",  # 13128
    "ACA46440FFD0652617A212898AE727ABF11917C447DC49BCD1AFBBED24A3597A",  # 24287
    "4C0E65D64A0124144F318F21B72D127EAAB47D5AC8791AC31C3D45FFCC337A66",  # 24288
    "684C499A4C172A0994A40362C02AA27630F149B3D0B73BD8BD47F79E4C0749EE",  # 24300
    "9A3013F7F2F8193EE517605901D58A1C0856409799C2AD959602F5F269493354",  # 35396
    "85C827B8B0166E4BBE9DC69416D366631716570CFD8FF0DA0882032165C8532F",  # 36510
    "3ACE58124A5FB53F00856EC2442A17BAE3134CF49B7E0FD4661AE43C01E050E2",  # 37177
    "18A3B586FD9667C7BA801428C660A60C4BB436CB206F63593EF42EF6D2A7DA27",  # 48336
)
TUNED_GLOW_FRAMES = (
    "D67F10CED266C1051F2E5CFA45D4C09D7AAD507EDC8CCA46D6CA987AF20FFE7C",  # 180
    "BD6E4D91D19C9946C8BA6E90B4F6441EFCE48A03DBE291C9FA291A24481B7048",  # 600
    "541F03243F06BD1F15A7DD9CFB5113B9601D77A13C43439BCF64043B869A2254",  # 6000
    "E545A116D1A51F46A66CF6C006EFF228106F0833A00FA9BAAC73EAD979A27EC7",  # 13128
    "3A8D1C62A85CEC4833534822BC86AA5A11BD791AEF918D9AA7C5381C8021532E",  # 48336
)
SHADED_FRAME = "FE6B1D306C4E36FECA1A29BF5160C531188B88558EB6890755497EA47EE947C3"
SHADED_GLOW_FRAME = "202F61219BDC18C047CE6BA615428C0B7B3AE4478E95306B3E3BE6686A3FD0DB"
SHADED_TUNED_FRAME = "DAD6B67BB422BCAEB4E94959958208063812977031D6BDDA587486772670E75A"
SHADED_TUNED_GLOW_FRAME = "450BB24281CF456316ED72351837FAA54DB27F984563627BCDA2F4BB9DD963B7"

# Wet road pins retain all seven optional effect combinations.
WET_FRAMES = {
    180: "360ECE4ACA668BF9420E22B585E9E2C16CD96F63612F49DD1C02DB92165A29A9",
    6000: "E719020A5FD4420E5FFDD68EDF8B5B80B331A0CED8469BA52FAFE7F27F8404CB",
    11344: "5FCBDDD599D15B63E32A7BA30F7220E9F26615924DA15B44F8BCE16DD79A8619",
    11360: "C927B7E790E1BA5513E601D30CE59142A15046CC59C4504DDBF715996F34BB23",
    48336: "B4E8130D045215A498B1425322C6E3E28A0FC24B7B2F55BC8CEA1A0AAC1B15F6",
}
WET_EFFECT_FRAMES = {
    (False, True, False): (
        "C0A0D383AEE5C8DA5BC1FD68583D3D8FD88CD25F751D2C39F534FFE933437BAA",  # 180
        "4D1D01B80A082477C0A74B0F9409D01568940277D59855DC05DF4E0E08586AD1",  # 6000
        "127283E52ECFA664E3AA4D125E355B64B8759B946225271B243BEA4F4DA03541",  # 11344
        "B37E84BC208B3ED7B0B43B5A031916BF89A6EB5A3F92FE9BE6D99F7E0F615AA4",  # 11360
        "A0EB06E41AEBDF82B30A10F59C46FDD4AB06A5A8649DDF4436219AC279F24D69",  # 48336
    ),
    (False, False, True): (
        "6BA6B04684AE0193ABBC96CD786F7DF9736CB7477B2CFC0FD1B07D626FC8AD26",  # 180
        "CBD87C2C3EE71E668D877F6399C08D4A73E9C353670DD376121938B4017007D5",  # 6000
        "A1F3B52C53F486FB607425157DCCB081AA392A9C345A9FE1821FA8483B10A3AE",  # 11344
        "AED73F44DE8C3C152C1C79291AC07E05A5E7414FF314200D94741F1C1B44567B",  # 11360
        "88BC99EFF398EB1F622FE383C3F7660BF25909122B51F23F3E33C1070AEC10FF",  # 48336
    ),
    (False, True, True): (
        "27222912F42013FBEE3AADBACF7DEAD12D5583E987C0B253B4DE87D29E67ACAE",  # 180
        "73BB41221C25E666782E298BB33053C57AE89954CE675CB2010A0C026CAD1925",  # 6000
        "86B400277B520F7603E37BF8DE913C424B15920D337C7534AFA7A8CA08DB15E7",  # 11344
        "80405EA205DEB29F83C00B9F52775A870475ED96975482F17369A7A5E5835C1A",  # 11360
        "00E55A0EB7E4AF75611B16AF1EF66B6A390B283A88590FDB90AFDAEA8BA7A3F7",  # 48336
    ),
    (True, False, False): (
        "E18B74F44554CB4920F724568766A96245C91F2D5E2BD336059338E07A548551",  # 180
        "554AC8EBB1D148C42457610940F0E28348F35D3065654913932126454C174357",  # 6000
        "EF77AC52430FC896159A2A03A86FF9F93C0D5AD05AD058CA859F60A02A08B64E",  # 11344
        "155B3B4CE9E70C357EE782CAEFB498ACFC806D9EFB0DA1FD464505A44FC74064",  # 11360
        "0966C5E17FD1A1396076A54327BCF9B03561BE487EBD96EB03D0FB876EAF7431",  # 48336
    ),
    (True, True, False): (
        "C4CC2102A51C9C5F09A201DD308337A1F68D16E149AF665D5126553E0563290B",  # 180
        "8D65A1B9E7295017C3EA7D886D37821A551CE3A0E8C8221DF549FFA37DE35397",  # 6000
        "980F38321583D29CB9F1CF95B22AFA10D8C5ABA42FA73EB39F3BCB8813CFF7F2",  # 11344
        "DA6C3481D7E3A8629EE2098C8A7E83347C0871945B582F27A7AAF17616210BF3",  # 11360
        "AF60589AA7EE998581549F61EA1904CCDF328F9CC6D2C97F22FF6530C8581A4D",  # 48336
    ),
    (True, False, True): (
        "62BEECF76FB6C37855E005F3DC54D90DEA4AAAC2C7DA8161A03F810C0AFDF812",  # 180
        "8C926A3713F185009C81AC65551F41746D3ED10F7A8C04984FADDD4F2C373378",  # 6000
        "210CFBFBB9ED069BA6AFFBD9E5BD57035CD2C0B232EE8016060C30216AC76D08",  # 11344
        "E1737270A5E611D2498C3964DF30969CCBED51A0C7C8A85389A2182D9DBAF6F9",  # 11360
        "4A1644D3DFCFD48DC7882A1A30B4C44BCB79B0D9AF68A9480873E4C1B07DA1F5",  # 48336
    ),
    (True, True, True): (
        "FBA35D956E13A68D5BC6D4870D4585B8B314908E2D7B77DAA286742A689D1F85",  # 180
        "F49E6FBF271DCE161E57072FA43D9CE4DD03B625F919063668BC15A15DB31FCC",  # 6000
        "5218FA65022B115D9A862087BA0E1841A91ACDDC034C575273195D851400F5CC",  # 11344
        "8FC7E7293C5259CA2C8E28FD7EB8D0D1909AC4BDB98E8FDD58B18C1B76476347",  # 11360
        "18CF33BD4A84EC7EC98724842A0BD9F9DCE3EDA89FE4AB55EC6745FF130C04FA",  # 48336
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

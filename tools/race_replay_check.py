"""Check event 1's retail replay pins, using only fresh output directories."""
import argparse
import csv
import io
import hashlib
from pathlib import Path
import subprocess

STEPS = (1, 180, 600, 5449, 5450, 6000, 11344, 12459, 13128, 24287, 24288, 24300, 35396, 36510, 37177, 48336)
# The integrated effects and native HUD both affect CPU frames.
# Gear-slot and finish-line brackets retain HUD coverage.
# Replay state, trajectory, input and step count are unchanged.
FRAMES = (
    "F3A84AD76A8794A9CDD201D3651631055772DE3735ECE535183676753D9190B0",  # 1
    "C9A25E1E1485C73D3008F751F9C68E7DDF5F71CAFF43FA722AB7CA3166ACBB31",  # 180
    "FC8040441064B5424417C87794F62DF755C5A3CB0061D9838D933A0BE4C72D59",  # 600
    "F73F839259C49E8C4660F6F942F6C7BE759C1D002F8F6D1763CFE6C875EFAA4D",  # 5449
    "CDB4AA854F293E177BFA0CC11A1EA768593C2D704842B50C2B2AB0D8A1F872E1",  # 5450
    "CE109F662BD2194D3370697A6512FDC828C7C9076D33153F8345D3845841A518",  # 6000
    "653F353121ED65D60D19659C3161F7F5BB9E182F203321A6EA0375CF04415711",  # 11344
    "E2636034C2E38C7E3512266FD2D1F4ED4E0AC9C53BCF5C3165398480076BBFEE",  # 12459
    "54DCC3C189748A32C33145A8CF1A2170F4EC016974C0ED848396F08D01AF285D",  # 13128
    "CB6589D7CEE1D6A87AC6B2AB9451BE13ADC713DB79A40406835F17C760765EDA",  # 24287
    "93C150A41C300E4AC112AB0925AEAC91E7FE60FB31066C886CB698FA887EA7EE",  # 24288
    "7B82960E2C864118FA59672EC81B9B99222B8585678E423706C30C9CC41EC67C",  # 24300
    "674482003FD94D2F9740E0AD0F4D3F5C77F9EDC305DD21E4D32250B297792381",  # 35396
    "992348BA79EFDB218BF3D5E9E46342E732DC09A3EFF5B11504C8D41E5C4E1AFB",  # 36510
    "EE9A8B06BAD4F93D8A0E9B92C9F09F1BCF262D978C426339F6A1ABA9702AA35F",  # 37177
    "B9739FC2095BFF21E19696ECFF5B4AABE826C401A4E1C1698E09F048CDF7A113",  # 48336
)
# Tuned, glow and shading presets use the same HUD overlay.
TUNED_STEPS = (180, 600, 6000, 13128, 48336)
TUNED_FRAMES = (
    "CA1F0C3020363B038C1A519ABF85760F2F5A7346F5610D6E3D19AFA39E711039",  # 180
    "7D8BE9B9EA70D22D3FA05AE48DB52B01C0AAFA09B3CDAE53A170A148C4067D65",  # 600
    "615C0F7980730A0A7DE32348A501046822C1794258B0C3F478D080C0FE1A590B",  # 6000
    "07680976B7060265CBF586E24B9F74362A4DC93DB5F4E046F8B70F683357A1B4",  # 13128
    "B5DBF922DF09E6DB24DFB45F2E6C17FA9BE85CD641155BF720CB43408164B998",  # 48336
)
GLOW_FRAMES = (
    "315626AFE9F2AED95E32373E08FB95546B28E0CE7D2BDD0D6DB0DA05F9299BD8",  # 1
    "582192A84FF4CF089AC90C325FD3332F343F986B8C28D17FEC8AD52FBB4AF378",  # 180
    "904F0C886C807AA757DF8E37C7CED8BB47C16CA66EC721F0E041D5C37BFC4CC0",  # 600
    "DA3B7A73C87B7E4B6381D59B8DD0218C787D2136CDE837D79931A7D929D41E99",  # 5449
    "9236E1E373406B68BB6566C0572AB3DEF4A1BFC2F2D09844267497E976B65FC3",  # 5450
    "9520DFA46F62BC630BBB512EBE7B71CE39705FDA065352A7EB7F1AAE87B0019A",  # 6000
    "3A9BACE5DF1C6CE559D22CE585DEB3844D40F1D07B086B8EAEC25EDDC21FD646",  # 11344
    "7327DCBFABA098D7E6F2DD4CB43379960B0B0FC063A790C9C85DE95D870F0F0B",  # 12459
    "FC1110C9364904B89B7D3F34A8CA3422AC68F3DB0EEEA20F401895AA006AB9EA",  # 13128
    "68722A1F51C932AD3C4ED4566FB76A88F9207ACA8BC9BCA7FBFFBC8CA94150D4",  # 24287
    "2427668CB392F9953350C87B3BF0A8938304718A6A5D10B75133558B5E183F5D",  # 24288
    "2A5A5BD74056E8DFD0CE8AA4309D4CD8832FA7DF87F412939D7F442503DF74DF",  # 24300
    "AFAF560066470DB1958236BA39B5698C9C417D7FCA28293E377CA888FC7B46E1",  # 35396
    "DF94C80C4D18EA855D7B639EC3096628A1413262F07F332FEE61FB7250D97D29",  # 36510
    "9961958859BDD9DD0A1D129D6AE1D36348FD3F84EEE40C65646CA08D368B9289",  # 37177
    "9FC2ABB1FCA03563087C420A3668740986C592299777174B16C44835EE926108",  # 48336
)
TUNED_GLOW_FRAMES = (
    "F2D3C065CFECB167AEAA2AC9722AC9C862F3EBF768A3DD2A61359B26337BDBA4",  # 180
    "6C312593A99E89A76BF451EDF2A10292849190327652474717E81209E90486EF",  # 600
    "7BBD50246DB12FA08E4ADABAACAD23BB1BC4C5D3AEEA52A6AD68F2D797249442",  # 6000
    "50885BB20827CAE5B2BECC21857DB5ED6C52E1085A4EEAA1601B2A55CFEA2B53",  # 13128
    "B00DB3F79059EED14E31E9E1F57163BDFC56447CDB6C54CA6BC7DAB7D7B98214",  # 48336
)
SHADED_FRAME = "9308DD5CA4EC6B4A94B85C89F9D6AF6F509565938EC6395BF90EBFDE21350E92"
SHADED_GLOW_FRAME = "63BFCBE835336B8FDB602143DA4E99BC48D7397103F87C43C44E4A1DBA3D82D3"
SHADED_TUNED_FRAME = "BB72313B8A9BDF2FFC7C2075D44F199E9743B79D0B7DCDDF87D9F50B7922632E"
SHADED_TUNED_GLOW_FRAME = "34385C2C7DF3BB7AAF9FF3EAE0C4D8812BC493E478EEFB7D20B783B75C14F2B3"

# Wet road pins retain all seven optional effect combinations.
WET_FRAMES = {
    180: "840525EE00A890C8B605C564E596D512EC4ABC5A6EE2BCC7FEDAAD7279680532",
    6000: "7B6F85CED6DB274CECEEFB253B5B5A743F9820B4D0343E36401D745BACC2FBAA",
    11344: "8C34A22E94548A2FF227CD22DCE1D05F3DC4FA82AE8276AF910773979E857117",
    11360: "D6C36BA9F4E661CE7198690055043AD831365F7C1C2BC99CE2A425CB2FEDBA0D",
    48336: "4C995E2DBCFDF132B2C5837023CD40E3941558A91D4AA758638589C3AAB40B87",
}
WET_EFFECT_FRAMES = {
    (False, True, False): (
        "5A6672A6376D56DD73C153E26F761D9833570D637BF140800804133CB4E7D6A0",  # 180
        "AAA793D7C53B49BF7561582A85A24D5CE751BEC69DB4F9C2AA10329315D8B580",  # 6000
        "6CDE3AE1269D3107F833F0C24390852847065E75662246AD1701C1DCABEB1632",  # 11344
        "C05FCDEFE5F9B8D06A64B240A80AF7AF979BA040379D45603046E378C2765637",  # 11360
        "2FC883846ADF39B623891D8F56D254B4D9CA23D4EA5BF3E82D967C1A82027DF3",  # 48336
    ),
    (False, False, True): (
        "CF2AEBC7686B3A602D0884491EF9D3672A9A6F689BC91161AFAED2B568AF82DF",  # 180
        "52DB333472B5B3F7B470840E031722FDE81BAF3A1CD126583F544E05A5C356CF",  # 6000
        "B49D861B96EF44AEC7D4DDF53F84203564D9A439A35670BFEDCC2490F73B4D49",  # 11344
        "862BA7819CA6741DD89496AF67CE08DD96D5AD0D37609B7813C82AE52876EC5E",  # 11360
        "A47F82DCC8BADDD40B127786F964EE2722ABA772FA175125C98E090DE8AD897F",  # 48336
    ),
    (False, True, True): (
        "E65A519728427496990D0F8C3DCE90B87DB14FED8D691B9E65ECD7E0C1C6A607",  # 180
        "4D1EFED60F308E6BE9D5CA865BBE954112705298E3426D93BB820440B776C691",  # 6000
        "0BE908B4D2A8C5E3FB83448F64F60A4B5AD61C5C81F719A1E460CEF867F8B88C",  # 11344
        "70645B5804241C23F3F611137E7AF869958DA8CBC793A22579C483E917F8D820",  # 11360
        "EB326E2AA80B6D515EC55CADC836D5E56276E2C28CFF8BE5374E13D93728C5E1",  # 48336
    ),
    (True, False, False): (
        "F28DD1BD0DD571E08D5E6E3A1DAF233A4F8EC239DAC3F609DF8519F31715BAED",  # 180
        "93FB0FB5D5D6F4814B537C0BFE3E09CEAAA937668E4D0EDD6380A8FB25A7981D",  # 6000
        "BD66117CDE3A46E4AEE74C2AB312BDBAD6C6650903387F9283F6C31AFC4A7D18",  # 11344
        "B56005A5BEA58470A46DEFE759A5C54894BEA4067EA28B11224B5BE28959238D",  # 11360
        "BE50A8C603029121B8E6CFE79A02314CCA7CA9AD128B7804C10D11F1B11D0428",  # 48336
    ),
    (True, True, False): (
        "892361E941D8291F36E8C953607E247306923E250E2A97CEFA5AA32ED6D306B8",  # 180
        "61A3ADD5751EBEEB21F37FC6B015EB584AB7204D6FDE871611454C7D0DFDBF42",  # 6000
        "BC43373D2DBB6AFDBC6A2FBA77778A2D055C15F3CE1516A61D781FC43550ACD4",  # 11344
        "1131BF10BB967E32B3845D7072E61ADB3B7430C175566D5CF1E2D39D6880875B",  # 11360
        "6A2B9F7EB279B423FF639A46585206E256C7F3F935A7A2F8DCDDC37891CAFD9F",  # 48336
    ),
    (True, False, True): (
        "9D5962226F56B846705322DBAE83A605373E9A88570A197CF331FDF2F2C2C681",  # 180
        "5986C0ED652877D3A543556E938496010A88605BC80B2BD53F34CDA918585FAB",  # 6000
        "5C72D55742BF78F0A4517D043A5C40FCB4D61E46B89F3A2AFBDFCBD30E9CC803",  # 11344
        "27C33D587FC45BBAC9CEBF6F25E2BC79C93EC3819B2EF70582DACF615ADE3815",  # 11360
        "C8EB53F8C3163E522E5E2B03C8E7B23CC49AD74AE304EB9734E24406FB06996F",  # 48336
    ),
    (True, True, True): (
        "0341B6E340513F53898DF0B2954813FD02A3A77087EBCA4DB0BB4E85F46DAA1A",  # 180
        "1D7C13B59DC28E9409B963F3365718ADD5F17B06E20458C61D9E33D3C6D5321A",  # 6000
        "4C57F2D4C099200180BAE021BC5B5EC640033C3536E1F427D6A120FCBD2269A4",  # 11344
        "293C993B44CE55FD3B574736BCEC6219273B46C6FD682897C48ACF490942578F",  # 11360
        "B6E5A7A368697F0893CD9C03F875A97A45EF5317FD23CD2675F75F2EF0F4D492",  # 48336
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

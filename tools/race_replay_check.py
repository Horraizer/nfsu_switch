"""Check event 1's retail replay pins, using only fresh output directories."""
import argparse
import csv
import io
import hashlib
from pathlib import Path
import subprocess

STEPS = (1, 180, 600, 5449, 5450, 6000, 11344, 12459, 13122, 24287, 24288, 24300, 35396, 36510, 37177, 48314)
# Combined issue253 HUD and issue275 physics visual pins measured against
# the reviewed physics parent. Every state/trajectory/result matches that
# parent; pixel differences are confined to the bottom-right HUD region.
# Selected gear HUD samples and finish-line brackets retain coverage.
# Low-support torque changes airborne state and the generated driver recording.
FRAMES = ('F0281CB7E752745230BE4A66B9F6FCE148A4BE4F5D8A582C779AE5F765E10940', '2CEC7619D9B8755A1814A50D89A992C3D1E13740FAE6A27CF69B5DA289AB27ED', 'F11FEED4249520B854597CA28CFB886C5FD43BF7A63D6044BC16EA10DB3517CF', '3F52E5BB52FB7C12DDF1A9241140A7370E105265A0B7ECD31AF03AF1823357E7', '48E34599E927F990D5BFE5699041BA05A6FD2FD85475A7BCD02244B19D7750DC', '6FE345BA112CF803BA4B90C89AA408A9CB8540F4115AE9A525A8E4E37D6700FC', '3D4DF282D6BF36C392BAADF2B20706B028F45665673C05B0AAC855985539FBFD', '3F6A9A2047C076456D345620D5B1077DCE592A9D107A909E4AE9C6F19DAA031A', 'DC215571EE4ABC54A6FD21E14E58E21D4C3391F0E199E844A5521CFD62FCE066', '9AD36C97773D053559F143152B126AA838D4EF7FD76D6CC93A861FB7DAC72EAE', '4B61D323A3D808D59AF27733A96B58AA51DB37794F918C40C0DAC1EF762CEAE2', 'AE9854075C033E1D79A9ED38405ED9DEBA6F02F2E3A8FC613099E8B8B005F9B0', '6E2CED3FC13C508FEA9DDA7AC42310EAA2441B4A05591AFEFD55125826428C77', '776C035529BED2BADAD5AEC1C5CE756F6592248B7061008F9574C78EAEAF2080', 'DC2875EAA160A79C43F2989FFF9E588306E641D4951C59716A33A54898B31847', '8450C2C40782F290997604D94F0C680C6E0E9F98E082646AEAD5E1B4A3130FD3',)
# Tuned, glow and shading presets use the same HUD overlay.
TUNED_STEPS = (180, 600, 6000, 13122, 48314)
TUNED_FRAMES = ('E0BC5D83C89AA06619D18913EC9DDFAB4CCA103498B8CDF5A2D50CF893E6FC8F', 'FEBCA5252A94209FD0507F1B28AD679A71F68451C6861941D7E41706F6647569', '932B21B706A10E29A00C3BB8312EB756F915F130E537F5D8E2B51758E5ADF5FC', '40050DBB739C6FCAC7B1EFF3D414D5E664FDEAB326C2FCF56F95768A851AE5B1', '2B2C3937EE8D797E2C4B781E25AE3A893B40A252FEFB69CBC7DA0FCAD169F490',)
GLOW_FRAMES = ('8FB4151462085163E24305BEB51F5BD0F529213569C33B006187D170CAB5D8DC', '6E578A218581B83C833F8294927C4992752317114198259F45F9F4F9B09375C9', '8901C6E2DC0AB02B9FD395DB382FF94D22A007E929374A688F222B7ADF8E17E4', '667BE7A12ED23ADE94D356D7394831630831CFB3CB4B15676D15FC2272732378', '7125AE8130E114903B5B8D77FC68577E730408D94ABA046308C70616D90CBD12', 'F2123121B900BEA86C36700BEC9CDB8391CEC2F7F403620704A302109C5F1844', '7894A7F1E13249165A9AA1E6913BBCA6DA62CEE218876B48C3A129CEE2E02C94', '0CCE314F6083588A0D482D589295BC0939D73D3B936A53F73C80095237D95051', '8F70FA535BD9A8832683E4827D10709CDB2497E31B42F94F75904C8F4017A9C8', '10C79605855EBD9218A2B19121B01E8BD6F89CD4E90978239B443BEADFC14E4E', 'A8B6466E89B91441B7FC87DECFA40116166593E33B09B9FBD35B0F4156EE9D6F', '4C0253595ECA90142E33E81A9346489D6939F76CF3E6FD67FEE26E6C4916D14A', '72DF350E710185F6D6EBC90EA42AB248669B791E8384FA0F133BF987E84315BD', '1B685C7024FBECC621BE23C93635CD7D131FF01DB20364F4F3D68E7641EFFA21', '293CF88506FF7782B6C28F607BBE01ED184C0F07FE16C1FC1650003B917ECDBF', '0B67FF81C2B66955385069E1E68AD38869270D590C8C60AFD0FEEF6A89DBAD97',)
TUNED_GLOW_FRAMES = ('1A41C1909E9D27BD977589FF4C5749C2367119E2803823DB9FE4E7433FB49ADB', 'C4C2B08910E33DAD480F8D2E88812E186D4D87B9556D33AF9D87DA54ECE69684', '3D7F0FBB202CAD8269D7188FC75FC3341B7FA4593B191B60622CF4BE14117C22', 'D3AE4770285F3272E27AA58327387EFC56E01713C183C69412C104328F12665D', '1810C9E2D8294AF85DC00005B9F9AF68E1E5718468F9316B2F9ACEC567D288EB',)
SHADED_FRAME = '6D74E8A321D8A395FDC7161C80445A8FF13374B0D9638010C985AE6077ACEB9A'
SHADED_GLOW_FRAME = '3EDF7B99D3DC51964739198C1ADE04AA75CA15BA14D76E7F93E5B91C9E177CB2'
SHADED_TUNED_FRAME = '63711DAC88B562DCE481B670346CA584768FF6C3A5D243DA8FD45D7FF514E493'
SHADED_TUNED_GLOW_FRAME = 'E498987356DC133AD587B14C1CF80203F6B474BBF01B1F76A61BF2EA87CC8024'

# Wet road pins retain all seven optional effect combinations.
WET_FRAMES = {180: '1738C0F1B5B1DDE941D0A4818F461302C0D1FF0D61DDEBCAE81EA73F7776D185', 6000: '18B5220B47DFEA078C411ED34CC0C9A0F1F6683B4D4CEC3DC34A41CD394482CD', 11344: 'EA39F540E216A1755664FAA02BD303C1B12B4B43106897BF878D1BA3EAED5A14', 11360: '9DC780599BD46FA6DE8332C0902E0D487522AB88F8169FAF921024C272BAD960', 48314: '0E3327DC3696CEE885A9A5DF9FE63848D939CB1628745C15CB8109D63D2CE34A'}
WET_EFFECT_FRAMES = {(False, False, True): ('DECE502C2B9999F175385121A188985827BADC2171A06B7F01D0F4F8AAA397F2', 'E71AE484DF04372A8F5DEF74DB7C87E1F8B6A3C7D9C0DC187D08221AB9185040', 'CCCDF70038F7EF7AC9DE1451AD7107A65C233454247FD61ABE0B9AD03987392C', '99D916442E2E53204852247957C63748975F0685A1D605C8D19E4098A77624B5', '67F4D65B600430810BD7B7034D35C6AF2A1F08C7709FB2EA93EBBDDEF9CE31D3',), (False, True, False): ('B8A31E5737420BF3DBCA451421CA087D3EE3EE591249C5A2F48C4FE291913BD3', '038829A53C9D1093FD679F87C9B7C485FB8B3CFCBAB25753D69B0292A4906DA4', '57C43A0CBAD05FC11D902112AA14CBC968E0BC7A723A50FCCEE142AA3B1BAB84', 'D415B92205BADC849C55C4A29F017D8FD2A3D9DBB1B32EE0DE19D43932F65BAC', 'A4D1725DB123E9AF4565DA63B7E5975B0BE0B32B1E111C88C34C212E645A5F83',), (False, True, True): ('8C58941556D9D93CB4F228FFB1F1C22FB89F338DDDF81BE4D11D415764DD0E15', '5B5975CDDFFD3EF0FFBB942A5DCE24490C72FB22EF0DD5FC248686CE155613CB', '902DFC512B75E75C7B1A4B6E76474A9444CFB82F5AE093B8612E0E01AA6CBFED', '638D29899044F931F05FC4591FDF07A0B2AB53B2F746E4E34F82580F367690F2', 'EC30945DA1DC6F6BCCF299C3CAEB0BF03CD62F22BB0D0B6D13BEC2A0C3885ABF',), (True, False, False): ('BDBB3777D06EC0793DA5A11ADA69E8F599835905C3EECCEE9240B486F876B42F', '44C40CC77D6508023E1AED1BAC9007EB50454FEDC53A915A71109043A752F250', 'F50F3BA38B074853330DDB18000BD4408E08D7AC92CB151B00695080BA0552FD', '5F547B57F568B0CF80E0F48B5EA36BC10790A3695B277373DDC2F7388AD7C18A', 'CC235B4052F4895D0F209A8C7FB9F946A2AD56D956E1FE12CA270B5CF2755912',), (True, False, True): ('D0D31C2DA63EF95B9824BC2E4BCA8124C5BDDFFA2125C5AD561534F859DE4102', '6CBF59B2C41FAF3D02091B8F01237399F6BECB758EB09E673C7ACC238A540E37', 'C22C6F998FC5C6DA49AAE49CF69A274204ED9D0AB45D7CAA9AD0A42E85330AE9', '67A1B11B4A337603FF58CA6AC51E930E453D455A6F9172EB0034F244EFF241D6', '3DFE5B938920956A5B66DE13643A6719CF74CD6B0EE033A4F2CC624CBBA771FD',), (True, True, False): ('45DB068F86DC0652AD3DD2AEE010689257A4DB827E5E7DB94E5EA59F8556E54B', '05BEB042D9DB4010235B0A5C8027879FFD05FA3D7AAB1E544655FD444482DAC3', '35D28CE76941C436AA932043A47DAF6F7FA902DBCB6C4F6D297499FD9B176F35', 'D793908CB2B21EBFEFD9E4D236730A2445B4938EB4567B2F5CCA550A67EC5230', '96172A6A6DD910C191A4ED0DFB92C1F4BF3494280F9A0C49B46DD4C75C554CD8',), (True, True, True): ('F1DC36D3C3C04DFA7872187EF2919E48FEA8A6482E2F98EB0C23D548AC05FFEC', 'D8CC2118C0C9B851F61BCBB2BA6CF811EF4CCFAE6737E30A70ADEEFEC573F396', 'BC8454C069462A9BB64C145E3FA25C5B77AAA54E6C719A45769F28915CA1AFA0', '953A9A17A447F10AD7879161CD6FE613C459E3550D37E2B80DB09EE65DFA2A12', 'FBD5CC98A2B63FDD54C79CC1FB9DF36AE0BB5E45651A61D162284B1760FD7959',)}

RESULT = (
    'event 1 track 1003 laps 2 outcome finished steps 48314 resets 0',
    'lap 1 ticks 1615480 time 6:43.87',
    'lap 2 ticks 1610640 time 6:42.66',
    'total ticks 3226120 time 13:26.53',
    'state-sha256 45186220CDCA35B2FD3BED5ADDF1D519116D15E6FBCE87260D739623ADAAB95A',
    'stream loads 78 releases 0 bytes-read 36471524',
)
STATE_FILE = 'A26D98A62A665D2087EF2858D4F934E7DC9E8052F74A3998E4EBA3F3386C60A8'
INPUT = '37CF11193A1E929E07D78D895397982EAD0DDB7C438E453E070B3869726D03DA'
TRAJECTORY = '3A516989B6C80DBC32AB8DB5A1AFF9B7A1C7D47E728CBAE0F56FC3E9DE39742C'

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

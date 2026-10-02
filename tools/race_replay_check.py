"""Check event 1's retail replay pins, using only fresh output directories."""
import argparse
import csv
import io
import hashlib
from pathlib import Path
import subprocess

STEPS = (1, 180, 600, 5449, 5450, 6000, 11344, 12459, 13128, 24287, 24288, 24300, 35396, 36510, 37177, 48343)
# The integrated effects and native HUD both affect CPU frames.
# Gear-slot and finish-line brackets retain HUD coverage.
# Low-support torque changes airborne state and the generated driver recording.
FRAMES = (
    "F3A84AD76A8794A9CDD201D3651631055772DE3735ECE535183676753D9190B0",  # 1
    "C9A25E1E1485C73D3008F751F9C68E7DDF5F71CAFF43FA722AB7CA3166ACBB31",  # 180
    "FC8040441064B5424417C87794F62DF755C5A3CB0061D9838D933A0BE4C72D59",  # 600
    "D33661B019BE38F31662E41B26E48BBA8261840EC1AC3D277BD9F9E0845A5178",  # 5449
    "DBCBB178271BC2A9D358635B26E045DFBEA76308A1C4A4208226E25DD1239926",  # 5450
    "4AC2553B228E671773ED732A482C2725A1F3B83E2CE7ACC7F7ED4BD092C9068B",  # 6000
    "EE831F928C7F62D0E65AE87B941DB52C59AEAA0AB3F9C7939045B68EA83D9BF8",  # 11344
    "5CEAF937FCFDB2FC91D711D2DE55263F4A5FF2FD79E98667A9F43924FEC14D75",  # 12459
    "5161090FCAC17CE8048FB578E3A12A65558874F98E47FEAE32E3F5D29BD36774",  # 13128
    "C0485EB82A701BD2A72572FE0687E0375C54FEDC015923DA6F1C96E257557F2E",  # 24287
    "4BC202E02A65F871A9D8FCBB3A890CFF6FFC81D93ADB59CFFE495132D5A05906",  # 24288
    "FBEAAEE8D5DC20CABF5BE41FD360204437261607CCAC4B69073482540A2889FA",  # 24300
    "941D2EBEB97695C12445A6CFE0D3696197F549E9D9466B63F8ACD6099BC39E01",  # 35396
    "726BB2170C7798D6538082554F9A802A9ECD0196B1906D13E837E85C1BF36D2D",  # 36510
    "2273B8016B45DCEE50C33FE1621A7FCCE2D7D61C09A3639D7B2FB8077A3C9741",  # 37177
    "47323A30A4617D4113C48DDB338B21BFBA1CB83075FA157CA5A3992E8CAA681F",  # 48343
)
# Tuned, glow and shading presets use the same HUD overlay.
TUNED_STEPS = (180, 600, 6000, 13128, 48343)
TUNED_FRAMES = (
    "CA1F0C3020363B038C1A519ABF85760F2F5A7346F5610D6E3D19AFA39E711039",  # 180
    "7D8BE9B9EA70D22D3FA05AE48DB52B01C0AAFA09B3CDAE53A170A148C4067D65",  # 600
    "462B9CF33D9D6DAC82E97CC0175D7DA11C70E4680A7D465C9B50D9D2B53AE535",  # 6000
    "9EEDA51506B2B01E2E797B7137E90C12A6FEB898B87A67338321B4784130C334",  # 13128
    "CAA7BCD8487140A4561D9F1A6B43CA75C28E99DF62722ABD3B1E71B056555B63",  # 48343
)
GLOW_FRAMES = (
    "315626AFE9F2AED95E32373E08FB95546B28E0CE7D2BDD0D6DB0DA05F9299BD8",  # 1
    "582192A84FF4CF089AC90C325FD3332F343F986B8C28D17FEC8AD52FBB4AF378",  # 180
    "904F0C886C807AA757DF8E37C7CED8BB47C16CA66EC721F0E041D5C37BFC4CC0",  # 600
    "2AE74CCA3DB854D0A7ED3ADD065E289C1D9B11D1BC02F272091A1E7165089994",  # 5449
    "07F37C4936567F6BEEE18D4D9D57E577E696EEF507780E05296D975E6D89CD0F",  # 5450
    "E81C07FA23CBB2188AE880F5CF6F8AA4DE53DEDBF47AF024CCF4629273519757",  # 6000
    "BA162CDB31273A69804DDDFA1CCD64FF7BD871BD8E9AED315B0D5F14E4ECAD1B",  # 11344
    "DF6D80526049E693BCB01453665161EE599D7A32BC50AE59CA235A0CD29C8345",  # 12459
    "374A6CCD9640620A813C500E7FE258E332C7E9FF843CA8A6CEDF9BD2F809CC7B",  # 13128
    "13F150DE71E85F30F3096435933B1541A59C855B40CCE150A5E1EBF5DC98F2E3",  # 24287
    "28B6EFE9950BDB396596BC136ADA93D86B83F884BD621F5A076F8537FABEC31B",  # 24288
    "99225A0697376F3030A96AAAB94F839A798367BDBB43BD4F8704B0C9973DEB8D",  # 24300
    "E65BE8E888921149005781A226D9595DE6E64F8FFACA6AAB9A86E25C53D7925E",  # 35396
    "127F2D48D5B68C6B8E5B23420DDAB56276E2314C6B76FC6A440A530346AE5362",  # 36510
    "3AA92174B1E729259F4E33702D8D002B8B1C461A41F7BA8CAF2A88A145237FBD",  # 37177
    "9C1C19CFABF3438BF830002060E8F3C3CD6461BA145980069D48589467B7ECFE",  # 48343
)
TUNED_GLOW_FRAMES = (
    "F2D3C065CFECB167AEAA2AC9722AC9C862F3EBF768A3DD2A61359B26337BDBA4",  # 180
    "6C312593A99E89A76BF451EDF2A10292849190327652474717E81209E90486EF",  # 600
    "CFAC49F8FD6E27DCC469D364FE395A2DCA2C1389717F0BDDDE0D488393C7618A",  # 6000
    "04DCD3D8B40F9E18A2AA8E390E4150C46DFAE4EB8EC705A5534F1D6B9A114878",  # 13128
    "C9396F0FA8A896237342726BD1247A6DE14D6956C6E2EDAE4ED4861FD6D110C3",  # 48343
)
SHADED_FRAME = '9308DD5CA4EC6B4A94B85C89F9D6AF6F509565938EC6395BF90EBFDE21350E92'
SHADED_GLOW_FRAME = '63BFCBE835336B8FDB602143DA4E99BC48D7397103F87C43C44E4A1DBA3D82D3'
SHADED_TUNED_FRAME = 'BB72313B8A9BDF2FFC7C2075D44F199E9743B79D0B7DCDDF87D9F50B7922632E'
SHADED_TUNED_GLOW_FRAME = '34385C2C7DF3BB7AAF9FF3EAE0C4D8812BC493E478EEFB7D20B783B75C14F2B3'

# Wet road pins retain all seven optional effect combinations.
WET_FRAMES = {
    180: '840525EE00A890C8B605C564E596D512EC4ABC5A6EE2BCC7FEDAAD7279680532',
    6000: '8625DCED5ED8BBC0EF84D6DCF5CA70E0B8ED5943AF195679F14A67736F568360',
    11344: 'EDB1DBABBF2B45328256A8BB9C7DC6F917CBA974A2BDB327F21DD6E45BFE9D2B',
    11360: 'E3910F85B91B0A5EE8AB9B6580D57AD016B7EAFADD2BB463229F94332215F7CF',
    48343: '25AC29E38C190A99A9BB94E2256FCEE35F12BB252707BDCE2D44AC2DFFE8B215',
}
WET_EFFECT_FRAMES = {
    (False, False, True): (
        "CF2AEBC7686B3A602D0884491EF9D3672A9A6F689BC91161AFAED2B568AF82DF",  # 180
        "1D0794204073EDA0714A9A7E4008B30B012317040C01DBB5E01A625DFC7671C2",  # 6000
        "5E80F377FE7FCBDF5F1BDA4C19203EFC40870FFBA4A8C04BD90430CD19BA7801",  # 11344
        "20CDD5A5B82792752B3F8D6F180082A1152FFD75C01FDCFD5FAF1ABA41C7CC40",  # 11360
        "E2E12658F813FDB0C372580DE8C3BB8A7DA5403908A003F6214B7CB4A0DE3AD3",  # 48343
    ),
    (False, True, False): (
        "5A6672A6376D56DD73C153E26F761D9833570D637BF140800804133CB4E7D6A0",  # 180
        "F536A8CB58A5C2AFCA14DB411AF35D52D0BF9CC127EA80D6D76C8339D7DABC82",  # 6000
        "FE085C69C5AA2CD57C5B66D23F746C990D51E98C76C232C6B8709B6C098A715F",  # 11344
        "32F84CE8E1F9BCD6D9F9375B1686A5134B3813BBA58C56743C2175992891A8CC",  # 11360
        "E8D4D9AD65EF2C1E5FB6AD122D154E45B2A232D739DCE82E51E238F665ACC614",  # 48343
    ),
    (False, True, True): (
        "E65A519728427496990D0F8C3DCE90B87DB14FED8D691B9E65ECD7E0C1C6A607",  # 180
        "F5020E83369B51071042F18CF52E1E3CD99C01011613E0B31A09C8A295A68259",  # 6000
        "A947E722D8ED2142E85AB4BF3E7B2B5435B7462FA1661435B1833E1A4AFD40AA",  # 11344
        "55E2957D1391AE6AF50E27564742512464BA20C136FE36FEB270C090D3B2BF29",  # 11360
        "60CC7D27CB712901EABD89AD35EF8FEB7786B6868D5797F4259FE74AF4C019C9",  # 48343
    ),
    (True, False, False): (
        "F28DD1BD0DD571E08D5E6E3A1DAF233A4F8EC239DAC3F609DF8519F31715BAED",  # 180
        "48789401B2AD9B894A3022238C73282B7CB22983DC245A0D1C641EBEE9178CAF",  # 6000
        "C6BCE9082A589B51FCF6636BC349C5A6A20AA293795F7A067CC990626F297B58",  # 11344
        "6FC9445E5C7B93547FC8C2C21A104D551BF92087841B96CF5DD1BE6A2ED35F61",  # 11360
        "54DD562154329A938CB80BC628140ADF4D9620A4EAEC515FEDF24535B559CCDB",  # 48343
    ),
    (True, False, True): (
        "9D5962226F56B846705322DBAE83A605373E9A88570A197CF331FDF2F2C2C681",  # 180
        "9BBE587D9AFE20C9D61C411796C636EB3594B37BD25B7D02F16FA27AF64F1E0F",  # 6000
        "61AF45C2F3342A5C5471C6F098454FA85AEF86F011C248062228CA208C0BEB23",  # 11344
        "98048588D78B99CCD1F40332F239B7657F775FCB92AD476665916B1308E65819",  # 11360
        "ED5588FF726821BB83CD6FCFD5780F5E409DD9C390E2A0A018DAD9670FD9C1ED",  # 48343
    ),
    (True, True, False): (
        "892361E941D8291F36E8C953607E247306923E250E2A97CEFA5AA32ED6D306B8",  # 180
        "14058CF4D8845FC5D908066FB0F5C4CF2361EB36731BFDF0E96438FEDD959B2E",  # 6000
        "72688C26BB6A6239CEB70D2D57ADCFC93E3D12DBE5B44AD4B4303710BD0439EC",  # 11344
        "7E8A66B17F253CDD8C7A44D03E0DCFE6C7CF9A81B0BA5EA0969A0DD7480ECBD1",  # 11360
        "ADC36F065A07A238962C14EA6B04F10F25027D22F1A7EFEE72A76E4AFA601F3D",  # 48343
    ),
    (True, True, True): (
        "0341B6E340513F53898DF0B2954813FD02A3A77087EBCA4DB0BB4E85F46DAA1A",  # 180
        "44FDEF15CEA07055C1131D4F3AFEE1DCB9104A37DC477E96FBDD6C7E53F1E433",  # 6000
        "88ED07DA6B3493FD27D981B144C90CF23D74AC7397754ACF9FC12E5845F5FB7B",  # 11344
        "8D9E693A6DA2CE311CCB057C2E1128B07A02B53BFB729779F11BAC494B6F95A0",  # 11360
        "8B2C571CC7A4652CAC68993ADAC2439FE2BCFF36639EB7739A3C9846CD5B4146",  # 48343
    ),
}

RESULT = (
    'event 1 track 1003 laps 2 outcome finished steps 48343 resets 0',
    'lap 1 ticks 1616320 time 6:44.08',
    'lap 2 ticks 1611720 time 6:42.93',
    'total ticks 3228040 time 13:27.01',
    'state-sha256 321C957E298D7FC6FD694B6ECED4E679AA17401D9675B635A1DA1F4049CF6C6B',
    'stream loads 78 releases 0 bytes-read 36471524',
)
STATE_FILE = '8A45CB6686CA31BACF5C5F14AC14A7380486E899F0EAC8A3EDF7316F44CB55E5'
INPUT = 'D4D5172161A897FD4A746DC58E9C35CE235739658267E56F9E7BF401A9247515'
TRAJECTORY = '68C4755237D1120690C4E7B3BF3C0ED1F6ADA354C5553D6053BD485ADEB9F3B6'

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

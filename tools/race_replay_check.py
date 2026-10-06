"""Check event 1's retail replay pins, using only fresh output directories."""
import argparse
import csv
import io
import hashlib
from pathlib import Path
import subprocess

STEPS = (1, 180, 600, 5449, 5450, 6000, 11344, 12459, 13122, 24287, 24288, 24300, 35396, 36510, 37177, 48343)
# Combined issue253 HUD and issue275 physics visual pins measured against
# the reviewed physics parent. Every state/trajectory/result matches that
# parent; pixel differences are confined to the bottom-right HUD region.
# Selected gear HUD samples and finish-line brackets retain coverage.
# Low-support torque changes airborne state and the generated driver recording.
FRAMES = ('F0281CB7E752745230BE4A66B9F6FCE148A4BE4F5D8A582C779AE5F765E10940', '9DCFA74DF4B1B599F8D3DE506998B30F3F535CBF2F87166D3605FA5C95CBE555', 'DDEA0EE6E77341B24C9BF084FBFE2489A4FA3700147261B8DCC96C4E5CE5941B', '79E1A3F8568EEB7B95E13580205C37748E738AC036EAAA96DCE1D4B31594761A', '69669427DCA8FD444C54962D729E6818AB4B8D283ACFAACD9EB18C3293B7065C', '25257F2FEAD3780052DBBDA3768A0A2751FCD4DA1F90060CE24B0B2006687137', 'BC219C52A942CF683913F0C03F85B4319EA1B3C321EBB36F40E97E91B6E63723', '9884E0C8DDDA9998A0C6F25E870BAD48727D30A02C0D9D61B3C03ED51EE8B878', 'C053D55EE104C76C4D586E41F05BF7CCD78017CE56DDDD08B4F0299739815F69', 'B1D0F5F0C809288CEF4324B53970F43F42C72DE85A7BF745CE134061F6B301E2', '16E24488FEEAD8BEF70556D31AE8678B271CBC680E4D3BA567D27189691B941C', 'C5BD90C9CFD33A84026235AEC28B60AB7A6F8FA3E77589896A38E3D283E72254', 'BA7A06BB0FFFC48A59969EFBAF6AD23F8FE2D02DC3613DB7DC810F9440EC1156', '809A24CAD7A6CA2A55ED4D7D64E71432B954132C5B2EEE3B6B670215B1D11DEF', '138BB541BCE865D319A95621018DD980D320F25F5F0A1A7F1CAC1AC51F98BD51', 'C7D078DAB34393E3B48CC3580426852550B0934D7B3151D190ACD415631C097B',)
# Tuned, glow and shading presets use the same HUD overlay.
TUNED_STEPS = (180, 600, 6000, 13122, 48343)
TUNED_FRAMES = ('AAD788E77C6C69F07557832A38AE7C9BF8F6281ECCC3E30C4F0BDE121B8BC60B', 'B1528AAF4EAABF0C34B178030114CB60B978279C884EE045EF4A35E7079E9991', '75EC2FAB3D28F02C7BD157B7DD8F473EA77415493682454A5949EA650CB28AF9', '8FC5EA2126FD7D0D892A47FDBDB91B5255ECF62C4CDD28E0BEEA44ADD4238A62', 'A2A9BE5D6AF68B48EDAB76913935EAF6B0BD1B3EB3606103704E1BF1906E7AF2',)
GLOW_FRAMES = ('8FB4151462085163E24305BEB51F5BD0F529213569C33B006187D170CAB5D8DC', '954325E7308F0B39CCA9111644D6A5EE39B336F945F41D4C27571EF63D4DA1F4', '453CB515B76475F3592AB3642FA3153E16A839D198B0A36B5BCACC480ABB4D66', 'D18966BAADDC6BCD2B34B9DD3DBE31256617F0094C285D7662B0D51EE05C5C68', '244DEFE45FB4692CF9AC1A4F3F6BD2571026338881FF8008B625257407D2132B', '0C1AD395A315EDF025E75BEB7BDDF5944C97AF4659B977B70FF3D4007EE8ED76', 'B5E93A71F283F2AE6FCA624B177DA9B0019418297518D7112CA9BBA54266F4A9', '84490B0F604A58BF3A9EDA511EDBDF5098433FF26634D6BE219BA30BF7B9D01E', '482E20FAC4EBFDBACDC0C49F0A1486DC77E40AE6D9EE2FAC749F2C1B64C13719', '85F43966CFAA300C4D2D8E19ACF6663D18D73455123A6B23411A1D7F5117077B', '141D8AC5FB6534E8DA51A156EB4CDB2A99265B1715A2AF41EF26FD8FD8EEDB07', '32A469677EAAAB295E15CE534C3E7147743A12675337532DCBD1F9608729E73D', 'B6E42F409D2E333235EE1AE6A061C2FCFE046CF381629CA49F2C50868B57A9EE', 'D8589B3F4C7E440A786A5A7A76D9FBF96C7B7FBB9EA47E5F419074B0FDE91345', '1DC2C615C9D488709E25DDE0968B2ADDB6BBF8F2DFD78F436A164000045DADDA', '5A0DA37EA80C5607F021317DBB28AC8A7E83F8F73A76477D9983E0C6C62DA912',)
TUNED_GLOW_FRAMES = ('1460A652091091FE4E4D0A126E140ADACA16776661FE1AACCFE9FC2FE8B0898F', '236761DF94E36E1E71977942C8A5D485CCE4FC06C88B79D184DFCB0222E31E29', '54F578036ECCDCA1F2B48B3A1A9E82493B0745BBE0B5555366671C9C27B38FF9', 'AB4C4752CF6E70A3BF4C1AD7FCE4D3E364F7A9E27FF1BE11B33855EB9BB9981A', '4280DD249ED305E26E777D940A8908F45E5D20608F899E2668137286348D3E96',)
SHADED_FRAME = 'E384589FAC920C1086507B0F404C0DB6F1FA8EF61E6CC95C76742AD515F19349'
SHADED_GLOW_FRAME = 'F4B8E3804D393554F69150466CBB06B5F5E4583DFDC8AA8944DC0CC07241C625'
SHADED_TUNED_FRAME = '637E6B597F347FAC5106D27A34EF776A8DF6FBF4AF91FEBE2731EFAD146C0624'
SHADED_TUNED_GLOW_FRAME = '4916983646A715900ED171CEEFCDE19A88A850A9D8DC32B031F838C262698440'

# Wet road pins retain all seven optional effect combinations.
WET_FRAMES = {180: 'F54BB16D93375C73A007ED3AC312A738C598C310124355F868F8CB4A9E57B000', 6000: 'E51C7347EFF85309DD063095D39FEA148D08DA75C489181048308C52CFD5E294', 11344: 'F5E76DB823D83BA72C556A1DA58E9853BAB57AFEDB9573469A57A60678C5BFB2', 11360: '38B955A5E28882C1CD4A5FB98911A7035267948818F568B5F42AE23E6C0D30EE', 48343: '377BB4CBBE6F1244FC3E6F087A45CC7A3CEC5A513C15D90196884883048E3A45'}
WET_EFFECT_FRAMES = {(False, False, True): ('7108D8DB76D4B0340CF47C2970D796F075393859521C072C5E494BCCB2BB63B0', '478AFAF4ECA7DDD6DD13812B0092C03084B40B5048E72C736C61062DBDF23066', 'C70F48AA135F0E56469C3D89AF33C984373585CF13D7F59A6FF69E1C8F9A41EC', '2EDB92429DFC84F1F8BDB47F24EF70EEA142E68409F089096C46B1913E1793F0', '16ADBE4A7416204350860C26FB9550FEE849C6146404AD8BFF55CDC909B35DBA',), (False, True, False): ('A5F94EB890C3E826B8FC4A354E7B7C6878A594A5A57D44A77CECF7F1CFCE2EC0', '9D6DF8D30D5A3C764C5A3A8453EFF5E9410B6D6AE436C6EC7FEB5DE33E9A39DA', 'B0E7152BBAEDD14AB42DB843D8E1D3AEE81D01AA3BE65AED7EAC8C3572077F1E', 'AF37C9B9DE8B0249A451ADAAF4C8652D73EC438EA4F2B5E8B85BD29BC183BA91', '94D5794475B7C5DB64750BBF04CA6659A7A44F940323442F2110E40C1A225CB0',), (False, True, True): ('C65BB217702B6AF28C757F1E38F693C2538DD77CEAECC599121C6EE78967F1F6', '0BE147424EDA72AD8D7E7356B69B530D7F10A240E3863195054ABDE2487745A5', '18D9DD65528312DBA17851DC72D12FBCCAA856C5246B116006EED2442ED9FF34', 'CDE38353A59E35283DBB2B3FD129FB7F7BEA7AAD8D1CA840B8D90280CC4E78FC', 'C18A240DE764A5CE1BD258947887EC50F0713EB0E1C57591A85422260CAFED41',), (True, False, False): ('50EBA2CF9BEFF0FD1BC59D4D6FF620A43A42AD7F01690AA80948ECDF80C9B0C4', 'A6DFE48E40A819A6EF790050003BFB3B700C74A88A73E334F0AB46D1EC2DA582', 'C1CF07101FA35E0F4EAF1EF2000F420BC9390B9E62434AB0CD4FB4B4FFC4F155', 'ADC9D182DD9F2112A87D14F5C56D22AD03D1B067CE4E377E8992BF2F70271F4E', '19CD586322C405BF348D2FC6785BD7A56F1BEBFA44BE5D8DCCEAE78CC7317378',), (True, False, True): ('C7EC908BC26C99BF57989815ABBD6E9F7EF5613C6A6E9D38B68F50A63B33846E', '0FE1C6DDCDD70DE3E30761910EA6DE1D55194046C34A10B75831A386730E2A91', '41964448E34B6FE7CDBE309DFBC65DD9371F9F3482364CDF0CB0092EE2E6904D', '60CCC01AECDAFA1277EF8C53F236884D7E10D759D2E02CC5701E7C32546B7205', 'E795F3BF00741EB16FAD6975A049406DA14CAD54CCD3A565D6BA6BD3FD32D90D',), (True, True, False): ('59D4AC60F2703902BCE0CAF02F133B1BA7FD755F40C8BB687AC2A751C0A3F836', '6BC60223607C75B7A522F8DE24AF066010BCD42FC323DB9DF11C4A66A4186A56', '5A2E164061486118AB2317E7A4334F251C2B44EE62D5F3AF1CD3AFCD88593C6D', '543C26DE50B0F11F21B02E243485EB8ACFED125175CFE31D26B5173E520F6B73', 'F902BDC7C4C401E414D2F65F35C373607943F93BBA242B1FA3C946587CE42877',), (True, True, True): ('839896848F32F8753C996B8540AA594DD46045CD53FFB419FD466DB12483272C', '493DFF91B9CE8010F501051717106891E674B5432930FBFDDC7FDAC55DB0AA9E', '60122FF6D61C9B1CD8F5011E4FFACA2A02A12DE9D44CE5C4396CD5058E46A5E3', 'FBF18E844E82AE8A1789BFD053DE2806D0EF493A0A5A05CE0FEE978F1600C90C', '33078760DF514F066522F776E2A82389810068F44D21B4E4E34D980872896C59',)}

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

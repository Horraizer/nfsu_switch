# Common477775/479640/46E020 physics: regenerated driver and successful full replays.
# Native instruction fixtures remain independent of these derived regression pins.
# Live zero bump coefficient: regenerated input and complete successful replays.
# Outcome, contact, upright, support and per-slice assertions are retained.
# Start frames refreshed only inside the original 321_Go HUD object.
# Paired before/after renders retain identical states and trajectories.
# Camera images refreshed for original40DD00 independent focal scales.
# Native cycle/projection oracle: tools/race_camera_native.py.
# States, trajectory, replay input and outcomes remain unchanged.
# Camera-only pins refreshed after native chase surge recovery.
# Full stock/upgraded replays retain all noncamera states and physics.
# Native camera fixtures remain the independent behavior oracle.
"""Check event 1's retail replay pins, using only fresh output directories."""
import argparse
import csv
import io
import hashlib
from pathlib import Path
import subprocess

STEPS = (1, 180, 600, 5449, 5450, 6000, 11344, 12459, 13122, 24253, 24254, 24300, 35396, 36510, 37177, 48260)
# Canonical retail runtime stock setup, shared start timing and native callbacks.
# Host pedals update every frame; a brief opening parking stroke covers smoke.
# All per-slice physical bounds remain unchanged.
# Optional effects share all simulation states, trajectory and race results.
# Pins are harvested only from successful fresh runs with retail unchanged.
# Original ordinary-world RGB: texture * D3DCOLOR(BGRA to RGB) * 2.
# Fresh control frames retain prior pins; simulation and alpha remain unchanged.
FRAMES = ('49E72E48B6BE5B154F4534A0E298F52646A7B1861110A9F40F610A3761292408', '405BCAA7F1EA4AA6E98714936E8AC6319F718ADD598DE081A01B93A27AE54A37', '96438FE594AE57F8C4D865CC7966FD1138663AEF88847B530CF1AE3D26096570', 'F5A701BAE142D593D5B1BAF24F49E686C8080BD67E2EB0687017DC70734B25C3', 'DD849688E4CA033CD79F73FFCE71D50D225B095E63D5286E3C2D764FCCBC40E3', 'D5C1F634B1798430A3BD998B73D236E3BD6D5F9916EBF56D461D6BE176B54D17', '19B09D8C756D259D78B6C7F38B5183187EFE1AB267093518BC12AFFEAE7898FD', '6B680B08E118385EA29D330C0D207178535F1904F0A367048E15EB71D62ABEBF', '4A1DBCF213539AF22A66470A232D2CC649D1A0003B10D553D3400776ADC993EF', '8E280D693849ACD00D409DA649BDD0A0F53BFE818179F1FC80FACDD0DCCE0336', 'E07AD5641AD85C21CEE3F8FA24A2BF26406022220C6A23EE8AFB9B0427F1CE1A', '44906A2168CE0602808D1890F3540DF1DB831AEF03F7BF90B3DA94A38020306A', '916F38AF7CF87A9C4463BFCEEA58327650BD45973CBD721623413FC061007DB6', 'F94465EC945F8F9F9DB16C37E76059F4556957C23D6B19698B1BCBAC4888D4B3', '0162728E04E26AFC32051967C851A3EA912AC60D57BF6BAF5A5F63D36515669B', 'EE129FAB1E751CC66BCF9363A45445AD12E3CF088E636D211C566A4E334B8A7D')
# Tuned, glow and shading presets use the same HUD overlay.
TUNED_STEPS = (180, 600, 6000, 13122, 48260)
TUNED_FRAMES = ('EE5736F4EBF71577970DFD2055B60DD64F4B594646B86FFFBE062067D3A4F1D0', '08FAF404AAD84B54BB083FBF664F31E87747BAF909A4149D65839286FE836DDB', '2032DFFBAE11892269C96412765D18DB85B41747830D4C8EC3E77A050EFF5737', '2E007CE41F7522BF5507DF886112630FAA2184D1DA0AC143F95626E2EDC7F53A', '7CCFFC5EAD8F4D0872EAC0DF5828C6E62FA4E1A9EF7E015DE4BC812AB748AD7A')
GLOW_FRAMES = ('D1CA14C00B0A7CCB4AE6C422B9CBAC3FAFE9C3B0520EDAB41661BBD8BA563557', '396327D0B44A4E3D066B623217B9E13A416F3E8B5F08F180BBBC76DE1B5381A4', 'FA1F1B868A5F45E53DFCD272C6B021C2EA2BEA24A1D8F16A1DC80D16635AC42C', '2DC083B8C90A7987E4F58AB750E5B246A8A2DB6E715ADBF6F1E29C179A4FFC20', 'C31863A5C14C97FD9756B3937952657EEFD02FE0507254EB57A2ADE171E521F9', 'E635F6141E21C048E9F7FA55DA471A28BA75948918DD10D1CF76088A9672D4AD', '97EC3394E270A4D27D4740B0E9E5CCD630E1314BE6D47A2A1667148904F9A00D', 'D2C7F420187807A78CBFEB0FBAFF987BFDD989B64A2158A18A8F8103AD5CA4AF', '1777B7CEF5FEF9E1F894E985AF69C8C180E2EEFB27689E2C8BFD92B895732361', 'B2926E4A4B0B47E9E0CCDD7352FE5E37557893E0CA69510CE263B7541E3A65D9', 'ED6F066EE40427652519E83C6EB0720B4B3BBA4BCB69E23066E20B657D61FAAF', '5C2564A84AE074AF861F99ECCB42D35A0414CC52A3FAE98E7802079AD40DBE73', 'A4AE349CFDC8E0C944ADE3BBD0AF2DD8CBEB034C987A784C6BFC985E21F2B28B', 'A52C18D6A0DE6463656D8969AF15A800412E13F4311B92E0B31E62F234F16B09', 'E55C154833E073801F96164811E161BE7A721A620E617F4ABA5F98577239AC7A', '705F009BAA2FAC2E802467153E785937EFFFECF5F283A2ACA2932A789E15445F')
TUNED_GLOW_FRAMES = ('478E31D6F6D5433E4ED846C0A811FC6FB68817C6BCBAC8FEEF89C2F66761D1E0', '55EA64208BED53E015AEEF37C83E5F9260F8CB2E2040CE52B2DD4EE2C4F76CBE', '21EEFE0429F10540B8021A39BFADC6993113E379EE2F47580DECA90D7D38FFA2', 'DE991BB52BEDB5258746ADFD08BC360813F3986C9746A873461A90612D0CD94F', 'D3919CA88D7C1CAB6BF183648BE29F4489884CDEB025EBF8FB76CB71309DC03A')
# Re-pinned for the installed-BASE_PAINT material correction (review F2):
# the shaded stock 240SX now paints with GLOSS_L1_COLOR08's own material
# instead of the blue swatch surrogate.
SHADED_FRAME = '68772BFB97288D696F017A47B31A5151C267F04AB837F1F20D02AAC4BBE59DB6'
SHADED_GLOW_FRAME = '6691B6B9AD3A216B1D10CB90D4C60578FF0DB62F74D16D510DA10A481B3F7236'
SHADED_TUNED_FRAME = 'B4441CB119FDA09462BBB255968B9E95F145665F1E6AB03E7308B0B1C8799131'
SHADED_TUNED_GLOW_FRAME = '06BB1C1C372B4AC5C2619B68E87CFC6DA5F7A62D5B751B9674C2480E594FC193'

# Wet road pins retain all seven optional effect combinations.
WET_FRAMES = {180: '94BD099E271197C62F514123A667C7B86BFA9142FE9B3A44C0EF7C5343208861', 6000: 'C2D714A0E3062F002CFD9902438BA53FE7CDE6DF850FEC2131931CDE5D4A3618', 11344: '36686DBAC629DC01B9F0228807FFB243CF23B803F95E282B3859A3CE0C5E9671', 11360: '10ACB382DA609A1B75EF11FA5C9B54C56A89281E41C30E81254E859674D3529F', 48260: 'B575FE2DB5FD63EED3C4FAB9F4D583CC38A14E58F89CD3D79FC41BD9528DC0AE'}
WET_EFFECT_FRAMES = {(False, False, True): ('98E8D39EF3C9D586CCC3C20CE1AF8D541F93AA9E4616EEC6A5CE8AE31328A2EB', '6469B44E3F9F58390A0A17756655F87F45B65EFFD5B545560F25D13D8D28BB9E', 'BD5481F0CED0909C1EEF7AE97F3A54F239B724E7ECDB44F7AF0C1397BD240911', '9E3F977B24A7F03F6E16BEF43E893F6B9E7B048DEE1BA5821B5C749C2BB14373', '17F491C385108E57A50F332E713E7D6E028FA57F2777D7F703F140CD884263D0'), (False, True, False): ('F55F9F80AA1D0F81177CB29545BC9D2D24768C229F12C95EFBECA5575C11BA3D', '0B102A6CFE5D0AE4C772525EA34C5727661D13BB1DA5AA5F1ACF8AEB5CC0580F', 'D3778AF29A9FFEBCD2E62DEF9866190166D033A22F82C280755A3579B82EFCD8', '2055B8DDFDE63D94ED6528CEF4F204889D196C6F0EB1104ABEA53C1FA310660C', 'ED9DCF3422200DE743C1465DD1D0A892F2892838B5D40C7771BD525C174364E6'), (False, True, True): ('2723E68BB59956E05CDB8C4E1D16AF7A22A862C0CFA8B35685659D5BC9DBA3F7', '0E9D555F1E24433D61AC1013B0B98DEAA345494540707D4E2EBC05EB07935EDE', '790AAC9880E3A3B84F03439472DAA8F224E4602BF7BB308E3C9C6851B5E95FBE', '75FBFC260C4C66141500E6CCEC735B6C5B16C6F23EE5A13F02CF0B93FC513ADC', 'F0C28713547C50B177F3B348A16178CA25346AE5B0AE1500A6C3402DC0C275FA'), (True, False, False): ('B77E809A9ACED1473F480BC9B284170661396AC600F867FBA62AF880E2E7A192', '9E5620BDCF27D0E9E4F6AA3020B9D6A9969B7B8DEB32F8D2FB7144B13379C813', '536D5DAE3FD36537089F309E70AA20F21BB308D5B34DF4661D4134B7218013A6', 'A1FFE4976C370BECE2521EA24C60EDDC6D516BB5B154D66EEEFADCBDA7EFB5A9', 'A383D4183F62AC7BD30A2886D576C03EECEB22B081140D69775FB490471E7EAE'), (True, False, True): ('D21187A30405EFA03B580839E8CB85DAC440BB41DF54AFBAE3B2958DFD83307F', '95443D1F6E31392DF6BEEF4535347C16B1AC7911BA04698B2C6E7C3DE1F914CD', '1A4935620A1FE371069F7BD901716B169A59F44F21C3BB76586742297741514C', '5EB14388C620845E3A5382F981A05C2411CE7DA2ABB87B64DDFAFF336A210EB8', '2D0819E109F6E4A4F4D032FC388284C62EE4786E9042F2618A6195B46480659F'), (True, True, False): ('F26EE7BDE084AECF27EE059DDE004861C3F8E1C691AAF2C1D99B16BDA239BE84', '230E8D5BE1A929C04B9755F189DFBA2BA5DD583A80E7507F6462E6B698E9161D', 'AD373700170E5217438FE24B878F3322101CA505B525E67D4282849784C5DE55', 'EFD73FD83DB8CEC8F16BC9CD7324B3753358324952214F4BF5C24463DC445832', 'A71981E0C07306B659E2FA8F78EBEA42E74BC9C350F6AF1F3C38089A91C523F7'), (True, True, True): ('0440F553E8B6E41D6CF6FFFDF86594308DDD81EE43D38C2BFC3BDC22E0F73B58', '043CCB5BB77AC4559A5B2B0E87828CBAD2628A4DBAA174C425C7B5332497AF97', 'AFA079FC236FEB9835B9FAB2010A4486D0A8AF87C866EB3F74845FE8F5D10860', 'BF62EDC6BD285C7A9A1046918A11AE87250EEF1BD46621389B7ABB0B985D0526', 'CEAF123272CF7248BBC965F579B0D112256DDF7D5D2FE307FB19723BEC815C2B')}

RESULT = ('event 1 track 1003 laps 2 outcome finished steps 48260 resets 0', 'lap 1 ticks 1614080 time 6:43.52', 'lap 2 ticks 1608400 time 6:42.10', 'total ticks 3222480 time 13:25.62', 'state-sha256 70D6FA38CCAED185EA55C4482E249C163152B05885E055BF1B7CB965BDAE06AD', 'stream loads 78 releases 0 bytes-read 36471524')
STATE_FILE = 'CF08E75406E4AA051BE0297E8D2E85D0F52764E8DFC01A42ADB82946671CC001'
INPUT = '6E9CE2689BA9A33E4C2D8E61795E7BDB677B6AAC45E2812B0A57FD7F30FCA66F'
TRAJECTORY = 'BE3DD5F616DC24ADE3500FA11E0C8FEA6C4805F43A255A841206CF7304D5FDB1'

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
        require(digest(trajectory) == TRAJECTORY, "240 Hz trajectory pin differs")
        samples = list(csv.DictReader(io.StringIO(trajectory.decode("ascii"))))
        require(len(samples) == 4*(STEPS[-1]-163), "missing physics samples")
        for physics, sample in enumerate(samples, 1):
            require(int(sample["physics"]) == physics and int(sample["tick"]) == 163+(physics+3)//4,
                    "trajectory does not cover all four substeps in order")
            require(int(sample["contacts"]) == 0 and int(sample["resets"]) == 0,
                    "body contact or reset in trajectory")
            # World-up alone rejects a correctly banked car on inclined road.
            # Retain the same angular limit against every supporting road
            # normal; the exception requires all four road wheels and an
            # upward body. Missing/invalid normals cannot qualify.
            upright = float(sample["up"]) >= 0.98 or (
                int(sample["road_wheels"]) == 4 and float(sample["up"]) > 0 and
                float(sample["road_alignment"]) >= 0.98)
            require(upright and int(sample["road_wheels"]) >= 1,
                    "tilted relative to road or airborne car in trajectory")
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
        print("PASS", exe.name, str(STEPS[-1]) + " step hashes, result, " + str(len(frame_steps)) + " frames, clean 240 Hz trajectory" +
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

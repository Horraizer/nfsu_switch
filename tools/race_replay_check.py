"""Check event 1's retail replay pins, using only fresh output directories."""
import argparse
import csv
import io
import hashlib
from pathlib import Path
import subprocess

STEPS = (1, 180, 600, 5449, 5450, 6000, 11344, 12459, 13122, 24287, 24288, 24300, 35396, 36510, 37177, 48332)
# Fresh integrated replay: corrected tyre arctangent and direct engine HUD.
# Optional effects share all simulation states, trajectory and race results.
# Pins are harvested only from successful fresh runs with retail unchanged.
FRAMES = ('A773FC78816A5D2463ED1CDEB5AC0551D1A61C1FA55A7622F5EEF629FA8DBE0D', '9B28256286FD49D9018CB5B8F3E8781E0838CBC208622161D457BC21C1FD484D', '73B91FF45F47554F0D06F2401A3709079E2CF5FE0DC631C7C1720D872818FE2B', '5643ADA8A70F48DEB594122843F441ED1F913C43DB0576BC605F6590A2899744', '9C6E1565ECE0F47F1D746C4F9F6ADC7D46C31FE4B8F162F1E6BDC7E6852EF7BD', '9ED5D9A6B1C21BC9C03D91DFB79DACB073723F17A93894CC0DCA6EB79AC11F80', '29F999A7427AE592D141A2FD289581D22E3B4835816F63775C626634F2AF1903', 'D37F7FEB3DF5C5B1FD7A7BA15C9B93FAD9F5F2689739CB6A2631E09951AC1F19', 'FDACC34A7DB4C53F924FF76CC17674B2B71E4AA6F313AFB5383F39B8F3E5EA1E', '0B2BDC29E6A26A22AB9EC871975F8A7F3C91A16912A8F458620F3F0470AAAE3B', '6A2B14DAAE7AC3685E791F3693062C4FBDF380E0CBF715B2ADF82C42C7AE6822', 'F09B0DB7A26D59563CE32A2613F8AE8862522FBD425CECA6B66BA1A0882EBDC8', '39C3612A427B4208692AAB8DF77C4468926D46184C10C22B3D3339B9D62F49CE', '14EF96CB8FD4CE7065AF52955955746AE477C7BE22B41A9638D125D09EBF9069', 'EC0D667420242E2CD225F6381B61C047AD917204183CEA4A2328952E82263A5C', '19B638C9F2C89B8DC44B681E576FA689AB11DFCC3C20AA17DB5D5AFD31757548')
# Tuned, glow and shading presets use the same HUD overlay.
TUNED_STEPS = (180, 600, 6000, 13122, 48332)
TUNED_FRAMES = ('A83F6F137247DDFC0F644821A0F0FC7AA562F75280D947C4CCF4D7207EB533B9', '68DC031E7C97322E4BAD2DAAC460319BED56916E2E5E10C4CE81E22BE14B7A9F', 'A61246E2F82E1B95AA0FBFA01D3FA33AE0A7BDFDCC8DCBE2F3811D27BC9C99A0', '6F532798C0392E827F921B9BFA145B0634A49B7426BE333C6D6A31DD45FADD54', '563CA3ED2BFC6A35DFA5348416A3EE87F8E7C13E1A490F9618F9CDAA7C8D7ADC')
GLOW_FRAMES = ('6DBC44C73CEDD559C955199C95D492E2C7C111DB81A9BB0E20E72BFB3BBC505A', 'FED03F01C83B757C8E745C011F4E53C4E11BA3F303FDA576F7FD0E4FF1EE34EE', '7224F896F523E40BA39E525B7731ED5EAA142710EE4A75201EE3F949C414B5B7', 'B3363AD7E7EC2E47E5C9ADB58C80476F036903D156251BF2C6AD6629EB7B8B2B', 'EBB74E3D90D57D537824FADCA59248BC7CB372FB1520EFC8D72BE2086A270D46', 'D60361145F48E5B280A7979B1E930E45EF4DD84D1CEA5677D47BFF6C2E3836AE', '3EF4995111C6811064AFE377F10345AE56C3CA2442AE147194C2C7FDCA9D03A9', '50446C0B3C2A456F515A061CB2DAE7359F9A1DB5C3693FBC7228E086AD06726C', '1FC5608392D94B1AF538B0AA94640339B76EC01EEA3C780183BFA3E90685C004', '59B32B515B75B731606ECF7C0687B142A7CEC40DA5681BF211798AD09F2472E3', 'DFF760CA5743DE050833975D3004E2032FAB98CC13F4046F3E1BD578153093A1', '0B461962BCC7A4A2BE772D6D1D19FC8EE63879B124C0EA5548C233BC7ABAF6AF', '252854041D72D8086F4CDE61CC97AE917E231980FD45A4D561712B578DE9D9E4', '8E23CB78C94DBF899F672ADE22754BC98A7378F8F8610E08F908EBA71F26CBC7', 'B7DA8505A50A543F32E4143A9F913EB2FBB25061F0C8135A65C1FBE7AFC01BE5', '3872128BF98C56B669050904751E795077C90AEE9D2A5E59FC28A7AEF0DA0ACB')
TUNED_GLOW_FRAMES = ('B1B1B641F21F7BE5F065D80AE8EA4F7352BD4A26F9769DDDDCFC42C6FF5CBD8E', '77CBE4F8167BA2B1BEACD5754B9880B46CC04F6F4490C935D2DB33C61FA3D08B', 'DF531C9D5BAF8F7DE7CF667EEE6835E6F4DD6FB4A77E09636439A8F9E6CE92D0', 'FE971A28FA2A9AFD88FC51E6C5E0E556973363309F24311D7C6CDB58051E8608', '769298088AD793CD365F257B288C6B68671488BB2C8E3A7FE95E39FF5110BD51')
# Re-pinned for the installed-BASE_PAINT material correction (review F2):
# the shaded stock 240SX now paints with GLOSS_L1_COLOR08's own material
# instead of the blue swatch surrogate.
SHADED_FRAME = '493C8CDE516DB40568640AED195245A0D27FEAA4D67B3F8DFF6CD61B6C34ADBC'
SHADED_GLOW_FRAME = '25A46749B2BBEBA8805B5FFF587993D84FB8C18DF45D2F0CA3ED379452AD9406'
SHADED_TUNED_FRAME = '2A03C66E851825F6F755EAC556F7BA0A60583320B91EB762150241083D6490AF'
SHADED_TUNED_GLOW_FRAME = 'A422162EDC2996928BC851364B1AB3969FBF9368B7D80BA19EB7FBABEAABACB6'

# Wet road pins retain all seven optional effect combinations.
WET_FRAMES = {180: 'AE1677D05AC7C7EB73B963972200C8F01278CEC015FFE371E98339DA329988B7', 6000: '818F8E25E792D39AA3E4325BB58BAEB1EDA1DA65B744A300F75DE33B9AD7072A', 11344: '77FBDE1DF05EB464CF77C72766E4F293DD933ECEB7391AE048D5D888F39317F0', 11360: '1A98A746592809C6F412EAD3D514D29EAC6E9AEBF92F92BB89C8CF097BE48C30', 48332: 'DFB37D44875045D2AC4E293A9E765C7686C7927848A0B54C8676F40407C135E9'}
WET_EFFECT_FRAMES = {(False, False, True): ('314543DEC3A4DCF33C0B99037EFD99C226DAB216167DA5E25A7AD4DEAD72DF63', '223EC2F38D07632BA69273B69DB66AE5B518D7D5D8F9BB593B489F0408690D46', 'DF36D3C919CE544CBE5B4CFD49999D705B85DE0654C25698F78E8309C7848F62', '610FDD3372DB99533BD59E6E9F3F5ED2F5AA2EF85470214A10230DEA3767600B', '7B6EC47C354B7FDE86940F6B944287B11AE32B810EFA9C35DDDB61AAB60D36DC'), (False, True, False): ('104876496A7966446981E647F43B245387DA2C741ACB223605ACC23BC7B0B8B5', '69D99AAE2E5650BBC2AD00965E684973BDE417C0F11848B90C3AB8E5A9207657', 'C614B5F0CC7E2F9E8E8991893D5E29B83F1F9AA33CA21C92488DE22E8FDBE1E4', '410A8B1C5EF1A470835AFF3294BE2D2183371F13FF473608CF7DFC18D652EEC3', '8586DD7DD6CAAC8FC7FEF8A27C34103D6C7BBD5DDE60A03FB242DEA3A77B33B3'), (False, True, True): ('2441758D1FD45E165C518F4A45053CFE29836DCF7DF7D61D074725849C9FF30D', '1D9F9E9A910EA722A774417EBA49FC2B326ABFF00E29A521E4067B9255B3C5C8', '4F83FED038ED5C3D165F5535EBD92E0D86BA5D5F7E41620B92CBDBC6CA48F297', 'C1A8A71391889B3B960AC5159EF53B1C6F160C6DD10DFA5C1588E92739CB9AE0', '98AABEA6CBAA96FDDEF4AEAC19905D23871FEEA837F6F6F514545BC02F0D3B4B'), (True, False, False): ('DE6472CD822F9E6B361E771B0681815A9B11DEB0229C2282F4D2A0ACC1B24997', '948CF6BEF54274B3526279C17CB6FC111BEA0434D4C525F9722D178CA16A58C5', '933249A0F0D2D10B9231717F9C388985937DE989F0A97A5286C59D1FDE67CF64', 'A50D56E507490D92A460860BC68A35C3CA67B22404761C036610D439EDFC3D71', '8F9B12A2C64A416B86D16E7912AF62EDC6D99FEB62D36247F95E3BFF47B0CCFC'), (True, False, True): ('D0B26AE67BF4D05EA0CEB554F7A1F5F3C749C6B9EC0213AF94CF452D238F6A76', '00683725D5DBEE6BAC3A6AE5E7574F4CAEAB45CE05088E614C254C2BD24BA194', '71AF05030706F13C30A7DCCBE000129D8583751729F11649F023768732E923C5', 'F9B7110EFE534E0BE8A63C45F2CB81E0F4BED288B7106DAF107E56CD305A768D', '0B5D3111A0C3E0C65E9498A09726A07AD75A6325A2DE2FF6FD9309BDA9732B6E'), (True, True, False): ('F6C6A4B4FBCCCF82AB0167AFBE3CEE7BCD8F522CDD793FF7F5AE97C820D8993A', '476243B109CD0DCB35DC117C84A6FCD61BA2496EA50B3003DF4350E11D149F98', '02743E9B8D7253CC1476233D77AC21E28C6CE0C7B36428DA3DFD902FC3F5B81C', '7862D8E3D432FBF5D261CFC16283E1D27F1AC7E5C0335C899BC1682790DD3B28', '29C6A0E549F35F51208C94F54FB85CC1FEB7BD5B8B52E7AFBCBAB8E3EDDEC975'), (True, True, True): ('23CB15C9F1AEEDACE2FC9C3B6862B1D5D2605CECE3895FF10841B3FC1E42E741', 'B4C16153A165112C960FBFFFB69438670B9CC860102A8C62A448E478C31EAAAE', '4456349416BD7113164C3D0CE4A008F0F807E79CC58C8DDB7E5A79BA1804E9E8', 'EBF631ED9AF7E131A1AD249329059CC51EBAA98943B494A121ED21CD9F801650', '37E8ACD8E4CDE7BB01FE84B32212141E67196BB41887D9A82CB974421C7E0EAC')}

RESULT = ('event 1 track 1003 laps 2 outcome finished steps 48332 resets 0', 'lap 1 ticks 1615960 time 6:43.99', 'lap 2 ticks 1611360 time 6:42.84', 'total ticks 3227320 time 13:26.83', 'state-sha256 9FD5C24B88B4E379F3A08D581F92A91CA7B541A6794A2DC7A1DCA61ECB3E4AFD', 'stream loads 78 releases 0 bytes-read 36471524')
STATE_FILE = 'AE4066872227C97EFE489BE1D81FB534926018D37AD0F8ADD40ADC98E1317AEE'
INPUT = 'B18E98154A6F4F97ABA53239F7F16D7E65538E61F1B42EAF2FE340481C98E2E3'
TRAJECTORY = '249FD5ACEE15F084D2FC926B00056559A9CED602D6F0E5549BEE8BA08EDE3A06'

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

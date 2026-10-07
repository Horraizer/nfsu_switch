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
FRAMES = ('A773FC78816A5D2463ED1CDEB5AC0551D1A61C1FA55A7622F5EEF629FA8DBE0D', '9DCFA74DF4B1B599F8D3DE506998B30F3F535CBF2F87166D3605FA5C95CBE555', 'DDEA0EE6E77341B24C9BF084FBFE2489A4FA3700147261B8DCC96C4E5CE5941B', '8A89C1F0411388B46BDF5BEEB0BF4B9D384A964D5FB298AFE3210FAD6AB56875', 'B5CAF9E5E796A3A47D327D49088F22761345AE659B381D5A33268C94D42BA0E3', 'DB71831D683131FBBCFEAF78A35DC8126E2B0D72F7A774832AE5A9244E39FB2B', 'F9C1179E4FAE413536B505E56670241CB803CDF202013EB397E24C59FD9C46C9', '04649091130C2CFD206F672A5F701DEF83DC956BDE736EB653C93202308B3B58', 'C6E16E978C373FC6D7D78BCC3D7B583668486E8EF5D8C7804E02AE2E739A5A7E', 'B1D0F5F0C809288CEF4324B53970F43F42C72DE85A7BF745CE134061F6B301E2', '16E24488FEEAD8BEF70556D31AE8678B271CBC680E4D3BA567D27189691B941C', '049D137F41A22C0E0200290B21E02FA668F58A482C7F3CFBC5D4CB6773ACF25A', '51176C00C23EC8F45C8AF8AD821F0A403F1FB6ACEB7696EF1EE934AAACF3A3E6', 'A0FD435D7C3D1856CC62403A2651F6355841E8BB833E22207E80410ED94E0B76', '138BB541BCE865D319A95621018DD980D320F25F5F0A1A7F1CAC1AC51F98BD51', '647E4F117B1B3F9704D0E4D360D55637CC59AA55C9D9B008B3C92CB51BEEC3C7')
# Tuned, glow and shading presets use the same HUD overlay.
TUNED_STEPS = (180, 600, 6000, 13122, 48343)
TUNED_FRAMES = ('AAD788E77C6C69F07557832A38AE7C9BF8F6281ECCC3E30C4F0BDE121B8BC60B', 'B1528AAF4EAABF0C34B178030114CB60B978279C884EE045EF4A35E7079E9991', 'B8D1601BBBD2737026511EC69CFCF445CFF35D2E8EA19AF46AC30A321B1117F5', '2772A49D34EAAAC01AD08C594CAAAA857FEF617C31BAA42690AB15D5870822D8', '9C97632EDD05A1DAC780C75AC7498F779DF9057CAF27EB53EAD6C266B702DE53')
GLOW_FRAMES = ('6DBC44C73CEDD559C955199C95D492E2C7C111DB81A9BB0E20E72BFB3BBC505A', '954325E7308F0B39CCA9111644D6A5EE39B336F945F41D4C27571EF63D4DA1F4', '453CB515B76475F3592AB3642FA3153E16A839D198B0A36B5BCACC480ABB4D66', 'DDDD2B0A1BE14F38F5A2F430DFCB42D128CE1AF61987F2C48A03058C2C155638', '1C85866E8C23739FBEBC84CCF78A3C2D907722165F21B6465324579A0FB0A403', '4D314A47D015B655B877FFD018F64613533B6E3A233C2AC770329597E31EE95A', '64FE2EAE6DCB4DA00DCDA85718BE0F2ED08B4D1B52AEE0FC4AFA06F86C58C0E1', 'D63191A683979DC30B4D003CE31CDAFBD7878968D10027DD9CB71397CDAD44AE', 'BFA44E524008858FB67DFBF8DB8BB236DB4D9B5BF80A79378FD73292EF2656D5', '85F43966CFAA300C4D2D8E19ACF6663D18D73455123A6B23411A1D7F5117077B', '141D8AC5FB6534E8DA51A156EB4CDB2A99265B1715A2AF41EF26FD8FD8EEDB07', 'D7AA4DEAD6CC9F96DD12D0F631DD1E55EB641C0A79F1FDA062734C9DAA8AF542', '4D6530B999B847BADE346185692273F418375BA15137C5DAB11479B6C586F2FC', '2C5AC6331E81241B3B7DA4CCAD0DF987418A91947F4E022172F35690C66C1E32', '1DC2C615C9D488709E25DDE0968B2ADDB6BBF8F2DFD78F436A164000045DADDA', '1385B406C2D7511C3395A22E70C71239E3AD86EAB01109838362215A746B4129')
TUNED_GLOW_FRAMES = ('1460A652091091FE4E4D0A126E140ADACA16776661FE1AACCFE9FC2FE8B0898F', '236761DF94E36E1E71977942C8A5D485CCE4FC06C88B79D184DFCB0222E31E29', '3FF59D06B2A31BE0247BD1D5ECBC4F7DAB4204F5F723B3336B8E7B7A41B2E7DA', '285B048C4EEBC4B32EFD87C76B30D60DE6C0A1276F627A7168B0D5B341E84E56', '5D9CB00C52CE200BFEA77AA2B1E13C8D973D9F6AC89A59651D03288B795D65C4')
# Re-pinned for the installed-BASE_PAINT material correction (review F2):
# the shaded stock 240SX now paints with GLOSS_L1_COLOR08's own material
# instead of the blue swatch surrogate.
SHADED_FRAME = '7CEA3A3308F538EAE62F4E605A9CFAB55332355B417C4AD0E342DD94036B77B0'
SHADED_GLOW_FRAME = 'F4B8E3804D393554F69150466CBB06B5F5E4583DFDC8AA8944DC0CC07241C625'
SHADED_TUNED_FRAME = '637E6B597F347FAC5106D27A34EF776A8DF6FBF4AF91FEBE2731EFAD146C0624'
SHADED_TUNED_GLOW_FRAME = '4916983646A715900ED171CEEFCDE19A88A850A9D8DC32B031F838C262698440'

# Wet road pins retain all seven optional effect combinations.
WET_FRAMES = {180: 'F54BB16D93375C73A007ED3AC312A738C598C310124355F868F8CB4A9E57B000', 6000: '2D4E7740872B044CA6646B758A239578957A7CAE41420503959249DAFC4C6B7F', 11344: '84631870713EE2FE6AC4F14F348F9D2A68857CE10A638C0A3843FF1AB6E91AB4', 11360: '7241198C4A677EB64B19D89A83DEAFEFC0032CD1ACAF36578BE1E52404B4DF2C', 48343: 'DF92CF6BE86232CE203CE7C90006DFC719782B4D261EEE581D6C905B45F18023'}
WET_EFFECT_FRAMES = {
    # (False, False, True) re-pinned for the installed-BASE_PAINT material
    # correction (review F2): the shaded wet frames paint with the installed
    # GLOSS_L1_COLOR08 material instead of the swatch surrogate.
(False, False, True): ('388EBAC89C69962C1A33C42221E9187C90D86A05BC2B42DE24269EE4D793C852', '39A6570E4CF971DE120887F41AC4707E03AE609EBE62F14AA62AB0F6F83FBF40', '7785A5760996736A2A361AFF4B36F009794D44500B0BF839EFF7E26AF0CF9963', '5A2DF8DA2690F73753E3E4CA50D4A0F7F85C72DD59A04EA937AFFCFBDFE51D87', '99C0F1E5DF7EA3E6B5AFCB23BC283E459C03E35A9DB36A326771D2767A1F903D'), (False, True, False): ('A5F94EB890C3E826B8FC4A354E7B7C6878A594A5A57D44A77CECF7F1CFCE2EC0', 'B075E477813CD45CD5F52045C6236EBAB3C6A7B750F5D0ABA72A71C2631B5E5D', '70134873ADE5AF88197670B2D4A133BEC362AD543271193EE93D1BB4423D36ED', '37362FB354DF8BB04FDAE799684F062BFF00D2FC4E7F895CBBD33D762AB33C1B', 'D3BA0DC0BCAF2DBCBD8B120AC58C8AB254EA5C8217AB6046C63B78A9789F28B0'), (False, True, True): ('C65BB217702B6AF28C757F1E38F693C2538DD77CEAECC599121C6EE78967F1F6', 'FA5D88BDB374D62B265FD37EFE942D194EB3FD2840B00278C9C1A62504B7CCCC', '106616FC95DC0F897B3A8D3AB53A66166D22A08FEF39976957B5B922F4E5E4EC', '0775FBFA8166150B48AA0B551B6F07A6AC08B7FBC117D6BCCB57259C2B962451', 'F90E8DA4FD0971D4B83218E9EAFB752F17DA8CD8C0C5FF8CFC92B30A501A049C'), (True, False, False): ('50EBA2CF9BEFF0FD1BC59D4D6FF620A43A42AD7F01690AA80948ECDF80C9B0C4', '28F29E8127DCA8147470A674B75B30B526942D1B80C410B30A1A6375CC4A44DD', 'F385599E96C6FFB65276614CE5D5F62D1D4D1226EE908FB34C8FDA7F8AAE7CAB', 'FA5CFC5E8269092FD3A80C57094CFD730D75B5F0FE630385E36D2031D6D9E5D1', 'B3CA438F30E50F4FE66B21CD7418BD4E845BE9B0C72D397425C8037BE0BF87BC'), (True, False, True): ('C7EC908BC26C99BF57989815ABBD6E9F7EF5613C6A6E9D38B68F50A63B33846E', 'C7BE021592258C62F13E92667F5AF20CC252EEB2B4151EE9CF51F9ADF6287264', '3E5384F71A8791744B91CD5CA3833DB3D6CA7803B2918FE92B159D256E048FB7', 'E32A90A7F84B1D369E47E9F8867D115BF3A509ADCFE2ECBDACE003DD5EA44271', '5078A7423423F75AEBA430EA30543B0ECC0CB2801240EC664E18A62B5547DA03'), (True, True, False): ('59D4AC60F2703902BCE0CAF02F133B1BA7FD755F40C8BB687AC2A751C0A3F836', '7D2E4CD3AD0C41D57EC2E04C14069EB312234A3E212A92012904FCB3D7A25D62', '64A30C044EC14055A31A523D194AF20C09EB80663BB844EB7670FEF7D8478DA8', 'D08AA2B3DD9F53D44FF01346C7D8FBEFB0C7AEC8D3A66F2F200CB02C48E5E983', '3A1550A875ED8B55FE7C8566CB4056933F3C59C17A77C5AB1AC75F66FEAD4303'), (True, True, True): ('839896848F32F8753C996B8540AA594DD46045CD53FFB419FD466DB12483272C', '8FACA8262AC7E5410994C145E5E45985C724C0E1DC35643299D950A11145193C', '1B80D5A07BA91CEB338D76ADDDB55E78B44F8A86244F9DBD5F740010E4AC6AAE', '1B83EE32B44359411601E7F7FE3D9729ECC3F0F3FED502E6EC6C0D60BE94C56B', 'D43781BE982466F20E4B10353030C4C3E14B622DE6989A4E6CF00B6A94EA8F4A')}

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

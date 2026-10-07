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
# Original ordinary-world RGB: texture * D3DCOLOR(BGRA to RGB) * 2.
# Fresh control frames retain prior pins; simulation and alpha remain unchanged.
FRAMES = ('1B57B14F2871C333A66FF4538AB3864E0AD5CD8691EEC4AD444EB4DF8E6602C9', 'D6C1E813724C79B08B660B730A96C3AF5B52A028EBB9B01E8DDFC29F39A62A6A', 'B7758F60EF0FE8F7864C84AA2AB1D5ECD8602B3E61886D123AFCCB5D0E9494B2', 'FB1A1D7CCB56B0EBAF51A4A0E33BFB6CAB16B7121A26AE56A84F500ED76DACBF', '03145ED16D49D329FEEDF535765C14FDC7E253D397F16449947071678F86B080', '8A878C49557E83AE764B3BEF67980D88936D94A5005DB5F726B582A1E80C3C77', 'AAA3A3941C559B61A2C0EE3E1AB9DBF35E2D230A6D4B3CA20EC743E077F446D9', '7C4B9705B85092C5A6B9C2866C186A9ACE3AC32E3364F258527F876A4F942BE7', '024268B4BBE7E7E98121617D4B509515CA3FEDF30B6A642EEEAA6A9BD322E95A', '3230AD1EC21DE80CC99DCF62970E2AE6B59015C8C0CC7F8BE75448729B45B1DF', '88907D6F933899AE586E1C2DD97E2470BD96BC2D870854F192957FFDDAC92FCB', 'C448E3DBA9727D493246AC3974D21CBE6F7CA2590D9556FAC0A8D6EA2343AFE1', 'CB0837DBD2835381E69633B0B2F48958E643F400A06F362C6D92266D216EABD1', '906C24D756CA35A39A39CA421845D5A54E5C53525C1A4D01327D66DF77A9B575', 'E94C01A7539F03376380F2875C707795FE625AC8CB0285FB5821CE6312141E0D', '4B0EDE7EE9CE9E37768F87DDFA37FD4AF545591FDCF02323F6729BAAA4A3A977')
# Tuned, glow and shading presets use the same HUD overlay.
TUNED_STEPS = (180, 600, 6000, 13122, 48332)
TUNED_FRAMES = ('EF75690AEAB433B8EEB4A76039F6CFFBD5E5CFA2399FD0D82B918DB9E596D16E', '16F7832516867288C24A9F1119B239A9A52E45B89062659A13B4BCC19ACA9888', '1DC356B49CD6E8C2BF609A4E529660CD1648E51C66CBE966A231E465905EA57C', 'D5A5C241AE8CB174E3B5570BA511799C886A4126D50975A3147AC3EE9B71A4F6', '7AD85D2F09BA02F6E7764F215E3009C8E599AE97109EACBF57D970B203A68987')
GLOW_FRAMES = ('7E97325B99141DBA15034EECB0BD9475F809529866DD74CC5F13C5BA71151768', 'F046BE627C04F4E43E8A7777CA359C544F36F71F1B8A3DF549A593943C0C4FE4', 'EA83F98D54E1AD39145EC193B6FA4498E2145E33EA867A546F50FD31428C4BAE', '476A10E5DD658DAF3F6E44183F504D821CBCDC09D26B9473054C0002E6F27898', 'CF80239345D0BC158BBF03A9C4E86B7746324FDBF82DD38F31F0BD11C779E2F5', 'B5F5655C723A478C212CCA6B19232743E94B5AEA7FC56F731793D386A865AF0E', '8942F75F395B140101912AA6226A417EC633825F2F88248B6D0B0437B02DA4CA', '937BF5A42F17F29C15808A827E73BC97E17C625FC1DB939471974D6B4B69A06B', 'FCA2813283D7E8CA4F929301AD7179CC76399AD2BD913993501C3AF298778573', 'FC4D24465F2021152B52F6A95ACD9ECE3CF07E6E22B23251D9141ABA1F60F224', 'DBB6A5E7D508203A86EBC5D35AD399F0DCADC687834B5B615D17905E7FCDAAB1', '9060536CA227F7B8221888259004FA2434C6A111CEEEB5E2A1B3CA494BB9380C', '3EB689753CD4DA02C7B0F58C08616618B7D061E6DB751DB7B9ECF116D6D342D3', '9AF4B5C3B6667D09202BA0A338C8531475561F00D36122C351F8285F29D61A5C', '5071465BA44FD1A3CC50EC444BEA8D83AE8628A5E00BABD942537E8F34C6C19D', 'EDEDA3353C09CFC0E05814E0DD50A5F00EF5F41D5AA21A535DCCABACADAE898C')
TUNED_GLOW_FRAMES = ('864A82C7C5B17A164F9577E4076D7AD4320D914703CBC7504B650AB9E03AFF42', 'DFBA4F7A5F859AEA1125C7E0BC1D41BC2AD582155AB8BD9B0221E0DD1367DF2E', 'AAF67A74737D5AA37A4850A5710B705F29BE9451A8DC44E23BA91DA90A84B9B6', '7A4A18D3F4B8EB25DFBA45619389D0CEB71C34FF36898FBBB562306A98FEAA31', '2C4B804CF756D2CD667C79C1C0CE3A3EF2EEB02168241C6534111362E3DEFFF3')
# Re-pinned for the installed-BASE_PAINT material correction (review F2):
# the shaded stock 240SX now paints with GLOSS_L1_COLOR08's own material
# instead of the blue swatch surrogate.
SHADED_FRAME = 'B77AFDC80FF6897718B9F1AA3CA6AA91C9FDFF0245FDE10D9A58A0565A7B07FB'
SHADED_GLOW_FRAME = 'E3A4074454B4BFB05C316D17757819ADD16F3D3B66A917A16906B0A4D6AA9E03'
SHADED_TUNED_FRAME = 'DD3A1AF36F0A5BC694E02C46B7D058B83A83BB4EC5765631C17FCE8103E350FF'
SHADED_TUNED_GLOW_FRAME = 'E722CFEB1E6CFDC7496F4DBD7C482D8FA647255807EB83730682D7F3B8AC8281'

# Wet road pins retain all seven optional effect combinations.
WET_FRAMES = {180: '895729FD43ACB7B2CEEE7CA9A99BC157899B30A45C4F71BC0C952ADAE417EE62', 6000: '6898288F4FDE2D86D73DE67298A6B14A8DF100FB515582BE1CC6A4943EFD430B', 11344: '9D816A912F40783032063893D5CFE3F77469216D78C7EA5A1DABDD495D9B8697', 11360: 'F78028D9A0C5370B6BCB5A1C20CB63FADE841901C8B82E8256CFADBCF90B6AB9', 48332: '04C7CE5663A5CA3BDFB0CAA863A3FB2E7BFAED0922E2363F0FF37CAC3A67B447'}
WET_EFFECT_FRAMES = {(False, False, True): ('75A0FEA1AF7EC0D67EDAAC9E6ED9CE0687B54EDF08C31EB7B271574C93A92CD4', 'EFB77A76D0AABB832E50FCCFC1D16AE04D74C47A23089483D59759A7F3EAF7D1', 'BD2F2364312868F26F701B0DBAF01548039340047CEB5D328288EA23C9B35810', '30E09E74865662F521337AFF645D8B5D37C13733B3B6961CABD2947DAD44AAAF', '38611DC092D7A1E2CEDC6EC8A090291EDBE1CEB75202FD5D6E88EC6909D1972F'), (False, True, False): ('58C1DE44644DBA795CF403AD1B1365CF48BB7602BA3D0EDEA0937086EE398171', '553556786C427D1B4934E40713F19616E199A22B94109C762A99763C2B3B18CD', '585153CDB9877198CCFC9D75D8C8B30FDE5B3F2AAD27D0A6327CF6A37042C43C', '25A9CECF3CFB5022852ED980AEB1FBF98F86691B5A7676084131CBB42F2AFA3C', '3FDF75CE0792964A3706054140FA630A355C2E9215991BB0C6C188E4C98BE7FD'), (False, True, True): ('592478E84A6E4DE80FF83A079A54482C8F0B979FD78A0457A9141284565B7853', '747659C5B5532EEE7A4DBF804A27947BA20A7E4A8D49DD60F0C4422213887394', 'C188C9D7DBC0191C76B51F37EA23494CFB0AA9AC4C54AB2993902271FB0D3718', '1AD40A5508AA7110396454B0E8C7FE533C75AA637B65D7F48BA13681DA2B261E', 'BFEB81DEBE09005C388415539D7F809B659A9C41E442844419656044774EA379'), (True, False, False): ('5B2376BB95A58C854E68D9F209EAB47DDF0D47FB64117ED5F0B32731116A5474', 'CC6BD5B47A97FFC3C8B10D74AABCDD2A27D47182A0DAD945311A073568709D7D', '638C22A490A0E5A6CA813AA463200EC36B16F09DE8E989517F5B73A340687B2B', '115F9BEEF7AF4889EF7253A0C785D2CF456A70ED343B2770F53879D89798CD47', '1F7D89FDBD138374346CEF32740D26B209D47DAAFD65A3D2C5D6C84C5F7E36F9'), (True, False, True): ('C8F6EB1DA913A6D57759F8DC2A76A0EFDF2AA964EA888FA96C1A3DCC6C81F2D6', 'EBCE89065A977D8BD3A5A3D3DD7D3CA046E592F8587B529C21C58428EB8B0AF5', '3A339E448C530428B8108591C51A60A272ABE262FD67C4AB55B1E5C9CAFA25EA', '02026FE02F00F3E6452A25FEFF026D69101F918AD491EB1A57F75A10227F9A4C', '9AB35E36D95172CFE542B8F1FBE193545E4E2049651B88DF8443EFDF3605A638'), (True, True, False): ('31E9CCFCD648E5E1B3489E6A8292E5D70677258E1FBD0D3A6B4362D31696F3C8', '15E5B2BA292E803D6AFB4B12C7F44E5BF7C578BE550B6D2B659130C19B165839', '988ADF7081ECC2F616DA4F56D6D7B90FB27DC4976414F61292DCA0860C998A24', '3A6A9EA7F4FA9949797BD071C1551BB94F8F317C43B004BFB0BFA1AC7C81BE7A', '585D59C3FD994BCA965DF5DA543F934BC91AC8B6CCA5954FFE4679B01838A8DD'), (True, True, True): ('97B4683CCB73476BCFC80E7C55849ABA54429792A95F6106808E1D093CF4FDD1', '816351642D0B47FC8F832D4F847C13457E123E97E1C6682AAD17614005611B45', '46F68D7CD3E51933ED59D153C74FCE94805E3B3BD7D49FF5527002943D641B64', 'D15AC027E2250129E2335E452BA92FBD11712CF40483FF1C05D3B942491F2428', '103F48E9B8654D9A34345B254B69A83798FFEF46728164FE72005A1A4C7DCE98')}

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

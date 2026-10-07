"""Issue 312: bounded original audio witnesses, Python 3.8+.

Needs Capstone 5.0.7 and Unicorn 2.1.4. Run in the pinned checkout with a
fresh ignored output directory. Original files are read-only; no PCM export.
Port comparisons are source-pinned analytical models, not executed C++.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from hud254_trace import PE, chunks, manifest
from audio312_native import (BankGraph, Machine, collision_intensities, contact_age,
                             engine_parameters, scrape_fade, spatial_gain,
                             random_sequence, stock_bank, u32)

INPUTS = {
    'speed.exe': 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7',
    'Global/GLOBALB.BUN': '39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9',
    'Sound/CAR_0/ENG_UG_0/C0U0_M0_ENG_MB.abk': '3a955c62f01ef3d72344dea2f4084b2b5f21f203c7fba520de9edbffcdd9cd98',
    'Sound/CAR_2/ENG_UG_0/C2U0_M0_ENG_MB.abk': '4906be623db05bf20be1e8b5a7faff2da3828f79c68b6404d00a105e13419cff',
    'Sound/CAR_2/ENG_UG_0/C2U0_M0_EXH_MB.abk': '968035920bb2697ee2f959312e8a34c988ee6d8f5216dcc20a6ad455f50f5523',
    'Sound/CAR_2/ENG_UG_0/SHIFTING_SML_MB.abk': 'aff6e1366b1c29ba567e5d7d63cabdf14faf6bfed9c06a77b3bfb3d8c24a1a02',
    'Sound/IG_GLOBAL/IMPACT_MB.abk': 'c1ee8237c292e2934e296caf10e7a78b25bdae8326c517d1aa034379362cc6d5',
    'Sound/IG_GLOBAL/SCRAPE_MB.abk': 'f8064c70c59bb0b6e61596de7f1a6856ea4ac48ace3a48d50849c2b4e9fecb50',
}
# Integration328 changes RaceAudioService::start only to select the original
# IG playlist and extend its call signature. RPM/load and collision analytical
# consumers below are unchanged; retain their bounded evidence classification.
SOURCES = {
    'src/race_audio.cpp': '2e09629b1e86ebd1db8ae417cf9e5e51559a7416c276c654e42d261e93771a32',
    'src/collision_audio.cpp': 'c5a855ff6edff52d9d3249314f790a886d97380f0a8905566baae21968cf5d5d',
    'src/audio.cpp': '3e70ffbeceb77c7023d95c008bb2f0de1d89623c76df464a5a0115eb81791f26',
    'src/audio_playback.cpp': '5d2fd0ee1ee65aea3d7569296ee3391246123ca19fc469c9f751f9d5de31b71b',
    'src/vehicle_track.cpp': '451cac169c622fd815856386b1f326c7f23285fe53222eba35c2329b73e02d44',
}
RANGES = (
    ('car_sound_index', 0x520320, 0x5203D3),
    ('car_descriptor_and_event_records', 0x51E030, 0x51E17C),
    ('event_record_consumer', 0x51D2F0, 0x51D351),
    ('manager_constructor', 0x521F80, 0x522011),
    ('manager_upgrade', 0x522020, 0x522049),
    ('bank_lookup', 0x521C80, 0x521E0E),
    ('engine_parameter_producer', 0x5236D0, 0x523888),
    ('load_transition_fragment', 0x523BFE, 0x523D6A),
    ('shift_rpm_fragment', 0x523EC0, 0x524003),
    ('gear_event_fragment', 0x526AF0, 0x526B4B),
    ('shift_consumer', 0x529100, 0x5292AD),
    ('shift_event_parameters', 0x5257F0, 0x5258C3),
    ('pair_allocation_fragment', 0x467220, 0x4672A0),
    ('pair_reuse_fragment', 0x467300, 0x46730D),
    ('physics_expiry_fragment', 0x420AB0, 0x420AD7),
    ('world_curves_initializer', 0x530340, 0x530570),
    ('spatial_gain', 0x530740, 0x5307E0),
    ('ftol', 0x674898, 0x67490D),
    ('shared_random', 0x6511B0, 0x651255),
)


def sha(blob):
    return hashlib.sha256(blob).hexdigest()


def checked(blob, expected, label):
    if sha(blob) != expected:
        raise ValueError('unsupported SHA256: '+label)
    return blob


def output_path(workspace, root, requested):
    """Only a fresh ASCII-named child of a checkout ignored build directory."""
    workspace, root = workspace.resolve(), root.resolve()
    out = (workspace/requested).resolve()
    if out == root or root in out.parents or out in root.parents:
        raise ValueError('output overlaps retail tree')
    if workspace not in out.parents:
        raise ValueError('output must be inside checkout')
    parts = out.relative_to(workspace).parts
    if not (parts[0].startswith('build') or parts[0] == 'out' or
            parts[0].startswith('cmake-build-')):
        raise ValueError('output must be in an ignored build/out directory')
    if any(any(ord(c) < 32 or ord(c) > 126 for c in part) for part in parts):
        raise ValueError('output path components must be ASCII')
    if out.exists():
        raise ValueError('output already exists; choose a fresh directory')
    return out


def stock_record(bundle):
    matches = []
    for kind, begin, end in chunks(bundle):
        if kind == 0x34600:
            start = (begin+15)//16*16
            if end-start != 35*0xC90:
                raise ValueError('unexpected CarTypeInfo table size')
            for index in range(35):
                off = start+index*0xC90
                if bundle[off:off+32].split(b'\0')[0] == b'240SX':
                    matches.append(dict(car_type=index, offset=off,
                                        chunk_payload=begin, record_size=0xC90))
    if len(matches) != 1 or matches[0]['car_type'] != 18:
        raise ValueError('unexpected 240SX record identity')
    return matches[0]


def bank_metadata(bank):
    """Inspect PT headers and graph tables only. Never emit sample bytes."""
    bnk = u32(bank, 0x20)
    if bank[bnk:bnk+5] != b'BNKl\x05':
        raise ValueError('unexpected bank header')
    samples = []
    for index in range(struct.unpack_from('<H', bank, bnk+6)[0]):
        entry = bnk+0x14+4*index
        if not u32(bank, entry):
            continue
        head = entry+u32(bank, entry)
        if bank[head:head+2] != b'PT':
            raise ValueError('expected PT')
        pos, tags = head+4, {}
        while pos < head+512:
            tag, pos = bank[pos], pos+1
            if tag in (0xFE, 0xFF):
                break
            if tag in (0xFC, 0xFD):
                continue
            length, pos = bank[pos], pos+1
            if length == 255:
                length, pos = int.from_bytes(bank[pos:pos+4], 'big'), pos+4
            if pos+length > min(len(bank), head+512):
                raise ValueError('PT patch outside bound')
            if length <= 4:
                tags[tag] = int.from_bytes(bank[pos:pos+length], 'big')
            pos += length
        else:
            raise ValueError('PT lacks terminator')
        samples.append(dict(index=index, header_offset=head,
            tags={hex(k): v for k, v in tags.items()},
            port_interpretation=dict(codec=tags.get(0xA0, 10),
                rate=tags.get(0x84, 22050), channels=tags.get(0x82, 1),
                frames=tags.get(0x85, 0), loop_start=tags.get(0x86, 0),
                loop_end_exclusive=tags[0x87]+1 if 0x87 in tags else 0)))
    exports = []
    table = u32(bank, 0x38)
    for k in range(u32(bank, table)):
        at = table+4+12*k
        name = u32(bank, at+4)
        exports.append(dict(module_field=hex(u32(bank, at)),
            event_id=hex(u32(bank, name)),
            name=bank[name+4:bank.index(0, name+4)].decode('ascii')))
    return dict(samples=samples, exports=exports)


def port_engine(rpm, throttle):
    """Analytical reading of pinned race_audio.cpp, NOT a C++ execution."""
    centers = (800, 2500, 4500, 7000)
    rpm = min(max(rpm, 800), 8000)
    weights = [0.0]*4
    if rpm <= centers[0]:
        weights[0] = 1
    elif rpm >= centers[-1]:
        weights[-1] = 1
    else:
        for k in range(3):
            if centers[k] <= rpm < centers[k+1]:
                weights[k+1] = (rpm-centers[k])/(centers[k+1]-centers[k])
                weights[k] = 1-weights[k+1]
                break
    return [dict(sample=k+1, gain_q15=int(w*(.22+.33*throttle)*32768),
                 pitch_q16=int(min(max(rpm/centers[k], .5), 2)*65536))
            for k, w in enumerate(weights)]


def disassembly(image, banks):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    from collision_audio_trace import RANGES as collision_ranges
    decoder, lines = Cs(CS_ARCH_X86, CS_MODE_32), []
    for label, start, end in (*RANGES, *collision_ranges):
        blob = image.read(start, end-start)
        ins = list(decoder.disasm(blob, start))
        if sum(i.size for i in ins) != len(blob):
            raise ValueError('invalid instruction boundary: '+label)
        lines.append('\n%s %08X..%08X SHA256 %s' % (label, start, end, sha(blob)))
        lines.extend('%08X %-8s %s' % (i.address, i.mnemonic, i.op_str) for i in ins)
    for name, bank in banks.items():
        module = u32(bank, 0x1C)
        for index in range(struct.unpack_from('<H', bank, 10)[0]):
            start, template = u32(bank, module+40), u32(bank, module+44)
            lines.append('\n%s module %X init %X template %X' % (name, module, start, template))
            ended = False
            for i in decoder.disasm(bank[start:template], start):
                suffix = (' ; callback ordinal=%d' % u32(bank, i.address+1)
                          if i.mnemonic == 'call' else '')
                lines.append('%08X %-8s %s%s' % (i.address, i.mnemonic, i.op_str, suffix))
                if i.mnemonic == 'ret':
                    ended = True
                    break
            if not ended:
                raise ValueError('bank initializer lacks RET')
            module += 60+4*(bank[module+36]+bank[module+39])
    return '\n'.join(lines)+'\n'


def witnesses(image, blobs):
    selection = [stock_bank(image, event_type=k) for k in (12, 13)]
    require(selection[0]['bank'] == 'C2U0_M0_ENG_MB' and
            selection[1]['bank'] == 'C2U0_M0_EXH_MB', 'stock bank mapping')
    for row in selection:
        require(row['event_record_type_upgrade_group'] == [row['event_type'], 0, 2],
                'event producer field order')
    engines = {}
    for short in ('C0U0_M0_ENG_MB', 'C2U0_M0_ENG_MB', 'C2U0_M0_EXH_MB'):
        bank = next(b for name, b in blobs.items() if name.endswith(short+'.abk'))
        group = 0 if short.startswith('C0') else 2
        cases = []
        for rpm in (800, 2500, 4000, 7000):
            for load in (0, 50, 100):
                graph = BankGraph(image, bank)
                params = engine_parameters(image, rpm, load, group=group)
                history = []
                for tick in range(120):
                    players = graph.tick(params)
                    if tick in (0, 1, 2, 5, 15, 59, 119):
                        history.append(dict(tick=tick, players=players))
                require(history[-1]['players'] == history[-2]['players'], 'steady graph')
                cases.append(dict(rpm=rpm, audio_load=load, parameters=params,
                    native_history=history, port_model_at_throttle_load_div_100=
                    port_engine(rpm, load/100)))
        graph = BankGraph(image, bank)
        transitions = []
        for load in (0, 100, 0):
            params = engine_parameters(image, 2500, load, group=group)
            for tick in range(120):
                players = graph.tick(params)
                if tick in (0, 1, 2, 5, 15, 59, 119):
                    transitions.append(dict(load=load, tick=tick, players=players))
        engines[short] = dict(cases=cases, fixed_rpm_load_transitions=transitions)
    mid = engines['C2U0_M0_ENG_MB']['cases'][4]
    require([p['gain_q15'] for p in mid['native_history'][-1]['players']] ==
            [8553, 4203, 9830, 7216], 'native engine load-layer witness')
    require([p['pitch_q12'] for p in mid['native_history'][-1]['players']] ==
            [3095, 1547, 3095, 1547], 'native paired pitch witness')
    require([p['gain_q15'] > 0 for p in mid['port_model_at_throttle_load_div_100']] ==
            [False, True, False, False], 'source model adjacent RPM layer')
    ages = [contact_age(image, age) for age in (0, 33, 66, 67, 400, 1199, 1200, 1201)]
    for row in ages:
        require(row['physics_expired'] == row['audio_expired'] ==
                (row['native_clock_units'] >= 1200), 'expiry boundary')
    fade = scrape_fade(image)
    require(abs(fade['duration_seconds']-.133) < 1e-7 and
            fade['from_value'] == 1 and fade['to_value'] == 0 and fade['expired'],
            'scrape retirement curve')
    intensities = []
    for subtype, multiplier in enumerate((12, 1, 1, 1, 17, 13, 1)):
        for value in (.1, .5, 1, 2, 3, 10, 20, 39, 100):
            row = collision_intensities(image, value, subtype, value)
            row['port_model_impact_index'] = round(min(value/(3*multiplier), 1)*127)
            row['port_model_scrape_index'] = round(min(value/40, 1)*127)
            intensities.append(row)
    rounding = next(v for v in intensities if v['subtype'] == 5 and v['impulse'] == .5)
    require(rounding['native_impact_index'] == rounding['native_scrape_index'] == 1 and
            rounding['port_model_impact_index'] == rounding['port_model_scrape_index'] == 2,
            'ftol differs from nearest-even')
    spatial = spatial_gain(image, 22000, 16384, 16384)
    require(spatial['native_gain'] == 5500, 'spatial/category gain composition')
    return dict(stock_record=stock_record(blobs['Global/GLOBALB.BUN']),
        selection=selection, engines=engines,
        collisions=dict(ages=ages, scrape_fade=fade, intensities=intensities,
            original_rng_synthetic_state=random_sequence(image),
            spatial=spatial, port_model_same_pair_steps=[1, 3],
            port_model_second_contact_starts_new_impact=(3 > 1+1)))


def require(condition, label):
    # Explicit checks survive python -O; these are measurement acceptance pins.
    if not condition:
        raise ValueError('witness check failed: '+label)


def check_guards(image, blobs, workspace, root, out):
    def rejects(action, message):
        try:
            action()
        except (ValueError, RuntimeError) as error:
            if message in str(error):
                return
            raise
        raise ValueError('negative check unexpectedly passed: '+message)
    rejects(lambda: checked(b'wrong', INPUTS['speed.exe'], 'speed.exe'), 'SHA256')
    rejects(lambda: output_path(workspace, root, out), 'already exists')
    rejects(lambda: output_path(workspace, root, root/'audio312-forbidden'), 'overlaps')
    rejects(lambda: output_path(workspace, root, Path('docs/audio312-forbidden')), 'ignored')
    rejects(lambda: output_path(workspace, root, Path('build')/chr(0x100)), 'ASCII')
    rejects(lambda: Machine(image, []).run(0x520320), 'outside allowlist')
    # A stop request must fail the observation fixture, not silently let a
    # destroyed bank keep producing "steady" results on subsequent ticks.
    graph = BankGraph(image, blobs['Sound/CAR_2/ENG_UG_0/C2U0_M0_ENG_MB.abk'])
    graph.m.wi(graph.base+graph.template+0x28, 1)
    rejects(lambda: graph.tick(engine_parameters(image, 2500, 50)), 'requested deletion')
    graph = BankGraph(image, blobs['Sound/CAR_0/ENG_UG_0/C0U0_M0_ENG_MB.abk'])
    rejects(lambda: graph.tick(engine_parameters(image, 2500, 50)), 'requested deletion')
    print('PASS: wrong identity, stale/retail/tracked/non-ASCII output, escaped execution and deletion rejected')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    workspace = Path.cwd().resolve()
    root = (workspace/args.root).resolve(strict=True)
    out = output_path(workspace, root, args.out)
    for name, expected in SOURCES.items():
        checked((workspace/name).read_bytes().replace(b'\r\n', b'\n'), expected, name)
    out.mkdir(parents=True, exist_ok=False)
    before = manifest(root)
    (out/'before.json').write_bytes(before)
    try:
        blobs = {name: checked((root/name).read_bytes(), expected, name)
                 for name, expected in INPUTS.items()}
        image = PE(blobs['speed.exe'])
        check_guards(image, blobs, workspace, root, out)
        banks = {name: b for name, b in blobs.items() if name.endswith('.abk')}
        (out/'instructions.txt').write_text(disassembly(image, banks), encoding='ascii')
        report = dict(input_provenance={name: dict(bytes=len(b), sha256=sha(b))
                                       for name, b in blobs.items()},
            source_sha256_lf=SOURCES,
            native_tables=dict(callbacks=[hex(u32(image.read(0x6E9AE8+4*k, 4)))
                                          for k in range(40)],
                               sound_groups=[u32(image.read(0x6F8100+4*k, 4))
                                             for k in range(20)]),
            fixture=dict(graph_delta_ms=1000/60, event_gain_q15=32767,
                note='Synthetic audio RPM/load. Load/100 is NOT proven live throttle. '
                     'Player and inactive deletion callbacks intercepted; no mixer or PCM. '
                     'Port side is a source-pinned analytical model, NOT executed C++.'),
            metadata={name: bank_metadata(b) for name, b in banks.items()},
            witnesses=witnesses(image, blobs))
        (out/'report.json').write_text(json.dumps(report, ensure_ascii=True, indent=2)+'\n',
                                       encoding='ascii')
        print('PASS: pinned inputs/sources, stock bank chain, 36 native graph cases and load transitions')
        print('PASS: native contact expiry, fade, 63 intensity cases and spatial/category witness')
        print('PASS: bounded disassembly and bank metadata; no PCM output')
    finally:
        after = manifest(root)
        (out/'after.json').write_bytes(after)
        require(before == after, 'complete retail manifest unchanged')
        print('PASS: complete retail SHA256/size/mtime/directory manifest unchanged '+sha(after))


if __name__ == '__main__':
    main()

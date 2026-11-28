"""Original mode-specific Race Over geometry; authenticate optional Xbox packages."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from world_body_trace import Image
from hud253_trace import jdlz
from hud254_trace import feng_objects, resources
from hud254_presentation_native import Renderer
from race_over_native import sample, tracks

WORK = Path(__file__).resolve().parent.parent
PACKAGES = (
    (0, 0x16F40C, 'HUD_SingleRace.fng', '652dd66715cc8eff180a64c59377098385b3896b584a1430c014d7476dea2e63'),
    (1, 0x16D50C, 'drag_HUD_test.fng', '14f2f2df0c78b2ecd9de13522759d354314d7bce20bcde7dcb67d68bb1422f63'),
    (2, 0x17140C, 'HUD_Drift.fng', 'ce6d1e26b9608022f2a2afbb57189bd1d1de7aa2cfd0308bd565c6b7a253e827'),
    (3, 0x168F0C, '2PHudTop.fng', 'b6e88ad15574a4a06771e8deb9dcd7f44cd6fea3f2eac0c55c9d93a69e1b029c'),
    (4, 0x16B10C, '2PHudBot.fng', 'ecb890e8f709608c9195ff95119eccdc57f217489b1eb09d780dddae7acfe82e'),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--xbox-root', type=Path)
    parser.add_argument('--write-fixture', action='store_true')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    exe = (args.root/'speed.exe').read_bytes()
    assert hashlib.sha256(exe).hexdigest() == 'c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    image = Image(exe)
    bun = (args.root/'Global/INGAMEB.BUN').read_bytes()
    assert hashlib.sha256(bun).hexdigest() == '044724ab4c167831587e80f29961a1e7d8404c9dd5266533f3563c555a997299'
    language = (args.root/'Languages/English.bin').read_bytes()
    rows, identities = [], []
    xbox = (args.xbox_root/'GLOBAL/INGAMEB.BUN').read_bytes() if args.xbox_root else None
    for mode, offset, name, digest in PACKAGES:
        package = jdlz(bun[offset:])
        assert hashlib.sha256(package).hexdigest() == digest
        identity = dict(mode=mode, package=name, offset=offset, sha256=digest)
        if xbox is not None and mode in (3,4):
            xb_offset = 0x1ACE0C if mode == 3 else 0x1AF00C
            xb_package = jdlz(xbox[xb_offset:])
            assert xb_package == package, 'PC/Xbox two-human authored package differs'
            identity.update(xbox_offset=xb_offset, xbox_identical=True,
                            xbox_bun_sha256=hashlib.sha256(xbox).hexdigest())
        identities.append(identity)
        requests = next(c['records'] for c in resources(package)[0]['chunks'] if c['tag'] == b'RsRq'.hex())
        for obj in feng_objects(package, ('raceover',)):
            if obj['name'] not in ('RaceOverMessage','RaceOverBacking'):
                continue
            fields = {f['tag']:bytes.fromhex(f['raw']) for f in obj['fields']}
            index = struct.unpack_from('<H',fields['OP'],12)[0]
            drag_labels = ('Race Over','Blown Engine','Totaled!',
                           'Winner!\nYOUR TIME:   26.08  ',
                           'You Finished Second!\nYOUR TIME: 1:00.08  ',
                           'You Finished Third!\nYOUR TIME:    1.69  ',
                           'Last Place\nYOUR TIME: 1:56.70  ')
            labels = drag_labels if mode in (1,3,4) else ('Race Over','You were knocked out!') if mode == 0 else ('Race Over',)
            for label in labels:
                for tick in (0,120,180,240,260,300,340,420,460,520,560,1200):
                    state = bytearray(fields['SA'])
                    for track in tracks(package,obj):
                        value = sample(image,track,tick)
                        at = track['target']*4
                        state[at:at+len(value)] = value
                    updated = dict(fields,SA=bytes(state))
                    native_obj = dict(obj,default_resource=requests[index],fields=[dict(tag=k,raw=v.hex()) for k,v in updated.items()])
                    renderer = Renderer(image)
                    text = obj['name'] == 'RaceOverMessage'
                    if text:
                        renderer.load_font(language,'font_conduitmditctt38bi',(512,256))
                    renderer.object_fields(native_obj,label if text else '')
                    result = renderer.render_text() if text else renderer.render_image((16,16))
                    vertices = [v for q in result['local']['quads'] for v in q['positions']]
                    bounds = [min(v[0] for v in vertices), max(v[0] for v in vertices),
                              min(v[1] for v in vertices), max(v[1] for v in vertices)]
                    rows.append(dict(mode=mode, message=label, text=text, tick=tick, bounds=bounds,
                                     packed_alpha=int(result['local']['quads'][0]['colors'][0],16)>>24,
                                     state_hex=state.hex(), resource=requests[index], native=result))
    def number(x):
        text = format(x,'.9g')
        return text + ('' if '.' in text or 'e' in text else '.') + 'F'
    fixture = '// Original4FC100 and FEng consumers over authenticated mode packages.\n'
    fixture += 'static constexpr NativeRaceOverMode kNativeRaceOverModes[]={\n'
    fixture += ''.join('    {%d,%d,%d,%d,%s,'% (r['mode'],r['text'],r['tick'],r['packed_alpha'],json.dumps(r['message'],ensure_ascii=True))+
                       ','.join(number(v) for v in r['bounds'])+'},\n' for r in rows)
    fixture += '};\n'
    target = WORK/'tests/race_finish_modes_native.inc'
    if args.write_fixture:
        target.write_text(fixture,encoding='ascii')
    else:
        assert target.read_text(encoding='ascii') == fixture, 'mode native fixture stale'
    (args.out/'oracle.json').write_text(json.dumps(dict(identities=identities,rows=rows),indent=2,ensure_ascii=True)+'\n',encoding='ascii')
    print('PASS original finish mode geometry: %d rows; Xbox identical two-human packages=%s' % (len(rows),xbox is not None))


if __name__ == '__main__':
    main()

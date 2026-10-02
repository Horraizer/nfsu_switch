"""Create issue 253 font comparisons from read-only images; requires Pillow.

Fresh outputs only. Retail references keep native pixel scale after removing
their extra 80 top rows. Threshold bounds describe raster observations, not
font binding, temporal behavior or CPU/GPU equivalence.
"""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OWNER = (
    ('gear 4 / 100 MPH', 'codex-clipboard-6cc22993-f2cf-4cd3-962c-22be2a27e1de.jpg',
     '4A169C90FEA480E71B651352930A7D9A1EDCCCE47FF7F105E1B911AB80A3A241'),
    ('gear 3 / 75 MPH', 'codex-clipboard-05ea4a96-6aca-4a49-bd9b-b7d1f6e4bd81.jpg',
     '5722BE7F85132F2D46AD94FD08C7A9B81BC3C0DABB110F4FC93FC5AAC6BD628D'),
    ('gear 1 / 41 MPH', 'codex-clipboard-f8a81b01-ca01-4baa-b713-6d7dd53fd3ef.jpg',
     'C697F40AAB56CCB2376CD4A8890B45C2AC070AF8D0AC09C21AD59DE240A4CB18'),
)
BOXES = {'MPH': (1190, 634, 1245, 657), 'speed': (1105, 652, 1188, 694),
         'gear': (1208, 568, 1242, 627)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reference', 'owner', 'before', 'after', 'out'):
        parser.add_argument('--'+name, required=True, type=Path)
    for name in ('source', 'before-source'):
        parser.add_argument('--'+name, required=True)
    args = parser.parse_args()
    for value in (args.source, args.before_source):
        if len(value) != 40 or any(c not in '0123456789abcdef' for c in value):
            parser.error('source must be a full lower-case SHA')
    out = args.out.resolve()
    for path in (args.reference, args.owner, args.before, args.after):
        base = path.resolve() if path.is_dir() else path.resolve().parent
        if out == base or base in out.parents:
            parser.error('output must be outside input directories')
    out.mkdir(parents=True, exist_ok=False)
    identity = {'source': args.source, 'before_source': args.before_source,
                'owner_source': args.before_source, 'inputs': [], 'measurements': []}

    def load(path, label, reference=False, owner_hash=None):
        raw = path.read_bytes()
        sha = hashlib.sha256(raw).hexdigest().upper()
        if owner_hash and sha != owner_hash:
            raise ValueError('owner input identity differs: '+path.name)
        image = Image.open(path).convert('RGB')
        expected = (1280, 800) if reference else (1280, 720)
        if image.size != expected:
            raise ValueError('unexpected image dimensions: '+path.name)
        identity['inputs'].append(dict(label=label, name=path.name, bytes=len(raw),
                                       size=image.size, sha256=sha))
        return image.crop((0, 80, 1280, 800)) if reference else image

    refs = [load(args.reference/name, name, reference=True)
            for name in ('retail-01.webp', 'retail-02.webp')]
    before = load(args.before, 'before CPU')
    after = load(args.after, 'after CPU')
    owners = [load(args.owner/name, label, owner_hash=sha) for label, name, sha in OWNER]

    def measure(image, label, orange=False):
        record = {'label': label}
        for feature, box in BOXES.items():
            points = []
            for y in range(box[1], box[3]):
                for x in range(box[0], box[2]):
                    r, g, b = image.getpixel((x, y))
                    match = (r > 120 and g > 55 and r > g+35 and g > b+25) if (
                        orange and feature == 'gear') else (g > 105 and b > r+35 and b > g+12)
                    if match:
                        points.append((x, y))
            bounds = [min(x for x, y in points), min(y for x, y in points),
                      max(x for x, y in points)+1, max(y for x, y in points)+1] if points else None
            record[feature] = dict(region=box, bounds_exclusive=bounds, pixels=len(points))
        identity['measurements'].append(record)
        return record

    measured = [measure(refs[i], 'retail-%02d' % (i+1), orange=True) for i in range(2)]
    measured += [measure(before, 'before CPU'), measure(after, 'after CPU')]
    title_font, font = ImageFont.load_default(size=18), ImageFont.load_default(size=15)

    def comparison(name, panels, footer):
        canvas = Image.new('RGB', (480*len(panels), 570), '#20242a')
        draw = ImageDraw.Draw(canvas)
        for index, (image, title, note, metrics) in enumerate(panels):
            x = index*480
            crop = image.crop((1110, 560, 1255, 700)).resize((435, 420), Image.Resampling.NEAREST)
            canvas.paste(crop, (x+15, 55))
            draw.text((x+15, 8), title, font=title_font, fill='white')
            draw.text((x+15, 31), note, font=font, fill='#ffcf90')
            if metrics:
                lines = ['%s bbox %s' % (key, metrics[key]['bounds_exclusive']) for key in BOXES]
                draw.multiline_text((x+15, 482), '\n'.join(lines), font=font, fill='white')
        draw.text((15, 548), footer, font=font, fill='white')
        canvas.save(out/name)

    comparison('hud253-font-reference.png', (
        (refs[0], 'Original stock: retail-01', '1280x800; top 80 rows removed', measured[0]),
        (before, 'Before CPU: '+args.before_source[:12], 'Constructed stock idle / 0 MPH / gear 1', measured[2]),
        (after, 'After CPU: '+args.source[:12], 'Same constructed state; native font metrics', measured[3]),
    ), '3x nearest crops. Threshold bounds have raster/AA uncertainty; original gear colour remains unresolved.')
    comparison('hud253-font-owner-before.png', tuple(
        (image, 'Owner before: '+label, args.before_source[:12]+'; issue #253 comment 6543', None)
        for image, (label, name, sha) in zip(owners, OWNER)),
        'Approved owner evidence before correction. Different driving states; these stills do not establish shift timing.')
    identity['method'] = dict(regions=BOXES, crop=[1110, 560, 1255, 700], enlargement=3,
        blue='G>105, B>R+35, B>G+12', orange_gear='R>120, G>55, R>G+35, G>B+25',
        uncertainty='Threshold/JPEG/antialiasing affect bounds; no exact original raster-equivalence claim.')
    (out/'provenance.json').write_text(json.dumps(identity, indent=2, sort_keys=True)+'\n', encoding='ascii')
    print('PASS: font comparisons, pinned owner inputs, provenance and threshold measurements')


if __name__ == '__main__':
    main()

"""Create issue 253 annotated comparisons and port-only shift tables.

Requires Pillow. Read-only inputs, fresh output directory; derived images must
remain outside Git. Reference pictures are bottom-aligned, never stretched.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OWNER_NAMES = (
    'codex-clipboard-3143df62-bdcb-40a4-adce-cb0d4b8cbb50.jpg',
    'codex-clipboard-44f2ca20-5bd4-4a41-8ac6-3013c9e8d7e8.jpg',
    'codex-clipboard-d66b90e1-7b08-4832-96b8-168cd1fe4a84.jpg',
    'codex-clipboard-33fbe4c5-01c3-4695-8485-7928f89051c3.jpg',
    'codex-clipboard-a02b0bb1-9094-413e-85c2-2a2a2c81da6a.jpg',
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reference', 'owner', 'fixtures', 'audit', 'out'):
        parser.add_argument('--'+name, required=True, type=Path)
    parser.add_argument('--source', required=True, help='full source SHA for generated captures')
    args = parser.parse_args()
    if len(args.source) != 40 or any(c not in '0123456789abcdef' for c in args.source):
        parser.error('source must be a full lower-case SHA')
    out = args.out.resolve()
    for root in (args.reference, args.owner, args.fixtures, args.audit):
        if out == root.resolve() or root.resolve() in out.parents:
            parser.error('output cannot be under an input directory')
    out.mkdir(parents=True, exist_ok=False)
    font = ImageFont.load_default(size=20)
    identity = {'source': args.source, 'references': [], 'owner': [], 'generated': []}
    for group, paths in (
            ('references', sorted(args.reference.glob('retail-*'))),
            ('owner', [args.owner/name for name in OWNER_NAMES]),
            ('generated', sorted(args.fixtures.glob('*.tga')) +
             sorted(args.audit.glob('frame-*.tga')) + [args.audit/'hud.csv'])):
        for path in paths:
            row = {'name': path.name, 'bytes': path.stat().st_size, 'sha256': sha(path)}
            if path.suffix != '.csv':
                with Image.open(path) as image:
                    row['size'] = image.size
            identity[group].append(row)
    if len(identity['references']) != 30 or len(identity['owner']) != 5:
        raise ValueError('incomplete provided image set')

    def comparison(name, panels, notes):
        image = Image.new('RGB', (2100, 620), '#171b20')
        draw = ImageDraw.Draw(image)
        for index, (path, title, pivot, extra) in enumerate(panels):
            with Image.open(path) as source:
                # The reference 800px bottom becomes the 720px bottom after
                # removing 80px at the top. No gauge rescale in this step.
                crop = source.convert('RGB').crop((930, source.height-250,
                                                  1280, source.height))
            crop = crop.resize((700, 500), Image.Resampling.NEAREST)
            image.paste(crop, (700*index, 60))
            x, y = 700*index+(pivot[0]-930)*2, 60+(pivot[1]-470)*2
            draw.line((x-12,y,x+12,y), fill='#ffff40', width=2)
            draw.line((x,y-12,x,y+12), fill='#ffff40', width=2)
            draw.text((700*index+12, 10), title, font=font, fill='white')
            draw.text((700*index+12, 35), extra, font=font, fill='#ffd090')
        draw.text((12, 574), notes, font=font, fill='white')
        image.save(out/name)

    comparison('hud253-stock.png', (
        (args.reference/'retail-01.webp', 'Original stock 0 MPH / face 0..8',
         (1133,601), '1280x800; top 80px removed'),
        (args.owner/OWNER_NAMES[0], 'Owner c319a4b-red / stock-looking face',
         (1072.9,591.75), '1280x720; paint does not prove an upgrade'),
        (args.fixtures/'stock-idle.tga', 'New CPU stock fixture / idle 800 RPM',
         (1133,601), '1280x720; port-only constructed state'),
    ), '2x nearest crop; yellow crosses mark the pivot. Original orange gear remains unresolved in port.')
    comparison('hud253-faces.png', (
        (args.reference/'retail-09.jpg', 'Original face 0..9 / upgrades unknown',
         (1133,601), 'A still cannot identify max/redline or RPM'),
        (args.fixtures/'stock-driving.tga', 'New CPU stock / max 8000 redline 7500',
         (1133,601), '6200 RPM / second gear / 62 MPH'),
        (args.fixtures/'engine-b-level2.tga', 'New CPU explicit Engine B level 2 inputs',
         (1133,601), '6500 RPM / max 9000 redline 8000'),
    ), 'Port-only fixtures isolate face selection from cosmetic tuning. No original temporal-parity claim.')
    with (args.audit/'hud.csv').open(newline='', encoding='ascii') as source:
        rows = list(csv.DictReader(source))
    selected = [row for row in rows if 5446 <= int(row['tick']) <= 5470]
    fields = list(rows[0]) + ['display_gear', 'screen_angle']
    with (out/'hud253-shifts.csv').open('w', newline='', encoding='ascii') as target:
        writer = csv.DictWriter(target, fields)
        writer.writeheader()
        for row in selected:
            writer.writerow(dict(row, display_gear=int(row['gear'])-1,
                                 screen_angle=270-float(row['rotation'])))
    ticks = (5447,5448,5449,5450,5451,5452,5453,5454,5465,5466,5467,5468,5469,5470)
    lookup = {int(row['tick']): row for row in rows}
    image = Image.new('RGB', (1400, 1420), '#171b20')
    draw = ImageDraw.Draw(image)
    draw.text((12,8), 'PORT ONLY: event1 replay; first upshift and subsequent downshift', font=font, fill='white')
    for index, tick in enumerate(ticks):
        x, y = (index % 4)*350, 50+(index // 4)*340
        row = lookup[tick]
        with Image.open(args.audit/('frame-'+str(tick)+'.tga')) as source:
            image.paste(source.convert('RGB').crop((465,235,640,360)).resize(
                (350,250), Image.Resampling.NEAREST), (x,y))
        label = ('tick %d / physics %s / %.6fs\ngear %d RPM %.3f\nrotation %.3f; screen %.3f' %
                 (tick,row['physics'],float(row['seconds']),int(row['gear'])-1,
                  float(row['engine_rpm']),float(row['rotation']),270-float(row['rotation'])))
        draw.multiline_text((x+5,y+254), label, font=font, fill='white', spacing=3)
    image.save(out/'hud253-sequence.png')
    (out/'hud253-provenance.json').write_text(json.dumps(identity, ensure_ascii=True, indent=2)+'\n', encoding='ascii')
    print('PASS: 30 original and 5 owner identities; annotated stock/setup comparisons and port-only shift sequence/table')


if __name__ == '__main__':
    main()

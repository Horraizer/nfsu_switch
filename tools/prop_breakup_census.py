"""Read-only census of retail prop child/damage definitions and placements."""
from collections import Counter
from pathlib import Path
import json
import struct
import sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path.cwd()/'tools'))
from world_body_trace import chunks,jdlz

counts=Counter(tracks=0,placements=0,composite_placements=0,damage_placements=0)
unsupported=[]
root=Path(sys.argv[1])
for path in sorted((root/'Tracks').glob('TRACKB*.lzc')):
    image=path.read_bytes()
    if image.startswith(b'JDLZ'):image=jdlz(image)
    definitions={}
    placed=Counter()
    for tag,start,end in chunks(image):
        if tag not in (0x34026,0x34027):continue
        first=(start+23)&~15
        stride=256 if tag==0x34026 else 112
        if first>end or (end-first)%stride:raise ValueError('invalid prop array')
        for at in range(first,end,stride):
            if tag==0x34027:
                placed[struct.unpack_from('<I',image,at)[0]]+=1
                continue
            key=struct.unpack_from('<I',image,at+128)[0]
            name=image[at+8:at+32].split(b'\0')[0].decode('ascii')
            children=struct.unpack_from('<I',image,at+140)[0]
            damage=struct.unpack_from('<f',image,at+200)[0]
            definitions[key]=(name,children,damage)
    counts['tracks']+=1
    for key,(name,children,damage) in sorted(definitions.items()):
        if children or damage:
            unsupported.append(dict(track=path.name,name=name,children=children,damage=damage,placements=placed[key]))
    for key,n in placed.items():
        if key not in definitions:continue
        name,children,damage=definitions[key]
        counts['placements']+=n
        counts['composite_placements']+=n if children else 0
        counts['damage_placements']+=n if damage else 0
if not counts['tracks'] or not counts['placements']:raise ValueError('no retail prop inputs')
print(json.dumps(dict(counts=counts,definitions_with_children_or_damage=unsupported),indent=2))

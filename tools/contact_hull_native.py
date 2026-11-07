from pathlib import Path
import sys,json,hashlib,math
from world_body_trace import Image
import contact256_native as n
from contact257_retail import prepare,inputs
if len(sys.argv)!=3:raise SystemExit('usage: contact_hull_native.py retail-root output-json')
root=Path(sys.argv[1]);raw=(root/'speed.exe').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
im=Image(raw);data=inputs(root);rows=[]
for offset in ((4.4,0,0),(4.38,.7,0),(4.2,.7,0),(0,2,0),(.7,2,0),(0,0,2)):
 m=n.fixture(im,offset,(0,0,0),(0,0,0));p=prepare(m,n,data);q=n.query(m)
 row={'offset':offset,'query':q,'com':n.vector(m,0x1120060),'prerequisite':p};rows.append(row);print(offset,q)
Path(sys.argv[2]).write_text(json.dumps(rows,indent=2),encoding='ascii')

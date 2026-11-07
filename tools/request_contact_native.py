from pathlib import Path
import sys,ast,json,hashlib,copy
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path.cwd()/'tools'))
from physics256_native import STACK
if len(sys.argv)!=4:raise SystemExit('usage: request_contact_native.py RETAIL INPUT_JSON OUTPUT')
root=Path(sys.argv[1]).resolve();source=Path('tools/handling_player_native.py')
out=Path(sys.argv[3]).resolve()
if out==root or root in out.parents:raise ValueError('output must be outside retail root')
out.mkdir(exist_ok=False)
raw=source.read_bytes();tree=ast.parse(raw)
fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='witness')
end=next(i for i,n in enumerate(fn.body) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='conversion' for t in n.targets))
imports=ast.Module(body=[n for n in tree.body if isinstance(n,(ast.Import,ast.ImportFrom))],type_ignores=[])
prefix=ast.Module(body=fn.body[:end],type_ignores=[])
body_heading_x=1.;body_heading_y=0.;external_yaw=0.
exec(compile(imports,str(source),'exec'),globals());exec(compile(prefix,str(source),'exec'),globals())
assert not m.events
m.allowed += [(0x46D700,0x46D75A),(0x477698,0x4776A5)]
snapshots=[(at,bytes(m.uc.mem_read(at,size))) for at,size in [(engine,0x100),(gearbox,0x100)]+[(wheels+0x200*k,0x200) for k in range(4)]]
base=json.loads(Path(sys.argv[2]).read_text());results=[]
for name,override in [('current-request',None),('current-equality',4.),('current-below',3.9999997615814209)]:
 row=copy.deepcopy(base);row['name']=name
 if override is not None:row['current160'][0]=override
 for at,blob in snapshots:m.uc.mem_write(at,blob)
 for offset,key in ((0x3c,'gear'),(0x38,'request'),(0x44,'mode')):m.put(gearbox+offset,row[key])
 for offset,key in ((0x78,'up_offset'),(0x7c,'down_offset')):m.put(gearbox+offset,row[key],'f')
 m.put(engine+0x14,row['engine_rad'],'f');m.put(engine+0x78,row['throttle'],'f')
 for k in range(4):
  w=wheels+0x200*k
  m.put(w+0x160,row['current160'][k],'f');m.put(w+0x164,row['current160'][k],'f')
  m.put(w+0x155,row['contacts'][k],'B');m.put(w+0x28,row['spins'][k],'f')
 m.put(STACK+0x18,physics);m.run(0x477698,end=0x4776A5,eax=0xffffffff)
 assert not m.events
 results.append(dict(input=row,original_request=m.get(gearbox+0x38)))
report=dict(exe_sha256=hashlib.sha256(data).hexdigest(),bun_sha256=hashlib.sha256(bun).hexdigest(),constructor_sha256=hashlib.sha256(raw).hexdigest(),precision='007F',results=results,limits='Held request operands from port prefix; original caller clip and constructors. Not an original whole trajectory.')
(out/'report.json').write_text(json.dumps(report,indent=2),encoding='ascii')
def f(x):return float(x).hex()+'F'
lines=['// Original request clip007F on current contact-correction prefix; old34 records retained.']
for x in results:
 r=x['input'];lines.append('{"'+r['name']+'",true,'+','.join(str(r[k]) for k in ('gear','mode','request'))+','+','.join(f(r[k]) for k in ('engine_rad','up_offset','down_offset'))+','+','.join(str(r[k]) for k in ('actor_rpm','wheel_rpm'))+',{{'+','.join('true' if v else 'false' for v in r['contacts'])+'}},{{'+','.join(f(v) for v in r['current160'])+'}},{{'+','.join(f(v) for v in r['spins'])+'}},'+str(x['original_request'])+'},')
(out/'pins.inc').write_text('\n'.join(lines)+'\n',encoding='ascii')
print('PASS original requests',[(x['input']['name'],x['original_request']) for x in results])

"""Production grid, simulation and rendered-command smoke for career16..30."""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--root',type=Path,required=True);ap.add_argument('--app',type=Path,required=True);ap.add_argument('--work',type=Path,required=True)
a=ap.parse_args();a.work.mkdir(parents=True,exist_ok=True)
out=Path(tempfile.mkdtemp(prefix='career-block30-',dir=a.work));rows=[]
for event in range(16,31):
 for round_index in range(4 if event==20 else 3 if event==30 else 1):
  env=os.environ.copy();env['NFSU_CAREER_TEST_ROUND']=str(round_index);env['NFSU_CAREER_SCENE_CHECK']='1'
  env['NFSU_CAREER_TEST_CAR']='CIVIC';env['NFSU_CAREER_TEST_DIFFICULTY']='0'
  command=[str(a.app.resolve()),str(a.root),str(event),'360']
  result=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=150)
  log=out/('event-'+str(event)+'-round-'+str(round_index)+'.log');log.write_bytes(result.stdout)
  text=result.stdout.decode('utf-8',errors='replace')
  good=result.returncode==0 and 'PASS' in text and not any(v in text for v in ('FAIL','ERROR','error:'))
  rows.append(dict(event=event,round=round_index,command=command,exit=result.returncode,passed=good,log=log.name))
  (out/'results.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='ascii')
  print('PASS' if good else 'FAIL','event',event,'round',round_index,flush=True)
  if not good:print(text[-8000:].encode('ascii',errors='backslashreplace').decode('ascii'));raise SystemExit(1)
print('PASS career16..30 production grid, simulation, HUD and GPU commands: '+str(len(rows))+' authored stages',flush=True)

"""Real Trade In scene, selected-logo raster and transaction regression."""
from pathlib import Path
import argparse,os,subprocess,tempfile
ap=argparse.ArgumentParser()
for key in ('app','root','work'):ap.add_argument('--'+key,type=Path,required=True)
a=ap.parse_args();a.work.mkdir(parents=True,exist_ok=True)
out=Path(tempfile.mkdtemp(prefix='career-trade-',dir=a.work))
for name,full in (('starter-stock',False),('all-cars-tuned',True)):
 env=os.environ.copy()
 for key in ('NFSU_TRADE_ALL_CARS','NFSU_TRADE_TUNED'):env.pop(key,None)
 if full:env.update(NFSU_TRADE_ALL_CARS='1',NFSU_TRADE_TUNED='1')
 # Keep native narrow output arguments ASCII even in the real Unicode checkout.
 destination=os.path.relpath(out/name,Path.cwd());assert destination.isascii()
 result=subprocess.run([str(a.app.resolve()),str(a.root),destination,'trade-live'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
 text=result.stdout.decode('ascii',errors='backslashreplace');(out/(name+'.log')).write_text(text,encoding='ascii')
 print(text,flush=True)
 assert result.returncode==0 and 'PASS resident Trade In' in text and not any(value in text for value in ('FAIL','ERROR','error:')),name
 for index in (0,1):
  path=out/name/('trade-'+str(index)+'.ppm')
  assert path.is_file() and path.stat().st_size>1280*720*3,'fresh rendered Trade In frame missing'
print('PASS stock and tuned complete Trade In banks')

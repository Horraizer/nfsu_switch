"""Production loading, simulation, HUD and draw commands for every campaign stage."""
import argparse,concurrent.futures,hashlib,json,os,struct,subprocess,sys,tempfile
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from world_body_trace import jdlz,chunks

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('root','app','work'):ap.add_argument('--'+key,type=Path,required=True)
    ap.add_argument('--finish-sweep',action='store_true')
    a=ap.parse_args();a.work.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='career-full-',dir=a.work))
    front=jdlz((a.root/'FrontEnd/FrontB.lzc').read_bytes())
    assert hashlib.sha256(front).hexdigest()=='94a714d1e8e3bef7f77aedcee4fdc6350e285b06059b92d392687710e64e5d77'
    spans=[(at,end) for tag,at,end in chunks(front) if tag==0x34a02];assert len(spans)==1
    at,end=spans[0];at+=8;assert (end-at)%0x14c==0
    stages=[]
    for offset in range(at,end,0x14c):
        number,kind=struct.unpack_from('<2I',front,offset)
        count=struct.unpack_from('<I',front,offset+0x98)[0]
        if 1<=number<=113 and kind!=8:
            assert 1<=count<=8
            stages.extend((number,r) for r in range(count))
    assert stages and stages[-1]==(113,0) and not any(e==85 for e,r in stages)
    manifest=subprocess.run([str(a.app.resolve()),str(a.root),'--sweep-manifest'],
        check=True,capture_output=True,text=True,timeout=30).stdout
    assert [tuple(map(int,line.split())) for line in manifest.splitlines()]==[(0,0)]+sorted(stages)
    def run(stage,large_start=False):
        event,round_index=stage;env=os.environ.copy()
        env.update(NFSU_CAREER_TEST_ROUND=str(round_index),NFSU_CAREER_SCENE_CHECK='1',NFSU_CAREER_START_SCENE_CHECK='1',NFSU_CAREER_TEST_CAR='CIVIC',NFSU_CAREER_TEST_DIFFICULTY='0')
        env.update(NFSU_CAREER_SHADOW_SILHOUETTE='1')
        env.pop('NFSU_CAREER_TEST_LEVELS',None);env.pop('NFSU_CAREER_TEST_START_SCENE',None)
        if a.finish_sweep:
            env.update(NFSU_CAREER_FINISH_SWEEP='1',NFSU_CAREER_TEST_DIFFICULTY='1')
            if event==0:
                env['NFSU_CAREER_TEST_CAR']='INTEGRA';env.pop('NFSU_CAREER_START_SCENE_CHECK')
        if large_start:env.update(NFSU_CAREER_TEST_LEVELS='weight=0,suspension=0,engine-a=1,induction=0,nitrous=0,engine-b=0,gearbox=1,tyres=1,grip=0,aero-a=1,aero-b=1',NFSU_CAREER_TEST_START_SCENE='2318',NFSU_CAREER_TEST_DIFFICULTY='1')
        command=[str(a.app.resolve()),str(a.root),str(event),'360']
        try:
            result=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
            data=result.stdout;code=result.returncode
        except subprocess.TimeoutExpired as e:data=e.stdout or b'';code=None
        log=out/('event-'+str(event)+'-round-'+str(round_index)+('-large-start' if large_start else '')+'.log');log.write_bytes(data)
        text=data.decode('utf-8',errors='replace')
        marker='PASS career finish probe' if a.finish_sweep else 'PASS career'
        good=code==0 and marker in text and not any(v in text for v in ('FAIL','ERROR','error:'))
        row=dict(event=event,round=round_index,large_start=large_start,command=command,exit=code,passed=good,log=log.name,sha256=hashlib.sha256(data).hexdigest())
        print('PASS' if good else 'FAIL','event',event,'round',round_index,flush=True)
        if not good:print(text[-2000:].encode('ascii',errors='backslashreplace').decode('ascii'),flush=True)
        return row
    if a.finish_sweep:stages=[(0,0)]+stages
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:rows=list(pool.map(run,stages))
    if not a.finish_sweep:rows.append(run((8,0),True))
    (out/'results.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='ascii')
    passed=all(r['passed'] for r in rows)
    print('PASS' if passed else 'FAIL','full career finish probe' if a.finish_sweep else 'full career production stages',
        len(rows),'failures',sum(not r['passed'] for r in rows),flush=True)
    return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())

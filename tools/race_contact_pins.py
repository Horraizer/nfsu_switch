"""Export original world-contact witnesses into the C++ fixture layout."""
from pathlib import Path
import argparse,json

def export(kind,source,target):
    assert not target.exists()
    manifest=json.loads((source/'manifest.json').read_text('ascii'))
    assert manifest['completed'] and manifest['fpu_control_word']=='007F'
    cases=[]
    if kind=='world':
        for side in ['front','back']:
            cases.extend((side,i) for i in range(1,10))
        for side in ['reverse-front','reverse-back']:
            cases.extend((side,i) for i in [1,6])
    else:
        for side in ['floor-front','floor-back']:
            cases.extend((side,i) for i in [1,2,3,8])
    def number(v):return float(v).hex()+'F'
    def vector(v):return '{'+','.join(number(x) for x in v)+'}'
    def body(address):
        return {'0x1100000':0,'0x1101000':1}.get(address,10)
    rows=['// Original420380/469810, normal full57F2B0 setup, PC=007F.',
          '// Reproduce native episodes, then tools/race_contact_pins.py.',
          '// Controlled pair/wall'+('/floor' if kind=='ground' else '')+' witnesses; no full race parity claim.']
    for side,index in cases:
        r=json.loads((source/('all-world-'+side+'-'+str(index)+'.json')).read_text('ascii'))
        assert r['success'] and len(r['geometry'])==len(r['queued_order'])<=4
        jobs=[]
        for q,g in zip(r['queued_order'],r['geometry']):
            jobs.append('{'+str(body(q['a']))+','+str(body(q['b']))+','+number(q['remaining'])+','+vector(g['point'])+'}')
        states=[]
        for s in r['after_response']:
            states.append(vector(s['position']+s['velocity']+s['angular_velocity']+[s['state']]))
        assert len(states)==2
        inertia=[vector(s['inertia']) for s in r['selected_prerequisites']]
        rows.append('{'+number(r['world_setup']['corners'][0][0])+','+str(index)+',{{'+','.join(inertia)+'}},'+str(len(jobs))+',{{'+','.join(jobs)+'}},{{'+','.join(states)+'}},'+('true' if side.startswith('reverse') else 'false')+(',true' if kind=='ground' else '')+'},')
    target.write_text('\n'.join(rows)+'\n',encoding='ascii')
    print('PASS exported original contact cases',len(cases))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('kind',choices=['world','ground']);p.add_argument('source',type=Path);p.add_argument('target',type=Path)
    a=p.parse_args();export(a.kind,a.source,a.target)

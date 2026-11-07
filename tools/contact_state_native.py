"""Authenticated 42A530 recovery boundary; synthetic actor/body memory only."""
import argparse, hashlib, json
from pathlib import Path
from world_body_trace import Image
import contact256_native as n

def run(executable):
    raw=Path(executable).read_bytes()
    if hashlib.sha256(raw).hexdigest()!='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7':
        raise ValueError('unsupported executable')
    image=Image(raw)
    rows=[]
    for name,state,timer,support,flag,ticks in [
        ('begin',1,0,4,0,4000),('before',1,1,4,0,4399),
        ('threshold',1,1,4,0,4400),('after',1,1,4,0,4401),
        ('support_lost',1,1,3,0,8000),('body_active',1,1,4,1,8000),
        ('state2_begin',0,0,4,1,8000),('state2_end',2,0,4,0,8000),
        ('state2_air',2,0,3,0,8000),('negative_flag',0,0,4,-1,8000)]:
        m=n.fixture(image,(3,0,0),(0,0,0),(0,0,0))
        m.put(n.ACTOR_A+0x3c,n.BODY_A)
        m.put(n.ACTOR_A+0x38,state);m.put(n.ACTOR_A+0x4cc,timer,'f')
        m.put(n.ACTOR_A+0x3cc,support);m.put(n.BODY_A+0x1c,flag,'b')
        m.put(0x73ad34,ticks,'i')
        scale=m.get(0x6cca38,'f')
        m.run(0x42a530,ecx=n.ACTOR_A)
        rows.append(dict(name=name,state=state,timer=timer,support=support,flag=flag,ticks=ticks,
            seconds=ticks*scale,after_state=m.get(n.ACTOR_A+0x38),after_timer=m.get(n.ACTOR_A+0x4cc,'f')))
    return rows
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('executable');p.add_argument('output');args=p.parse_args()
    rows=run(args.executable)
    Path(args.output).write_text(json.dumps(rows,indent=2),encoding='ascii')
    print('PASS original recovery cases:',len(rows))

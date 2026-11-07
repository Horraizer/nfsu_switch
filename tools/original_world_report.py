"""Validate a world tape and export actual native call/motion observations."""
import argparse,collections,json,struct
from pathlib import Path
import original_recorder as base
from original_world_recorder import CALL,SNAP,decode_snapshot

def records(path,fmt):
    with path.open('rb')as file:
        while True:
            data=file.read(fmt.size)
            if not data:return
            if len(data)!=fmt.size:raise ValueError('Truncated tape: '+path.name)
            yield data

def report(session,out):
    metadata=json.loads((session/'metadata.json').read_text(encoding='ascii'))
    if metadata.get('schema')!='nfsu-world-trace-1'or metadata.get('snapshot_words')!=1344:raise ValueError('Unsupported world trace schema')
    if out.exists():raise FileExistsError('Use a fresh output directory')
    base.make_directory(out,exclusive=True)
    instrumentation=json.loads((session/'instrumentation.json').read_text(encoding='ascii'))
    names={i:x['name']for i,x in enumerate(instrumentation['entries'])}
    counts=collections.Counter();stacks={};unbalanced=[];durations=collections.defaultdict(list);last=0;input_count=0
    with(out/'input-events.jsonl').open('x',encoding='ascii')as inputs:
        for data in records(session/'calls.bin',CALL):
            row=CALL.unpack(data);seq,call,kind,phase,actor,actor_id,role=row[:7]
            if seq!=last+1:raise ValueError('Native call sequence gap')
            last=seq;stack=stacks.setdefault(row[12],[]);name=names[kind]
            counts[(name,phase,role)]+=1;cycles=row[28]|row[29]<<32
            if phase==1:
                if row[14]!=(stack[-1][0]if stack else 0):unbalanced.append(dict(seq=seq,reason='parent mismatch'))
                stack.append((call,kind,cycles))
                if kind in(15,17,18)or kind>=19:
                    inputs.write(base.encoded(dict(seq=seq,call=call,kind=name,actor=actor,
                        actor_id=actor_id,role=role,frame=row[8],clock=row[9],track=row[34],mode=row[35],
                        object=row[7],args=list(row[24:27])+[row[36]],esi=row[20],caller=row[15]))+'\n');input_count+=1
            else:
                if not stack or stack[-1][:2]!=(call,kind):unbalanced.append(dict(seq=seq,reason='return mismatch'))
                else:
                    begin=stack.pop();durations[name].append(cycles-begin[2])
    actors={};snapshot_count=0;physics_posts=0
    with(out/'native-states.jsonl').open('x',encoding='ascii')as states:
        for data in records(session/'snapshots.bin',SNAP):
            row=decode_snapshot(data);snapshot_count+=1
            if row['seq']>last:raise ValueError('State references an absent call')
            # Preserve every boundary, including duplicate positions and pre/post epochs.
            states.write(base.encoded(row)+'\n')
            key=(row['track'],row['address'],row['id'],row['role']);summary=actors.setdefault(key,dict(first_clock=row['clock'],last_clock=row['clock'],states=0,physics_posts=0,maximum_native_speed_raw=None))
            summary['last_clock']=row['clock'];summary['states']+=1
            speed=row['speed_mps']
            if speed is not None:summary['maximum_native_speed_raw']=max(speed,summary['maximum_native_speed_raw']or speed)
            if row['phase']==2 and row['kind']in(3,4):summary['physics_posts']+=1;physics_posts+=1
    result=dict(schema='nfsu-world-report-1',source=str(session),synthetic_fixture=metadata.get('synthetic_fixture',False),
        calls=last,snapshots=snapshot_count,input_events=input_count,whole_physics_post_states=physics_posts,
        unbalanced=unbalanced,open_calls={str(k):v for k,v in stacks.items()if v},
        counts=[dict(name=n,phase=p,role=r,count=v)for(n,p,r),v in sorted(counts.items())],
        actors=[dict(track=k[0],address=k[1],actor_id=k[2],role=k[3],**v)for k,v in actors.items()],
        cycle_spans={name:dict(count=len(v),median=sorted(v)[len(v)//2],maximum=max(v))for name,v in durations.items()if v},
        limitations=['Cycle spans include instrumentation and nested calls; they are not Switch CPU timings.',
            'Native speed field retains the legacy speed_mps key; raw vectors and coordinates are also preserved for unit validation.',
            'Use the clock/4000, native paths, poses and input events to measure lane changes; no guessed lane labels are generated.',
            'Whole-world traffic samples and cross-frame markers remain in the source samples.jsonl.',
            'Unbalanced/open calls and an incomplete stop are reported, not discarded or counted as successful replay.'])
    base.write_json(out/'report.json',result)
    print(base.encoded(dict(calls=last,snapshots=snapshot_count,actors=len(actors),input_events=input_count,unbalanced=len(unbalanced),open_calls=sum(map(len,stacks.values())))))
    return result

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--session',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args();report(a.session,a.out)
if __name__=='__main__':main()

"""Fresh scalar-native/C++ differential, no images or gameplay claims.

Requires an authenticated fresh trace report and the compiled worker driver.
Manifests bracket every retail read/driver invocation. Report is ASCII only.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess

import minimap266_native as native
from hud254_trace import PE, manifest, output_path, sha
from minimap266_trace import INPUTS


def triangles(quads):
    for q in quads:
        for ids in ((0, 1, 2), (0, 2, 3)):
            yield [(q['screen'][i], q['uvq'][i], q['colors'][i]) for i in ids]


def sample(quads, xy):
    for t in triangles(quads):
        a, b, c = [r[0] for r in t]
        denom = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(denom) < 1e-8:
            continue
        u = ((b[1]-c[1])*(xy[0]-c[0])+(c[0]-b[0])*(xy[1]-c[1]))/denom
        v = ((c[1]-a[1])*(xy[0]-c[0])+(a[0]-c[0])*(xy[1]-c[1]))/denom
        w = 1-u-v
        if min(u, v, w) >= -1e-5:
            return ([sum(weight*r[1][k] for weight, r in zip((u,v,w),t)) for k in range(3)],
                    [sum(weight*r[2][k] for weight, r in zip((u,v,w),t)) for k in range(4)])
    return None


def compare(expected, actual, label, maxima):
    # Compare submission geometry independent of fan start/topology. Native
    # clipping decomposes a convex polygon into pieces; no rasterizer is run.
    expected_vertices=[v for q in expected for v in q['screen']]
    actual_vertices=[v for q in actual for v in q['screen']]
    for vertices,other_quads in ((expected_vertices,actual),(actual_vertices,expected)):
        for v in vertices:
            if not other_quads:
                raise AssertionError('%s missing polygon' % label)
            distances=[]
            for q in other_quads:
                for i,p in enumerate(q['screen']):
                    b=q['screen'][(i+1)%4]
                    dx,dy=b[0]-p[0],b[1]-p[1]
                    length=dx*dx+dy*dy
                    t=max(0,min(1,((v[0]-p[0])*dx+(v[1]-p[1])*dy)/length)) if length else 0
                    distances.append(math.hypot(v[0]-p[0]-t*dx,v[1]-p[1]-t*dy))
            delta=min(distances)
            maxima['screen']=max(maxima['screen'],delta)
            if delta>1e-4:
                raise AssertionError('%s screen vertex mismatch %r: %r' % (label,v,delta))
    samples = [(x+0.37,y+0.29) for x in range(22,150,3) for y in range(176,304,3)]
    samples += [tuple(sum(v[0][k] for v in t)/3 for k in range(2)) for t in triangles(expected)
                if abs((t[1][0][0]-t[0][0][0])*(t[2][0][1]-t[0][0][1])-
                       (t[1][0][1]-t[0][0][1])*(t[2][0][0]-t[0][0][0]))>1e-4]
    hits = 0
    for xy in samples:
        e, a = sample(expected,xy), sample(actual,xy)
        if (e is None) != (a is None):
            raise AssertionError('%s coverage mismatch at %r; native=%r port=%r' % (label,xy,expected,actual))
        if e is None:
            continue
        hits += 1
        for k in range(3):
            delta = abs(e[0][k]-a[0][k])
            maxima['uvq'] = max(maxima['uvq'],delta)
            if delta > 1e-5:
                raise AssertionError('%s UVQ mismatch %r %r at %r; native=%r port=%r' % (label,e,a,xy,expected,actual))
        for k in range(4):
            delta = abs(e[1][k]-a[1][k])
            maxima['color'] = max(maxima['color'],delta)
            if delta > 1.01:
                raise AssertionError('%s color mismatch %r %r' % (label,e,a))
    if expected and not hits:
        raise AssertionError('%s no comparison samples' % label)
    return hits


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--driver',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    root=args.root.resolve()
    out=output_path(args.out,[root])
    before=manifest(root)
    try:
        data=json.loads((args.report/'data.json').read_text(encoding='ascii'))
        source=json.loads((args.report/'native.json').read_text(encoding='ascii'))
        for name,digest in source['tool_hashes'].items():
            if sha((Path(__file__).parent/name).read_bytes())!=digest:
                raise ValueError('trace is stale for '+name)
        if data['source_commit']!=source['source_commit']:
            raise ValueError('trace source identities disagree')
        blob=(root/'speed.exe').read_bytes()
        if sha(blob)!=INPUTS['speed.exe']:
            raise ValueError('unsupported native executable')
        image=PE(blob)
        m=native.prepare(image,data)
        color=list(m.uc.mem_read(0x7313DD,3))
        rows=list(source['projections'])
        for line in data['lines']:
            xy=line['position'][:2]
            rows.append(dict(name=line['name'],xy=xy,uv=native.project(m,xy)))
        commands=['P '+' '.join(map(str,row['xy'])) for row in rows]
        cases=[]
        player=data['route_nodes'][11]['xy']
        seam=next(r['position'][:2] for r in data['lines'] if r['name']=='ForwardStartLine')
        for style in (1,3,5,6):
            for angle,heading in ((0x8000,0),(0x4000,0x4000),(0x1234,0x3456)):
                for view in (player,seam,data['grid'][1]['raw_position'][:2]):
                    # Measured points include nodes, all grid entries, corners,
                    # out-of-map and separated IDs. Every case executes native.
                    points=[('id100',view),('id101',[view[0]+10,view[1]]),
                            ('id102',[view[0],view[1]+25]),('outside',[view[0]+5000,view[1]+5000])]
                    case=native.geometry(image,data,style,view,points,angle,heading)
                    cases.append(case)
        # Exercise exact and adjacent native UV boundaries, with world values
        # inverted from the same track transform (not road spline positions).
        base=native.geometry(image,data,3,player,[],0x8000,0)
        t=data['track']; c=source['world_matrix'][0]
        points=[]
        for i,uv in enumerate(base['boundary']):
            for delta in (-0.00001,0,0.00001):
                xy=[((uv[0]+delta)*t['world_span']+t['offset_x'])/c,
                    (t['offset_y']-uv[1]*t['world_span'])/c]
                points.append(('edge%d_%s'%(i,delta),xy))
        cases.append(native.geometry(image,data,3,player,points,0x8000,0))
        for style in (1,3,5):
            s=source['styles'][style]
            cases.append(native.geometry(image,data,style,seam,[('ForwardStartLine',seam)],
                0x8000,(t['rotation_u16']-t['start_angle_u16']-0x8000+0x4000)&0xffff,s['marker_sizes'][1:]))
            for angle in (0x8000,0x4000,0x1234):
                for first in range(0,len(rows),16):
                    points=[(r['name'],r['xy']) for r in rows[first:first+16]]
                    cases.append(native.geometry(image,data,style,player,points,angle,0x3456))
        for case in cases:
            s=source['styles'][case['style']]
            values=[s['index'],s['tilt_u16'],s['distance'],*s['offset'],*s['extent_add'],
                    *s['uv_extent'],s['alpha'],s['flags'][1],s['flags'][2],s['enabled'],
                    case['map_angle'],*case['player_xy'],len(case['markers'])]
            commands.append('G '+' '.join(map(str,values)))
            for i,row in enumerate(case['markers']):
                angle=(t['rotation_u16']-case['map_angle']+0x4000-case['marker_angle'])&0xffff
                sizes=case['marker_sizes'] or [s['marker_sizes'][0]]*2
                texture=0x72974A14 if case['marker_sizes'] else (0x7E22DED8 if s['flags'][2] else 0x135663D6)
                commands.append(' '.join(map(str,[100+i,*row['xy'],angle,*sizes,*color,5,1,1,texture])))
        result=subprocess.run([str(args.driver.resolve()),'--retail',str(root),'--oracle'],
                              input='\n'.join(commands)+'\n',text=True,capture_output=True,check=False)
        if result.returncode or 'FAIL' in result.stdout+result.stderr or not result.stdout.endswith('PASS: minimap checks\n'):
            raise RuntimeError('driver failed: '+result.stdout+result.stderr)
        lines=iter(result.stdout.splitlines())
        maxima=dict(projection=0,center=0,boundary=0,screen=0,uvq=0,color=0)
        for row in rows:
            tokens=next(lines).split()
            assert tokens[0]=='P'
            for e,a in zip(row['uv'],map(float,tokens[1:])):
                delta=abs(e-a); maxima['projection']=max(maxima['projection'],delta)
                assert delta<1e-7, (row['name'],e,a)
        samples=0
        for index,case in enumerate(cases):
            tokens=next(lines).split(); assert tokens[0]=='G'
            count=int(tokens[1]); values=list(map(float,tokens[2:])); quads=[]
            for e,a in zip(case['center'],values[:2]):
                delta=abs(e-a); maxima['center']=max(maxima['center'],delta)
                assert delta<2e-6,(index,'center',e,a)
            for e,a in zip([v for xy in case['boundary'] for v in xy],values[2:]):
                delta=abs(e-a); maxima['boundary']=max(maxima['boundary'],delta)
                assert delta<2e-6,(index,'boundary',e,a)
            for _ in range(count):
                q=next(lines).split(); assert q[0]=='Q'
                v=list(map(float,q[4:])); quads.append(dict(route=int(q[1]),id=int(q[2]),kind=int(q[3]),
                    screen=[v[i:i+2] for i in range(0,36,9)],
                    uvq=[v[i+2:i+5] for i in range(0,36,9)],colors=[v[i+5:i+9] for i in range(0,36,9)]))
            samples+=compare(case['route_clipped'],[q for q in quads if q['route']],str(index)+' route',maxima)
            for i,marker in enumerate(case['markers']):
                samples+=compare(marker['clipped'],[q for q in quads if not q['route'] and q['id']==100+i],str(index)+' '+marker['name'],maxima)
        assert next(lines)=='PASS: minimap checks'
        out.mkdir(parents=True)
        (out/'differential.json').write_text(json.dumps(dict(source_commit=data['source_commit'],
            driver_sha256=sha(args.driver.read_bytes()),tool_sha256=sha(Path(__file__).read_bytes()),
            projections=len(rows),cases=len(cases),samples=samples,maxima=maxima,
            limits='Scalar native with synthetic inputs and submission/binding stubs; geometric samples, no pixels/GPU/runtime acceptance'),
            indent=2,ensure_ascii=True)+'\n',encoding='ascii')
    finally:
        after=manifest(root)
        if before!=after:
            raise RuntimeError('retail manifest changed')
    print('PASS: fresh native/C++ minimap differential; manifests unchanged; '+str(maxima))


if __name__=='__main__':
    main()

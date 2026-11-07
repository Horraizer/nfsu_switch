"""Read-only458340 shapes and457C3E..45806B pair-walk witnesses, zero hooks.

The walk starts after actor-specific path prediction and cursor refresh with
explicit supplied current-body inputs. It is not whole4576B0 or live-game AI.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from physics256_native import Machine,bits
from world_body_trace import Image
from ai_path_vectors import literal

SHA='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
def group(values):return '{'+','.join(map(literal,values))+'}'
def hexes(values):return '{'+','.join('0x%08Xu'%x for x in values)+'}'

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('root','shapes','walks','evidence'):ap.add_argument('--'+key,type=Path,required=True)
    a=ap.parse_args();data=(a.root/'speed.exe').read_bytes()
    if hashlib.sha256(data).hexdigest()!=SHA:raise ValueError('unsupported executable')
    m=Machine(Image(data))
    m.allowed=[(0x458340,0x4584e6),(0x564bd0,0x564cc6),(0x457c3e,0x45806b),
               (0x458750,0x458a4a),(0x4586a0,0x45874d),(0x4584f0,0x458692)]
    path,lo,hi,nodes=0x1150000,0x1150200,0x1150220,0x1151000
    records=[];shape_lines=['// Original458340/564BD0,007F,zero hooks. '+SHA]
    low,high=(-2.,-1.,-.5),(2.,1.,1.1)
    def shape(name,points,heading=0,scale=1.,bounds=(low,high)):
        m.uc.mem_write(path,bytes(0x1c0));m.put(path,len(points),'b');m.uc.mem_write(nodes,bytes(20*0x50))
        for i,p in enumerate(points):
            for k,v in enumerate(p):m.put(path+0x10+16*i+4*k,v,'f')
        for addr,values in ((lo,bounds[0]),(hi,bounds[1])):
            for k,v in enumerate(values):m.put(addr+4*k,v,'f')
        m.visited.clear();m.run(0x458340,(0,path,heading,bits(scale)),eax=hi,ecx=lo,esi=nodes)
        offsets=(0,4,8,0x10,0x14,0x20,0x24,0x40,0x44,0x48)
        expected=[[m.get(nodes+0x50*i+off) for off in offsets] for i in range(len(points))]
        index=len(records)
        records.append(dict(name=name,points=points,bounds=bounds,heading=heading,scale=scale,
                            expected=expected,visited=sorted(m.visited),raw=bytes(m.uc.mem_read(nodes,20*0x50))))
        shape_lines.append('// '+name)
        shape_lines.append('{%d,%s,%s,%s,%d,%s,%s},'%(len(points),'{'+','.join(map(group,points))+'}',
            group(bounds[0]),group(bounds[1]),heading,literal(scale),'{'+','.join(map(hexes,expected))+'}'))
        return index
    straight=[(20.,0.,0.),(40.,0.,0.),(60.,0.,0.),(80.,0.,0.)]
    base=shape('straight',straight)
    ahead=shape('ahead',[(p[0]+10,p[1],p[2]) for p in straight])
    lane=shape('parallel-lane',[(p[0]+10,5.,0.) for p in straight])
    cross=shape('crossing',[(20.,10.,0.),(20.,30.,0.),(20.,50.,0.)],16384)
    reverse=shape('opposite',[(60.,0.,0.),(40.,0.,0.),(20.,0.,0.),(0.,0.,0.)],32768)
    curve=shape('curve',[(18.25,1.75,0.),(32.75,12.25,0.),(36.125,29.875,0.),(21.75,49.125,0.)],1234,.8)
    merge=shape('merge',[(20.,6.,0.),(40.,0.,0.),(60.,0.,0.),(80.,0.,0.)],30000,.8)
    repeated=shape('zero-segments',[(0.,0.,0.),(0.,0.,0.),(30.,0.,0.),(30.,0.,0.),(60.,0.,0.)])
    tiny=shape('tiny-segment',[(.000009,0.,0.),(20.,0.,0.),(40.,0.,0.)])
    elevated=shape('elevated-bounds',[(20.,0.,4.),(40.,0.,6.),(60.,0.,0.)],65535,1.3,((3.,-2.,4.),(-2.,1.,-1.)))
    for h in (1,8192,16383,16384,16385,32768,49151,49152,49153,65535):shape('heading-%d'%h,straight,h)
    empty=shape('empty',[])
    cases=[]
    def walk(name,ai,bi,pa,pb,sa,sb,acc=0.,horizon=4.,first=(0,0)):
        # Start after original shape/cursor/body refresh. All live services
        # preceding this slice are excluded explicitly, not replaced by hooks.
        sp,bp,body=0x1160000,0x1172000,0x1173000
        m.uc.mem_write(sp,bytes(0x1200))
        for off,v in [(0x24,sa),(0x20,sb),(0x80,acc),(0x2c,99999.),(0x44,pa[0]),(0x48,pa[1])]:m.put(sp+off,v,'f')
        m.put(sp+0xd0,len(records[ai]['points']),'b');m.put(sp+0x290,len(records[bi]['points']),'b')
        m.uc.mem_write(sp+0xa90,records[ai]['raw']);m.uc.mem_write(sp+0x450,records[bi]['raw'])
        m.put(sp+0x18,first[1]);m.put(body+0x40,pb[0],'f');m.put(body+0x44,pb[1],'f');m.put(bp+0x18,horizon,'f')
        m.visited.clear();m.run(0x457c3e,end=0x45806b,esp=sp,ebp=bp,esi=body,ebx=first[0],eax=first[1])
        row=dict(name=name,a=ai,b=bi,pa=pa,pb=pb,sa=sa,sb=sb,acc=acc,horizon=horizon,first=first,
                 result=m.get(sp+0x2c,'f'),bits=m.get(sp+0x2c),visited=sorted(m.visited))
        cases.append(row)
    for acc in (0.,2.,-1.):
        walk('catch',base,ahead,(0.,0.),(10.,0.),20.,10.,acc)
        walk('pull-away',base,ahead,(0.,0.),(10.,0.),10.,20.,acc)
        walk('parallel-clear',base,lane,(0.,0.),(10.,5.),20.,10.,acc)
        walk('crossing',base,cross,(0.,0.),(20.,-10.),20.,10.,acc)
        walk('oncoming',base,reverse,(0.,0.),(80.,0.),20.,20.,acc)
        walk('curve',curve,merge,(0.,0.),(8.,6.),14.,12.,acc)
        walk('merge',base,merge,(0.,0.),(10.,6.),20.,12.,acc)
    for horizon in (-1.,0.,.1,.25,.5,1.,2.):walk('horizon',base,ahead,(0.,0.),(10.,0.),20.,10.,horizon=horizon)
    walk('overlapping',base,ahead,(0.,0.),(1.,0.),20.,10.)
    walk('exact-touch',base,ahead,(0.,0.),(4.,0.),20.,10.)
    walk('stopped-clamped',base,ahead,(0.,0.),(10.,0.),.001,.001)
    walk('zero-segments',repeated,ahead,(0.,0.),(10.,0.),20.,10.)
    walk('both-zero-segments',repeated,repeated,(0.,0.),(0.,0.),20.,10.)
    walk('tiny-segment',tiny,ahead,(0.,0.),(10.,0.),20.,10.)
    walk('height-ignored',elevated,ahead,(0.,0.),(10.,0.),20.,10.)
    walk('cached-cursors',base,ahead,(24.,0.),(36.,0.),20.,10.,first=(1,2))
    walk('self-exhausted',base,ahead,(80.,0.),(36.,0.),20.,10.,first=(4,2))
    walk('other-exhausted',base,ahead,(24.,0.),(90.,0.),20.,10.,first=(1,4))
    walk('empty',empty,ahead,(0.,0.),(10.,0.),20.,10.)
    walk('empty-other',base,empty,(0.,0.),(10.,0.),20.,10.)
    walk('decelerate-through-zero',base,lane,(0.,0.),(10.,5.),2.,3.,-4.)
    walk('epsilon-distance',base,lane,(19.99999,0.),(10.,5.),20.,10.)
    walk_lines=['// Original457C3E..45806B pair traversal slice,007F,zero hooks. '+SHA]
    for r in cases:
        walk_lines.append('// '+r['name'])
        walk_lines.append('{%d,%d,%s,%s,%s,%s,%du,%du,%s,%s,0x%08Xu},'%(r['a'],r['b'],group(r['pa']),group(r['pb']),
            literal(r['sa']),literal(r['sb']),*r['first'],literal(r['acc']),literal(r['horizon']),r['bits']))
    a.shapes.write_text('\n'.join(shape_lines)+'\n',encoding='ascii');a.walks.write_text('\n'.join(walk_lines)+'\n',encoding='ascii')
    for r in records:r.pop('raw')
    a.evidence.write_text(json.dumps(dict(executable_sha256=SHA,fpu='007F',hooks=m.events,shapes=records,walks=cases),indent=2)+'\n',encoding='ascii')
    print('Original shapes:',len(records),'walks:',len(cases),'hooks:',len(m.events))
    print('Walk collision/no-collision:',sum(r['result']<99999 for r in cases),sum(r['result']==99999 for r in cases))
if __name__=='__main__':main()

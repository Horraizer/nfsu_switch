"""Capture original forward5A58D0 growth, without replacement road callbacks."""
import argparse,hashlib,json,bisect,struct
from pathlib import Path
import capstone
from unicorn import UC_HOOK_MEM_READ,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP
from world_body_trace import Image
from controller293_scene import PairedController
from controller293_planner import inputs,CTX
from controller293_strategy import CONTROLLER
from controller293_native import EXEC_RANGES
from physics256_native import ACTOR,STATUS,bits
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('root','output','evidence'):ap.add_argument('--'+name,type=Path,required=True)
    args=ap.parse_args();data=(args.root/'speed.exe').read_bytes()
    identity='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest()!=identity:raise ValueError('unsupported executable')
    m=PairedController(Image(data),inputs(args.root),event_rides=True).m;im=Image(data)
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    ranges=list(EXEC_RANGES)+[(0x458bd0,0x458cbb),(0x462840,0x4629dc),(0x4629e0,0x4629ff),
        (0x4216f0,0x421740),(0x435ca0,0x435d30),(0x44bad0,0x44baef),(0x5a53c0,0x5a5503),
        (0x5a5730,0x5a57e8),(0x5a57f0,0x5a58ce),(0x5a5ef0,0x5a5f75),(0x5a6080,0x5a6146),(0x5a5c30,0x5a5eeb),(0x5a6150,0x5a6306),(0x5a6860,0x5a6af7),(0x5a5340,0x5a53b9),(0x5a58d0,0x5a5c29),(0x5a5510,0x5a55ad),(0x5a5f80,0x5a5ff7),
        (0x5a6f50,0x5a6f7a),(0x5a6f80,0x5a701b)]
    for a,b in ranges:m.instructions.update({i.address:i.size for i in md.disasm(im.read(a,b-a),a)})
    world=m.get(0x7361f8);chain=m.get(CTX+0x3a8);routes={}
    for rid in range(128):
        a=m.get(0x77af30+rid*4)
        if a:routes[rid]=(a,m.get(a+0x10,'h'))
    spans=sorted((a+0x27c,a+0x27c+n*0x88,rid) for rid,(a,n) in routes.items());starts=[a for a,b,c in spans]
    touched=set();collect=[False]
    def read(uc,access,address,size,value,user):
        if not collect[0]:return
        k=bisect.bisect_right(starts,address)-1
        if k>=0:
            a,b,rid=spans[k]
            if address<b:touched.add((rid,(address-a)//0x88))
    m.uc.hook_add(UC_HOOK_MEM_READ,read)
    config=m.get(ACTOR+0x14);difficulty=max(0,min(9,m.get(config+0x735,'b')))
    probabilities=[m.get(0x779ff8+difficulty*0x54+k*4,'f') for k in range(3)]
    gate=m.get(m.get(0x736174)+0xb8,'h');role=m.get(config+4)
    curvature=[None]
    def capture(uc,address,size,user):
        if address==0x5a5c8f:curvature[0]=m.get(uc.reg_read(UC_X86_REG_ESP)+0x18,'f')
    m.uc.hook_add(UC_HOOK_CODE,capture,begin=0x5a5c8f,end=0x5a5c8f)
    # Output-only x87 store bridge, after the original function has returned.
    store=0x119f100;result_address=0x119f000
    code=b'\xd9\x1d'+struct.pack('<I',result_address)+b'\xc3'
    m.uc.mem_write(store,code)
    m.instructions.update({i.address:i.size for i in md.disasm(code,store)})
    def scalar(address,args=(),**regs):
        m.run(address,args,budget=3000000,**regs);m.run(store,budget=20)
        return m.get(result_address,'f')
    cases=[];near_queries=[];curve_queries=[];locate_queries=[];refresh=[];scratch=0x1150400
    for rid,index,horizon,offset in ((35,60,180.,0.),(35,78,120.,2.),(35,93,120.,-3.),(37,10,150.,4.),(37,100,150.,0.),(35,60,0.,0.)):
        if rid not in routes or index>=routes[rid][1]:raise ValueError('missing supplied start')
        category=m.get(m.get(ACTOR+0x10)+0x424,'b');m.run(0x5a5340,esi=chain,budget=300000)
        m.put(0x78a2fc,0);m.put(world+0x2d0,0x60000000);m.put(STATUS+0x10,0);m.put(0x73ad34,500000)
        m.uc.mem_write(CONTROLLER+0xc,bytes(32));m.put(CONTROLLER+0x478,0xffffffff)
        m.run(0x435ca0,(STATUS,ACTOR),budget=300000);hold=bool(m.reg('eax')&255)
        values=[offset]+[offset*(k+1)/7 for k in range(6)]
        for k,v in enumerate(values):m.put(scratch+4*k,v,'f')
        touched.add((rid,index));collect[0]=True
        m.run(0x5a5510,(chain,rid,scratch,scratch+4,bits(0.),0),eax=index,budget=300000)
        m.run(0x5a58d0,(chain,bits(horizon)),eax=ACTOR,budget=3000000)
        collect[0]=False
        cells=[]
        for j in range(m.get(chain+4)):
            c=m.get(chain+8+j*4);cr=m.get(c+0x5c,'b');ci=m.get(c+0x5e,'h');touched.add((cr,ci))
            cells.append(dict(route=cr,point=ci,distance=m.get(c+0x58),junction=bool(m.get(c+0x60,'B')),
                              offsets=[m.get(c+4+4*k) for k in range(7)]))
        curvature[0]=None;m.run(0x5a5c30,(chain,),eax=ACTOR,budget=3000000)
        if curvature[0] is None:raise ValueError('curvature not captured')
        for j,cell in enumerate(cells):
            c=m.get(chain+8+j*4)
            cell['lower']=[m.get(c+0x20+4*k) for k in range(7)]
            cell['upper']=[m.get(c+0x3c+4*k) for k in range(7)]
        cases.append(dict(curvature=curvature[0],route=rid,point=index,horizon=horizon,category=category,offsets=values,cells=cells,
            input=[0,role,gate,m.get(STATUS+0x14,'i')*.00025,hold],probabilities=probabilities,
            rng=m.get(world+0x2d0),cache=[m.get(CONTROLLER+0xc+k*4) for k in range(8)],marker=m.get(CONTROLLER+0x478,'i')))
        ci=len(cases)-1
        for first in (0,1,len(cells)-2,len(cells)-1):
            for distance in (-1.,0.,30.,1000.):
                value=scalar(0x5a6860,(category,bits(distance)),eax=chain,ecx=first)
                curve_queries.append([ci,first,category,distance,bits(value)])
        for at in (0,len(cells)//2,len(cells)-1):
            point=routes[cells[at]['route']][0]+0x27c+cells[at]['point']*0x88
            for shift in (-4.,0.,4.):
                position=[m.get(point+k*4,'f')+shift*m.get(point+0xc+k*4,'f') for k in range(2)]+[m.get(point+8,'f')]
                for k,v in enumerate(position):m.put(scratch+0x40+k*4,v,'f')
                m.run(0x5a65b0,(scratch+0x40,scratch+0x60,scratch+0x64,category),eax=chain,budget=300000)
                locate_queries.append([ci,position,category,m.reg('eax'),m.get(scratch+0x60),m.get(scratch+0x64)])
                for flags in (0,1,2,3):
                    m.run(0x5a6150,(chain,scratch+0x40,flags),eax=0,budget=300000)
                    selected=m.reg('eax');selected=selected if selected<0x80000000 else selected-0x100000000
                    near_queries.append([ci,position,flags,selected])
        chosen=cells[min(3,len(cells)-1)]
        selected_route=routes[chosen['route']][0];selected_point=selected_route+0x27c+chosen['point']*0x88
        for cold in (False,True):
            if cold:m.run(0x5a5340,esi=chain,budget=300000)
            before=dict(rng=m.get(world+0x2d0),cache=[m.get(CONTROLLER+0xc+4*k) for k in range(8)],marker=m.get(CONTROLLER+0x478,'i'))
            horizon=scalar(0x458b90,ecx=CONTROLLER)
            for k,v in enumerate((selected_route,selected_point,category)):m.put(scratch+0x80+4*k,v)
            collect[0]=True;m.run(0x5a5730,(scratch+0x80,ACTOR),eax=chain,budget=3000000);collect[0]=False
            output=[]
            for j in range(m.get(chain+4)):
                c=m.get(chain+8+j*4);cr=m.get(c+0x5c,'b');pt=m.get(c+0x5e,'h');touched.add((cr,pt))
                output.append(dict(route=cr,point=pt,distance=m.get(c+0x58),junction=bool(m.get(c+0x60,'B')),
                    offsets=[m.get(c+4+4*k) for k in range(7)],lower=[m.get(c+0x20+4*k) for k in range(7)],upper=[m.get(c+0x3c+4*k) for k in range(7)]))
            refresh.append(dict(source=ci,cold=cold,before=before,route=chosen['route'],point=chosen['point'],horizon=horizon,
                position=[m.get(ACTOR+0x60+4*k,'f') for k in range(3)],cells=output,rng=m.get(world+0x2d0),
                cache=[m.get(CONTROLLER+0xc+4*k) for k in range(8)],marker=m.get(CONTROLLER+0x478,'i')))
    raw=[];lines=['// Authenticated forward chain5A58D0 with supplied starting cells.','const RawRoad raw_roads[]={']
    for rid,index in sorted(touched):
        a,n=routes[rid];p=a+0x27c+index*0x88
        v=[m.get(p+k,'f') for k in (0,4,8,12,16,0x1c,0x24,0x28,0x2c,0x30)]
        v += [m.get(p+0x3c+2*k,'h')/256 for k in list(range(5))+list(range(11,17))]
        e=[m.get(p+k,'B') for k in (0x81,0x82)]
        lines.append('{%d,%d,%d,%s,{%d,%d},{%s}},'%(rid,index,n,str(bool(m.get(a+0x15,'B'))).lower(),*e,','.join(map(literal,v))))
        raw.append(dict(route=rid,point=index,count=n,reverse=bool(m.get(a+0x15,'B')),events=e,values=v))
    lines.append('};');lines.append('const ChainCase chains[]={')
    for c in cases:
        d,r,g,t,h=c['input']
        lines.append('{%d,%d,%d,%s,{%s},{%d,%d,%d,%s,%s,{%s}},%d,{'%(c['route'],c['point'],c['category'],literal(c['horizon']),
            ','.join(map(literal,c['offsets'])),d,r,g,literal(t),str(h).lower(),','.join(map(literal,c['probabilities'])),len(c['cells'])))
        for cell in c['cells']:lines.append('{%d,%d,0x%08Xu,%s,{%s},{%s},{%s}},'%(cell['route'],cell['point'],cell['distance'],str(cell['junction']).lower(),','.join('0x%08Xu'%v for v in cell['offsets']),','.join('0x%08Xu'%v for v in cell['lower']),','.join('0x%08Xu'%v for v in cell['upper'])))
        lines.append('},0x%08Xu,{%s},%d,%s},'%(c['rng'],','.join('0x%08Xu'%v for v in c['cache']),c['marker'],literal(c['curvature'])))
    lines.append('};')
    lines.append('const NearQuery near_queries[]={')
    for ci,position,flags,selected in near_queries:lines.append('{%d,{%s},%d,%d},'%(ci,','.join(map(literal,position)),flags,selected))
    lines.append('};');lines.append('const CurveQuery curve_queries[]={')
    for ci,first,category,distance,value in curve_queries:lines.append('{%d,%d,%d,%s,0x%08Xu},'%(ci,first,category,literal(distance),value))
    lines.append('};');lines.append('const LocateQuery locate_queries[]={')
    for ci,position,category,cell,distance,lateral in locate_queries:
        lines.append('{%d,{%s},%d,%d,0x%08Xu,0x%08Xu},'%(ci,','.join(map(literal,position)),category,cell,distance,lateral))
    lines.append('};');lines.append('const Refresh refresh_cases[]={')
    for c in refresh:
        b=c['before']
        lines.append('{%d,%s,%d,%d,%s,{%s},0x%08Xu,{%s},%d,%d,{'%(c['source'],str(c['cold']).lower(),c['route'],c['point'],literal(c['horizon']),
            ','.join(map(literal,c['position'])),b['rng'],','.join('0x%08Xu'%v for v in b['cache']),b['marker'],len(c['cells'])))
        for cell in c['cells']:lines.append('{%d,%d,0x%08Xu,%s,{%s},{%s},{%s}},'%(cell['route'],cell['point'],cell['distance'],str(cell['junction']).lower(),
            ','.join('0x%08Xu'%v for v in cell['offsets']),','.join('0x%08Xu'%v for v in cell['lower']),','.join('0x%08Xu'%v for v in cell['upper'])))
        lines.append('},0x%08Xu,{%s},%d},'%(c['rng'],','.join('0x%08Xu'%v for v in c['cache']),c['marker']))
    lines.append('};')
    args.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,raw=raw,cases=cases,near_queries=near_queries,curve_queries=curve_queries,locate_queries=locate_queries,refresh=refresh),indent=2)+'\n',encoding='ascii')
    print('original chain cases',len(cases),'raw points',len(raw),'emitted cells',sum(len(c['cells']) for c in cases))
if __name__=='__main__':main()

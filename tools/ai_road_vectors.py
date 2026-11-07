"""Original event0 route decisions/topology on authenticated initialized data.
No replacement AI callbacks. These are bounded topology inputs, not a race.
"""
from pathlib import Path
import argparse,hashlib,json
import capstone
from world_body_trace import Image
from controller293_scene import PairedController
from controller293_planner import inputs,CTX
from controller293_strategy import CONTROLLER
from controller293_native import EXEC_RANGES
from physics256_native import ACTOR,STATUS
from ai_path_vectors import literal

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--evidence',type=Path,required=True);args=ap.parse_args()
    data=(args.root/'speed.exe').read_bytes();identity='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    if hashlib.sha256(data).hexdigest()!=identity:raise ValueError('unsupported executable')
    im=Image(data);pair=PairedController(im,inputs(args.root),event_rides=True);m=pair.m
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    for a,b in list(EXEC_RANGES)+[(0x458bd0,0x458cbb),(0x458cc0,0x458d81),(0x462840,0x4629dc),(0x4629e0,0x4629ff),
        (0x44bad0,0x44baef),(0x4216f0,0x421740),(0x435ca0,0x435d30),(0x5a53c0,0x5a5503),(0x5a6f50,0x5a6f7a)]:
        m.instructions.update({i.address:i.size for i in md.disasm(im.read(a,b-a),a)})
    base=m.get(0x736150);count=m.get(0x736154);world=m.get(0x7361f8)
    config=m.get(ACTOR+0x14);difficulty=max(0,min(9,m.get(config+0x735,'b')))
    probabilities=[m.get(0x779ff8+difficulty*0x54+k*4,'f') for k in range(3)]
    gate=m.get(m.get(0x736174)+0xb8,'h');role=m.get(config+4)
    events=[]
    for j in range(count):
        a=base+j*32;events.append([m.get(a,'f'),m.get(a+4,'H')]+[m.get(a+k,'B') for k in (8,9,11,13,15)])
    lines=['// Original event0 decisions458BD0/462840 and topology5A53C0.',
           'const AiRoadEvent original_events[]={']
    for e in events:lines.append('{'+literal(e[0])+','+','.join(map(str,e[1:]))+'},')
    lines.append('};');lines.append('const Decision decisions[]={');rows=[];steps=[]
    def prepare(direction,seed,held):
        m.put(0x78a2fc,direction);m.put(world+0x2d0,seed)
        m.put(STATUS+0x10,500000 if held else 0)
        m.put(0x73ad34,500000);m.uc.mem_write(CONTROLLER+0xc,bytes(32));m.put(CONTROLLER+0x478,0xffffffff)
        m.run(0x435ca0,(STATUS,ACTOR),budget=300000);hold=bool(m.reg('eax')&255)
        return [direction,role,gate,m.get(STATUS+0x14,'i')*.00025,hold]
    def state():return [m.get(CONTROLLER+0xc+4*k) for k in range(8)]
    for held in (False,True):
      for direction in (0,1):
       for seed in (1,0x3fffffff,0x40000000,0x60000000,0x7ffffffe):
        for j in range(count):
            config_input=prepare(direction,seed,held)
            m.run(0x458bd0,(base+j*32,),edi=CONTROLLER,budget=300000)
            take=m.reg('eax')&255;rng=m.get(world+0x2d0);cache=state();marker=m.get(CONTROLLER+0x478,'i')
            m.run(0x458bd0,(base+j*32,),edi=CONTROLLER,budget=300000)
            if take!=(m.reg('eax')&255) or rng!=m.get(world+0x2d0) or cache!=state():raise ValueError('cache replay changed')
            d,r,g,t,h=config_input
            lines.append('{%d,{%d,%d,%d,%s,%s,{%s}},0x%08Xu,%s,0x%08Xu,{%s},%d},'%(j,d,r,g,literal(t),str(h).lower(),
                ','.join(map(literal,probabilities)),seed,str(bool(take)).lower(),rng,','.join('0x%08Xu'%v for v in cache),marker))
            rows.append(dict(event=j,input=config_input,probabilities=probabilities,seed=seed,take=take,rng=rng,cache=cache,marker=marker))
    lines.append('};');lines.append('const Topology topology[]={')
    chain=m.get(CTX+0x3a8);scratch=0x1150300
    for route_id in range(128):
        route=m.get(0x77af30+4*route_id)
        if not route:continue
        for index in range(m.get(route+0x10,'h')):
            point=route+0x27c+index*0x88;ids=[m.get(point+k,'B') for k in (0x81,0x82)]
            if ids==[255,255] and index not in (0,m.get(route+0x10,'h')-1):continue
            config_input=prepare(0,0x60000000,False)
            for k,v in enumerate((route,point,index,0)):m.put(scratch+4*k,v)
            m.run(0x5a53c0,(chain,scratch,scratch+4,scratch+8,scratch+12,CONTROLLER),budget=300000)
            advanced=bool(m.reg('eax')&255);dest=m.get(scratch);destination=m.get(dest+0xa,'h')
            output=[destination,m.get(scratch+8),bool(m.get(scratch+12,'B'))]
            d,r,g,t,h=config_input
            lines.append('{%d,%d,%d,%s,{%d,%d},{%d,%d,%d,%s,%s,{%s}},0x60000000u,%s,{%d,%d},%d,%s,0x%08Xu,{%s},%d},'%(
                route_id,index,m.get(route+0x10,'h'),str(bool(m.get(route+0x15,'B'))).lower(),*ids,d,r,g,literal(t),str(h).lower(),
                ','.join(map(literal,probabilities)),str(advanced).lower(),destination,output[1],m.get(dest+0x10,'h'),str(output[2]).lower(),
                m.get(world+0x2d0),','.join('0x%08Xu'%v for v in state()),m.get(CONTROLLER+0x478,'i')))
            steps.append(dict(route=route_id,point=index,events=ids,advanced=advanced,output=output))
    lines.append('};');lines.append('const Expiry expiry[]={');expired=[]
    length=m.get(0x7372b0,'f');threshold=m.get(0x6b753c,'f')
    circuit=m.get(0x7372b8)>0 and bool(m.get(m.get(0x7372bc)+1,'B'))
    for direction in (0,1):
      for progress in (0.,100.,1000.,2000.,3000.,length):
        prepare(0,1,False);m.put(ACTOR+0x814,progress,'f')
        before=[(j*7<<2)|3 for j in range(8)]
        for k,value in enumerate(before):m.put(CONTROLLER+0xc+4*k,value)
        m.run(0x458cc0,(direction,),eax=CONTROLLER,budget=300000)
        after=state()
        lines.append('{%s,%s,%s,%s,%s,{%s},{%s}},'%(literal(progress),literal(length),str(circuit).lower(),
            str(direction==0).lower(),literal(threshold),','.join(str(v)+'u' for v in before),','.join(str(v)+'u' for v in after)))
        expired.append(dict(progress=progress,length=length,circuit=circuit,same_direction=direction==0,threshold=threshold,before=before,after=after))
    lines.append('};')
    args.output.write_text('\n'.join(lines)+'\n',encoding='ascii')
    args.evidence.write_text(json.dumps(dict(executable_sha256=identity,events=events,decisions=rows,topology=steps,expiry=expired),indent=2)+'\n',encoding='ascii')
    print('original decisions',len(rows),'topology',len(steps),'cache repeats',len(rows))
if __name__=='__main__':main()

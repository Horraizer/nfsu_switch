"""Bounded original effect wiring/RNG comparison; supplied backend feedback.

Actual651150/6511B0 execute with fresh SND configuration seed0.
No RNG result is hooked. No live PCM/backend equivalence is claimed.
Retail inputs are read-only. Outputs contain controls, never sample data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import struct
from audio312_native import BankGraph,u32,x
from hud254_trace import PE

def check_interleaved(image,root,probe,out):
    # Same supplied control caller: two SND modules, B recreated at tick60.
    # Native machines exchange only original shared generator input state;
    # actual6511B0 executes, and neither RNG values nor decisions are hooked.
    path=root/'Sound/SKIDS/SKID_PAV_MB.abk';bank=path.read_bytes()
    before=(path.stat().st_size,path.stat().st_mtime_ns)
    params=[32766,32766,32766,1024,512,256,0,0,4096,4096,4096,10,60,0]
    def simulate(private):
        rows=[];prefixes=[]
        def make():
            state=dict(graph=BankGraph(image,bank),retired=False)
            g=state['graph'];g.m.allowed.append((0x651150,0x651255))
            def player(m):
                n=m.ri(m.uc.reg_read(x.UC_X86_REG_ESP)+4);t=m.ri(n+4)
                index=max(0,min(struct.unpack('<i',m.uc.mem_read(n+20,4))[0],m.ri(t)-1))
                v=[m.ri(t+8+12*index),m.ri(n+24),4096,32767,0]
                for j in range(m.uc.mem_read(n+14,1)[0]):
                    key=m.ri(n+28+12*j);value=struct.unpack('<i',m.uc.mem_read(n+36+12*j,4))[0]
                    if key==0:v[2]=value
                    elif key==2:v[3]=value
                    elif key==3:v[4]=value
                rows[-1].append(v);m.return_stub(v[1])
            def deletion(m):
                n=m.ri(m.uc.reg_read(x.UC_X86_REG_ESP)+4);state['retired']|=bool(m.ri(n+12));m.return_stub()
            g.m.callbacks[0x64d860]=player;g.m.callbacks[0x64c690]=deletion
            return state
        actors=[make(),make()]
        actors[0]['graph'].m.run(0x651150,(0,))
        initial=bytes(actors[0]['graph'].m.uc.mem_read(0x713fcc,24))
        shared=initial;private_states=[initial,initial]
        for tick in range(120):
            if tick==60:
                actors[1]=make()
                if private:private_states[1]=initial
            for actor,state in enumerate(actors):
                if state['retired']:continue
                g=state['graph'];g.m.uc.mem_write(0x713fcc,private_states[actor]if private else shared)
                for i,value in enumerate(params):g.m.wi(g.base+g.template+0x40+4*i,value)
                rows.append([]);prefixes.append(str(tick)+':'+str(actor))
                g.m.run(g.base+g.start,(g.base+g.template+24,))
                shared=bytes(g.m.uc.mem_read(0x713fcc,24));private_states[actor]=shared
        return prefixes,rows
    prefixes,rows=simulate(False);wrong_prefixes,wrong=simulate(True)
    assert prefixes==wrong_prefixes and rows!=wrong,'interleaved RNG case does not discriminate private state'
    command=[str(probe.resolve()),'--alternating',str(path)]+list(map(str,params))
    process=subprocess.run(command,capture_output=True,text=True)
    (out/'interleaved-port.txt').write_text(process.stdout+process.stderr,encoding='ascii')
    parsed=[];actual_prefixes=[]
    if process.returncode==0:
        for line in process.stdout.splitlines():
            parts=line.split();actual_prefixes.append(parts[0]);parsed.append([list(map(int,v.split(',')))for v in parts[1:]])
    differences=[i for i,(a,b)in enumerate(zip(rows,parsed))if a!=b]
    ok=process.returncode==0 and actual_prefixes==prefixes and len(parsed)==len(rows) and not differences
    (out/'interleaved-native.json').write_text(json.dumps(rows),encoding='ascii')
    assert before==(path.stat().st_size,path.stat().st_mtime_ns) and bank==path.read_bytes()
    return dict(bank='SKIDS/SKID_PAV_MB.abk',shared_native_rng=True,module_replaced_at=60,private_state_negative_control=True,
                rows=len(rows),ok=ok,differences=differences[:10])

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True)
    ap.add_argument('--probe',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    image_bytes=(args.root/'speed.exe').read_bytes()
    assert hashlib.sha256(image_bytes).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    image=PE(image_bytes);results=[]
    cases=[('NOS/Nitrous_00_MB.abk',[0,22000,4096,0,0]),
           ('NOS/Nitrous_01_MB.abk',[0,22000,4096,0,0]),
           ('CAR_2/ENG_UG_1/SHIFTING_MED_MB.abk',[1,22000,4096,0,0,0]),
           ('CAR_2/ENG_UG_1/SHIFTING_MED_MB.abk',[2,22000,4096,0,0,0])]
    #529100 copies the real player POV2 to the last shift-bank parameter.
    for upgrade,name in [(0,'SML'),(1,'MED'),(2,'MED'),(3,'LRG')]:
        for direction in [1,2]:cases.append((f'CAR_2/ENG_UG_{upgrade}/SHIFTING_{name}_MB.abk',[direction,22000,4096,0,0,1]))
    for variant in [0,1,2,5,6,7]:
        cases.append(('IG_GLOBAL/ENV_COMMON_MB.abk',[variant,22000,4096,32768,0]))
    for upgrade,name in [(0,'SML'),(3,'LRG')]:
        for direction in [1,2]:cases.append((f'CAR_2/ENG_UG_{upgrade}/SHIFTING_{name}_MB.abk',[direction,22000,4096,0,0,0]))
        cases.append((f'CAR_2/ENG_UG_{upgrade}/SHIFTING_{name}_MB.abk',[1,20999,4096,0,1,0]))
        cases.append((f'CAR_2/ENG_UG_{upgrade}/SHIFTING_{name}_MB.abk',[0,20999,4096,0,0,0]))
    for level in (2,3):
        for variant in range(5):
            cases.append((f'CAR_1/ENG_UG_{level}/Sparkchatter_01.abk',[variant,18000,4096,32768,512,7500,60,0]))
    for name in ['SKID_PAV_MB.abk','SKID_PAV2_MB.abk']:
        for slip in [0,128,512,1024]:
            cases.append(('SKIDS/'+name,[32766,32766,32766,slip,slip//2,slip//3,0,0,4096,4096,4096,10,60,0]))
    for intensity in [0,1,20,64,127]:
        cases.append(('IG_GLOBAL/IMPACT_MB.abk',[22000,4096,32768,0,intensity]))
    for path in sorted((args.root/'Sound/TURBO').glob('TURBO_*_MB.abk')):
        for mode in (0,1,2,3):
            for rpm in (0,7000):cases.append(('TURBO/'+path.name,[mode,22000,4095,32768,512,rpm]))
    # Cardinal/edge pans discriminate a dropped bank parameter from center.
    for pan in (1,16384,49152,65535):
        cases.extend([('NOS/Nitrous_00_MB.abk',[0,22000,4096,pan,0]),
                      ('CAR_2/ENG_UG_1/SHIFTING_MED_MB.abk',[1,22000,4096,pan,0,0]),
                      ('IG_GLOBAL/ENV_COMMON_MB.abk',[0,22000,4096,pan,0]),
                      ('TURBO/TURBO_00_MB.abk',[0,22000,4095,pan,512,7000])])
    for case,(name,params) in enumerate(cases):
        path=args.root/'Sound'/name;bank=path.read_bytes();before=(path.stat().st_size,path.stat().st_mtime_ns)
        graph=BankGraph(image,bank);rows=[];retired=[False]
        graph.m.allowed.append((0x651150,0x651255));graph.m.run(0x651150,(0,))
        def player(m):
            n=m.ri(m.uc.reg_read(x.UC_X86_REG_ESP)+4);t=m.ri(n+4)
            index=max(0,min(struct.unpack('<i',m.uc.mem_read(n+20,4))[0],m.ri(t)-1))
            v=[m.ri(t+8+12*index),m.ri(n+24),4096,32767,0]
            for j in range(m.uc.mem_read(n+14,1)[0]):
                key=m.ri(n+28+12*j);value=struct.unpack('<i',m.uc.mem_read(n+36+12*j,4))[0]
                if key==0:v[2]=value
                elif key==2:v[3]=value
                elif key==3:v[4]=value
            rows[-1].append(v);m.return_stub(v[1])
        def deletion(m):
            n=m.ri(m.uc.reg_read(x.UC_X86_REG_ESP)+4)
            retired[0]|=bool(m.ri(n+12));m.return_stub()
        graph.m.callbacks[0x64D860]=player
        graph.m.callbacks[0x64C690]=deletion
        param_offset=graph.template+(0x2c if ('SHIFTING' in name or 'Sparkchatter' in name) else 0x40)
        for tick in range(120):
            for i,p in enumerate(params):graph.m.wi(graph.base+param_offset+4*i,p)
            rows.append([]);graph.m.run(graph.base+graph.start,(graph.base+graph.template+24,))
            if retired[0]:break
        process=subprocess.run([str(args.probe.resolve()),str(path)]+list(map(str,params)),capture_output=True,text=True)
        (args.out/f'case{case:02}-port.txt').write_text(process.stdout+process.stderr,encoding='ascii')
        parsed=[]
        if process.returncode==0:
            for line in process.stdout.splitlines():parsed.append([list(map(int,v.split(','))) for v in line.split()[1:]])
        differences=[i for i,(a,b) in enumerate(zip(rows,parsed)) if a!=b]
        ok=process.returncode==0 and len(parsed)==len(rows) and not differences
        results.append(dict(bank=name,sha256=hashlib.sha256(bank).hexdigest(),parameters=params,
            ticks=len(rows),ok=ok,differences=differences[:10]))
        (args.out/f'case{case:02}-native.json').write_text(json.dumps(rows),encoding='ascii')
        assert before==(path.stat().st_size,path.stat().st_mtime_ns) and bank==path.read_bytes()
    results.append(check_interleaved(image,args.root,args.probe,args.out))
    (args.out/'report.json').write_text(json.dumps(results,indent=2),encoding='ascii')
    print(json.dumps(results,indent=2))
    return 0 if all(r['ok'] for r in results) else 1

if __name__=='__main__':raise SystemExit(main())

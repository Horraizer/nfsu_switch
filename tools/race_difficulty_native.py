"""PC1.4 USA adaptive levels and human-history witnesses; no semantic hooks."""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
from hud254_trace import PE
from physics256_native import Machine,bits,STACK
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EIP


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('root','probe','out'):ap.add_argument('--'+key,type=Path,required=True)
    args=ap.parse_args();args.out.mkdir(exist_ok=True)
    bun=(args.root/'Global/GLOBALB.BUN').read_bytes();exe=(args.root/'speed.exe').read_bytes()
    assert hashlib.sha256(exe).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
    m=Machine(PE(exe));m.allowed=[(0x57f2b0,0x57f6f2),(0x57f700,0x57f94d),(0x575960,0x576450),
      (0x581b30,0x581d22),(0x46d1c0,0x46d384),(0x583ea0,0x583f20),(0x674898,0x674920),(0x4387e0,0x4387f6)]
    table=0x1110000;m.uc.mem_write(table,bun[1678864:1791424]);m.put(0x734588,table)
    names=[bun[1678864+i*3216:1678864+i*3216+32].split(b'\0')[0].decode('ascii') for i in range(35)]
    def ride(at,name,levels):
        m.uc.mem_write(at,bytes(0x900));m.put(at,names.index(name));m.put(at+0x410,64);m.put(at+0x420,at+0x500)
        for i,v in enumerate(levels):m.put(at+8+4*i,v)
        m.run(0x57f2b0,(0,),edi=at,budget=3000000)
    records=[];requests=[];expected=[]
    pairs=[('CIVIC','CIVIC'),('CIVIC','NEON'),('CIVIC','GOLF'),('CIVIC','PEUGOT'),('NEON','CIVIC'),
      ('GOLF','MIATA'),('PEUGOT','FOCUS'),('INTEGRA','ECLIPSE'),('INTEGRA','SUPRA'),('INTEGRA','CELICA')]
    for player,rival in pairs:
      for level in (0,1,3):
        print('NATIVE_ADAPTIVE',player,rival,level,flush=True)
        a,b=0x1100000,0x1101000;ride(a,player,[level]*11);ride(b,rival,[0]*11)
        reference=m.get(a+0x428);category=m.get(a+0x424,'B')
        m.run(0x57f700,(b,a,level,category,1,bits(5.)),end=0x57f94d,budget=30000000)
        levels=[m.get(b+8+4*i) for i in range(11)];rating=m.get(b+0x428)
        requests.append(player+' '+rival+' '+str(level));expected.append([*levels,reference,rating,*[m.get(b+at) for at in (0x2c4,0x2c8,0x2cc)]])
        records.append(dict(player=player,rival=rival,level=level,levels=levels,reference_rating_bits=reference,rival_rating_bits=rating))
    assert not m.events
    run=subprocess.run([str(args.probe.resolve()),str(args.root),'difficulty'],input='\n'.join(requests)+'\n',capture_output=True,text=True)
    (args.out/'adaptive-port-r1.log').write_text(run.stdout+run.stderr,encoding='ascii')
    actual=[list(map(int,line.split()[4:])) for line in run.stdout.splitlines() if line.startswith('ADAPTIVE ')]
    passed=run.returncode==0 and len(actual)==len(expected) and actual==expected
    mismatches=[dict(index=i,expected=e,actual=actual[i] if i<len(actual) else None) for i,e in enumerate(expected) if i>=len(actual) or actual[i]!=e]
    (args.out/'adaptive-native-r1.json').write_text(json.dumps(dict(cases=records,passed=passed,mismatches=mismatches,hooks=m.events,
      scope='Original57F2B0/57F700 under007F; physical levels/rating before final cosmetic580590 and forced category; supplied player and rival car/levels.'),indent=2)+'\n',encoding='ascii')
    print('PASS' if passed else 'FAIL',len(expected),'adaptive original cases; mismatches',len(mismatches))
    # Human performance history: original timer, ratio producer and ring buffer.
    m=Machine(PE(exe));m.allowed=[(0x4308d4,0x4308dc),(0x430778,0x4307ba),(0x43080a,0x4308d8),(0x584270,0x5842c4),(0x674898,0x674920)]
    ref,actor,history,buffer,road,point=0x1100000,0x1101000,0x1102000,0x1103000,0x1104000,0x1105000
    m.put(ref+4,actor);m.put(ref+0xa8,history);m.put(history+5,10,'B');m.put(history+0x10,buffer)
    def stop_without_sample(uc,address,size,data):
        if address==0x4308d6:m.finished=True;uc.emu_stop()
    m.uc.hook_add(UC_HOOK_CODE,stop_without_sample)
    cases=[]
    for i,(speed,limit,enabled) in enumerate(2*[(0,30,True),(15,30,True),(60,30,True),(7,0,True),(0,0,True),
      (10,30,False),(45,30,True),(30,30,True),(20,30,True),(70,30,True),(3,30,True),(31,30,True),
      (33,30,True),(12,30,True),(32,30,True),(0,30,True)]):
        m.put(0x73457c,1.,'f');m.run(0x430778,end=0x4307ba,esi=ref)
        if m.uc.reg_read(UC_X86_REG_EIP)==0x4307ba:
            m.put(actor+0x3d4,float(speed),'f');m.put(road+0x34,int(enabled),'B');m.put(point+0x6a,limit,'B')
            m.put(STACK+0x10,1.,'f');m.run(0x43080a,end=0x4308d6,edi=0,ebp=road,eax=point,esi=ref)
        cases.append([speed,limit,int(enabled),m.get(history+8),m.get(history+0xc),m.get(history+6,'B'),m.get(history+7,'B'),m.get(ref+0xac)])
    assert not m.events
    output=Path('tests/ai_catchup_history_native.inc')
    lines=['// Original430778/43080A/584270 supplied road/category; no call hooks.','static const std::uint32_t catchup_history_native[][8]={']
    lines+=['    {'+','.join(str(v)+'U' for v in row)+'},' for row in cases];lines+=['};','']
    output.write_text('\n'.join(lines),encoding='ascii')
    (args.out/'history-native-r1.json').write_text(json.dumps(dict(cases=cases,hooks=m.events),indent=2)+'\n',encoding='ascii')
    print('Captured32 original human-history samples including wrap and zero/disabled road speed')
    raise SystemExit(0 if passed else 1)

if __name__=='__main__':main()

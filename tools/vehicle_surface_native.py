"""Bounded PC1.4 USA material, wheel-force and positional-bump witnesses."""
import argparse, hashlib, json, struct
from pathlib import Path
from hud254_trace import PE
from physics256_native import Machine, bits


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--evidence',type=Path,required=True)
    args=ap.parse_args()
    data=(args.root/'speed.exe').read_bytes();bun=(args.root/'Global/GLOBALB.BUN').read_bytes()
    assert hashlib.sha256(data).hexdigest()=='c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7'
    assert hashlib.sha256(bun).hexdigest()=='39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9'
    record=bun[1736752:1736752+3216];image=PE(data);m=Machine(image)
    m.allowed=[(0x5868e0,0x586960),(0x586710,0x586762),(0x587330,0x587822),
      (0x587830,0x587a91),(0x587170,0x58721f),(0x587220,0x587330),
      (0x4387e0,0x4387f6),(0x674898,0x67490d),(0x564b10,0x564c00),
      (0x564cd0,0x564f20),(0x565230,0x565300),(0x441d80,0x441ecc),(0x5abd00,0x5abf00),(0x583ea0,0x583f18)]
    setup,owner,wheel,output,point=0x1100000,0x1101000,0x1102000,0x1103000,0x1104000
    m.uc.mem_write(setup,record[0x110:]);m.put(owner+0x3c0,0);m.put(owner+0x390,20.,'f')
    m.run(0x5868e0,(setup+0x10,setup+0xd0,setup+0x270),eax=wheel,ecx=owner)
    m.put(wheel+0x2c,.01,'f');m.put(0x6f0738,1/120,'f');m.put(0x6d4ac4,0x5abdb5)
    m.uc.mem_write(wheel+0x70,struct.pack('<16f',1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1))
    initial=bytes(m.uc.mem_read(wheel,0x200));cases=[];bumps=[];factors=[]
    trampoline=0x1105000
    m.uc.mem_write(trampoline,b'\xd9\x1d'+struct.pack('<I',output)+b'\xc3');m.allowed.append((trampoline,trampoline+7))
    for material in range(29):
        d=image.read(0x6ef2d0+material*148,148)
        factors.append([struct.unpack_from('<I',d,at)[0] for at in (0x24,0x28,0x2c,0x18,0x1c)])
        for x,y in [(0.,0.),(-2.25,3.75),(2319.48,-14.7605),(2320.16,-249.99)]:
            m.put(point,x,'f');m.put(point+4,y,'f')
            m.run(0x441d80,(material,),eax=point);m.run(trampoline)
            bumps.append([material,bits(x),bits(y),m.get(output)])
        for spin,longitudinal,lateral,torque,service,locked in [(80.,20.,5.,1.,0.,False),(0.,0.,0.,50.,0.,False),(80.,20.,5.,-20.,.8,True)]:
            m.uc.mem_write(wheel,initial)
            for at,val in [(0x28,spin),(0xf0,longitudinal),(0xf4,lateral),(0x108,torque),(0x170,service),
                           (0x174,0.),(0x158,3.),(0x178,1.),(0x17c,1.),(0x180,1.),(0x16c,0.),(0x114,0.)]:m.put(wheel+at,val,'f')
            m.put(wheel+0x150,material);m.put(wheel+0x10c,int(locked));m.put(wheel+0x155,1,'B')
            m.run(0x587330,(0,0),esi=wheel)
            m.run(0x587830,(0,),end=0x587a91,eax=wheel)
            result=[m.get(wheel+at) for at in (0x28,0x108,0x118,0x11c,0x160)]
            result += [m.get(wheel+0x10c),m.get(wheel+0x154,'B')]
            m.run(0x587170,(output,),ecx=wheel)
            result += [m.get(output+4),m.get(wheel+0x114)]
            cases.append([material,bits(spin),bits(longitudinal),bits(lateral),bits(torque),bits(service),int(locked)]+result)
    assert not m.events
    lines=['// Generated from authenticated original code; no external call hooks.','static const std::uint32_t surface_factors[][5]={']
    lines += ['    {'+','.join(str(v)+'U' for v in row)+'},' for row in factors]
    lines += ['};','static const std::uint32_t surface_bumps[][4]={']
    lines += ['    {'+','.join(str(v)+'U' for v in row)+'},' for row in bumps]
    lines += ['};','static const std::uint32_t surface_wheels[][16]={']
    lines += ['    {'+','.join(str(v)+'U' for v in row)+'},' for row in cases]
    lines += ['};','']
    args.output.write_text('\n'.join(lines),encoding='ascii')
    args.evidence.write_text(json.dumps({'exe_sha256':hashlib.sha256(data).hexdigest(),'factors':factors,'bumps':bumps,'wheels':cases,'hooks':m.events,'scope':'Original441D80,587330,587830 through587A91,587170; supplied stock240SX owner/wheel inputs, no whole-road/trajectory claim'},indent=2)+'\n',encoding='ascii')
    print('PASS original29 material rows,116 positional bumps,87 wheel force/derivative cases,0 call hooks')

if __name__=='__main__':main()

"""Issue 251: execute the pinned retail steering functions in an isolated CPU.

Requires Unicorn 2 and a built nfsu_steering_probe. No retail process is
launched; PE imports, allocation and OS access are forbidden. Output must
be a fresh scratch directory outside the retail root. Numeric contexts are
synthetic, not a retail driving replay or a claim of whole-car parity.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
from handling_measure import manifest

EXE_SHA = 'C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('root', 'probe', 'out'):
        parser.add_argument('--'+key, type=Path, required=True)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.out.resolve()
    if out == root or root in out.parents:
        parser.error('output must be outside retail root')
    out.mkdir(parents=True, exist_ok=False)
    before = manifest(root)
    (out/'before.json').write_bytes(before)
    try:
        compare(root, out, args.probe.resolve())
    finally:
        after = manifest(root)
        (out/'after.json').write_bytes(after)
        if before != after:
            raise RuntimeError('retail tree changed')
        print('PASS retail manifests identical')


def compare(root, out, probe):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn import x86_const as x
    raw = (root/'speed.exe').read_bytes()
    if hashlib.sha256(raw).hexdigest().upper() != EXE_SHA:
        raise ValueError('unsupported speed.exe SHA-256')
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    if raw[pe:pe+4] != b'PE\0\0':
        raise ValueError('not PE')
    count = struct.unpack_from('<H', raw, pe+6)[0]
    optsize = struct.unpack_from('<H', raw, pe+20)[0]
    base = struct.unpack_from('<I', raw, pe+24+28)[0]
    size = struct.unpack_from('<I', raw, pe+24+56)[0]
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(base, (size+4095)&~4095)
    for k in range(count):
        at = pe+24+optsize+40*k
        _, va, length, offset = struct.unpack_from('<IIII', raw, at+8)
        if length:
            u.mem_write(base+va, raw[offset:offset+length])
    heap, stack, shim = 0x10000000, 0x20000000, 0x30000000
    u.mem_map(heap, 0x10000); u.mem_map(stack, 0x10000); u.mem_map(shim, 0x1000)
    u.mem_write(shim, b'\xd9\x1d'+struct.pack('<I', heap+0xf000)+b'\x90')
    reached = set()
    observed = {}

    def rf(at): return struct.unpack('<f', u.mem_read(at, 4))[0]
    def wf(at, v): u.mem_write(at, struct.pack('<f', v))
    def wi(at, v): u.mem_write(at, struct.pack('<I', v&0xffffffff))
    def st0():
        top = (u.reg_read(x.UC_X86_REG_FPSW)>>11)&7
        mantissa, exponent = u.reg_read(x.UC_X86_REG_FP0+top)
        sign = -1 if exponent&0x8000 else 1
        return sign*mantissa*2.0**((exponent&0x7fff)-16383-63)

    # These are verified function boundaries, not a sweep across padding.
    allowed = [(0x460930,0x46096d),(0x460970,0x460a76),
               (0x460a80,0x460dd8),(0x460de0,0x460e66),
               (0x4607a0,0x4607d4),(0x464ed0,0x464f11),
               (0x580240,0x5802bc),(0x583ea0,0x583f18),
               (0x583f20,0x583fe7),(0x5844b0,0x5844fa),
               (0x584500,0x58460e),(0x564cd0,0x564da8),(0x564db0,0x564ed0),
               (0x4387e0,0x4387f6),(0x674898,0x674910),
               (shim,shim+7)]

    def hook(cpu, address, length, _):
        if not any(a <= address and address+length <= b for a, b in allowed):
            raise RuntimeError('execution outside allowlist: %08X' % address)
        reached.add(address)
        sp = cpu.reg_read(x.UC_X86_REG_ESP)
        if address == shim+6:
            cpu.emu_stop()
        elif address == 0x4609e3:
            observed['target'] = rf(sp+0x14)
        elif address == 0x4609f4:
            observed['rate'] = st0()
        elif address == 0x460c24:
            observed['shift'] = rf(sp+0xc)
    u.hook_add(UC_HOOK_CODE, hook)

    def call(address, args=(), regs=(), floating=True):
        sp = stack+0xf000
        u.reg_write(x.UC_X86_REG_ESP,sp)
        wi(sp,shim if floating else shim+6)
        for k, v in enumerate(args): wf(sp+4*(k+1),v)
        for reg, value in regs: u.reg_write(reg,value)
        u.reg_write(x.UC_X86_REG_FPCW,0x37f)
        u.reg_write(x.UC_X86_REG_FPSW,0)
        u.reg_write(x.UC_X86_REG_FPTAG,0xffff)
        u.emu_start(address,0,count=100000)
        if u.reg_read(x.UC_X86_REG_EIP) != shim+6:
            raise RuntimeError('instruction budget exhausted')
        return rf(heap+0xf000) if floating else u.reg_read(x.UC_X86_REG_EAX)

    table = bytearray()
    for va in (0x705f48,0x705f5c,0x705f70,0x705f84,0x705f98,0x705fac,0x705f20,0x705f34):
        n, _, _, _, ptr = struct.unpack('<IfffI',u.mem_read(va,20))
        table.extend(u.mem_read(ptr,n*4))
    ptr, n = struct.unpack('<II',u.mem_read(0x705fe0,8))
    table.extend(u.mem_read(ptr,n*8))
    table_hash = hashlib.sha256(table).hexdigest().upper()

    control, car, inputs, setup, engine, handling = [heap+k*0x1000 for k in range(6)]
    def reset():
        u.mem_write(heap, bytes(0xe000))
        wi(control,car); wi(car+0x30,inputs); wi(car+0x20,setup)
        wi(car+0x3c,engine); wi(car+0x10,handling)
        wf(control+0x74,45)
        # 0x460E70 -> 0x584360/0x584180 constructor layout, no allocator stub.
        for off, capacity, window, values, times in (
                (8,33,.55,heap+0x6000,heap+0x6200),
                (0x3c,9,.15,heap+0x6400,heap+0x6600)):
            u.mem_write(control+off+4,bytes([4,capacity,0,0]))
            wi(control+off+0x10,values); wf(control+off+0x28,window)
            wi(control+off+0x30,times)
        u.mem_write(0x78a344,b'\0'); u.mem_write(0x78a345,b'\0')
        for at in (0x7361a8,0x7361a4,0x735fa0): wi(at,0)

    contexts, expected, names = [], [], []
    def sample(name, raw_input, tick, speed=25, slip=0, throttle=0, handbrake=0, brake=0,
               elapsed=1, multiplier=1, mode=0, predicate=0, dt=1/120):
        wf(car+0x3d0,speed); wf(car+0x70,abs(speed)); wf(car+0x74,0); wf(car+0x78,0)
        heading = call(0x564db0,[abs(speed),0],floating=False)&0xffff
        wi(car+0x3b4,(heading+int(slip*65536/360))&0xffff)
        native_slip = call(0x464ed0,regs=[(x.UC_X86_REG_ESI,car+0x40)])
        # Keep the extended conversion as native 0x460C49 does, rounded by
        # the context interface only. The focused tests allow binary32 error.
        native_slip *= rf(0x6cc9f4)
        wi(0x73ad3c,tick); wi(0x73ad34,tick)
        u.mem_write(0x736514,bytes([mode])); wi(0x735fa0,predicate)
        wf(inputs+0x1dc,throttle); wf(inputs+0x1e0,handbrake); wf(inputs+0x1e4,brake)
        wf(setup+0x270,multiplier)
        # Supply a cached predicted maximum as an explicit context input.
        # This tests steering's consumer, not 0x57F2B0's engine predictor.
        wf(handling+0x414,50); wf(handling+0x418,50)
        wi(handling+0x410,1); wi(handling+0x420,heap+0x6800)
        wf(handling+0x170,1); wf(handling+0x2c0,1)
        u.mem_write(handling+0x424,b'\0')
        wi(engine+0x14c,tick-round(elapsed/float(rf(0x6cca38))) if elapsed<1 else 0)
        observed.clear()
        word = call(0x460930,[raw_input,dt,0],[(x.UC_X86_REG_ESI,control)],False)&0xffff
        signed = word if word < 32768 else word-65536
        actual_elapsed = (tick-struct.unpack('<I',u.mem_read(engine+0x14c,4))[0])*rf(0x6cca38)
        seconds = tick*rf(0x6cca38)
        contexts.append('step '+' '.join(str(v) for v in [raw_input,seconds,dt,speed,abs(speed),
            native_slip,throttle,handbrake,brake,multiplier,50,actual_elapsed if elapsed<1 else 1,mode,predicate]))
        expected.append([rf(control+0x14),rf(control+0x48),rf(control+0x74),observed['target'],
                         observed.get('rate',0),signed*360/65536,observed.get('shift',0),word,
                         u.mem_read(control+4,1)[0]])
        names.append(name)

    # Fresh samples cross each limit/assist/device/mode branch without history
    # from another case. Sustained sequences exercise wrapping and expiration.
    for speed in (0,-1,1,15,25,35,160):
        for raw_input in (-1,-.3,0,.3,1):
            for mode in (0,1):
                reset(); contexts.append('reset')
                sample('speed-lock',raw_input,10000,speed=speed,mode=mode)
    for predicate in (0,1):
        for slip in (-40,-8,-5,0,5,8,40):
            for speed in (13,14,17,18,35):
                reset(); contexts.append('reset')
                for k in range(3): sample('assist',.7,10000+67*k,speed,slip,predicate=predicate)
    for elapsed in (0,.1,.2,.4,.5,.7,.999,1):
        for speed in (15,25,35,45):
            for controls in ((0,0,0),(1,0,0),(0,.7,0),(0,0,1)):
                reset(); contexts.append('reset')
                sample('shift',.3,10000,speed,elapsed=elapsed,throttle=controls[0],handbrake=controls[1],brake=controls[2])
    for mode in (0,1):
        for predicate in (0,1):
            reset(); contexts.append('reset')
            for k in range(240):
                raw_input = 0 if k<15 or k>=180 else .7 if k<90 else -.3
                sample('turn-release-reversal',raw_input,10000+33*k,speed=25,
                       elapsed=(k-70)/120 if 70<=k<190 else 1,mode=mode,predicate=predicate)
    # An irregular step produces actual expiry, unlike a full ring at 120 Hz.
    reset(); contexts.append('reset')
    for k, tick in enumerate((10000,10067,10200,10600,12200,16000,16067)):
        sample('history-expiry',.4 if k<4 else 0,tick,dt=1/60)
    reset(); contexts.append('reset')
    for k in range(120):
        sample('mode-transitions',.6 if k<70 else -.6,10000+67*k,speed=18,
               slip=8 if k<80 else -8,mode=(k//30)%2,predicate=(k//20)%2)
    for raw_input in (.001,.003,.016,.031,.05,1/45-1e-7,1/45,1/45+1e-7):
        for sign in (-1,1):
            for mode in (0,1):
                for dt in (1/120,1/60):
                    reset(); contexts.append('reset')
                    sample('small-input-and-snap',sign*raw_input,10000,speed=15,mode=mode,dt=dt)
    for multiplier in (.3,2):
        for sign in (-1,1):
            reset(); contexts.append('reset')
            sample('multiplier-and-asymmetric-cap',sign,10000,speed=15,mode=1,multiplier=multiplier)
    source = '\n'.join(contexts)+'\n'
    (out/'contexts.txt').write_text(source,encoding='ascii')
    result = subprocess.run([str(probe)],input=source.encode('ascii'),stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,check=False)
    (out/'port.txt').write_bytes(result.stdout)
    lines = result.stdout.decode('ascii').splitlines()
    if result.returncode or not lines or lines[0] != 'tables '+table_hash or 'ERROR' in result.stdout.decode('ascii'):
        raise RuntimeError('probe failed or table bytes differ')
    got = [[float(v) for v in line.split()[1:]] for line in lines[1:] if line.startswith('step ')]
    if len(got)!=len(expected): raise RuntimeError('missing output rows')
    if len(lines)!=len(expected)+1 or any(len(row)!=9 or not all(math.isfinite(v) for v in row)
                                         for row in got+expected):
        raise RuntimeError('invalid or nonfinite numeric rows')
    # Retail uses x87 extended intermediates. Port uses binary64 with explicit
    # binary32 stores; accept one angle word and small float roundoff, never
    # a missing output, stale artifact, wrong assist branch or broad tolerance.
    tolerances = [2e-6,2e-5,2e-4,2e-4,2e-3,360/65536+1e-8,2e-6,1,0]
    maxima = [0.0]*9
    failures = []
    for k,(a,b) in enumerate(zip(expected,got)):
        for column,(av,bv,tol) in enumerate(zip(a,b,tolerances)):
            delta = abs(av-bv)
            if column==7: delta=min(delta,65536-delta)
            maxima[column]=max(maxima[column],delta)
            if delta>tol: failures.append([k,names[k],column,av,bv,delta])
    evidence = dict(exe_sha256=EXE_SHA,table_sha256=table_hash,rows=len(expected),
                    max_errors=maxima,tolerances=tolerances,failures=failures,
                    reached_instruction_count=len(reached),native=expected,names=names)
    (out/'comparison.json').write_text(json.dumps(evidence,indent=2)+'\n',encoding='ascii')
    if failures: raise RuntimeError('%d comparison failures; see comparison.json' % len(failures))
    print('PASS native-vs-port steering %d rows; max-errors %s' % (len(expected),maxima))


if __name__ == '__main__':
    main()

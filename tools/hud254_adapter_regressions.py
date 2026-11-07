"""Original-instruction regressions for depth precision and FNTF indirection.

Only allocation/submission boundaries are stubbed. Arithmetic and glyph lookup
execute pinned PE instructions on synthetic Unicorn memory, never retail files.
"""
import struct

from hud254_native import Machine
from hud254_presentation_native import Renderer
from hud254_trace import chunks


def depth_native(image, pairs, drain=False):
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EDX,
                                  UC_X86_REG_ESI, UC_X86_REG_EIP, UC_X86_REG_ESP)
    m = Machine(image)
    m.write(0x7349B4,0x1001000); m.write(0x7349B8,0)
    alloc = [0x1006000]
    buckets, submitted = [], []
    for index in range(256):
        at = 0x746118+8*index
        m.write(at,at); m.write(at+4,at)
    def hook(uc, at, size, unused):
        if at == 0x4A9900:
            buckets.append((uc.reg_read(UC_X86_REG_ESI)-0x746118)//8)
            return  # Original append executes; capture does not change it.
        if at not in (0x567160,0x4F2CB0):
            return
        sp = uc.reg_read(UC_X86_REG_ESP)
        if at == 0x567160:
            uc.reg_write(UC_X86_REG_EAX,alloc[0]); alloc[0] += 16
        else:
            submitted.append((uc.reg_read(UC_X86_REG_EAX)-0x1002000)//256)
        uc.reg_write(UC_X86_REG_EIP,m.integer(sp))
        uc.reg_write(UC_X86_REG_ESP,sp+4)
    m.uc.hook_add(UC_HOOK_CODE,hook)
    for i,(depth,parent) in enumerate(pairs):
        obj = 0x1002000+256*i
        m.write(obj+0x2C,obj+0x80)
        m.uc.mem_write(obj+0x20,struct.pack('<H',1))
        m.uc.mem_write(obj+0xA4,struct.pack('<f',depth))
        m.uc.mem_write(0x1008000+0xD0+0x68,struct.pack('<f',parent))
        m.run(0x4F2FD0,registers={UC_X86_REG_EAX:obj,UC_X86_REG_EDX:0x1008000})
    if drain:
        m.run(0x4F2EB0,(0,),{UC_X86_REG_EAX:0x1008000})
    if len(buckets) != len(pairs) or (drain and len(submitted) != len(pairs)):
        raise AssertionError('original queue capture incomplete')
    return buckets,submitted


def regressions(image, language, report, probe, commands, run, close):
    pairs = [(100,struct.unpack('<f',struct.pack('<I',0x3F7FFFFF))[0]),
             (100,-.0000001),(255,-.0000001),(-5,0),(0,0),(300,0)]
    # Both sides of x87 rounding ties, cancellation and integer boundaries.
    for depth in (1,2,100,255,256,-1,-100,2147483520,-2147483520):
        for parent in (-1e-7,1e-7,-1e-15,1e-15,-1e-18,1e-18,0,-depth):
            pairs.append((depth,parent))
    for power in range(50,70):
        for sign in (-1,1):
            pairs.append((100,sign*2.0**-power))
    expected,_ = depth_native(image,pairs)
    lines = run(probe,['d %s %s' % pair for pair in pairs])
    for pair,bucket,line in zip(pairs,expected,lines):
        if list(map(int,line.split())) != [1,1,bucket,1]:
            raise AssertionError('original depth mismatch for %r: %s versus %s' % (pair,line,bucket))
    outside = [(2147483648,0),(-4294967296,0),(3.402823466e38,3.402823466e38)]
    native_outside,_ = depth_native(image,outside)
    if native_outside != [0]*len(outside):
        raise AssertionError('native indefinite conversion behavior changed')
    if run(probe,['d %s %s' % pair for pair in outside]) != ['0 0 0 0']*len(outside):
        raise AssertionError('unsupported conversion domain was accepted')
    queues = [[(100,0),pairs[0]],[(255,-1e-7),(254,0),(255,0),(100,-1e-7)],
              [(100,-1e-18),(100,0),(100,-1e-15),(100,1e-15)]]
    for queue in queues:
        _,order = depth_native(image,queue,True)
        line = run(probe,['r %d %s' % (len(queue),' '.join('%s %s' % pair for pair in queue))])[0]
        if list(map(int,line.split())) != order:
            raise AssertionError('original parent-depth queue ordering mismatch')
    relocated_count = 0
    seen = set()
    for row,command in zip(report['cases'],commands):
        if 'font' not in row or row['font']['name'] in seen:
            continue
        seen.add(row['font']['name'])
        obj = next(obj for obj in report['objects'] if obj['name'] == row['name'])
        tex = row['texture']; name = row['font']['name']
        r = Renderer(image)
        r.load_font(language,name,(tex['width'],tex['height']))
        meta = next(f for f in report['font_metadata'] if f['name'].upper() == name.upper())
        at = meta['fntf_offset']
        end = next(end for kind,begin,end in chunks(language) if kind == 0x30201 and begin+512 == at)
        fntf = bytearray(language[at:end])
        old = struct.unpack_from('<I',fntf,20)[0]
        count = struct.unpack_from('<H',fntf,10)[0]
        glyphs = fntf[old:old+16*count]
        new = (len(fntf)+15)//16*16
        fntf.extend(b'\0'*(new-len(fntf))); fntf.extend(glyphs)
        struct.pack_into('<I',fntf,20,new)
        # Poison every obsolete bearing. A hard-coded offset cannot agree.
        for i in range(count):
            fntf[old+16*i+9] = 31
        r.uc.mem_write(r.chunk+520,bytes(fntf))
        r.object_fields(obj,row['text'],row.get('sj_override'),row.get('parent'))
        output = r.render_text()
        parts = command.split(); parts[-2] = fntf.hex()
        values = run(probe,[' '.join(parts)])[0].split()
        quad_count = len(output['local']['quads'])
        if list(map(int,values[:2])) != [1,0] or int(values[3]) != quad_count or len(values) != 4+32*quad_count:
            raise AssertionError('relocated font output incomplete')
        cursor = 4
        for quad,draw in zip(output['local']['quads'],output['draws']):
            for i,vertex in enumerate(draw['vertices']):
                close(list(map(float,values[cursor:cursor+3])),quad['positions'][i])
                close(list(map(float,values[cursor+3:cursor+5])),quad['uv'][i])
                close(list(map(float,values[cursor+5:cursor+7])),vertex['uv'])
                if int(values[cursor+7]) != int(vertex['color'],16):
                    raise AssertionError('relocated font color mismatch')
                cursor += 8
        relocated_count += 1
    # Bounds are an adapter contract: the original has no supplied-buffer size.
    minimal = bytearray(160); minimal[:4] = b'FNTF'
    struct.pack_into('<H',minimal,10,1); struct.pack_into('<I',minimal,12,0x40000)
    minimal[128] = minimal[144] = ord('1'); minimal[137] = 31; minimal[153] = 255
    struct.pack_into('<I',minimal,20,144)
    if run(probe,['f '+minimal.hex()]) != ['1 1 1 49 -1']:
        raise AssertionError('minimal declared glyph offset ignored')
    invalid = []
    for offset in (0,127,145,0xFFFFFFFF):
        struct.pack_into('<I',minimal,20,offset); invalid.append('f '+minimal.hex())
    if run(probe,invalid) != ['0 0 0']*len(invalid):
        raise AssertionError('out-of-buffer glyph offset accepted')
    print('PASS: original depth precision cases=%d; signed-domain refusals=%d; parent queues=%d; relocated real fonts=%d; invalid offsets=%d' %
          (len(pairs),len(outside),len(queues),relocated_count,len(invalid)))

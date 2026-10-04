"""Synthetic execution of pinned presentation producers, never the game process.

Input structures and allocator/texture/GPU boundaries are declared in reports.
The original font loader, text layout, glyph builder and quad transform execute.
"""
import struct

from hud254_native import Machine, format_native, mask_native
from hud254_trace import chunks


def u32(blob, offset=0):
    return struct.unpack_from('<I', blob, offset)[0]


def hash_name(name):
    value = 0xFFFFFFFF
    for char in name.upper():
        value = (value * 33 + ord(char)) & 0xFFFFFFFF
    return value


def language_case(image, selected):
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP
    m = Machine(image)
    m.write(0x6EE6F0, -1)
    m.write(0x73578C, 0)
    calls = []
    def hook(uc, at, size, _):
        if at not in (0x59F7C0, 0x51C5F0):
            return
        sp = uc.reg_read(UC_X86_REG_ESP)
        calls.append('%08X' % at)
        uc.reg_write(UC_X86_REG_EIP, m.integer(sp))
        uc.reg_write(UC_X86_REG_ESP, sp + (8 if at == 0x51C5F0 else 4))
    m.uc.hook_add(UC_HOOK_CODE, hook)
    m.run(0x59FA20, registers={UC_X86_REG_EAX: selected})
    actual = m.integer(0x6EE6F0)
    return dict(selected=selected, language_global=actual,
                text=format_native(image, 240000, actual), stub_calls=calls)


def permission_case(image, option, initial):
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP
    m = Machine(image)
    m.run(0x432890, registers={UC_X86_REG_EAX: 0x78A384})
    initialized = m.integer(0x78A394)
    # Synthetic first-player profile. No default profile or live event is claimed.
    for at in (0x7588D5, 0x7588D6, 0x7588D8, 0x7588DC):
        m.uc.mem_write(at, b'\x01')
    m.uc.mem_write(0x7588DA, bytes((option,)))
    m.write(0x78A394, initial)
    m.write(0x758948, 0)
    m.write(0x7361A4, 0)
    m.uc.mem_write(0x78A345, b'\0')
    m.run(0x4B47A0, registers={UC_X86_REG_EAX: 0})
    settings = m.integer(0x78A394)
    view = 0x1001000
    m.run(0x42BAC0, registers={UC_X86_REG_EAX: view + 0x20,
                             UC_X86_REG_ECX: 0x78A384})
    copied = m.integer(view + 0x30)
    state = bytes(m.uc.mem_read(view+0x20,1))[0]
    m.write(0x7361B4,view)
    toggles=[]
    def hook(uc,at,size,_):
        if at == 0x4CD610:
            sp=uc.reg_read(UC_X86_REG_ESP)
            uc.reg_write(UC_X86_REG_EAX,0)
            uc.reg_write(UC_X86_REG_EIP,m.integer(sp))
            uc.reg_write(UC_X86_REG_ESP,sp+4)
        elif at == 0x4BFE1B:
            # Stop at the documented slice end, before unrelated menu refresh.
            uc.reg_write(UC_X86_REG_EIP,m.stop)
    m.uc.hook_add(UC_HOOK_CODE,hook)
    for _ in range(2):
        m.run(0x4BFD90,(0x9120409E,),{UC_X86_REG_ECX:0x1005000})
        toggles.append(dict(view=m.integer(view+0x30),settings=m.integer(0x78A394),
                            profile=bytes(m.uc.mem_read(0x7588DA,1))[0]))
    return dict(option=option, initial=initial, initialized=initialized,
                view_state=state, settings=settings, view=copied,
                mask=mask_native(image, 4, copied, view_state=state),toggles=toggles,
                toggle_stubs=['004CD610 selects supplied player zero','004BFE1B stop before menu refresh'])


class Renderer(Machine):
    """Original local geometry with host-side allocation and submit capture."""
    def __init__(self, image):
        from unicorn import UC_HOOK_CODE
        super().__init__(image)
        self.next_alloc = 0x1080000
        self.texture = 0x1008000
        self.font = 0x1009000
        self.context = 0x100A000
        self.obj = 0x100B000
        self.sa = 0x100C000
        self.string = 0x100D000
        self.color = 0x100E000
        self.chunk = 0x1010000
        self.submissions = []
        self.draws = []
        self.material_calls = []
        self.stub_calls = []
        self.executed = set()
        self.write(0x6EE6F0, 0)
        # Select the PE's scalar matrix/vector implementations; initialization
        # normally resolves these dispatch pointers before FEng is called.
        self.write(0x6D4ACC, 0x5AC6A2)
        self.write(0x6D4AC4, 0x5ABDB5)
        self.write(0x6E7540, 0x100F000)  # FNTF callback table, all null.
        self.write(0x735704, 0x100F100)  # Synthetic descriptor allocator identity.
        self.write(0x735708, 0x100F200)  # Synthetic quad pool.
        self.write(0x73570C, 1)          # Select allocation call, not free list.
        self.write(self.context+0x10, self.context+0x10)
        self.write(self.context+0x14, self.context+0x10)
        self.write(0x736404, 0)
        self.write(0x736450, 0)
        self.write(0x73640C, 0x100F500)  # Supplied shader context, type zero.
        self.write(0x100F518, 0x100F600)
        self.write(0x73636C, 0x100F600)  # Supplied COM object/vtable.
        self.write(0x100F600, 0x100F700)
        for offset in (0x68, 0xD0, 0xE4, 0x104, 0x14C):
            self.write(0x100F700+offset, 0x10FD000+offset)
        self.write(self.texture, 0x100F900)
        self.write(0x100F918, 1)  # Non-null synthetic graphics texture handle.
        self.uc.hook_add(UC_HOOK_CODE, self.hook)

    def ret(self, cleanup=0, result=None):
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP
        sp = self.uc.reg_read(UC_X86_REG_ESP)
        if result is not None:
            self.uc.reg_write(UC_X86_REG_EAX, result)
        self.uc.reg_write(UC_X86_REG_EIP, self.integer(sp))
        self.uc.reg_write(UC_X86_REG_ESP, sp+4+cleanup)

    def floats(self, at, count):
        return list(struct.unpack('<%df' % count, self.uc.mem_read(at, count*4)))

    def hook(self, uc, at, size, _):
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_ESI, UC_X86_REG_EDI
        self.executed.add(at)
        if at == 0x567160:
            self.stub_calls.append('allocator 00567160')
            result = self.next_alloc
            self.next_alloc += 0x200
            if self.next_alloc > 0x10E0000:
                raise RuntimeError('synthetic allocator exhausted')
            self.ret(result=result)
        elif at == 0x5461C0:
            self.stub_calls.append('texture lookup 005461C0')
            self.ret(result=self.texture)
        elif at == 0x59FB80:
            self.stub_calls.append('localization 0059FB80: supplied string')
            self.ret(result=0)
        elif at == 0x514B90:
            sp = uc.reg_read(UC_X86_REG_ESP)
            descriptor = self.integer(sp+4)
            quads, cursor = [], self.integer(descriptor+0x18)
            while cursor != descriptor+0x18:
                if len(quads) >= 128:
                    raise RuntimeError('quad list exceeds bound')
                quads.append(dict(positions=[self.floats(cursor+0x20+i*16, 3) for i in range(4)],
                                  uv=[self.floats(cursor+0x60+i*12, 2) for i in range(4)],
                                  colors=['%08X' % self.integer(cursor+0x90+i*4) for i in range(4)]))
                cursor = self.integer(cursor)
            self.submissions.append(dict(matrix=self.floats(descriptor+0x20,16), quads=quads))
            # Observe, do not stub: original loop and NDC/UV conversion execute.
        elif at in (0x410550,0x410650):
            self.material_calls.append(dict(at='%08X' % at,
                esi=uc.reg_read(UC_X86_REG_ESI),edi=uc.reg_read(UC_X86_REG_EDI)))
            self.stub_calls.append('render state %08X' % at)
            self.ret()
        elif at in (0x10FD068,0x10FD0D0,0x10FD0E4,0x10FD104,0x10FD14C):
            sp = uc.reg_read(UC_X86_REG_ESP)
            if at == 0x10FD14C:
                primitive, count, vertices, stride = [self.integer(sp+n*4) for n in (2,3,4,5)]
                if (primitive,count,stride) != (6,2,32):
                    raise RuntimeError('unexpected draw contract')
                self.draws.append(dict(primitive=primitive,count=count,stride=stride,
                    vertices=[dict(position=self.floats(vertices+i*stride,3),
                                   color='%08X' % self.integer(vertices+i*stride+12),
                                   uv=self.floats(vertices+i*stride+16,2)) for i in range(4)]))
            self.stub_calls.append('COM boundary %08X' % at)
            self.ret(cleanup=20 if at == 0x10FD14C else 12,result=0)

    def load_font(self, language, name, texture_size):
        from unicorn.x86_const import UC_X86_REG_EDI, UC_X86_REG_EDX
        found = []
        for kind, begin, end in chunks(language):
            if kind == 0x30201:
                actual = language[begin:begin+256].split(b'\0',1)[0].decode('ascii')
                if actual.upper() == name.upper():
                    found.append((begin,end))
        if len(found) != 1:
            raise ValueError('font name missing or ambiguous: '+name)
        begin,end = found[0]
        if end-begin > 0x60000:
            raise ValueError('font chunk exceeds synthetic allocation')
        self.uc.mem_write(self.chunk, language[begin-8:end])
        self.uc.mem_write(self.texture+0x44, struct.pack('<hh', *texture_size))
        self.run(0x4F44C0, registers={UC_X86_REG_EDX:self.chunk, UC_X86_REG_EDI:self.font})
        # Supplied one-font registry. The real lookup and fallback logic execute.
        self.write(0x7460FC,self.font)
        self.write(0x746100,self.font)
        self.write(self.font,0x7460FC)
        self.write(self.font+4,0x7460FC)
        return dict(name=name, hash='%08X' % self.integer(self.font+0x14),
                    texture_hash='%08X' % self.integer(self.font+0x18),
                    line_height=self.floats(self.font+0x24,1)[0],
                    baseline=self.floats(self.font+0x28,1)[0],
                    line_spacing_scale=self.floats(self.font+0x2C,1)[0])

    def object_fields(self, obj, text, sj=None, parent=None):
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX
        fields = {f['tag']:bytes.fromhex(f['raw']) for f in obj['fields']}
        self.uc.mem_write(self.sa, fields['SA'])
        self.write(self.obj+0x2C,self.sa)
        self.write(self.obj+0x18,u32(fields['Ot']))
        self.write(self.obj+0x1C,u32(fields['OP'],8))
        self.write(self.obj+0x24,hash_name(obj['default_resource']['name'].replace('\\','/').split('/')[-1].rsplit('.',1)[0]))
        if 'SH' in fields:
            self.write(self.obj+0x5C,u32(fields['SH']))
        self.write(self.obj+0x60,self.string)
        self.uc.mem_write(self.string,(text+'\0').encode('utf-16le'))
        loader, field = 0x100F300, 0x100F400
        self.write(loader+0xC,self.obj)
        for tag in ('Sj','Sl','Sw'):
            if tag in fields:
                value = struct.pack('<I',sj) if tag == 'Sj' and sj is not None else fields[tag]
                self.uc.mem_write(field,tag.encode('ascii')+struct.pack('<H',len(value))+value)
                self.run(0x501AC0, registers={UC_X86_REG_EAX:loader,UC_X86_REG_EDX:field})
        if parent is not None:
            self.uc.mem_write(self.obj+0x20,struct.pack('<H',1))
            base = self.context+0xD0+0x20
            self.uc.mem_write(base,struct.pack('<4I',*parent['color']))
            self.uc.mem_write(base+0x10,struct.pack('<16f',*parent['matrix']))

    def render_text(self):
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX
        self.run(0x4F25F0,(0,),{UC_X86_REG_EAX:self.context,UC_X86_REG_EBX:self.obj})
        if len(self.submissions) != 1:
            raise RuntimeError('text did not produce exactly one submission')
        return dict(local=self.submissions[0],draws=self.draws,material=self.material_calls)

    def render_image(self, texture_size):
        from unicorn.x86_const import UC_X86_REG_EAX
        self.uc.mem_write(self.texture+0x44,struct.pack('<hh',*texture_size))
        self.run(0x4F2000,(self.context,self.obj),{UC_X86_REG_EAX:0})
        if len(self.submissions) != 1:
            raise RuntimeError('image did not produce exactly one submission')
        return dict(local=self.submissions[0],draws=self.draws,material=self.material_calls)

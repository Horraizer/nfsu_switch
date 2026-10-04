"""Original retail hull/handling prerequisites on explicit synthetic pair poses.

This is not a live actor admission or scheduler capture. Only the external
allocator and explicit zero-QPC service are supplied; original game math executes.
"""
import hashlib
import capstone
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EIP

INPUTS = {
    'GLOBAL/GLOBALB.BUN': (2361176, '39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9'),
    'Cars/240SX/GEOMETRY.BIN': (4780816, '002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619'),
}


def binding_source(image):
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    assertions={
        0x4255AB: ('mov','dword ptr [ebx + 0x10], esi'),
        0x4255C5: ('add','esi, 0x60'),
        0x4255C8: ('mov','dword ptr [ebx + 0x20], esi'),
        0x4663A0: ('mov','eax, dword ptr [eax + 0x64]'),
        0x466197: ('fld','dword ptr [eax + 0x14]'),
        0x46619A: ('fsub','dword ptr [eax + 0x44]'),
        0x472F7F: ('call','dword ptr [0x69717c]'),
    }
    for address,expected in assertions.items():
        instruction=next(decoder.disasm(image.read(address,16),address,count=1))
        if (instruction.mnemonic,instruction.op_str)!=expected:
            raise ValueError('original prerequisite binding mismatch')
    return {str(address):list(expected) for address,expected in assertions.items()}


def inputs(root):
    data = {}
    for name, (size, digest) in INPUTS.items():
        blob = (root/name).read_bytes()
        if len(blob) != size or hashlib.sha256(blob).hexdigest().upper() != digest:
            raise ValueError('unsupported retail input: '+name)
        data[name] = blob
    return data


def refresh_cases(image, native):
    """Original explicit refresh phase; no scheduler/publication placement claim."""
    cases=[]
    for name,mass,limit,momentum,angular,alternate in (
        ('refresh_normal',2.,200.,(10.,20.,30.),(2.,3.,4.),False),
        ('refresh_speed_clamped',2.,200.,(900.,1200.,0.),(2.,3.,4.),False),
        ('refresh_low_mass',.5,200.,(900.,1200.,0.),(2.,3.,4.),False),
        ('refresh_mass_boundary',.7,200.,(900.,1200.,0.),(2.,3.,4.),False),
        ('refresh_mass_above',.7000000476837158,200.,(900.,1200.,0.),(2.,3.,4.),False),
        ('refresh_angular_clamped',2.,200.,(10.,20.,30.),(300.,400.,0.),False),
        ('refresh_alternate_tensor',2.,200.,(10.,20.,30.),(2.,3.,4.),True),
        ('refresh_rotated_tensor',2.,200.,(10.,20.,30.),(2.,3.,4.),True)):
        m=native.fixture(image,(3.,.7,0.),(0.,0.,0.),(0.,0.,0.),mass=mass)
        rigid=native.RIGID_A
        m.put(rigid+0x144,mass,'f');m.put(0x6F0904,limit,'f')
        native.vector(m,rigid+0x70,momentum);native.vector(m,rigid+0x80,angular)
        native.matrix(m,rigid+0x190)
        if alternate:
            m.put(rigid+0x138,1)
            for offset,value in ((0,2.),(0x14,3.),(0x28,4.)):
                m.put(rigid+0x190+offset,value,'f')
        if name=='refresh_rotated_tensor':
            for offset,value in ((0,0.),(4,1.),(0x10,-1.),(0x14,0.)):
                m.put(rigid+0x30+offset,value,'f')
            for offset,value in ((4,.25),(0x10,.25),(8,.125),(0x20,.125),
                                 (0x18,.5),(0x24,.5)):
                m.put(rigid+0x190+offset,value,'f')
        before=native.snapshot(m)[0]
        def world_tensor(x):
            at=x.get(x.reg('esp')+12)
            before['inverse_inertia']=[x.get(at+i*4,'f') for i in range(16)]
        m.observers[0x5ABDAF]=world_tensor
        m.run(0x585810,ecx=rigid,budget=3000000)
        cases.append(dict(name=name,before=before,raw_mass=mass,speed_limit=limit,
            after=native.snapshot(m)[0],executed=m.executed,hooks=m.events))
    return cases


def prepare(m, native, data):
    # SEH fs:[0] is the only permitted null-page access. No null actor field
    # is made readable by this environment prerequisite.
    m.uc.mem_map(0, 4096)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    def null_guard(uc, access, address, size, value, user):
        ip = uc.reg_read(UC_X86_REG_EIP)
        instruction = next(decoder.disasm(m.image.read(ip, 16), ip, count=1))
        if address != 0 or size != 4 or 'fs:[0]' not in instruction.op_str:
            raise RuntimeError('unexpected null-page access')
    m.uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, null_guard, begin=0, end=4095)
    m.uc.mem_map(0x2000000, 0x1000000)
    heap = [0x2A00000]
    def allocate(x):
        size = x.get(x.reg('esp')+4)
        if not 0 < size < 0x100000 or heap[0]+size >= 0x2F00000:
            raise ValueError('allocator bounds')
        address = heap[0]
        heap[0] += (size+15) & ~15
        x.ret(value=address)
    m.hook(0x67121A, 'synthetic_external_allocator', callback=allocate)
    # 00472F7F calls the imported QueryPerformanceCounter through 0069717C.
    # Supply an explicit zero counter, preserving the original stdcall ABI.
    # The quotient plus a PRNG remainder supplies vertex cache stamps; no
    # clock-independent query or live timer claim follows from this fixture.
    # The game timestamp at 0073AD34 is separate and remains caller-supplied.
    def timer(x):
        stack = x.reg('esp')
        address = x.get(stack)
        x.put(address, 0)
        x.put(address+4, 0)
        x.reg('esp', stack+4)
        x.reg('eax', 1)
        x.uc.reg_write(UC_X86_REG_EIP, 0x472F85)
    m.put(0x7349C8, 1)
    m.hook(0x472F7F, 'synthetic_external_query_performance_counter_call', callback=timer)
    table_offset = 1736752-18*0xC90
    m.uc.mem_write(0x2300000, data['GLOBAL/GLOBALB.BUN'][table_offset:table_offset+35*0xC90])
    m.put(0x734588, 0x2300000)
    rides = (0x1120000, 0x1121000)
    for ride, actor in zip(rides, (native.ACTOR_A, native.ACTOR_B)):
        m.run(0x417600, edx=ride)
        m.run(0x57D160, (0, 0, 0), edx=ride, eax=18)
        m.run(0x57F2B0, (0,), edi=ride, budget=3000000)
        # Original actor constructor 004255AB/004255C5/004255C8 publishes
        # RideInfo and its physical-data subobject, respectively.
        m.put(actor+0x10, ride)
        m.put(actor+0x20, ride+0x60)
    m.uc.mem_write(0x2400000, data['Cars/240SX/GEOMETRY.BIN'])
    m.run(0x445720, esi=0x779BD8)
    m.run(0x472B00, (0x2400000+4778496,))
    source = m.get(0x779BD8)
    if m.get(source+0x18) != 0xC93AE898 or m.get(source+0x14) != 37:
        raise ValueError('unexpected original 240SX hull identity')
    shapes = (0x29F0000, 0x29F1000)
    for shape, body, ride in zip(shapes, (native.BODY_A, native.BODY_B), rides):
        # The car-body constructor's original clone/normalization branch.
        # Each car owns a separate mutable hull instance, as the constructor.
        m.run(0x472CB0, esi=shape)
        m.run(0x472CF0, (shape, source))
        m.run(0x472EA0, edx=shape)
        if not m.get(shape+0x3E, 'B'):
            m.run(0x472E40, ecx=ride+0x60, edx=shape)
        m.put(body+0x10, shape)
    m.run(0x4727C0, ecx=native.PAIR, eax=shapes[0], edx=shapes[1])
    return dict(hull_hash='C93AE898', hull_points=37,
                physical_lateral_bounds=[[m.get(ride+0x74, 'f'), m.get(ride+0xA4, 'f')]
                                         for ride in rides],
                mode='retail-hull-handling-with-synthetic-pair-pose-and-inertia')

"""Original retail hull/handling prerequisites on explicit synthetic pair poses.

This is not a live actor admission or scheduler capture. Only the external
allocator and profiling clock are supplied; original game math executes.
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


def prepare(m, native, data):
    # SEH fs:[0] is the only permitted null-page access. No null actor field
    # is made readable by this environment prerequisite.
    m.uc.mem_map(0, 4096)
    def null_guard(uc, access, address, size, value, user):
        if address != 0 or size != 4:
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
    # Supply zero profiling time, preserving the original call's stdcall ABI.
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
    m.hook(0x472F7F, 'synthetic_external_profiling_clock_call', callback=timer)
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

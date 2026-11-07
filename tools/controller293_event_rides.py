"""Original event0 preset and adaptive RideInfo construction in emulated memory."""
import hashlib
from event0_trace import chunks
from world_body_trace import jdlz
from controller293_scene import FRONT
from controller293_planner import GLOBAL

RIDES = 0x2D20000


def pointer_probe(m, write=False):
    address, size = m.get(m.reg('esp')+4), m.get(m.reg('esp')+8)
    permission = 2 if write else 1
    valid = size == 0 or (address >= 4096 and any(
        start <= address and address+size-1 <= end and flags & permission
        for start, end, flags in m.uc.mem_regions()))
    m.events[-1].update(pointer=address, bytes=size, bad_pointer=not valid)
    m.ret(pop=8, value=0 if valid else 1)


def construct(m, retail, event):
    # Authenticated PE imports: KERNEL32 IsBadReadPtr and IsBadWritePtr.
    # Native diagnostic formatting calls these; no preset/part result is hooked.
    for i, (iat, name, write) in enumerate(((0x697198, 'IsBadReadPtr', False),
                                           (0x69719C, 'IsBadWritePtr', True))):
        target = 0x1000600+i*0x10
        m.put(iat, target)
        m.hook(target, 'external_'+name,
               callback=lambda machine, writable=write: pointer_probe(machine, writable))
    for head in (0x748844, 0x73AD40):
        m.run(0x445720, esi=head)
    for tag, at, end in chunks(jdlz(retail['FrontEnd/FrontB.lzc'])):
        if tag == 0x30220:
            m.run(0x4B25B0, (FRONT+at,))
    for tag, at, end in chunks(retail['GLOBAL/GLOBALB.BUN']):
        if tag in (0x34600, 0x34601, 0x80034602):
            m.run(0x57C8B0, (GLOBAL+at,), budget=3000000)
    setup = event['setup']
    m.run(0x417600, edx=RIDES)
    # The original caller hashes its visual seeds, applies the event preset,
    # and writes the event0 player's eleven performance levels.
    m.run(0x4B6071, end=0x4B6102, ebx=setup, esp=RIDES-0x30, budget=3000000)
    m.run(0x57F2B0, (1,), edi=RIDES, budget=3000000)
    frame = 0x2D1F000
    m.put(frame+0xC, setup)
    sources = [RIDES]
    for i in range(3):
        ride = RIDES+(i+1)*0x1000
        sp, record = ride-0x80, setup+0x5974+i*0x1C
        m.put(sp+0x1C, i*0x1C)
        m.put(sp+0x18, RIDES)
        # Original rival caller constructs RideInfo, formats/hashes seed names,
        # resolves the event preset, derives context and invokes adaptive tuning.
        m.run(0x4B40A0, end=0x4B4197, ebx=i+2, ebp=frame, esp=sp, budget=3000000)
        m.run(0x4B41EB, end=0x4B42BE, esi=record, edi=setup,
              ebp=frame, esp=sp, budget=3000000)
        sources.append(ride)
    return sources, [snapshot(m, ride) for ride in sources]


def snapshot(m, ride):
    return dict(address=ride, car_type=m.get(ride),
                levels=[m.get(ride+8+4*i) for i in range(11)],
                items=[m.get(ride+0x34+4*i) for i in range(11)],
                category=m.get(ride+0x424, 'B'), rating=m.get(ride+0x428, 'f'),
                visual_seeds=[m.get(ride+0x444), m.get(ride+0x448)],
                slot_binding_sha256=hashlib.sha256(bytes(m.uc.mem_read(ride+0x44C, 120*4))).hexdigest(),
                bound_slots=sum(bool(m.get(ride+0x44C+4*i)) for i in range(120)),
                physical_sha256=hashlib.sha256(bytes(m.uc.mem_read(ride+0x60, 0x3C4))).hexdigest())


def hull(m, retail, name, address, map_size=None):
    if map_size:
        m.uc.mem_map(address, map_size)
    data = retail['Cars/'+name+'/GEOMETRY.BIN']
    m.uc.mem_write(address, data)
    found = 0
    for tag, at, end in chunks(data):
        if tag == 0x80034020:
            m.run(0x472B00, (address+at,))
            found += 1
    if found != 1:
        raise ValueError('unsupported original car hull count')

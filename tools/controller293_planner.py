"""Original initialized route planner on an explicit isolated actor scene.

The actor trajectory is an input. This is not the event0 runtime or physics.
Retail bytes stay in memory. External services are logged, never semantic AI.
"""
import math
import struct
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EIP
from physics256_native import Machine, ACTOR, RIDE, PHYSICS, STATUS, bits
from controller293_strategy import CONTROLLER, ACTION
from controller293_planner_ranges import RANGES
from controller293_scene_ranges import RANGES as SCENE_RANGES
from controller293_event_rides_ranges import RANGES as RIDE_RANGES
from world_body_trace import chunks, jdlz, digest_file, INPUTS

RETAIL = {
    'Cars/INTEGRA/GEOMETRY.BIN': (5780304, 'C5E4808EBBEE5A5DD990A97E1BC035753042F55CFCC46678179F7441E49BFFB4'),
    'Cars/ECLIPSE/GEOMETRY.BIN': (5923024, 'F28688371D708189E83B152168486618D29436D9A109B54E63E0C40D908084BF'),
    'FrontEnd/FrontB.lzc': (4182578, '76F368D5E5E30F96A8182BF457DBA4491395E075C3F336D86A32012B56E16F90'),
    'Tracks/TRACKB1003.lzc': INPUTS['Tracks/TRACKB1003.lzc'],
    'GLOBAL/GLOBALB.BUN': (2361176, '39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9'),
    'Cars/240SX/GEOMETRY.BIN': (4780816, '002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619'),
}
TRACK, GLOBAL, GEOMETRY = 0x2000000, 0x2A00000, 0x2400000
BODY, RACE, COUNTERS = 0x118F000, 0x1196000, 0x1194000
CTX = CONTROLLER + 0x2C


def inputs(root):
    result = {}
    for name, identity in RETAIL.items():
        path = root / name
        if (path.stat().st_size, digest_file(path)) != identity:
            raise ValueError('unsupported planner input identity: ' + name)
        result[name] = path.read_bytes()
    return result


def low_memory(uc, access, address, size, value, data):
    # FS:[0] is the emulated SEH chain. Other NULL reads/writes must fail,
    # including a four-byte read at zero by a non-FS instruction.
    eip = uc.reg_read(UC_X86_REG_EIP)
    if address != 0 or size != 4 or bytes(uc.mem_read(eip, 1)) != b'\x64':
        raise RuntimeError('NULL memory access at %08X by %08X' % (address, eip))


class PlannerMachine(Machine):
    """Exact instruction boundaries, including instruction length, fail closed."""
    def __init__(self, image):
        import capstone
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.instructions = {}
        for name, start, end in RANGES + SCENE_RANGES + RIDE_RANGES:
            rows = list(md.disasm(image.read(start, end - start), start))
            if sum(row.size for row in rows) != end - start:
                raise ValueError('incomplete planner block: ' + name)
            self.instructions.update({row.address: row.size for row in rows})
        super().__init__(image)

    def instruction(self, uc, address, size, data):
        self.visited.add(address)
        if address == self.end:
            self.finished = True
            uc.emu_stop()
        elif address in self.hooks:
            label, callback = self.hooks[address]
            self.events.append(dict(hook=label, address='%08X' % address))
            callback(self)
        elif self.instructions.get(address) != size:
            raise RuntimeError('undeclared planner execution at %08X' % address)


class ActorController:
    """Conditional original-code boundary, with one self actor and no rivals.

    No supplied action/driver path or replacement curve is accepted. Original
    constructors produce those. pose() accepts exogenous retail-route samples;
    step() retains controller, action, driver and RNG state between calls.
    """
    def __init__(self, image, retail, point_index=30, speed=20.0, event_seed=None, event_rides=False):
        self.m = m = PlannerMachine(image)
        m.run(0x689FD0)
        m.run(0x689FF0)
        m.uc.mem_map(0, 4096)
        m.uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, low_memory, begin=0, end=4095)
        m.uc.mem_map(TRACK, 0x1000000)
        self.heap = 0x2200000
        self.allocations = []
        for address in (0x67121A, 0x565B60):
            m.hook(address, 'external_heap_allocate', callback=self.allocate)
        for address in (0x671102, 0x565CE0):
            m.hook(address, 'external_heap_release')
        # Explicit external counter fixture, separate from game tick73AD34.
        # No claim of general timer independence for unexecuted hull queries.
        m.put(0x69717C, 0x1000500)
        m.put(0x7349C8, 1)
        m.hook(0x1000500, 'external_QueryPerformanceCounter', callback=self.qpc)
        for head in (0x73B07C, 0x73781C, 0x743C58, 0x743C60, 0x743C70, 0x779BD8, 0x779C70):
            m.run(0x445720, esi=head)
        m.run(0x442B00, edx=0x77AF20)
        # Original scalar D3DX dispatch, avoiding host CPU/Win32 detection.
        m.run(0x5B065B, (0,))
        m.run(0x59C850)
        track = jdlz(retail['Tracks/TRACKB1003.lzc'])
        m.uc.mem_write(TRACK, track)
        for tag, at, end in chunks(track):
            if tag == 0x80034147:
                m.run(0x4434D0, (0x7372B0,), eax=TRACK + at, budget=3000000)
            if tag in (0x34121, 0x34122, 0x34123, 0x34124):
                m.run(0x442DD0, (TRACK + at,))
        m.run(0x4430B0, budget=30000000)
        global_data = retail['GLOBAL/GLOBALB.BUN']
        m.uc.mem_write(GLOBAL, global_data)
        for tag, at, end in chunks(global_data):
            if tag == 0x34201:
                m.run(0x442040, (GLOBAL + at,))
        m.run(0x4484F0, esi=1003)
        # Authenticated retail resource identities, not a hand-built AI table.
        assert struct.unpack_from('<II', global_data, 2181120) == (0x34B00, 0x82C)
        m.run(0x462070, (GLOBAL + 2181120,))
        self.event = None
        if event_seed is not None:
            from controller293_scene import setup_event
            self.event = setup_event(m, retail, event_seed)
        ride = RIDE
        if event_rides:
            if self.event is None:
                raise ValueError('event rides require explicit event seed')
            from controller293_event_rides import construct
            from controller293_scene import configurations
            sources, self.event['rides'] = construct(m, retail, self.event)
            self.event['configs'] = configurations(m, self.event['setup'], sources)
            ride = self.event['configs'][1]['address']+0x10
        else:
            m.put(0x734588, GLOBAL + 1736752 - 18 * 0xC90)
            m.run(0x417600, edx=RIDE)
            m.run(0x57D160, (0, 0, 0), edx=RIDE, eax=18)
            m.run(0x57F2B0, (0,), edi=RIDE, budget=3000000)
        self.ride = ride
        # Explicit actor scene: normal mode, role2, driver/physics selector3,
        # grounded, lap0, no player/rival catch-up target, RNG initial state0.
        m.put(ACTOR + 0x10, ride)
        m.put(ACTOR + 0x20, ride + 0x60)  # original store4255C8
        m.run(0x464860, (ride + 0x60,), eax=ACTOR + 0x40,
              ecx=ACTOR + 0x540, edx=ACTOR + 0x828)
        m.put(ACTOR + 0x14, 0x1152000)
        m.put(0x1152004, 2)
        if self.event is not None:
            from controller293_scene import configurations
            if not event_rides:
                self.event['configs'] = configurations(m, self.event['setup'])
            m.put(ACTOR + 0x14, self.event['configs'][1]['address'])
        if event_rides:
            m.run(0x425598, end=0x4255A1, eax=m.get(ACTOR+0x14), ebx=ACTOR)
            m.run(0x4255AB, end=0x4255CB, ebx=ACTOR, esi=ride)
        m.put(ACTOR + 0x1C, RACE)
        m.put(ACTOR + 0x24, 3)
        m.put(ACTOR + 0x28, 3)
        if self.event is not None:
            m.put(ACTOR + 0x24, self.event['configs'][1]['controller_selector'])
            m.run(0x425150, eax=ACTOR)
            m.put(ACTOR + 0x28, m.reg('eax'))
        m.put(ACTOR + 0x2C, CONTROLLER)
        m.put(ACTOR + 0x34, PHYSICS)
        m.put(PHYSICS, 0x6C8468)
        m.put(ACTOR + 0x350, 1, 'B')
        m.put(ACTOR + 0x351, 1, 'B')
        m.put(ACTOR + 0x404, 1.0, 'f')  # declared initial speed scale
        m.put(0x73619C, STATUS)
        m.put(0x7361F0, STATUS)
        m.put(STATUS, 4)
        m.put(STATUS + 4, 1)
        if self.event is None:
            m.put(0x7361F8, COUNTERS)
            m.put(0x73AD34, 100000)
            m.put(0x78A2F0, 1003)
        self.pose(point_index, speed, update_world=False)
        # Type registry and the actual CarWorldObject factory/pose publisher.
        m.run(0x577B00, (0x73B118, 0, 0, 0), ecx=0x6BA6CC)
        m.run(0x577B00, (0x73B0E4, 0x6BA6CC, 0x577F60, 1), ecx=0x6CAD50)
        m.run(0x577B00, (0x73B084, 0x6CAD50, 0x57A1B0, 1), ecx=0x6CAD40)
        m.run(0x577C30)
        self.pool(0x7345BC, 0x1190000, 0x1191000)
        m.put(0x7345B8, 0x1192000)
        for i in range(8):
            m.run(0x445720, esi=0x1192000 + i * 8)
        m.run(0x57A1B0, (ACTOR, 0))
        m.put(ACTOR + 0x82C, m.reg('eax'))  # original store425715
        m.run(0x57A380, esi=m.get(m.get(ACTOR + 0x82C) + 0x20))
        # Original hull loader and constructor prefix including local bounds.
        # Stops before spatial world registration, which is outside this scene.
        if event_rides:
            from controller293_event_rides import hull
            hull(m, retail, 'ECLIPSE', GEOMETRY)
        else:
            m.uc.mem_write(GEOMETRY, retail['Cars/240SX/GEOMETRY.BIN'])
            m.run(0x472B00, (GEOMETRY + 4778496,))
        m.put(0x736070, 0x1195000)
        m.put(0x1195000, 32)
        m.put(0x1195004, 0x1195100)
        m.put(0x736050, 0x1195200)
        m.put(ACTOR + 0x3C, BODY)
        m.run(0x469B00, (BODY, ACTOR), end=0x469D1E)
        self.pool(0x733F64, 0x1180000, 0x1181000)
        self.pool(0x733F60, 0x1180100, 0x1182000, 64)
        self.pool(0x7344C8, 0x1197000, 0x1198000)
        self.pool(0x73557C, 0x1197100, 0x1199000)
        self.pool(0x73609C, 0x1168000, 0x1164000)
        self.pool(0x736098, 0x1169000, ACTION)
        # Complete original controller and driver/history constructors.
        m.run(0x4620C0, (CONTROLLER,), edi=ACTOR, budget=3000000)
        self.driver = m.get(CONTROLLER + 8)
        m.put(ACTOR + 0x30, self.driver)
        m.run(0x44B831, end=0x44B85D, ebx=CONTROLLER)
        self.initial = dict(action_count=m.get(ACTION + 0x20, 'B'),
                            driver_count=m.get(self.driver + 0x10, 'B'),
                            marker=m.get(CONTROLLER + 0x478, 'i'))
        m.run(0x44C250, (1,), ecx=CONTROLLER, budget=3000000)
        self.tick = m.get(0x73AD34)

    def allocate(self, m):
        size = m.get(m.reg('esp') + 4)
        if not 0 < size < 0x100000 or self.heap + size > 0x2300000:
            raise RuntimeError('heap request exceeds declared fixture')
        address = self.heap
        self.heap += (size + 15) & ~15
        self.allocations.append(dict(address=address, size=size))
        m.ret(value=address)

    @staticmethod
    def qpc(m):
        m.put(m.get(m.reg('esp') + 4), 0, 'Q')
        m.ret(pop=4, value=1)

    def pool(self, global_at, pool, block, count=1):
        m = self.m
        m.put(global_at, pool)
        m.put(pool + 0x10, block)
        for i in range(count):
            m.put(block + i * 0x100, block + (i + 1) * 0x100 if i + 1 < count else 0)

    def pose(self, point_index, speed, update_world=True, actor=ACTOR):
        m = self.m
        route = m.get(0x77AF34)
        if not 0 <= point_index < m.get(route + 0x10, 'h') or not math.isfinite(speed):
            raise ValueError('unsupported exogenous pose')
        point = route + 0x27C + point_index * 0x88
        for j in range(3):
            m.put(actor + 0x60 + j * 4, m.get(point + j * 4, 'f'), 'f')
        for j in range(2):
            tangent = m.get(point + 0xC + j * 4, 'f')
            m.put(actor + 0x70 + j * 4, speed * tangent, 'f')
            m.put(actor + 0x90 + j * 4, tangent, 'f')
        m.put(actor + 0xA0, -m.get(actor + 0x94, 'f'), 'f')
        m.put(actor + 0xA4, m.get(actor + 0x90, 'f'), 'f')
        m.put(actor + 0xB8, 1.0, 'f')
        m.put(actor + 0xCC, 1.0, 'f')
        # Recover heading through the original atan2 implementation.
        m.run(0x564DB0, (m.get(actor + 0x94), m.get(actor + 0x90)))
        m.put(actor + 0x3B4, m.reg('eax') & 65535, 'H')
        m.put(actor + 0x3D0, speed, 'f')
        m.put(actor + 0x3D4, abs(speed), 'f')
        m.put(actor + 0x814, m.get(point + 0x38, 'f'), 'f')
        if update_world:
            m.run(0x57A380, esi=m.get(m.get(actor + 0x82C) + 0x20))
        if actor == ACTOR:
            self.pose_input = dict(route_id=1, point_index=point_index, speed=speed)

    def step(self, dt=0.02):
        if dt != 0.02:
            raise ValueError('this witnessed adapter supports dt0.02 only')
        m = self.m
        self.tick += 80
        m.put(0x73AD34, self.tick)
        m.run(0x44CF30, (CTX, CONTROLLER), budget=3000000)
        if not (m.reg('eax') & 255) or m.get(CTX + 0x388, 'b') < 0:
            raise RuntimeError('original route context did not select self')
        m.run(0x450210, (CTX,), ecx=ACTION, budget=3000000)
        if m.get(ACTION + 0x20, 'B') < 2:
            raise RuntimeError('original action produced no usable path')
        m.run(0x44C550, (CONTROLLER,), end=0x44C7F1, budget=3000000)
        # Original caller's distance accumulator, supplied kinematics and dt.
        m.put(0x119A008, dt, 'f')
        m.run(0x426A5C, end=0x426A95, edi=ACTOR, ebp=0x119A000)
        m.run(0x45E970, (bits(dt),), ecx=self.driver, budget=3000000)
        driver = self.driver
        path = [[m.get(ACTION + 0x30 + i * 16 + j * 4, 'f') for j in range(3)]
                for i in range(m.get(ACTION + 0x20, 'B'))]
        return dict(inputs=dict(self.pose_input, world_tick=self.tick, dt=dt),
                    selected_self=m.get(CTX + 0x388, 'b'),
                    context_count=m.get(CTX + 4), path=path,
                    route_id=m.get(m.get(CTX + 0x38C) + 0xA, 'h'),
                    driver_count=m.get(driver + 0x10, 'B'),
                    desired_speed=m.get(driver + 0x200, 'f'),
                    catchup_scale=m.get(CONTROLLER + 0x4B8, 'f'),
                    action_tick=m.get(ACTION + 0x200), distance=m.get(ACTOR + 0x3B8, 'f'),
                    controls=dict(steering_word=m.get(driver + 0x1D0, 'H'),
                        gear_command=m.get(driver + 0x1D4, 'i'),
                        throttle=m.get(driver + 0x1DC, 'f'),
                        handbrake=m.get(driver + 0x1E0, 'f'),
                        brake=m.get(driver + 0x1E4, 'f'),
                        goal=[m.get(driver + off, 'f') for off in (0x1F4, 0x1F8)]))


def case(image, retail, speed=20.0, lifecycle=False):
    actor = ActorController(image, retail, speed=speed)
    frames = []
    sequence = ([(30 + min(i // 20, 7), speed) for i in range(160)] if lifecycle else
                [(30, speed)] * 20 + [(31, speed), (32, speed), (33, 12.0), (34, 12.0)])
    for point, value in sequence:
        actor.pose(point, value)
        frames.append(actor.step())
    m = actor.m
    return dict(name=('lifecycle' if lifecycle else 'speed-%g' % speed),
                initial=actor.initial, frames=frames, hooks=m.events,
                allocations=actor.allocations,
                visited=['%08X' % a for a in sorted(m.visited)],
                input_identities={k: list(v) for k, v in RETAIL.items()},
                generated_path=True, physical_integration=False, original_runtime=None,
                shape_kind=m.get(m.get(BODY + 0x10) + 0x10),
                body_width=m.get(BODY + 0x104, 'f') - m.get(BODY + 0xF4, 'f'),
                category=m.get(RIDE + 0x424, 'B'),
                policy_loaded=True, driver_constructed=True)


def suite(image, retail):
    return dict(cases=[case(image, retail), case(image, retail, 70.0),
                       case(image, retail, lifecycle=True)])

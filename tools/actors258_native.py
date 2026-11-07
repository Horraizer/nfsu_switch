"""Bounded original x86 execution on synthetic memory, never a game process.

All callees outside the explicitly permitted ranges must be declared hooks.
Unicorn failures, instruction limits and unexpected calls fail the experiment.
Construction, physics, AI decisions and lap crossing are NOT emulated here.
"""

import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP,
    UC_X86_REG_EIP, UC_X86_REG_FPCW,
)


REGS = dict(eax=UC_X86_REG_EAX, ebx=UC_X86_REG_EBX,
            ecx=UC_X86_REG_ECX, edx=UC_X86_REG_EDX,
            esi=UC_X86_REG_ESI, edi=UC_X86_REG_EDI,
            ebp=UC_X86_REG_EBP, esp=UC_X86_REG_ESP)
STOP = 0x1000000
WORLD = 0x1100000
STATUS = 0x1101000
ACTORS = 0x1120000
CONFIGS = 0x1140000
STACK = 0x11F0000


class Machine:
    def __init__(self, image):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(0, 0x1000)  # synthetic SEH head for constructors
        self.uc.mem_map(0x400000, 0x400000)
        self.uc.mem_map(STOP, 0x200000)
        for start, length, raw, _ in image.sections:
            self.uc.mem_write(start, image.data[raw:raw + length])
        self.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
        self.hooks = {}
        self.allowed = []
        self.finished = False
        self.uc.hook_add(UC_HOOK_CODE, self._instruction)

    def _instruction(self, uc, address, size, data):
        if address == self.end:
            self.finished = True
            uc.emu_stop()
        elif address in self.hooks:
            self.hooks[address](self)
        elif not any(a <= address and address + size <= b for a, b in self.allowed):
            raise RuntimeError('undeclared execution at %08X' % address)

    def reg(self, name, value=None):
        if value is None:
            return self.uc.reg_read(REGS[name])
        self.uc.reg_write(REGS[name], value & 0xFFFFFFFF)

    def put(self, address, value, fmt='I'):
        self.uc.mem_write(address, struct.pack('<' + fmt, value))

    def get(self, address, fmt='I'):
        return struct.unpack('<' + fmt, self.uc.mem_read(address, struct.calcsize(fmt)))[0]

    def ret(self, pop=0, value=None):
        sp = self.reg('esp')
        target = self.get(sp)
        self.reg('esp', sp + 4 + pop)
        if value is not None:
            self.reg('eax', value)
        self.uc.reg_write(UC_X86_REG_EIP, target)

    def run(self, start, ranges, args=(), end=STOP, **registers):
        self.allowed, self.end, self.finished = ranges, end, False
        self.reg('esp', STACK)
        self.put(STACK, STOP)
        for index, value in enumerate(args):
            self.put(STACK + 4 + index * 4, value)
        for name, value in registers.items():
            self.reg(name, value)
        self.uc.emu_start(start, 0, count=200000)
        if not self.finished:
            raise RuntimeError('instruction budget exhausted')


def make_actor(m, index, identity, role, grid=0):
    actor, config = ACTORS + index * 0x1000, CONFIGS + index * 0x800
    m.put(actor + 0x14, config)
    m.put(actor + 0xA, identity, 'h')
    m.put(config + 1, identity, 'b')
    m.put(config + 3, grid, 'b')
    m.put(config + 4, role)
    m.put(actor + 0x4D0, 1, 'B')
    m.put(actor + 0x8A9, 1, 'B')
    return actor, config


def link(m, actors):
    nodes = [WORLD] + actors
    for index, node in enumerate(nodes):
        m.put(node, nodes[(index + 1) % len(nodes)])
        m.put(node + 4, nodes[index - 1])
    m.put(WORLD + 8, WORLD + 8)
    m.put(WORLD + 12, WORLD + 8)
    m.put(0x7361F8, WORLD)


def ids(m, pointers):
    return [m.get(p + 0xA, 'h') for p in pointers]


def partition(m):
    m.hooks[0x40A880] = lambda x: x.ret()  # actual helper is identity ret
    m.run(0x41FE50, [(0x41FE50, 0x41FF47)], esi=WORLD)
    return [m.get(WORLD + 0x28 + i * 4) for i in range(m.get(WORLD + 0x18))]


def grid_case(image, flagged=False):
    m = Machine(image)
    configs = []
    for index, (identity, role, grid) in enumerate(
            [(0, 1, 2), (100, 2, 1), (101, 2, 4), (102, 2, 3)]):
        # Grid iterator uses stride 0x750, not the fixture helper's stride.
        config = CONFIGS + index * 0x750
        m.put(config + 1, identity, 'b')
        m.put(config + 3, grid, 'b')
        m.put(config + 4, role)
        configs.append(config)
    if flagged:
        m.put(configs[1], 1, 'b')
    m.put(0x78A418, CONFIGS)
    m.put(0x78A41C, len(configs))
    link(m, [])
    created = []

    def load_grid(x):
        target = x.get(x.reg('esp') + 8)
        for index in range(4):
            x.put(target + index * 0x20, index)
        x.ret()  # caller cleans 8

    def instantiate(x):
        sp = x.reg('esp')
        config, marker, kind = [x.get(sp + n) for n in (4, 8, 12)]
        actor = ACTORS + len(created) * 0x1000
        x.put(actor + 0x14, config)
        x.put(actor + 0xA, x.get(config + 1, 'b'), 'h')
        tail = x.get(WORLD + 4)
        x.put(tail, actor)
        x.put(WORLD + 4, actor)
        x.put(actor + 4, tail)
        x.put(actor, WORLD)
        created.append(dict(id=x.get(actor + 0xA, 'h'),
                            marker=x.get(marker), controller=kind))
        x.ret()  # object constructor/ground/driver are stubbed

    m.hooks.update({0x422580: load_grid, 0x422410: instantiate,
                    0x40A880: lambda x: x.ret()})
    m.run(0x422A70, [(0x422A70, 0x422B70), (0x41FE50, 0x41FF47)], args=(0,))
    table = [m.get(WORLD + 0x28 + i * 4) for i in range(m.get(WORLD + 0x18))]
    return dict(created=created, table=ids(m, table),
                indices=[m.get(a + 8, 'h') for a in table])


def registration_case(image, order):
    m = Machine(image)
    actors = [make_actor(m, i, identity, role)[0]
              for i, (identity, role) in enumerate(order)]
    link(m, actors)
    table = partition(m)
    visits = []

    def progress(x):
        row = x.reg('edi')
        visits.append(x.get(x.get(row) + 0xA, 'h'))
        x.ret()  # initial course projection is deliberately synthetic

    m.hooks[0x4338C0] = progress
    m.run(0x434A80, [(0x434A80, 0x434B46), (0x4333C0, 0x43349A),
                      (0x434ED0, 0x434F7F)], esi=STATUS)
    rows = [m.get(STATUS + 0x520 + i * 4) for i in range(m.get(STATUS + 0x24))]
    result = dict(table=ids(m, table), registered=visits,
                  ranked=ids(m, [m.get(row) for row in rows]),
                  rank_pairs=[[m.get(row + 0xA, 'B'), m.get(row + 0xB, 'B')] for row in rows],
                  backlinks=[m.get(m.get(row) + 0x1C) == row for row in rows],
                  sentinel=[m.get(row + 0x60) for row in rows])
    # A second role rebuild after insertion preserves identity, moves indices.
    added = make_actor(m, len(actors), 1, 1)[0]
    link(m, [added] + actors)
    new_table = partition(m)
    result['after_insert'] = dict(ids=ids(m, new_table),
                                 indices=[m.get(a + 8, 'h') for a in table])
    return result


def status_case(image, order, progress, finished=()):
    m = Machine(image)
    m.put(STATUS + 0x24, len(order))
    m.put(STATUS + 0x530, 2)
    m.put(0x78A30C, 1)
    visits, finishes = [], []
    for index, identity in enumerate(order):
        actor, _ = make_actor(m, index, identity, 2)
        row = STATUS + 0x30 + index * 0x13C
        m.put(row, actor)
        m.put(row + 9, 2, 'b')
        m.put(row + 0xA, index + 1, 'B')
        if identity in finished:
            m.put(row + 0x10, 1, 'H')
            m.put(row + 0xC, 1, 'B')
        m.put(STATUS + 0x520 + index * 4, row)

    def lap(x):
        row = x.reg('edi')
        identity = x.get(x.get(row) + 0xA, 'h')
        visits.append(identity)
        x.put(row + 0x20, progress[identity], 'f')
        x.ret()

    def finish(x):
        row = x.get(STATUS + 0x520 + x.reg('eax') * 4)
        sp = x.reg('esp')
        finishes.append(dict(id=x.get(x.get(row) + 0xA, 'h'),
                             args=[x.get(sp + n) for n in (4, 8, 12)]))
        x.put(row + 0x10, 1, 'H')  # not a native finish implementation
        x.ret(12)

    m.hooks.update({0x4338C0: lap, 0x434F80: lambda x: x.ret(4),
                    0x4350D0: finish})
    # Slice begins after race-clock update, ends before function epilogue.
    m.run(0x4355F1, [(0x4355F1, 0x435A64), (0x434ED0, 0x434F7F)],
          end=0x435A5F, esi=STATUS)
    return dict(lap_visits=visits, finish_dispatch=finishes)


def scheduler_case(image, dt, selector):
    m = Machine(image)
    order = [(100, 2), (0, 1), (50, 3), (102, 2), (101, 2), (7, 4)]
    actors = [make_actor(m, i, identity, role)[0]
              for i, (identity, role) in enumerate(order)]
    link(m, actors)
    table = partition(m)
    controllers, drivers = {}, {}
    for i, actor in enumerate(table):
        controller = 0x1180000 + i * 0x100
        driver = controller + 0x40
        m.put(actor + 0x2C, controller)
        m.put(actor + 0x30, driver)
        m.put(controller, 0x1190000)
        m.put(driver, 0x1190100)
        controllers[controller] = m.get(actor + 0xA, 'h')
        drivers[driver] = m.get(actor + 0xA, 'h')
    m.put(0x119000C, 0x1000100)
    m.put(0x1190104, 0x1000200)
    m.put(0x736200, selector)
    m.put(WORLD + 0x2F0, 0x1188000)
    events = []

    def event(label, pop, identity=None):
        def hook(x):
            row = [label]
            if identity is not None:
                row.append(identity(x))
            events.append(row)
            x.ret(pop)
        return hook

    # Scalar helper returns synthetic 0 through an explicit fldz stub.
    m.uc.mem_write(0x1000300, b'\xd9\xee\xc3')
    def scalar(x):
        x.uc.reg_write(UC_X86_REG_EIP, 0x1000300)
    m.hooks.update({
        0x426450: scalar,
        0x598600: event('traffic_manager', 4),
        0x1000100: event('controller', 4, lambda x: controllers[x.reg('ecx')]),
        0x1000200: event('driver', 4, lambda x: drivers[x.reg('ecx')]),
        0x4267A0: event('pre_contact', 4, lambda x: x.get(x.reg('esi') + 0xA, 'h')),
        0x595670: event('props', 0),
        0x420890: event('collision_nonzero', 8),
        0x420310: event('collision_zero_prepare', 8),
        0x420380: event('collision_zero_solve', 8),
        0x426930: event('post_contact', 4, lambda x: x.get(x.reg('eax') + 0xA, 'h')),
    })
    frame = STACK + 0x1000
    m.put(frame + 8, WORLD)
    m.put(frame + 12, dt, 'f')
    m.run(0x420C77, [(0x420C77, 0x420E50), (0x1000300, 0x1000303)],
          end=0x420E50, ebp=frame, edi=WORLD)
    return events


def gate_case(image, hide):
    m = Machine(image)
    actor, _ = make_actor(m, 0, 100, 2)
    m.put(0x73AD34, 12345)
    # Stack offset here is the original finish helper's second argument.
    m.run(0x435336, [(0x435336, 0x435364)], end=0x435364,
          args=(0, 0, 0, 0, 0, 0, hide), esi=actor)
    return dict(gate=m.get(actor + 0x8A9, 'B'),
                other=m.get(actor + 0x8A8, 'B'),
                stamp=m.get(actor + 0x884))


def teardown_case(image):
    m = Machine(image)
    actors = [make_actor(m, i, identity, role)[0]
              for i, (identity, role) in enumerate([(100, 2), (0, 1), (50, 3)])]
    link(m, actors)
    events = []
    def destroy(x):
        actor = x.get(x.reg('esp') + 4)
        # The actual unlink must already have happened before the destructor.
        if x.get(x.get(actor + 4)) == actor:
            raise AssertionError('actor still linked at destructor')
        events.append(['destroy', x.get(actor + 0xA, 'h')])
        x.ret(4)
    def free(x):
        actor = x.get(x.reg('esp') + 4)
        events.append(['free', x.get(actor + 0xA, 'h')])
        x.ret()
    m.hooks.update({0x425990: destroy, 0x671102: free})
    m.run(0x422B70, [(0x422B70, 0x422BA8)], args=(0,))
    return dict(events=events, empty=m.get(WORLD) == WORLD and m.get(WORLD + 4) == WORLD)


def place_case(image, occupied):
    m = Machine(image)
    m.put(STATUS + 0x24, len(occupied))
    for i, place in enumerate(occupied):
        row = STATUS + 0x30 + i * 0x13C
        m.put(STATUS + 0x520 + i * 4, row)
        m.put(row + 0xC, place, 'b')
    m.run(0x435BD0, [(0x435BD0, 0x435C0B)], args=(STATUS,))
    return m.reg('eax')


def physics_choice(image, role, flagged=0, override=0, drag=0):
    m = Machine(image)
    actor, config = make_actor(m, 0, 0, role)
    m.put(config + 0x734, flagged, 'B')
    m.put(0x736208, override)
    m.put(0x78A344, drag, 'B')
    m.run(0x425150, [(0x425150, 0x425195)], eax=actor)
    return m.reg('eax')


def suite(image):
    return dict(
        evidence='bounded original x86 with synthetic memory and declared hooks',
        grid=grid_case(image), grid_skipped=grid_case(image, True),
        registration=registration_case(image, [(100, 2), (0, 1), (102, 2), (101, 2), (50, 3)]),
        status_reorder=status_case(image, [0, 100, 102, 101], {0: 3.1, 100: 3.4, 102: 3.3, 101: 3.2}),
        status_ties=status_case(image, [102, 0, 101, 100], {x: 3.0 for x in (0, 100, 101, 102)}),
        status_finished=status_case(image, [0, 100], {100: 3.0}, finished=(0,)),
        scheduler_zero=scheduler_case(image, 0.016, 0),
        scheduler_nonzero=scheduler_case(image, 0.016, 1),
        scheduler_zero_dt=scheduler_case(image, 0.0, 0),
        gate_keep=gate_case(image, 0), gate_hide=gate_case(image, 1),
        teardown=teardown_case(image),
        places=[place_case(image, x) for x in ([], [1, 3], [2, 3], [1, 2, 3, 4])],
        physics=[physics_choice(image, *x) for x in
                 ((1,), (2,), (3,), (1, 1), (1, 0, 1), (2, 0, 0, 1))],
    )

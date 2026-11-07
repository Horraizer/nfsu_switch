"""Original x86 on synthetic memory; not a running game or host-native solver.

The hook log is part of the result. No implicit external call is permitted.
"""

import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP,
    UC_X86_REG_EIP, UC_X86_REG_FPCW,
)

REGS = dict(eax=UC_X86_REG_EAX, ebx=UC_X86_REG_EBX, ecx=UC_X86_REG_ECX,
            edx=UC_X86_REG_EDX, esi=UC_X86_REG_ESI, edi=UC_X86_REG_EDI,
            ebp=UC_X86_REG_EBP, esp=UC_X86_REG_ESP)
STOP, STACK = 0x1000000, 0x11F0000
ACTOR, DRIVER, PHYSICS, BODY = 0x1100000, 0x1101000, 0x1102000, 0x1103000
RIGID, CONFIG, RIDE, WORLD = 0x1104000, 0x1105000, 0x1106000, 0x1107000
STATUS, GROUP, PAIR = 0x1108000, 0x1109000, 0x110A000


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


class Machine:
    def __init__(self, image):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(0x400000, 0x400000)
        self.uc.mem_map(STOP, 0x200000)
        for start, length, raw in image.sections:
            self.uc.mem_write(start, image.data[raw:raw + length])
        self.uc.reg_write(UC_X86_REG_FPCW, 0x007F)
        self.hooks, self.events, self.allowed = {}, [], []
        self.visited = set()
        self.uc.hook_add(UC_HOOK_CODE, self.instruction)

    def instruction(self, uc, address, size, data):
        self.visited.add(address)
        if address == self.end:
            self.finished = True
            uc.emu_stop()
        elif address in self.hooks:
            label, callback = self.hooks[address]
            self.events.append(dict(hook=label, address='%08X' % address))
            callback(self)
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

    def hook(self, address, label, pop=0, value=None, callback=None):
        self.hooks[address] = (label, callback or (lambda m: m.ret(pop, value)))

    def scalar_hook(self, address, label, pop=0):
        # Explicit synthetic fldz/ret trampoline, never executable-file patching.
        at = STOP + 0x100 + pop * 8
        self.uc.mem_write(at, b'\xd9\xee\xc2' + struct.pack('<H', pop))
        self.allowed.append((at, at + 5))
        self.hook(address, label, callback=lambda m: m.uc.reg_write(UC_X86_REG_EIP, at))

    def run(self, start, args=(), end=STOP, budget=200000, **regs):
        self.end, self.finished = end, False
        self.reg('esp', STACK)
        self.put(STACK, STOP)
        for index, value in enumerate(args):
            self.put(STACK + 4 + index * 4, value)
        for name, value in regs.items():
            self.reg(name, value)
        self.uc.emu_start(start, 0, count=budget)
        if not self.finished:
            raise RuntimeError('instruction budget exhausted')


def fixture(image, selector, scheduler=0):
    m = Machine(image)
    m.put(ACTOR + 0x14, CONFIG)
    m.put(ACTOR + 0x10, RIDE)
    m.put(ACTOR + 0x20, RIDE)
    m.put(CONFIG + 4, 1 if selector == 1 else 2)
    m.put(ACTOR + 0x28, selector)
    m.put(ACTOR + 0x30, DRIVER)
    m.put(ACTOR + 0x34, PHYSICS)
    m.put(ACTOR + 0x3C, BODY)
    m.put(ACTOR + 0x3CC, 4)
    m.put(ACTOR + 0x3D8, 1)
    m.put(PHYSICS, 0x6C8364 if selector == 1 else 0x6C8468)
    m.put(PHYSICS + 4, DRIVER)
    m.put(PHYSICS + 0x20, RIGID)
    m.put(PHYSICS + (0x5C if selector == 1 else 0x34), ACTOR)
    m.put(PHYSICS + (0x58 if selector == 1 else 0x30), ACTOR + 0x40)
    m.put(PHYSICS + (0x54 if selector == 1 else 0x2C), RIDE)
    m.put(BODY, 0x6C8798)
    m.put(BODY + 0x140, ACTOR)
    m.put(BODY + 0x16, 1, 'B')
    m.put(BODY + 0x60, 1)
    m.put(BODY + 0x6C, RIGID)
    m.put(DRIVER, 0x6C8AD0 if selector == 1 else 0x6C8C18)
    m.put(DRIVER + 0x26C, ACTOR)
    m.put(DRIVER + 0x1F0, ACTOR)
    m.put(DRIVER + 0x1D4, 1)
    m.put(0x736200, scheduler)
    m.put(0x73619C, STATUS)
    m.put(STATUS, 4)
    m.put(0x735FB0, 0)
    m.put(0x735FB8, 0)
    m.put(0x735FBC, 0)
    m.put(0x7798E0, 0)
    m.put(0x69717C, STOP + 0x200)
    m.hook(STOP + 0x200, 'timer_stub', 4)
    for i, offset in enumerate((0x24, 0x28, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x50)):
        component = 0x1110000 + i * 0x1000
        m.put(PHYSICS + offset, component)
        m.put(component, 0x1120000)
    m.put(0x1120004, STOP + 0x210)
    m.hook(STOP + 0x210, 'steering_component_stub', 4)
    m.put(PHYSICS + 0x78, 1.0, 'f')
    # Driver control accessor functions are tiny actual source methods.
    m.allowed += [(0x424FF0, 0x425030), (0x674898, 0x67490D)]
    return m


def physics_hooks(m, selector):
    slices, exports = [], []

    def integrate(x):
        sp = x.reg('esp')
        slices.append(dict(system=x.get(sp + 4), dt=x.get(sp + 8, 'f'),
                           global_dt=x.get(0x6F0738, 'f')))
        x.ret(8)

    def export(x):
        exports.append(x.get(x.reg('esp') + 4, 'f'))
        x.ret(4)

    m.hook(0x46AA40, 'integration_capture_no_dynamics', callback=integrate)
    m.hook(0x40BD40, 'orientation_normalization_stub')
    if selector == 3:
        m.allowed += [(0x474A90, 0x474C99)]
        m.hook(0x4745B0, 'physics3_input_sync_stub')
        m.hook(0x474D40, 'physics3_parameter_stub', 4)
        m.hook(0x474730, 'physics3_publication_capture', callback=export)
    else:
        m.allowed += [(0x4774A0, 0x4782B6)]
        m.hook(0x479160, 'physics1_parameter_stub', 4)
        m.hook(0x477100, 'physics1_input_sync_stub')
        m.hook(0x465430, 'driver_flag_stub', value=0)
        m.hook(0x46E5B0, 'transmission_selection_stub', 4, 1)
        m.hook(0x479640, 'physics1_force_setup_stub', 4)
        m.hook(0x4789D0, 'physics1_publication_capture', callback=export)
        m.hook(0x587E90, 'wheel_effect_stub', 8)
        m.hook(0x479E20, 'physics1_post_effect_stub', 4)
        # Disable optional control/handling branches without claiming live values.
        m.put(0x78A344, 0, 'B')
        m.put(0x7361A8, 0)
        m.put(0x735FA0, 1)
    return slices, exports


def physics_case(image, selector, scheduler, dt, state=0, wheels=4):
    m = fixture(image, selector, scheduler)
    m.put(ACTOR + 0x38, state)
    m.put(ACTOR + 0x3CC, wheels)
    slices, exports = physics_hooks(m, selector)
    m.run(0x4774A0 if selector == 1 else 0x474A90, (bits(dt),), ecx=PHYSICS)
    return dict(selector=selector, scheduler=scheduler, dt=dt, state=state,
                wheels=wheels, slices=slices, exports=exports,
                rigid_flags=[m.get(RIGID + x) for x in (0x130, 0x134, 0x138)],
                setup_executed=0x477538 in m.visited if selector == 1 else True,
                hooks=m.events)


def dispatch_case(image, selector, scheduler, dt, active=True, member=True):
    m = fixture(image, selector, scheduler)
    slices, exports = physics_hooks(m, selector)
    m.allowed += [(0x420310, 0x42037F), (0x40CB70, 0x40CB7C),
                  (0x46A030, 0x46A060), (0x4264D0, 0x42679D)]
    m.hook(0x468F70, 'body_snapshot_stub', 4)
    m.hook(0x468EC0, 'body_bounds_stub')
    for target, label in [(0x42A530, 'actor_collision_state_stub'),
                          (0x429B10, 'actor_parameter_stub'),
                          (0x427930, 'actor_gear_post_stub'),
                          (0x42A4D0, 'actor_post_stub')]:
        m.hook(target, label)
    m.hook(0x4280A0, 'actor_recovery_predicate_stub', value=0)
    m.put(0x779C70, BODY + 4)
    m.put(BODY + 4, 0x779C70)
    m.put(BODY + 0x16, int(active), 'B')
    if scheduler == 0:
        m.run(0x420310, (WORLD, bits(dt)))
    else:
        # Actual default-group branch of 592D50, using its membership gate.
        m.allowed += [(0x592D50, 0x592E75)]
        m.put(GROUP + 8, dt, 'f')
        m.put(GROUP + 0x10, -1.0, 'f')
        m.put(BODY + 0xC, GROUP if member else GROUP + 0x100)
        m.run(0x592D50, esi=GROUP)
    return dict(selector=selector, scheduler=scheduler, active=active, member=member,
                slices=slices, exports=exports,
                body_called=0x46A030 in m.visited,
                actor_called=0x4264F5 in m.visited,
                bounds_called=any(x['hook'] == 'body_bounds_stub' for x in m.events),
                hooks=m.events)


def unique_group_case(image, dt, marked=False):
    m = fixture(image, 3, 1)
    m.allowed += [(0x592CB0, 0x592D4C)]
    # Two pairs share A; B is disabled, C is enabled. IDs are in-range.
    bodies = [BODY, BODY + 0x200, BODY + 0x400]
    for index, body in enumerate(bodies):
        m.put(body, 0x1121000)
        m.put(body + 0x16, int(index != 1), 'B')
        m.put(body + 0x18, index, 'h')
    m.put(0x1121028, STOP + 0x220)
    visits = []
    def capture(x):
        visits.append([bodies.index(x.reg('ecx')), x.get(x.reg('esp') + 4, 'f')])
        x.ret(4)
    m.hook(STOP + 0x220, 'body_step_capture', callback=capture)
    m.put(GROUP + 0x1C, PAIR)
    for i, other in enumerate(bodies[1:]):
        node, pair = PAIR + i * 0x20, PAIR + 0x100 + i * 0x300
        m.put(node, PAIR + 0x20 if i == 0 else GROUP + 0x1C)
        m.put(node + 8, pair)
        m.put(node + 0xC, int(marked and i == 1), 'B')
        m.put(pair + 0x258, BODY)
        m.put(pair + 0x25C, other)
    m.run(0x592CB0, (GROUP, bits(dt)))
    return dict(visits=visits, hooks=m.events)


def group_interval_case(image, total, current, next_hit):
    m = fixture(image, 3, 1)
    m.allowed += [(0x592D50, 0x592DB7)]
    m.put(GROUP + 8, total, 'f')
    m.put(GROUP + 0xC, current, 'f')
    m.put(GROUP + 0x10, next_hit, 'f')
    m.put(GROUP + 0x14, 1)
    m.put(GROUP + 0x18, PAIR)
    calls = []
    def advance(x):
        calls.append(['advance', x.get(x.reg('esp') + 8, 'f')])
        x.ret(8)
    def contact(x):
        calls.append(['contact', x.get(GROUP + 0xC, 'f')])
        x.ret()
    m.hook(0x592CB0, 'unique_group_advance_capture', callback=advance)
    m.hook(0x592FD0, 'group_contact_capture_no_solver', callback=contact)
    m.run(0x592D50, end=0x592DB7, esi=GROUP)
    return dict(total=total, current=current, next_hit=next_hit, calls=calls)


def publication_case(image, selector, disabled=False):
    m = fixture(image, selector)
    for offset, values in ((0x20, (10.0, 20.0, 30.0)),
                           (0xA0, (3.0, 4.0, 0.0)),
                           (0xC0, (0.25, 0.5, 0.75))):
        for index, value in enumerate(values):
            m.put(RIGID + offset + index * 4, value, 'f')
    for index in range(12):
        m.put(RIGID + 0x30 + 4 * index, float(index + 1), 'f')
    m.put(0x7798E0, int(disabled))
    # Stops after the actual position/matrix/linear/angular stores. The
    # origin adjustment helper is a declared stub; no whole export claim.
    m.allowed += [(0x474730, 0x4747F6), (0x4789D0, 0x478AA0)]
    m.hook(0x465390, 'origin_adjustment_stub')
    def matrix(x):
        x.uc.mem_write(x.reg('eax'), bytes(x.uc.mem_read(x.reg('ecx'), 48)))
        x.ret()
    m.hook(0x401CD0, 'matrix_copy_stub', callback=matrix)
    # Disabled branches exit this slice before any pose stores.
    target = 0x474A60 if selector == 3 else 0x478F53
    start = 0x474730 if selector == 3 else 0x4789D0
    end = target if disabled else (0x4747F6 if selector == 3 else 0x478AA0)
    m.run(start, (bits(0.02),), end=end, ebx=PHYSICS, eax=PHYSICS)
    return dict(selector=selector, disabled=disabled,
                position=[m.get(ACTOR + 0x60 + i * 4, 'f') for i in range(3)],
                velocity=[m.get(ACTOR + 0x70 + i * 4, 'f') for i in range(3)],
                angular=[m.get(ACTOR + 0xD0 + i * 4, 'f') for i in range(3)],
                hooks=m.events)


def driver_case(image, human, dt):
    m = fixture(image, 1 if human else 3)
    if human:
        m.allowed += [(0x4615E0, 0x4617C9)]
        m.put(ACTOR + 0x2C, 0x1130000)
        m.put(0x1130000, 0x1131000)
        m.put(0x1131040, STOP + 0x230)
        m.hook(STOP + 0x230, 'human_controller_input_stub')
        m.hook(0x461A60, 'processed_controls_stub', 8)
        # Selector1, driver+284 zero: transmission-management cases3/4 inactive.
        m.put(RIDE + 0x424, 0, 'B')
        m.put(0x7306C4, 0, 'B')
        start = 0x4615E0
    else:
        m.allowed += [(0x45E970, 0x45FAEC), (0x4740C0, 0x4740C6)]
        m.hook(0x45FB30, 'ai_ground_history_stub')
        m.hook(0x45E370, 'ai_goal_outputs_zero_stub', 12)
        m.hook(0x45E7B0, 'ai_gear_decision_stub', 4)
        m.hook(0x45E710, 'ai_state_callback_stub')
        m.hook(0x465530, 'ai_drive_state_predicate_stub', value=0)
        m.scalar_hook(0x583EA0, 'ai_curve_zero_stub', 4)
        m.put(DRIVER + 0x23C, 0x1132000)
        # Actual history arithmetic needs bounded storage even on airborne path.
        for offset in (0, 0x28, 0x50):
            m.put(0x1132000 + offset + 5, 4, 'B')
            m.put(0x1132000 + offset + 0x10, 0x1133000 + offset)
        start = 0x45E970
    m.run(start, (bits(dt),), ecx=DRIVER)
    return dict(human=human, dt=dt, selector=m.get(ACTOR + 0x28),
                physics_query=0x45F550 in m.visited,
                physics_integration=any(x in m.visited for x in (0x474A90, 0x4774A0)),
                control_word=m.get(DRIVER + 0x1D0, 'H'), hooks=m.events)


def snapshot_case(image, dt):
    m = fixture(image, 3)
    m.allowed += [(0x4267A0, 0x42681B), (0x426930, 0x426A5C)]
    for base, values in ((0x60, (10.0, 20.0, 30.0)),
                          (0x70, (1.0, 2.0, 3.0)),
                          (0xD0, (4.0, 5.0, 6.0))):
        for i, value in enumerate(values):
            m.put(ACTOR + base + i * 4, value, 'f')
    for base in (0x80, 0xE0):
        for i in range(3):
            m.put(ACTOR + base + i * 4, -99.0, 'f')
    m.run(0x4267A0, (bits(dt),), end=0x42681B, esi=ACTOR)
    # Synthetic changes stand for the unexecuted integration/contact work.
    for base in (0x70, 0xD0):
        for i in range(3):
            at = ACTOR + base + i * 4
            m.put(at, m.get(at, 'f') + 0.5 * (i + 1), 'f')
    m.run(0x426930, (bits(dt),), end=0x426A5C, eax=ACTOR)
    return dict(dt=dt,
                saved_position=[m.get(ACTOR + 0x490 + i * 4, 'f') for i in range(3)],
                saved_velocity=[m.get(ACTOR + 0x4A0 + i * 4, 'f') for i in range(3)],
                linear_delta_rate=[m.get(ACTOR + 0x80 + i * 4, 'f') for i in range(3)],
                angular_delta_rate=[m.get(ACTOR + 0xE0 + i * 4, 'f') for i in range(3)])


def suite(image):
    thresholds = [struct.unpack('<f', image.read(at, 4))[0] for at in (0x6FFE14, 0x6FFF10)]
    return dict(
        evidence='original x86 emulated on synthetic fixtures with explicit stubs',
        fpu_control_word='007F',
        physics=[physics_case(image, s, branch, dt) for s in (1, 3)
                 for branch in (0, 1) for dt in (0.0, 1.0 / 60, 0.03)],
        state_gates=[physics_case(image, s, 0, 0.02, state, wheels)
                     for s in (1, 3) for state, wheels in ((1, 4), (2, 0))],
        dispatch=[dispatch_case(image, s, branch, 0.02, active)
                  for s in (1, 3) for branch in (0, 1) for active in (False, True)],
        unique_group=unique_group_case(image, 0.004),
        marked_group=unique_group_case(image, 0.004, True),
        wrong_group=dispatch_case(image, 3, 1, 0.02, True, False),
        intervals=[group_interval_case(image, *v) for v in
                   ((0.02, 0.0, 0.012), (0.02, 0.0, 0.002),
                    (0.004, 0.0, -1.0), (0.02, 0.01, 0.013))],
        thresholds=[physics_case(image, s, 1, value)
                    for s, threshold in zip((1, 3), thresholds)
                    for value in (struct.unpack('<f', struct.pack('<I', bits(threshold)-1))[0],
                                  threshold,
                                  struct.unpack('<f', struct.pack('<I', bits(threshold)+1))[0])],
        publication=[publication_case(image, s, disabled)
                     for s in (1, 3) for disabled in (False, True)],
        drivers=[driver_case(image, human, 0.02) for human in (True, False)],
        snapshots=[snapshot_case(image, dt) for dt in (0.0, 0.02)],
    )

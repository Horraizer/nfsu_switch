"""Isolated original x86 contact witnesses on explicitly synthetic memory."""

import struct
from physics256_native import Machine, STOP, bits
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_FPTAG, UC_X86_REG_FPSW

BODY_A, BODY_B = 0x1100000, 0x1101000
ACTOR_A, ACTOR_B = 0x1102000, 0x1103000
RIGID_A, RIGID_B = 0x1104000, 0x1105000
SHAPE_A, SHAPE_B = 0x1106000, 0x1107000
PAIR, JOB, RECORD, CONFIG = 0x1108000, 0x1109000, 0x110A000, 0x110B000
NORMAL, POINT = 0x110C000, 0x110C010
BOUNDARIES = (0x471DE0, 0x472042, 0x46B9E2, 0x46BD89, 0x46C09B,
              0x46C14E, 0x465DB4, 0x465EB4, 0x466094, 0x466283,
              0x46C425, 0x46C4BA, 0x46C5D2, 0x46C5E4, 0x46A29A,
              0x58503E, 0x5B0668)


def vector(m, at, values=None):
    if values is None:
        return [m.get(at + i * 4, 'f') for i in range(3)]
    for i, v in enumerate(values):
        m.put(at + i * 4, v, 'f')


def matrix(m, at):
    for i in range(16):
        m.put(at + i * 4, float(i % 5 == 0), 'f')


def snapshot(m):
    return [dict(body='%08X' % body, actor='%08X' % actor, rigid='%08X' % rigid,
                 position=vector(m, rigid + 0x20),
                 orientation=[m.get(rigid + 0x30 + i * 4, 'f') for i in range(16)],
                 velocity=vector(m, rigid + 0xA0), angular_velocity=vector(m, rigid + 0xC0),
                 linear_momentum=vector(m, rigid + 0x70), angular_momentum=vector(m, rigid + 0x80),
                 state=m.get(actor + 0x38), state_timer=m.get(actor + 0x4CC, 'f'),
                 inverse_mass=m.get(rigid + 0x148, 'f'),
                 inverse_inertia=[m.get(rigid + 0x150 + i * 4, 'f') for i in range(16)],
                 actor_velocity=vector(m, actor + 0x70))
            for body, actor, rigid in ((BODY_A, ACTOR_A, RIGID_A), (BODY_B, ACTOR_B, RIGID_B))]


class ContactMachine(Machine):
    def __init__(self, image):
        self.image = image
        self.executed = {}
        self.observers = {}
        super().__init__(image)

    def run(self, *args, **kwargs):
        # Each top-level invocation is an isolated ABI boundary. No x87
        # argument is supplied; discard a preceding floating return value.
        self.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
        self.uc.reg_write(UC_X86_REG_FPSW, 0)
        return super().run(*args, **kwargs)

    def instruction(self, uc, address, size, data):
        if address != self.end:
            original = self.image.read(address, size)
            if bytes(uc.mem_read(address, size)) != original:
                raise RuntimeError('modified original instruction at %08X' % address)
            # The effects-tail entry is intercepted, so it is a visited
            # boundary but its original instruction does not execute.
            if address not in self.hooks:
                self.executed[address] = size
        if address in self.observers:
            self.observers[address](self)
        super().instruction(uc, address, size, data)


def fixture(image, offset, velocity_a, velocity_b, mass=2.0, state=0):
    m = ContactMachine(image)
    # Only authenticated executable sections can execute. Imported calls,
    # generated trampolines and data execution are not permitted.
    pe = struct.unpack_from('<I', image.data, 0x3C)[0]
    count = struct.unpack_from('<H', image.data, pe + 6)[0]
    optsize = struct.unpack_from('<H', image.data, pe + 20)[0]
    base = struct.unpack_from('<I', image.data, pe + 52)[0]
    for i in range(count):
        at = pe + 24 + optsize + 40 * i
        va, size = struct.unpack_from('<II', image.data, at + 12)
        flags = struct.unpack_from('<I', image.data, at + 36)[0]
        if flags & 0x20000000:
            m.allowed.append((base + va, base + va + size))
    # Original scalar dispatch initialization; synthetic selector 0 avoids
    # registry/CPU discovery. It copies the original fallback function table.
    m.run(0x5B065B, (0,))
    for body, actor, rigid, shape, pos, vel in (
            (BODY_A, ACTOR_A, RIGID_A, SHAPE_A, (0, 0, 0), velocity_a),
            (BODY_B, ACTOR_B, RIGID_B, SHAPE_B, offset, velocity_b)):
        m.put(body, 0x6C8798)
        m.put(body + 0x10, shape)
        m.put(body + 0x16, 1, 'B')
        m.put(body + 0x60, 1)
        m.put(body + 0x6C, rigid)
        m.put(body + 0x140, actor)
        m.put(actor + 0x14, CONFIG)
        m.put(actor + 0x28, 3)
        m.put(actor + 0x38, state)
        m.put(actor + 0x3CC, 4)
        m.put(actor + 0x4CC, 0.75, 'f')
        vector(m, rigid + 0x20, pos)
        vector(m, actor + 0x60, pos)
        matrix(m, rigid + 0x30)
        matrix(m, actor + 0x90)
        matrix(m, rigid + 0x150)
        m.put(rigid + 0x148, 1.0 / mass, 'f')
        vector(m, rigid + 0xA0, vel)
        vector(m, actor + 0x70, vel)
        vector(m, rigid + 0x70, [v * mass for v in vel])
        m.run(0x473CA0, tuple(bits(v) for v in (-1, -1, -1, 1, 1, 1)), esi=shape)
    m.put(PAIR + 0x258, BODY_A)
    m.put(PAIR + 0x25C, BODY_B)
    m.put(PAIR + 0x264, RECORD)
    m.put(RECORD + 0x64, PAIR)
    m.put(JOB + 0xC, BODY_A)
    m.put(JOB + 0x10, BODY_B)
    m.put(JOB + 0x14, PAIR)
    m.put(0x78A344, 0, 'B')
    m.put(0x7361A8, 0)
    m.put(0x78A345, 1, 'B')
    m.put(0x73AD34, 5.0, 'f')
    m.put(0x7361F8, CONFIG + 0x1000)
    m.put(CONFIG + 0x12D0, 12345)
    m.run(0x4727C0, ecx=PAIR, eax=SHAPE_A, edx=SHAPE_B)
    return m


def query(m):
    m.run(0x472830, (RIGID_A + 0x30, RIGID_B + 0x30, RIGID_B + 0x20),
          esi=PAIR, edi=RIGID_A + 0x20)
    dirty = m.get(PAIR + 0x240, 'B')
    m.run(0x472950, eax=PAIR)
    return dict(dirty_before_query=dirty, dirty_after_query=m.get(PAIR + 0x240, 'B'),
                scalar=m.get(PAIR + 0x10, 'f'), flag14=m.get(PAIR + 0x14, 'B'),
                flag15=m.get(PAIR + 0x15, 'B'),
                witness_a=vector(m, PAIR + 0x20), witness_b=vector(m, PAIR + 0x30))


def contact_case(image, name, offset, va, vb, mass=2.0, state=0,
                 solve=True, accumulated=(0, 0, 0), magnitude_sum=0):
    m = fixture(image, offset, va, vb, mass, state)
    vector(m, RECORD + 0x50, accumulated)
    m.put(RECORD + 0x60, magnitude_sum, 'f')
    before = snapshot(m)
    witness = query(m)
    m.run(0x46B8C0, edi=JOB, eax=NORMAL, ecx=POINT)
    geometry = dict(normal=vector(m, NORMAL), point=vector(m, POINT))
    hits, applied, after_hit, solve_gates = [], [], [], []

    def hit(x):
        sp = x.reg('esp')
        hits.append(dict(site='00465830', record='%08X' % x.get(sp + 4),
                         point=vector(x, x.get(sp + 8)), impulse=vector(x, x.get(sp + 12)),
                         tangent_speed=x.get(sp + 16, 'f'), bodies=snapshot(x)))

    def apply(x):
        sp = x.reg('esp')
        event = dict(return_address='%08X' % x.get(sp), body='%08X' % x.reg('ecx'),
                            impulse=vector(x, x.get(sp + 4)), arm=vector(x, x.get(sp + 8)),
                            bodies=snapshot(x))
        applied.append(event)
        x.observers[x.get(sp)] = lambda y: event.update(after=snapshot(y))

    m.observers[0x465830] = hit
    m.observers[0x46A260] = apply
    m.observers[0x46BD89] = lambda x: solve_gates.append(
        dict(site='0046BD89', normal_relative_velocity=x.get(x.reg('esp') + 0x10, 'f')))
    m.observers[0x4658B5] = lambda x: after_hit.append(dict(
        site='004658B5', bodies=snapshot(x), record_gate=x.get(RECORD + 0x10),
        accumulated=vector(x, RECORD + 0x50), magnitude_sum=x.get(RECORD + 0x60, 'f')))
    # The remainder dispatches effects/audio/feedback, outside this body probe.
    m.hook(0x4658B5, 'skip_current_hit_effect_tail_4658B5_to_465CF2',
           callback=lambda x: x.uc.reg_write(UC_X86_REG_EIP, 0x465CF2))
    if solve:
        m.run(0x46BAE0, (JOB,))
    return dict(name=name, input=dict(offset=offset, velocity_a=va, velocity_b=vb,
                mass=mass, state=state, solver_invoked=solve,
                accumulated=accumulated, magnitude_sum=magnitude_sum), before=before,
                witness=witness, geometry=geometry, hits=hits, applied=applied,
                after_hit=after_hit, solve_gates=solve_gates,
                after=snapshot(m), accumulated=vector(m, RECORD + 0x50),
                magnitude_sum=m.get(RECORD + 0x60, 'f'), record_gate=m.get(RECORD + 0x10),
                boundaries={'%08X' % a: a in m.visited for a in BOUNDARIES},
                hooks=m.events, executed=m.executed)


def cache_case(image):
    m = fixture(image, (8, 0, 0), (0, 0, 0), (0, 0, 0))
    first = query(m)
    # A non-cubic shape makes a rotation-only stale cache observable.
    m.run(0x473CF0, tuple(bits(v) for v in (-2, -1, -1, 2, 1, 1)), eax=SHAPE_B)
    m.put(PAIR + 0x240, 1, 'B')
    resized = query(m)
    # Rotate B 90 degrees around z with unchanged origins.
    for i, v in enumerate((0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)):
        m.put(RIGID_B + 0x30 + 4 * i, float(v), 'f')
    rotated_clean = query(m)
    m.put(PAIR + 0x240, 1, 'B')
    rotated_dirty = query(m)
    m.put(RIGID_B + 0x20, 9.0, 'f')
    moved = query(m)
    return dict(first=first, resized=resized, rotated_clean=rotated_clean,
                rotated_dirty=rotated_dirty, moved=moved, hooks=m.events,
                executed=m.executed)


def suite(image):
    cases = [
        ('head_on', (2.0005, 0, 0), (10, 0, 0), (-10, 0, 0), {}),
        ('offset', (2.0005, .7, 0), (10, 0, 0), (-10, 0, 0), {}),
        ('glancing', (2.0005, .7, .2), (3, 4, 0), (-2, -1, 0), {}),
        ('separating', (2.0005, 0, 0), (-10, 0, 0), (10, 0, 0), {}),
        ('stationary', (2.0005, 0, 0), (0, 0, 0), (0, 0, 0), {}),
        ('no_contact_query_only', (8, 0, 0), (10, 0, 0), (-10, 0, 0), {'solve': False}),
        ('distant_forced_solver', (8, 0, 0), (10, 0, 0), (-10, 0, 0), {}),
        ('hard_hit', (2.0005, .7, .2), (50, 0, 0), (-50, 0, 0), {}),
        ('already_state1', (2.0005, .7, .2), (50, 0, 0), (-50, 0, 0), {'state': 1}),
        ('seeded_accumulator', (2.0005, 0, 0), (10, 0, 0), (-10, 0, 0),
         {'accumulated': (100, 2, 3), 'magnitude_sum': 101}),
        ('light_mass', (2.0005, 0, 0), (10, 0, 0), (-10, 0, 0), {'mass': 1}),
        ('slow_closing', (2.0005, 0, 0), (.001, 0, 0), (-.001, 0, 0), {}),
        ('touching_origin_fallback', (2, 0, 0), (10, 0, 0), (-10, 0, 0), {}),
        ('overlap_without_separation_pass', (1.9, 0, 0), (10, 0, 0), (-10, 0, 0), {}),
    ]
    result = [contact_case(image, name, off, va, vb, **options)
              for name, off, va, vb, options in cases]
    return dict(cases=result, cache=cache_case(image))

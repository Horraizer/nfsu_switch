"""Original AI x86 on explicit synthetic state; no game process or physics."""

import struct
from unicorn.x86_const import UC_X86_REG_EIP
from physics256_native import Machine, bits, ACTOR, DRIVER, RIDE, PHYSICS, STATUS, STOP

HISTORY = 0x1140000
ROUTE = DRIVER + 0x10
OUT = 0x1150000

# Each interval is an instruction-aligned original routine (or named slice).
EXEC_RANGES = [
    (0x45E970, 0x45FAEC), (0x45E370, 0x45E515),
    (0x45FB30, 0x45FC87), (0x45FC90, 0x460084),
    (0x45E7B0, 0x45E96D), (0x460100, 0x46014B),
    (0x45E710, 0x45E741), (0x45E750, 0x45E7A7),
    (0x45E520, 0x45E57F), (0x45E580, 0x45E5B9),
    (0x45E650, 0x45E70D), (0x5842D0, 0x58430E),
    (0x45E180, 0x45E1AB), (0x45DB30, 0x45DB48),
    (0x4590A0, 0x4592E4), (0x459710, 0x459867),
    (0x4592F0, 0x4594C2),
    (0x564DB0, 0x564EF6), (0x564CD0, 0x564DA8),
    (0x583EA0, 0x583F18), (0x583F20, 0x583FE7),
    (0x4387E0, 0x4387F6), (0x674898, 0x67490D),
    (0x5672B0, 0x567304), (0x567370, 0x5673DC), (0x5673E0, 0x56744D),
    (0x432C90, 0x432CBF), (0x42AB50, 0x42AB73),
    (0x4740C0, 0x4740C6), (0x460090, 0x4600A6),
    (0x689FD0, 0x689FE3), (0x689FF0, 0x689FFB),
]


def fixture(image, points, speed=20.0, target=30.0, category=1,
            heading=0, enabled=True, grounded=True, recovery=False):
    m = Machine(image)
    m.allowed = list(EXEC_RANGES)
    # Two recovered startup initializers, not zero-filled BSS defaults.
    m.run(0x689FD0)
    m.run(0x689FF0)
    m.put(DRIVER, 0x6C8C18)
    m.put(DRIVER + 0x1F0, ACTOR)
    m.put(DRIVER + 0x1FC, 1.0, 'f')
    m.put(DRIVER + 0x218, 1, 'B')
    m.put(DRIVER + 0x220, int(recovery), 'B')
    m.put(DRIVER + 0x244, 0x1151000)
    m.put(ACTOR + 0x10, RIDE)
    m.put(ACTOR + 0x14, 0x1152000)
    m.put(0x1152004, 2)
    m.put(ACTOR + 0x20, RIDE)
    m.put(ACTOR + 0x24, 3)
    m.put(ACTOR + 0x28, 3)
    m.put(ACTOR + 0x30, DRIVER)
    m.put(ACTOR + 0x34, PHYSICS)
    m.put(ACTOR + 0x40, RIDE)
    m.put(ACTOR + 0x70, speed, 'f')
    m.put(ACTOR + 0x3D0, speed, 'f')
    m.put(ACTOR + 0x3D4, abs(speed), 'f')
    m.put(ACTOR + 0x3B4, heading, 'H')
    m.put(ACTOR + 0x3B8, 10.0, 'f')
    m.put(ACTOR + 0x350, int(grounded), 'B')
    m.put(ACTOR + 0x351, int(grounded), 'B')
    m.put(RIDE + 0x1C0, 0.0, 'f')
    m.put(RIDE + 0x424, category, 'b')
    m.put(PHYSICS, 0x6C8468)
    m.put(PHYSICS + 0x4C, 0)
    m.put(0x78A344, 0, 'B')
    m.put(0x7361A8, 0)
    m.put(0x73619C, STATUS)
    m.put(STATUS, 4)
    m.put(STATUS + 4, 1)
    m.put(0x73AD34, 100000)
    m.put(0x78A2F0, 1003)
    m.put(0x735FB0, 0)
    # Constructor-shaped empty history, five steering / four speed samples.
    for i, offset in enumerate((0x230, 0x234, 0x238, 0x23C, 0x240)):
        ptr = HISTORY + i * 0x100
        m.put(DRIVER + offset, ptr)
        for sub in (0, 0x28, 0x50):
            m.put(ptr + sub + 4, 4, 'B')
            m.put(ptr + sub + 5, 5 if i < 3 else 4, 'B')
            m.put(ptr + sub + 0x10, ptr + sub + 0x14)
        m.put(ptr + 0x78, 30.0, 'f')
    m.put(ROUTE, len(points), 'B')
    for i, point in enumerate(points):
        for j, value in enumerate(point):
            m.put(ROUTE + 0x10 + i * 0x10 + j * 4, value, 'f')
    # Use the actual original input setter, not direct desired-speed stores.
    m.run(0x45E180, (bits(target), 0, 0, int(enabled)), ecx=DRIVER)
    return m


def controls(m):
    return dict(steering_word=m.get(DRIVER + 0x1D0, 'H'),
                gear_command=m.get(DRIVER + 0x1D4, 'i'),
                throttle=m.get(DRIVER + 0x1DC, 'f'),
                handbrake=m.get(DRIVER + 0x1E0, 'f'),
                brake=m.get(DRIVER + 0x1E4, 'f'),
                goal=[m.get(DRIVER + off, 'f') for off in (0x1F4, 0x1F8)],
                speed_error=m.get(HISTORY + 0x300 + 0x7C, 'f'),
                steering_error=m.get(HISTORY + 0x7C, 'f'))


def driver_case(image, name, points, **kwargs):
    m = fixture(image, points, **kwargs)
    frames = []
    for i in range(6):
        m.put(0x73AD34, 100000 + i * 20)
        m.run(0x45E970, (bits(0.02),), ecx=DRIVER)
        frames.append(controls(m))
    inputs = dict(points=points, dt=0.02, frames=6, speed=20.0, target=30.0,
                  category=1, heading=0, enabled=True, grounded=True, recovery=False,
                  frozen_pose=True, x87_control_word='007F')
    inputs.update(kwargs)
    return dict(name=name, inputs=inputs,
                frames=frames, hooks=m.events,
                visited=['%08X' % x for x in sorted(m.visited)])


def suite(image):
    straight = [(10.0, 0.0, 0.0), (100.0, 0.0, 0.0)]
    cases = [
        driver_case(image, 'straight-accelerate', straight),
        driver_case(image, 'corner-positive', [(10.0, 10.0, 0.0), (60.0, 60.0, 0.0)]),
        driver_case(image, 'corner-negative', [(10.0, -10.0, 0.0), (60.0, -60.0, 0.0)]),
        driver_case(image, 'braking', straight, speed=30.0, target=10.0),
        driver_case(image, 'disabled', straight, enabled=False),
        driver_case(image, 'airborne', straight, grounded=False),
        driver_case(image, 'reverse-command', straight, speed=0.0, target=-10.0),
        driver_case(image, 'special-flag', straight, recovery=True),
        driver_case(image, 'corner-category3', [(10.0, 10.0, 0.0), (60.0, 60.0, 0.0)], category=3),
    ]
    return dict(cases=cases, reset=reset_case(image), action_exit=action_exit_case(image),
                producers=[producer_case(image, mode, cap) for mode, cap in
                           [('action', 100.0), ('action', 12.0), ('route', 100.0),
                            ('route', 12.0), ('zero-route', 100.0)]],
                original_runtime=None)


def action_exit_case(image):
    m = fixture(image, [(10.0, 0.0, 0.0), (100.0, 0.0, 0.0)], recovery=True)
    m.allowed.append((0x453830, 0x45385D))
    m.put(OUT, ACTOR)
    m.put(ACTOR + 0x88C, 1, 'B')
    m.run(0x45E970, (bits(0.02),), ecx=DRIVER)
    before = controls(m)
    m.run(0x453830, (OUT,))
    state = dict(flag=m.get(DRIVER + 0x220, 'B'), actor_flag=m.get(ACTOR + 0x88C, 'B'),
                 timers=[m.get(DRIVER + offset, 'f') for offset in (0x210, 0x21C)])
    m.run(0x45E970, (bits(0.02),), ecx=DRIVER)
    after = controls(m)
    m.run(0x45E970, (bits(0.02),), ecx=DRIVER)
    return dict(before=before, after=after, later=controls(m), state=state, hooks=m.events)


def reset_case(image):
    m = fixture(image, [(10.0, 10.0, 0.0), (60.0, 60.0, 0.0)])
    m.run(0x45E970, (bits(0.02),), ecx=DRIVER)
    before = controls(m)
    m.run(0x45E650, ecx=DRIVER)
    m.run(0x45E750, ecx=DRIVER)
    history = []
    for i in range(4):
        ptr = HISTORY + i * 0x100
        history.append(dict(current=m.get(ptr + 0x7C, 'f'),
                            previous=m.get(ptr + 0x80, 'f'),
                            counts=[m.get(ptr + x + 6, 'B') for x in (0, 0x28, 0x50)],
                            buffers=[[m.get(ptr + sub + 0x14 + j*4, 'f')
                                      for j in range(m.get(ptr + sub + 5, 'B'))]
                                     for sub in (0, 0x28, 0x50)]))
    return dict(before=before, after=controls(m), history=history, hooks=m.events)


def producer_case(image, mode, cap):
    """Execute controller control publication through actual setter; stop at 44C7F1.

    The route/action object is synthetic. Strategy cap is the only hook.
    Catch-up is gated out via synthetic null actor status, not stubbed.
    """
    m = fixture(image, [(10.0, 0.0, 0.0), (100.0, 0.0, 0.0)])
    controller, action, route, strategy = 0x1160000, 0x1161000, 0x1162000, 0x1164000
    m.allowed += [(0x44C550, 0x44C7F1), (0x44BD10, 0x44C00B),
                  (0x45FAF0, 0x45FB27), (0x459980, 0x459ABA),
                  (0x44CA10, 0x44CA1E), (0x442110, 0x442131)]
    m.put(controller, 0x6C89F0)
    m.put(controller + 4, ACTOR)
    m.put(controller + 8, DRIVER)
    m.put(controller + 0x3F8, action)
    m.put(controller + 0x3FC, strategy)
    m.put(controller + 0x3D8, route)
    m.put(controller + 0x4B8, 0.5, 'f')
    m.put(controller + 0x478, 0)
    m.put(route, 0 if mode == 'zero-route' else 0x1163000)
    m.put(route + 8, 1)
    m.uc.mem_write(action + 0x20, bytes(m.uc.mem_read(ROUTE, 0x1C0)))
    m.put(action + 0x1E4, 25.0, 'f')
    m.put(action + 0x1E8, 0.75, 'f')
    m.put(action + 0x1EC, 0.125, 'f')
    m.put(action + 0x1F0, int(mode == 'route'), 'B')
    m.put(action + 0x1F1, 1, 'B')
    m.put(0x7360B8, 0)
    m.put(0x736090, 0)
    m.put(0x736150, 0x1165000)
    m.put(0x1165009, 0, 'B')
    m.put(0x1165004, 1, 'H')
    m.put(0x77AF30, 0x1166000)
    m.put(0x1166000 + 0x27C + 0x88 + 0x6B, 40, 'B')
    m.put(strategy, 0x1167000)
    m.put(0x1167010, STOP + 0x300)
    m.put(OUT + 0x20, cap, 'f')
    # Explicit scalar-return trampoline; cap comes from the case, not retail.
    at = STOP + 0x400
    m.uc.mem_write(at, b'\xd9\x05' + struct.pack('<I', OUT + 0x20) + b'\xc3')
    m.allowed.append((at, at + 7))
    m.hook(STOP + 0x300, 'synthetic_strategy_speed_cap',
           callback=lambda x: x.uc.reg_write(UC_X86_REG_EIP, at))
    m.run(0x44C550, (controller,), end=0x44C7F1)
    return dict(mode=mode, cap=cap, desired_speed=m.get(DRIVER + 0x200, 'f'),
                duration=m.get(DRIVER + 0x204, 'f'),
                auxiliary=m.get(DRIVER + 0x208, 'f'), enabled=m.get(DRIVER + 0x20C, 'B'),
                hooks=m.events, stopped_before_controller_tail=True)

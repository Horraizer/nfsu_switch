"""Execute original PC rival setup, path-body and traffic manager gates."""
import argparse
from hashlib import sha256
from pathlib import Path
import struct

from campaign_ai_audit import authenticated, FRONT_SHA, jdlz, offsets, u32
from controller293_native import controls, fixture as driver_fixture
from physics256_native import (
    Machine, ACTOR, CONFIG, DRIVER, PHYSICS, RIDE, STACK, STATUS, STOP, WORLD, bits,
)
from world_body_trace import Image


EXE_SHA = "c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7"


def native(image, anchors, speed=5.0, support=1, race_ticks=120000, disabled=0,
           activate=0, lock=0, action=0, context=0, state=4, driver=3, finished=0):
    c, d, a, flow, table = 0x1160000, 0x1161000, 0x1162000, 0x1163000, 0x1164000
    m = Machine(image)
    m.allowed = [(0x44B060, 0x44B1A4), (0x421A30, 0x421A46), (0x44CA10, 0x44CA1E),
                 (0x421400, 0x421545), (0x43A2E0, 0x43A331)]
    m.put(c, table)
    m.put(table + 0x58, 0x44CA10)
    m.put(c + 4, ACTOR)
    m.put(c + 8, d)
    m.put(d + 0x10, driver, 'b')
    m.put(0x7361F0, flow)
    m.put(flow, state)
    m.put(c + 0x424, lock)
    m.put(c + 0x428, activate)
    m.put(ACTOR + 0x3D0, speed, 'f')
    m.put(ACTOR + 0x3CC, support)
    m.put(ACTOR + 0x828, disabled, 'B')
    if action is not None:
        m.put(c + 0x3F8, a)
        m.put(a + 0x1F8, action)
    m.put(ACTOR + 0x14, CONFIG)
    m.put(CONFIG + 0x735, context, 'b')
    m.put(0x7361F8, WORLD)
    m.put(WORLD + 0x1C, 1)
    for j, point in enumerate(anchors):
        for k, v in enumerate(point):
            m.put(WORLD + 0x280 + j * 0x10 + k * 4, v, 'f')
    m.put(0x73619C, STATUS)
    m.put(ACTOR + 0x1C, STATUS + 0x100)
    m.put(STATUS + 0x14, race_ticks)
    m.put(STATUS + 0x110, finished, 'H')
    m.run(0x44B060, (c,), budget=500)
    assert not m.events
    return bool(m.reg('eax') & 255)


def verify_partial(image):
    controller, action = 0x1160000, 0x1180000
    start, retained, blocking, other = 0x44B754, 0x44BAB9, 0x44B770, 0x44B7B2
    all_cases = []
    for selector in (1, 2, 3, 4):
        for kind in (0, 1, 5, 8, 10, 17, 19):
            machine = Machine(image)
            machine.allowed = [(start, blocking)]
            machine.put(controller + 4, ACTOR)
            machine.put(controller + 0x3F8, action)
            machine.put(ACTOR + 0x28, selector)
            machine.put(action + 0x1F8, kind)
            results = []
            for name, address in [('retain', retained), ('block', blocking), ('other', other)]:
                def observe(m, label=name):
                    results.append(label)
                    m.finished = True
                    m.uc.emu_stop()
                machine.hook(address, name + '-endpoint', callback=observe)
            machine.run(start, ebx=controller, budget=50)
            expected = 'retain' if selector == 4 else 'block' if kind == 5 else 'other'
            assert results == [expected], (selector, kind, expected, results)
            all_cases.append((selector, kind, results[0]))
    return len(all_cases)


def verify_admission(image):
    unused = (9999999.0,) * 3
    rows = [
        ('distance-below', ((119.99, 0, 0), unused), {}, False),
        ('distance-at', ((120, 0, 0), unused), {}, True),
        ('distance-above', ((120.01, 0, 0), unused), {}, True),
        ('two-axes', ((72, 96, 0), unused), {}, True),
        ('z-axis', ((0, 0, 120), unused), {}, True),
        ('second-slot-nearer', ((160, 0, 0), (5, 0, 0)), {}, False),
    ]
    for key, value in [('speed', 4.99), ('support', 0), ('race_ticks', 119999),
                       ('disabled', 1), ('lock', 1), ('activate', 1), ('state', 9),
                       ('action', 9), ('action', 10), ('action', 11), ('action', 12),
                       ('action', 14), ('action', None), ('context', -2), ('driver', 2),
                       ('finished', 1)]:
        rows.append((f'{key}={value}', ((120, 0, 0), unused), {key: value}, False))
    for key in ('lock', 'activate'):
        rows.append((key + '=sentinel', ((120, 0, 0), unused), {key: 0x7FFFFFFF}, True))
    for name, anchors, knobs, expected in rows:
        actual = native(image, anchors, **knobs)
        assert expected == actual, (name, expected, actual)
    return len(rows)


def verify_road_time(image):
    # Observe the branch after timing, before subtype selection or RNG.
    count = 0
    for gate in (-1, 0):
        for actor_id in (100, 101, 102):
            for drag in (False, True):
                boundary = 48000 - 2000 * (actor_id - 100)
                for age in (0, 40, boundary - 1, boundary, boundary + 1):
                    m = Machine(image)
                    controller = 0x1160000
                    event = 0x1161000
                    track = 0x1162000
                    m.allowed = [(0x462840, 0x462963), (0x435CA0, 0x435D29)]
                    m.put(0x736174, track)
                    m.put(track + 0xB8, gate, 'h')
                    m.put(0x73619C, STATUS)
                    m.put(0x73AD34, 700000 + age)
                    m.put(STATUS + 0x10, 700000)
                    m.put(STATUS + 0x14, age)
                    m.put(ACTOR + 0xA, actor_id, 'h')
                    m.put(controller + 4, ACTOR)
                    m.put(event + 0xB, 4, 'B')
                    m.put(0x78A2F0, 1001)
                    m.put(0x78A344, int(drag), 'B')
                    accepted = []

                    def observe(x):
                        accepted.append(True)
                        x.finished = True
                        x.uc.emu_stop()

                    m.hook(0x4628EE, 'after-time-gate', callback=observe)
                    m.run(0x462840, (event,), ecx=controller, budget=150)
                    expect = (drag or age >= boundary) if gate == -1 else age > 0
                    assert bool(accepted) == expect, (gate, actor_id, drag, age, expect)
                    count += 1
    return count


def verify_drag_launch_entry(image):
    # Use the source's retained offset and execute its countdown function.
    # Observe the selected command/caller branch before any driver callback.
    controller = 0x1160000
    start_tick = 40000
    rows = [
        ("intro-invalid-go", 2, 0, 0, -.5, "road"),
        ("countdown-before-lead", 3, 0, 17999, -.5, "road"),
        ("countdown-at-lead", 3, 0, 18000, -.5, "road"),
        ("countdown-inside-lead", 3, 0, 18001, -.5, "rev"),
        ("go-negative-lead", 4, start_tick, 20001, -.5, "road"),
        ("intro-positive-delay", 2, 0, 0, .5, "hold"),
        ("countdown-positive-delay", 3, 0, 20001, .5, "hold"),
        ("go-before-delay", 4, start_tick, 1999, .5, "hold"),
        ("go-after-delay", 4, start_tick, 2000, .5, "road"),
    ]
    for name, stage, go, age, offset, expected in rows:
        m = Machine(image)
        m.allowed = [(0x44B240, 0x44B31C), (0x435F80, 0x435FDF)]
        m.put(controller + 0x4A0, offset, "f")
        m.put(0x73619C, STATUS)
        m.put(STATUS, stage)
        m.put(STATUS + 4, go)
        m.put(STATUS + 8, start_tick)
        m.put(STATUS + 12, 5 / 3, "f")
        m.put(0x73AD34, start_tick + age)
        observed = []

        for branch, address in (("road", 0x44B31C), ("rev", 0x44B27E), ("hold", 0x44B2E8)):
            def observe(machine, label=branch):
                observed.append(label)
                machine.finished = True
                machine.uc.emu_stop()

            m.hook(address, branch + "-endpoint", callback=observe)

        m.run(0x44B240, ebx=controller, budget=150)
        assert observed == [expected], (name, expected, observed)
    return len(rows)


def verify_throttle(image):
    controller = 0x1160000
    racer = 0x1161000
    count = 0
    for drag in (False, True):
        for index, cap in enumerate((.93, .97, 1.)):
            for moving_target, moving_enabled in ((0., False), (0., True), (35., True)):
                desired = 100. if moving_target else 0.
                enabled = moving_target != 0.
                m = driver_fixture(image, [(10, 0, 0), (100, 0, 0)],
                                   speed=0, target=desired, enabled=enabled)
                m.allowed.extend([(0x4B5F73, 0x4B5F7F), (0x422523, 0x422538),
                                  (0x434BB0, 0x434C03), (0x45DB80, 0x45DBAB),
                                  (0x44CA10, 0x44CA1E)])
                m.put(CONFIG + 0x59E8, index)
                m.run(0x4B5F73, end=0x4B5F7F, ebx=CONFIG)
                m.run(0x422523, end=0x422538, esi=ACTOR)
                assert m.get(DRIVER + 0x1FC) == bits(cap)
                m.put(ACTOR + 0x2C, controller)
                m.put(controller, 0x6C89F0)
                m.put(controller + 4, ACTOR)
                m.put(racer, ACTOR)
                m.put(STATUS + 0x520, racer)
                m.put(0x78A344, int(drag), "B")

                def stage(value):
                    m.put(STATUS, value)
                    m.put(STACK + 0x18, STATUS)
                    m.run(0x434BB0, end=0x434C03, edx=STATUS, ebp=0x520, ebx=0)

                stage(3)
                m.run(0x45E970, (bits(1 / 60),), ecx=DRIVER)
                held = controls(m)
                assert (held["throttle"], held["brake"]) == (1., 1.)
                m.put(DRIVER + 0x1D4, 4)
                m.put(DRIVER + 0x1E0, 1., "f")
                stage(4)
                released = controls(m)
                assert (released["throttle"], released["brake"],
                        released["handbrake"], released["gear_command"]) == (1., 0., 0., -1)
                assert m.get(DRIVER + 0x228, "B") == 0
                m.run(0x45E180, (bits(moving_target), 0, 0, int(moving_enabled)), ecx=DRIVER)
                m.run(0x45E970, (bits(1 / 60),), ecx=DRIVER)
                moving = controls(m)
                assert bits(moving["throttle"]) == bits(cap if moving_target else 0.)
                assert moving["brake"] == (1. if moving_enabled and not moving_target else 0.)
                assert not m.events
                count += 1
    return count


def verify_quick_drag_throttle(image):
    m = Machine(image)
    m.allowed = [(0x4B5C2B, 0x4B5C32), (0x4B5C55, 0x4B5C5A),
                 (0x4B58B0, 0x4B58BA), (0x4B5951, 0x4B596B),
                 (0x422523, 0x422538)]
    m.run(0x4B5C2B, end=0x4B55B0, eax=5)
    m.put(ACTOR + 0x30, DRIVER)
    for difficulty, cap in enumerate((.93, .97, 1.)):
        m.put(0x78A3FC, difficulty)
        m.run(0x4B58B0, end=0x4B58BA)
        m.run(0x4B5951, end=0x4B596B)
        m.run(0x422523, end=0x422538, esi=ACTOR)
        assert m.get(DRIVER + 0x1FC) == bits(cap)
    assert not m.events
    return 3


def verify_path_body(image):
    # Use the selector-4 constructor's open-spline descriptor and scalar
    # vector dispatch. The only synthetic instructions store an x87 return.
    curve, spline, points, result = 0x1160000, 0x1161000, 0x1162100, 0x1163000
    save = STOP + 0x500
    machine = Machine(image)
    machine.allowed = [
        (0x456830, 0x456901), (0x4638A0, 0x463905), (0x463D50, 0x463E7F),
        (0x4645D0, 0x4646C2), (0x59C850, 0x59CEB0),
        (0x59D350, 0x59D42D), (0x565200, 0x565360), (0x567000, 0x567800),
        (0x583EA0, 0x583F16), (0x674898, 0x67490D), (0x4387E0, 0x438820),
        (0x4010A0, 0x4010D0), (0x5AC443, 0x5AC4E8), (0x5B065B, 0x5B0762),
        (save, save + 7),
    ]
    machine.run(0x5B065B, (0,))
    machine.uc.mem_write(save, b"\xd9\x1d" + struct.pack("<I", result) + b"\xc3")

    def scalar(address, args=(), **registers):
        machine.run(address, args, **registers)
        machine.run(save)
        return machine.get(result, "f")

    def path(heights):
        for j, z in enumerate(heights):
            machine.put(points + j * 16, j * 10., "f")
            machine.put(points + j * 16 + 8, z, "f")
        machine.run(0x59CB20, eax=spline, ecx=len(heights))

    for offset, value in ((0x10, RIDE), (0x20, CONFIG), (0x28, 4), (0x34, PHYSICS)):
        machine.put(ACTOR + offset, value)
    machine.put(CONFIG + 0x110, 1000., "f")
    machine.put(CONFIG + 0x260, 1., "f")
    machine.put(RIDE + 0x410, 1)
    machine.put(RIDE + 0x41C, 1., "f")
    machine.put(RIDE + 0x420, curve)
    machine.put(RIDE + 0x424, 1, "B")
    machine.put(curve, 4., "f")
    machine.put(PHYSICS + 8, ACTOR)
    machine.put(PHYSICS + 0x10, 10., "f")
    machine.put(PHYSICS + 0x14, spline)
    machine.put(spline + 4, 2)
    machine.put(spline + 0x1A, 2, "B")
    machine.put(spline + 0x24, points)
    machine.put(spline + 0x28, 0x7376B0)
    machine.run(0x59C850, edi=spline)

    cap_cases = 0
    machine.put(PHYSICS + 0x1C, 1.5, "f")
    for gradient in (-.2, 0., .2):
        path([j * 10 * gradient for j in range(5)])
        slope = -9.8 * gradient / (1 + gradient * gradient) ** .5
        assert abs(scalar(0x4638A0, ecx=PHYSICS) - slope) < 3e-6
        for scale in (1., 1.25):
            machine.put(ACTOR + 0x404, scale, "f")
            for drafting in (0., .25):
                machine.put(ACTOR + 0x47C, drafting, "f")
                drag = (1 - drafting) * (30 / scale) ** 2 / 1000
                for limit in (0., 23.):
                    machine.put(RIDE + 0x42C, limit, "f")
                    drive = drag if limit and 30 / scale >= limit else 4.
                    value = scalar(0x456830, (ACTOR, bits(30.)))
                    assert abs(value - (drive - drag + slope)) < 4e-6, (value, drive, drag, slope)
                    cap_cases += 1

    machine.put(PHYSICS + 0x1C, 1.8, "f")
    takeoff_cases = (
        (2., 0., 0.), (2., -4., .384), (2., -8., 2.7235),
        (4., 0., 0.), (4., -4., 3.5195), (4., -8., 9.0235),
    )
    for rate, drop, expected in takeoff_cases:
        path([0., 0., 0., drop, 2 * drop])
        machine.put(PHYSICS + 0x20, rate, "f")
        value = scalar(0x463D50, (bits(.5),), eax=PHYSICS)
        assert abs(value - expected) < 4e-6, (rate, drop, expected, value)
    # Check the spline core independently of the port's projection and
    # distance-to-parameter adapter. Native type 2 agrees on in-range
    # point/tangent for this non-linear open path.
    heights = (0., 2., 7., -2., 3.)
    path(heights)
    spline_samples = 0
    for index in range(4):
        b, c = heights[index:index + 2]
        a = heights[index - 1] if index else 2 * b - c
        d = heights[index + 2] if index + 2 < len(heights) else 2 * c - b
        linear, quadratic, cubic = c - a, 2 * a - 5 * b + 4 * c - d, 3 * (b - c) + d - a
        for t in (.25, .75):
            parameter = index + t
            pos = (10 * parameter, 0., b + .5 * t * (linear + t * (quadratic + t * cubic)))
            tangent = (10., 0., .5 * (linear + t * (2 * quadratic + 3 * t * cubic)))
            for order, expected in enumerate((pos, tangent)):
                machine.run(0x59D350, (result, bits(parameter), order), eax=spline)
                got = tuple(machine.get(result + i * 4, "f") for i in range(3))
                assert got == expected, (parameter, order, expected, got)
                spline_samples += 1
    assert not machine.events
    return cap_cases, len(takeoff_cases), spline_samples


def verify_path_exit_timer(image):
    # Test the caller after the native 463D50 prediction has returned.
    # A short synthetic FLD supplies that one scalar, and game code does
    # the threshold test, conversion to ticks, and timer publication.
    controller, trampoline = 0x1160000, STOP + 0x540
    count = 0
    for prediction in (0., .9999, 1., 1.0001, 9.):
        for prior in (0, 800, 0x7FFFFFFF):
            m = Machine(image)
            m.allowed = [(trampoline, trampoline + 13), (0x463F6B, 0x463F9B),
                         (0x674898, 0x67490D)]
            m.put(trampoline + 20, prediction, "f")
            code = (b"\xd9\x05" + struct.pack("<I", trampoline + 20) + b"\xe9" +
                    struct.pack("<i", 0x463F6B - trampoline - 11))
            m.uc.mem_write(trampoline, code)
            m.put(PHYSICS + 8, ACTOR)
            m.put(ACTOR + 0x2C, controller)
            m.put(controller + 0x424, prior)
            m.run(trampoline, end=0x463F9B, ebx=PHYSICS, budget=100)
            assert not m.events
            expected = 4000 if prediction > 1. else prior
            assert m.get(controller + 0x424) == expected, (prediction, prior, expected)
            count += 1
    return count


def verify_traffic_pace_gate(image):
    # Supply the selected human and two native road-direction vtable results.
    # The original function reads both published track positions and speed
    # magnitudes, wraps the distance, and applies its own strict comparisons.
    traffic, human = ACTOR, ACTOR + 0x1000
    first, second, first_vtable, second_vtable = 0x1160000, 0x1161000, 0x1162000, 0x1163000
    count = 0
    for delta in (-251., -250., 0., 250., 251.):
        for same in (False, True):
            for speed in (19., 20., 21.):
                m=Machine(image)
                m.allowed=[(0x598A50,0x598B54),(0x41F5D0,0x41F5D4),(0x41F5F0,0x41F5F7),
                           (0x443D60,0x443DA9),(0x424E10,0x424E17)]
                m.put(traffic + 0x2c,first)
                m.put(human + 0x2c,second)
                m.put(first,first_vtable)
                m.put(second,second_vtable)
                m.put(first_vtable + 0x40,STOP + 0x600)
                m.put(second_vtable + 0x40,STOP + 0x610)
                m.hook(STOP + 0x600,"traffic direction",value=1)
                m.hook(STOP + 0x610,"human direction",value=int(same))
                m.hook(0x59B490,"selected human",pop=4,value=human)
                m.put(traffic + 0x814,600. + delta,"f")
                m.put(human + 0x814,600.,"f")
                m.put(traffic + 0x3D4,speed,"f")
                m.put(human + 0x3D4,20.,"f")
                m.put(0x7372B8,0)
                m.run(0x598A50,(traffic,),ecx=0x1166000,budget=200)
                result = bool(m.reg("eax") & 255)
                expected = (same and speed > 20.) if delta > 0. else (not same or speed < 20.)
                assert result == expected, (delta,same,speed,expected,result)
                count += 1
    return count


def verify_traffic_nearness(image):
    # Native proximity uses published participant progress slots and is
    # strictly inside the distance threshold. The veto also checks
    # race status stage 4 before applying the published-distance test.
    manager = 0x1160000
    count = 0
    for stage in (3, 4):
        for delta in (-251., -250., -249., 0., 249., 250., 251.):
            m = Machine(image)
            m.allowed = [(0x59AC80, 0x59ACD2), (0x59AD90, 0x59AE5A),
                         (0x421550, 0x4216C0), (0x421960, 0x421963),
                         (0x43A2E0, 0x43A332), (0x405600, 0x40561A),
                         (0x41F5F0, 0x41F5F7)]
            m.put(0x73619C, STATUS)
            m.put(STATUS, stage)
            m.put(0x7361F8, WORLD)
            m.put(0x7372B8, 0)
            m.put(0x734454, 0)
            m.put(WORLD + 0x2C0, 800., "f")
            m.put(WORLD + 0x2C4, 9999999., "f")
            m.put(ACTOR + 0x814, 800. + delta, "f")
            m.run(0x59AD90, (bits(800. + delta),), ecx=manager, budget=250)
            expected = stage == 4 and abs(delta) < 250.
            assert bool(m.reg("eax") & 255) == expected, (stage, delta, expected)
            assert not m.events
            count += 1
    return count


def verify_traffic_admission(image):
    # Execute 5988C0's policy after the controller status query. Record the
    # call order for both arms. Child predicates have explicit fixture inputs.
    count = 0
    for has_path in (0, 1):
        for physical in (0, 1):
            for pending in (0, 1):
                for permitted in (0, 1):
                    for close in (0, 1):
                        m = Machine(image)
                        local_frame = STACK + 0x100
                        m.allowed = [(0x5988F7, 0x598A48), (0x41F5F0, 0x41F5F7)]
                        m.put(local_frame - 8, ACTOR)
                        m.put(local_frame + 8, ACTOR + 0x1000)
                        m.put(ACTOR + 0x4D0, physical, "B")
                        m.put(ACTOR + 0x1814, 800., "f")
                        m.put(ACTOR + 0x4D1, pending, "B")
                        def speed_gate(machine):
                            assert machine.get(machine.reg("esp") + 4, "f") == 800.
                            machine.ret(value=permitted)

                        m.hook(0x598B60, "distance and speed", callback=speed_gate)
                        m.hook(0x59AC80, "near racing participants", pop=4, value=close)
                        m.hook(0x598A50, "heading and relative speed", pop=4, value=permitted)
                        m.hook(0x59AC20, "stage-4 proximity veto", pop=4, value=close)
                        m.run(0x5988F7, end=0x598A41, eax=has_path, ebp=local_frame, budget=150)
                        names = [entry["hook"] for entry in m.events]
                        if has_path:
                            bypass = physical and permitted
                            expected = bypass or pending or not close
                            order = ["distance and speed"] if physical else []
                            if not bypass and not pending:
                                order.append("near racing participants")
                        else:
                            expected = pending or (permitted and not close)
                            order = ["heading and relative speed", "stage-4 proximity veto"]
                        assert bool(m.get(local_frame - 1, "B")) == expected
                        assert names == order, (has_path, physical, pending, permitted, close, order, names)
                        count += 1
    return count


def verify_traffic_policy_dispatch(image):
    # Run both actual vtables, the actual one-human distance test, and
    # 5988C0. Supply one coherent human registration and both progress slots.
    traffic, human, view, manager = ACTOR, ACTOR + 0x1000, 0x116A000, 0x1160000
    controller, policy = 0x1161000, 0x1162000
    count = 0
    for authored in (False, True):
        for gap in (0., 49., 50., 249., 250., 251.):
            for speed in (19., 20., 21.):
                for exited in (False, True):
                    m = Machine(image)
                    m.allowed = [(0x5988C0, 0x598A4B), (0x598B60, 0x598C00),
                                 (0x59AC80, 0x59ACCC), (0x421550, 0x4216C0),
                                 (0x43A2E0, 0x43A332), (0x405600, 0x40561A),
                                 (0x443D60, 0x443DA9), (0x41F5D0, 0x41F5D4),
                                 (0x41F5F0, 0x41F5F7), (0x424E00, 0x424E07),
                                 (0x44D650, 0x44D65B), (0x5314D0, 0x5314D3),
                                 (0x577FD0, 0x577FD3)]
                    m.put(traffic + 0x2C, controller)
                    m.put(controller + 0x3FC, policy)
                    m.put(policy, 0x6C97B4 if authored else 0x6C97D4)
                    m.put(controller + 0x4D0, int(exited), "B")
                    m.put(0x7361F8, WORLD)
                    m.put(0x7372B8, 0)
                    m.put(WORLD + 0x2C0, 600., "f")
                    m.put(WORLD + 0x2C4, 9999999., "f")
                    m.put(traffic + 0x814, 600. + gap, "f")
                    m.put(human + 0x814, 600., "f")
                    m.put(human + 0x3D0, speed, "f")
                    m.hook(0x408200, "one human view", value=1)
                    m.hook(0x408210, "view", value=view)
                    m.hook(0x417490, "view actor", value=human)
                    m.hook(0x598A50, "non-authored requesting relocation", pop=4, value=1)
                    m.hook(0x59AC20, "non-authored near human veto", pop=4, value=int(gap < 250.))
                    m.run(0x5988C0, (traffic,), ecx=manager, budget=600)
                    expected = gap >= 250. or (authored and exited and (gap >= 50. or speed >= 20.))
                    assert bool(m.reg("eax") & 255) == expected, (authored, gap, speed, exited)
                    count += 1
    return count


def verify_traffic_authored_exit(image):
    controller, context, road, record = 0x1160000, 0x1161000, 0x1162000, 0x116A000
    count = 0
    for direction in (0, 1):
        for anchor in (-1, 0, 1, 16):
            for point in (0, 1, 14, 15, 16, 17, 18, 19):
                machine = Machine(image)
                machine.allowed = [(0x5835F0, 0x58368D), (0x582F50, 0x582F57)]
                machine.put(controller, 0x6B96B0)
                machine.put(controller + 0x3D8, context)
                machine.put(controller + 0x4D4, direction)
                machine.put(context, road)
                machine.put(context + 4, road + 0x27C + point * 0x88)
                machine.put(road + 0xA, 13, "h")
                machine.put(road + 0x10, 20, "h")
                machine.put(0x77AF30 + 13 * 4, road)
                machine.put(record, 13)
                machine.put(record + 4, anchor, "h")
                machine.put(record + 6, anchor, "h")
                machine.run(0x5835F0, ecx=controller, edi=record, budget=150)
                expected = point < max(1, anchor) if direction else point > (18 if anchor < 0 else anchor)
                assert bool(machine.reg("eax") & 255) == expected, (direction, anchor, point, expected)
                assert not machine.events
                count += 1
    return count


def verify_traffic_lane_direction(image):
    count = 0
    for reverse in (0, 1):
        for seed in (0, 0x3FFFFFDF, 0x3FFFFFE0, 0x3FFFFFFF, 0x40000000, 0x40000001, 0x7FFFFFFF, 0xFFFFFFFF):
            m = Machine(image)
            m.allowed = [(0x59AEA0, 0x59AF17), (0x4216F0, 0x421740)]
            m.put(0x7361F8, WORLD)
            m.put(WORLD + 0x2D0, seed)
            m.put(0x78A2FC, reverse)
            m.put(0x78A304, .5, "f")
            m.run(0x59AEA0, ecx=0x1160000, budget=100)
            # 4216F0 uses the active x87 single-precision control word.
            below_half = seed % 0x7FFFFFFF < 0x3FFFFFE0
            assert bool(m.reg("eax") & 255) == (below_half != bool(reverse))
            a = seed ^ 0x1D872B41
            b = (a >> 5) ^ a
            assert m.get(WORLD + 0x2D0) == ((b << 27) ^ b ^ a) & 0xFFFFFFFF
            assert not m.events
            count += 1
    return count


def verify_traffic_manager(image):
    # Supply admission, placement and human registration. The spacing loop
    # is empty in this fixture. The executable owns timer, cursor and budget.
    manager = 0x1160000
    actors = [ACTOR + i * 0x1000 for i in range(4)]
    count = 0
    for drag in (False, True):
        for stage, seconds, eligible in (
            (3, .125, (True, True, True, True)),
            (4, .5, (True, True, True, True)),
            (4, .125, (False, True, True, False)),
            (4, .125, (False, False, False, False)),
        ):
            m = Machine(image)
            m.allowed = [(0x598600, 0x5988B6), (0x59AE60, 0x59AEA0),
                         (0x424EA0, 0x424EB0), (0x41F680, 0x41F6A0),
                         (0x41F5D0, 0x41F5D4), (0x421A50, 0x421A69)]
            m.put(0x78A300, 2)
            m.put(0x78A344, int(drag), "B")
            m.put(0x7361A8, 0)
            m.put(0x734458, 0)
            m.put(0x73619C, STATUS)
            m.put(STATUS, stage)
            m.put(0x7361F8, WORLD)
            m.put(WORLD + 0x18, len(actors))
            for i, actor in enumerate(actors):
                m.put(WORLD + 0x28 + i * 4, actor)
                m.put(actor + 0x2C, actor)
            m.put(manager, seconds, "f")
            m.put(manager + 4, 2)
            m.put(manager + 8, 1, "B")
            probes, placements = [], []
            human_calls = []

            def human_count(machine):
                human_calls.append(None)
                machine.ret(value=int(len(human_calls) == 1))

            def query(machine):
                actor = machine.get(machine.reg("esp") + 4)
                index = actors.index(actor)
                probes.append(index)
                machine.ret(pop=4, value=int(eligible[index]))

            def place(machine):
                actor = machine.get(machine.reg("esp") + 4)
                index = actors.index(actor)
                placements.append(index)
                machine.ret(pop=8, value=int(index != 2))

            m.hook(0x549130, "diagnostic prologue", pop=4)
            m.hook(0x6497D0, "manager maintenance")
            m.hook(0x59B6D0, "intersection maintenance")
            m.hook(0x408200, "human registration", callback=human_count)
            m.hook(0x5988C0, "admission", callback=query)
            m.hook(0x599830, "placement", callback=place)
            m.hook(0x5832A0, "park actor")
            m.run(0x598600, (bits(.125),), ecx=manager, budget=1500)
            due = stage == 4 and seconds == .125
            expected_probes = [3, 0, 1, 2] if due else []
            if due and any(eligible) and not drag:
                expected_probes = [3, 0, 1]
            assert probes == expected_probes, (drag, stage, seconds, probes)
            assert placements == [i for i in expected_probes if eligible[i]]
            assert m.get(manager + 4) == (expected_probes[-1] if due else 2)
            remaining = (.3 if drag else 1.) if due else seconds - (.125 if stage == 4 else 0.)
            assert m.get(manager, "f") == struct.unpack("<f", struct.pack("<f", remaining))[0]
            assert m.get(actors[2] + 0x4D1, "B") == int(2 in placements)
            count += 1
    return count


def verify_campaign_contexts(image, retail_root):
    # Stop before the tuning branch. The original selects the context and
    # mode from each physical rival record for all three difficulties.
    front = authenticated(jdlz((retail_root / "FrontEnd/FrontB.lzc").read_bytes()), FRONT_SHA)
    machine = Machine(image)
    machine.allowed = [(0x4B41EB, 0x4B4227)]
    address = 0x1100000
    observed = 0
    for start in offsets(front, 0x34A02, 0x14C):
        event, kind, mode = struct.unpack_from("<3I", front, start)
        if event > 113 or kind == 8 or mode == 3:
            continue
        for i in range(u32(front, start + 0x128)):
            row = start + 0xD4 + i * 0x1C
            machine.uc.mem_write(address, front[row:row + 0x1C])
            for difficulty in range(3):
                machine.put(0x78A3FC, difficulty)
                machine.put(STACK + 0x24, 100)
                machine.put(STACK + 0x28, 100)
                machine.run(0x4B41EB, end=0x4B4227, esi=address)
                context = struct.unpack_from("<h", front, row + 0x10 + 2 * difficulty)[0]
                behavior = front[row + 0x18]
                expected = context if 0 <= context <= 9 else -1, behavior if behavior <= 2 else 1
                actual = machine.get(STACK + 0x28, "i"), machine.get(STACK + 0x24, "i")
                assert actual == expected, (event, i, difficulty, expected, actual)
                observed += 1
    assert not machine.events
    return observed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    raw = (args.root / "speed.exe").read_bytes()
    if sha256(raw).hexdigest() != EXE_SHA:
        raise ValueError("unexpected PC 1.4 USA executable")
    image = Image(raw)
    admission = verify_admission(image)
    partial = verify_partial(image)
    roads = verify_road_time(image)
    launch = verify_drag_launch_entry(image)
    throttle = verify_throttle(image)
    quick_throttle = verify_quick_drag_throttle(image)
    cap, takeoff, spline_samples = verify_path_body(image)
    path_exit = verify_path_exit_timer(image)
    near = verify_traffic_nearness(image)
    traffic = verify_traffic_admission(image)
    manager = verify_traffic_manager(image)
    lanes = verify_traffic_lane_direction(image)
    exits = verify_traffic_authored_exit(image)
    policies = verify_traffic_policy_dispatch(image)
    pace = verify_traffic_pace_gate(image)
    contexts = verify_campaign_contexts(image, args.root)
    print(f"PASS {admission} original 44B060/421400, {partial} 44B754 and {roads} 462840/435CA0 cases")
    print(f"PASS {launch} original 44B240/435F80 drag launch entry cases")
    print(f"PASS {throttle} original throttle selector, held pedals, GO release and moving cap sequences")
    print(f"PASS {quick_throttle} original Quick Drag difficulty cap selections")
    print(f"PASS {cap} original 456830 path acceleration caps and {takeoff} 463D50 takeoff predictions")
    print(f"PASS {spline_samples} original 59D350 spline point/tangent cases")
    print(f"PASS {path_exit} original 463F20 exit-timer publications at supplied prediction")
    print(f"PASS {pace} original 598A50 direction/pace decisions with supplied directions")
    print(f"PASS {near} original 59AD90/421550 participant-distance veto cases")
    print(f"PASS {traffic} original 5988C0 traffic eligibility decisions with supplied child results")
    print(f"PASS {policies} original 5988C0 policy dispatch and distance cases")
    print(f"PASS {exits} original 5835F0 authored road exit cases")
    print(f"PASS {lanes} original 59AEA0 lane direction and 4216F0 RNG cases")
    print(f"PASS {manager} original 598600 traffic manager cases with supplied eligibility and placements")
    print(f"PASS {contexts} original physical opponent context/mode cases across the campaign")


if __name__ == "__main__":
    main()

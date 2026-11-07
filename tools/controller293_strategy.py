"""Recovered strategy creation and cap, with explicit synthetic allocator/state."""
from controller293_native import fixture, controls, ROUTE
from physics256_native import ACTOR, DRIVER, bits

CONTROLLER, ACTION, ROUTE_STATE = 0x1160000, 0x1161000, 0x1162000
STRATEGY, STRATEGY_POOL, ACTION_POOL = 0x1164000, 0x1168000, 0x1169000

RANGES = [
    ('action7-update', 0x450210, 0x4503F6),
    ('path-index-sampler', 0x459870, 0x459972),
    ('scheduler-first-strategy-action-slice', 0x44B831, 0x44B85D),
    ('controller-strategy-selection', 0x4623D0, 0x46242A),
    ('strategy-factory', 0x44AA10, 0x44AB31),
    ('strategy1-constructor', 0x44A150, 0x44A170),
    ('strategy2-constructor', 0x44A190, 0x44A1B0),
    ('strategy3-constructor', 0x44A1C0, 0x44A1E0),
    ('strategy-action-base-constructor', 0x44A080, 0x44A0D6),
    ('strategy123-speed-cap', 0x44A1F0, 0x44A1F7),
    ('strategy1-first-action', 0x44DCE0, 0x44DD21),
    ('strategy23-first-action', 0x44E260, 0x44E2A1),
    ('action-base-constructor', 0x44E2B0, 0x44E335),
    ('path-reset', 0x458F10, 0x459096),
    ('pool-allocation', 0x567160, 0x5671B9),
]


def create(image, mode='normal', role=2, track=1003, special=0, special_kind=0):
    m = fixture(image, [(10.0, 0.0, 0.0), (100.0, 0.0, 0.0)])
    m.allowed += [(a, b) for _, a, b in RANGES]
    m.put(CONTROLLER, 0x6C89F0)
    m.put(CONTROLLER + 4, ACTOR)
    m.put(CONTROLLER + 8, DRIVER)
    m.put(ACTOR + 0x2C, CONTROLLER)
    m.put(0x1152004, role)
    m.put(0x78A344, int(mode == 'flag'), 'B')
    m.put(0x7361A8, int(mode == 'pointer'))
    m.put(0x78A2F0, track)
    m.put(0x735FB0, special)
    m.put(0x735FB8, special_kind)
    for global_at, pool, block in [(0x73609C, STRATEGY_POOL, STRATEGY),
                                   (0x736098, ACTION_POOL, ACTION)]:
        m.put(global_at, pool)
        m.put(pool + 0x10, block)
        m.put(block, 0)
    # Run the real scheduler slice, including virtual selection, original
    # pooled allocation and original constructors; no hooked scalar/allocator.
    m.run(0x44B831, end=0x44B85D, ebx=CONTROLLER)
    return m


def construction(m):
    return dict(strategy_pointer=m.get(CONTROLLER + 0x3FC),
        strategy_table='%08X' % m.get(STRATEGY), strategy_kind=m.get(STRATEGY + 0x14),
        strategy_timed=m.get(STRATEGY + 4, 'B'), strategy_duration=m.get(STRATEGY + 0xC, 'f'),
        action_pointer=m.get(CONTROLLER + 0x3F8), action_table='%08X' % m.get(ACTION),
        action_kind=m.get(ACTION + 0x1F8), action_priority=m.get(ACTION + 0x1F4),
        path_count=m.get(ACTION + 0x20, 'B'),
        flags=[m.get(ACTION + n, 'B') for n in (0x1F0, 0x1F1, 0x1F2, 0x1F3)],
        action_duration=m.get(ACTION + 0xC, 'f'),
        input_fields=[m.get(ACTION + n, 'f') for n in (0x1E4, 0x1E8, 0x1EC)],
        pool_counts=[m.get(p + 0x18) for p in (STRATEGY_POOL, ACTION_POOL)])


def strategy_case(image, **inputs):
    m = create(image, **inputs)
    initial = construction(m)
    # Initial action and strategy must be reused on the next entry, not rebuilt.
    m.run(0x44B831, end=0x44B85D, ebx=CONTROLLER)
    return dict(inputs=inputs, initial=initial, repeated=construction(m),
                hooks=m.events, visited=['%08X' % x for x in sorted(m.visited)])


def publication_case(image, speed_byte=40, scale=0.5):
    m = create(image)
    initial = construction(m)
    m.allowed += [(0x44C550, 0x44C7F1), (0x44BD10, 0x44C00B),
                  (0x45FAF0, 0x45FB27), (0x459980, 0x459ABA),
                  (0x44CA10, 0x44CA1E), (0x442110, 0x442131)]
    # The original first action is kept. Its empty path is replaced by a
    # declared synthetic planner output; no planner/trajectory claim is made.
    points = [(0.0, 0.0, 0.0), (12.0, 0.0, 0.0), (24.0, 8.0, 0.0),
              (32.0, 24.0, 0.0), (28.0, 40.0, 0.0), (12.0, 48.0, 0.0)]
    m.put(ACTION + 0x20, len(points), 'B')
    for i, point in enumerate(points):
        for j, value in enumerate(point):
            m.put(ACTION + 0x30 + 16*i + 4*j, value, 'f')
    m.put(CONTROLLER + 0x3D8, ROUTE_STATE)
    m.put(CONTROLLER + 0x4B8, scale, 'f')
    m.put(CONTROLLER + 0x478, 0)
    m.put(ROUTE_STATE, 0x1163000)
    m.put(ROUTE_STATE + 8, 1)
    m.put(0x7360B8, 0)
    m.put(0x736090, 0)
    m.put(0x736150, 0x1165000)
    m.put(0x1165009, 0, 'B')
    m.put(0x1165004, 1, 'H')
    m.put(0x77AF30, 0x1166000)
    m.put(0x1166000 + 0x27C + 0x88 + 0x6B, speed_byte, 'B')
    m.run(0x44C550, (CONTROLLER,), end=0x44C7F1)
    published = dict(desired=m.get(DRIVER + 0x200, 'f'),
        duration=m.get(DRIVER + 0x204, 'f'), auxiliary=m.get(DRIVER + 0x208, 'f'),
        enabled=m.get(DRIVER + 0x20C, 'B'), path_count=m.get(ROUTE, 'B'))
    return dict(inputs=dict(speed_byte=speed_byte, catchup_scale=scale,
                synthetic_path=points), initial=initial, published=published,
                hooks=m.events, visited=['%08X' % x for x in sorted(m.visited)])


def suite(image):
    return dict(selection=[strategy_case(image, **v) for v in [
        {}, dict(mode='flag'), dict(mode='flag', role=1), dict(mode='pointer'),
        dict(track=1099, special=1, special_kind=4),
        dict(track=1099, special=1, special_kind=3),
        dict(track=1003, special=1, special_kind=4)]],
        publication=[publication_case(image), publication_case(image, 200, 10000000.0)],
        evolving=[evolving_case(image), evolving_case(image, True)],
        planner_boundary=planner_boundary(image),
        planner_executed=False, original_runtime=None)


def evolving_case(image, mirror=False):
    sign = -1.0 if mirror else 1.0
    points = [(0.0, 0.0, 0.0), (12.0, 0.0, 0.0), (24.0, 8.0*sign, 0.0),
              (32.0, 24.0*sign, 0.0), (28.0, 40.0*sign, 0.0), (12.0, 48.0*sign, 0.0)]
    m = fixture(image, points)
    m.allowed += [(0x45FAF0, 0x45FB27), (0x459980, 0x459ABA), (0x459870, 0x459972)]
    m.uc.mem_write(ACTION + 0x20, bytes(m.uc.mem_read(ROUTE, 0x1C0)))
    m.run(0x45FAF0, (ACTION + 0x20,), ecx=DRIVER)
    frames = []
    # Exogenous states, explicitly not the result of integrating these controls.
    for i, (x, y, vx, vy, speed, heading, target) in enumerate([
        (0, 0, 20, 0, 20, 0, 30), (10, 0, 20, 0, 20, 0, 30),
        (20, 5, 17, 10, 20, 4096, 12), (29, 17, 9, 18, 20, 8192, 12),
        (31, 29, -2, 14, 14, 16384, 24), (22, 44, -12, 8, 14, 24576, 24)]):
        values = dict(position=[x, y*sign, 0], velocity=[vx, vy*sign, 0],
                      speed=speed, heading=int(heading*sign) & 65535, target=target,
                      world_tick=100000+i*80, dt=0.02)
        for base, vector in ((0x60, values['position']), (0x70, values['velocity'])):
            for j, value in enumerate(vector):
                m.put(ACTOR + base + 4*j, float(value), 'f')
        m.put(ACTOR + 0x3D0, float(speed), 'f')
        m.put(ACTOR + 0x3D4, float(speed), 'f')
        m.put(ACTOR + 0x3B4, values['heading'], 'H')
        m.put(0x73AD34, values['world_tick'])
        m.run(0x45E180, (bits(target), 0, 0, 1), ecx=DRIVER)
        m.run(0x45E970, (bits(values['dt']),), ecx=DRIVER)
        frames.append(dict(inputs=values, controls=controls(m),
                           cache_tick=m.get(ROUTE + 0x1A4)))
    return dict(synthetic_path=points, frames=frames, hooks=m.events,
                visited=['%08X' % x for x in sorted(m.visited)],
                physical_integration=False, observed_runtime_inputs=False)


def planner_boundary(image):
    m = create(image)
    m.allowed += [(0x450210, 0x4503F6)]
    m.put(CONTROLLER + 0x2C, ACTOR)
    m.run(0x450210, (CONTROLLER + 0x2C,), ecx=ACTION, end=0x45B050)
    return dict(action_kind=m.get(ACTION + 0x1F8),
                stopped_at='0045B050', planner_executed=False,
                action_path_count=m.get(ACTION + 0x20, 'B'),
                hooks=m.events, visited=['%08X' % x for x in sorted(m.visited)])

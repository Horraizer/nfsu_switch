"""Original world-owned controller/driver phase on retained retail actors.

This conditional witness constructs actual selector1/3 physics modules but
omits their integration. Actor poses, view binding, clock and phase entry are
explicit inputs. Whole actor/world/view constructors are not claimed.
"""
from controller293_scene import PairedController, WORLD, HUMAN
from controller293_planner import TRACK, CTX
from controller293_strategy import CONTROLLER
from physics256_native import ACTOR, STATUS
from event0_trace import chunks
from world_body_trace import jdlz


def suite(image, retail):
    c = PairedController(image, retail, elapsed_ticks=0, event_rides=True,
                         native_ownership=True)
    m = c.m
    # Original startup grid/cache and pool producers, before module creation.
    m.run(0x441270, eax=0x77B130)
    m.run(0x4412B0, (0,), budget=3000000)  # stack argument unused by original
    loaded = []
    for tag, at, end in chunks(jdlz(retail['Tracks/TRACKB1003.lzc'])):
        if tag in range(0x34130, 0x34135):
            m.run(0x441ED0, (TRACK+at,), budget=3000000)
            loaded.append(tag)
    m.run(0x445720, esi=0x73B14C)
    m.run(0x4764E0, budget=3000000)  # actual event config count78A41C
    for actor, selector in ((HUMAN, 1), (ACTOR, 3)):
        m.run(0x4255CB, end=0x4255E0, ebx=actor, ecx=m.get(actor+0x10))
        # Explicit unconstructed-module boundary replaces legacy fake module.
        m.put(actor+0x28, 0)
        m.put(actor+0x34, 0)
        m.run(0x426110, esi=actor, ecx=selector, budget=3000000)
    # Original flow caller chooses countdown3; full race update chooses active4.
    # The empty frontend registry is produced by its original static initializer.
    m.run(0x68D500, end=0x68D50A)
    m.run(0x4233A5, end=0x4233B2, budget=3000000)
    countdown = dict(stage=m.get(STATUS), tick=m.get(STATUS+8),
                     interval=m.get(STATUS+0xC, 'f'))
    tick = m.get(0x73AD34)+16000  # external clock jump, no skipped physics claim
    m.put(0x73AD34, tick)
    m.run(0x435570, esi=STATUS, budget=3000000)
    initial = dict(countdown=countdown, stage=m.get(STATUS),
        race_start=m.get(STATUS+0x10), elapsed=m.get(STATUS+0x14),
        loaded_grid_chunks=loaded, actors=[],
        world_order=[m.get(WORLD+0x28+i*4) for i in range(m.get(WORLD+0x18))],
        list_links=[[m.get(x), m.get(x+4)] for x in (WORLD, HUMAN, ACTOR)],
        player_input_bound=bool(m.get(HUMAN+0x18)),
        rival_list_actor=m.get(m.get(WORLD+8)+8), event=c.initial['event'])
    for actor in (HUMAN, ACTOR):
        physics = m.get(actor+0x34)
        initial['actors'].append(dict(actor=actor, selector=m.get(actor+0x28),
            physics=physics, table=m.get(physics), rigid=m.get(physics+0x20),
            type_info=m.get(actor+0xC), config=m.get(actor+0x14),
            ride=m.get(actor+0x10), physical=m.get(actor+0x20)))
    frame = 0x11B8000
    m.put(frame+0xC, 0.02, 'f')
    frames = []
    for i in range(160):
        tick += 80
        m.put(0x73AD34, tick)
        m.put(0x73457C, 0.02, 'f')
        # Native loops own rival/human ordering, full controller dispatch,
        # full action scheduler, driver updates, and pre-physics road queries.
        m.run(0x420C8A, end=0x420E05, edi=WORLD, ebp=frame, budget=10000000)
        m.run(0x420E2B, end=0x420E50, edi=WORLD, ebp=frame, budget=10000000)
        m.run(0x435570, esi=STATUS, budget=3000000)
        driver = m.get(ACTOR+0x30)
        action = m.get(CONTROLLER+0x3F8)
        frames.append(dict(tick=tick, stage=m.get(STATUS), elapsed=m.get(STATUS+0x14),
            action=action, action_table=m.get(action), action_kind=m.get(action+0x1F8),
            action_count=m.get(action+0x20, 'B'), driver_count=m.get(driver+0x10, 'B'),
            driver_path=[[m.get(driver+0x20+j*16+k*4, 'f') for k in range(3)]
                         for j in range(m.get(driver+0x10, 'B'))],
            context_count=m.get(CTX+4), desired_speed=m.get(driver+0x200, 'f'),
            controls=dict(steering=m.get(driver+0x1D0, 'H'), gear=m.get(driver+0x1D4, 'i'),
                throttle=m.get(driver+0x1DC, 'f'), brake=m.get(driver+0x1E4, 'f')),
            physics=[m.get(x+0x34) for x in (HUMAN, ACTOR)],
            distance=m.get(ACTOR+0x3B8, 'f')))
    return dict(initial=initial, frames=frames, hooks=m.events,
        visited=['%08X'%v for v in sorted(m.visited)],
        physical_integration=False, complete_world_update=False,
        complete_actor_constructor=False, original_runtime=None)

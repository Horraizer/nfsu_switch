"""Original event metadata and paired controller consumers on supplied poses.

Stock 240SX physical data is deliberately retained for both actors. These
conditional witnesses do not resolve event presets or integrate physics.
"""
from event0_trace import chunks
from world_body_trace import jdlz
from physics256_native import ACTOR, RIDE, STATUS, bits

FRONT, SETUP, CONFIGS, WORLD = 0x3000000, 0x2D00000, 0x2D08000, 0x2D10000
REF, HUMAN, HUMAN_CONTROLLER = 0x11A0000, 0x11A2000, 0x11A7000


def setup_event(m, retail, seed):
    if not 0 <= seed <= 0xFFFFFFFF:
        raise ValueError('seed must be an explicit uint32')
    m.uc.mem_map(FRONT, 0x1000000)
    front = jdlz(retail['FrontEnd/FrontB.lzc'])
    m.uc.mem_write(FRONT, front)
    for tag, at, end in chunks(front):
        if tag == 0x80034A00:
            m.run(0x5A00A0, (FRONT + at,))
    m.put(0x735EC4, SETUP)  # caller-owned setup allocation
    m.run(0x5A0A26, end=0x5A0B30, esi=0x75EEF8)
    m.run(0x5A0CE0, esi=0x75EEF8, edi=0)
    m.put(0x6F227C, seed)  # declared pre-settings external RNG state
    m.run(0x431DF0, esi=0x78A2F0)
    m.run(0x4B5E2F, end=0x4B5E40)
    m.run(0x4B5E61, end=0x4B602B, ebx=SETUP)
    settings_next = m.get(0x6F227C)
    m.run(0x41F7C9, end=0x41F891, esi=WORLD, ebx=0)
    return dict(setup=m.get(0x75EEF8 + 0x88B0), input_seed=seed,
                settings_seed=m.get(0x78A420), settings_next_seed=settings_next,
                next_seed=m.get(0x6F227C), world_seed=m.get(WORLD+0x2D0),
                initial_tick=m.get(0x73AD34),
                track=m.get(0x78A2F0), mode=m.get(0x78A30C),
                difficulty=m.get(0x78A3FC), laps=m.get(0x78A31C),
                humans=m.get(0x78A320), rivals=m.get(0x78A324))


def configurations(m, setup):
    """Native constructor metadata with explicitly substituted stock RideInfo."""
    # Original caller reads the settings reset's initial player grid byte.
    m.run(0x4B6079, end=0x4B6080)
    grid = m.reg('esi')
    m.run(0x4B6102, end=0x4B611C, esi=grid)
    player_args = [m.get(m.reg('esp') + 4*i) for i in range(7)]
    player_args[2] = RIDE
    m.run(0x5809C0, tuple(player_args), esi=CONFIGS)
    for i in range(3):
        record = setup + 0x5974 + i * 0x1C
        m.run(0x4B41EB, end=0x4B4227, esi=record)
        m.run(0x4B42FC, end=0x4B4321, ebx=i+2, edx=record)
        args = [m.get(m.reg('esp') + 4*j) for j in range(7)]
        args[2] = RIDE
        m.run(0x5809C0, tuple(args), esi=CONFIGS + (i+1)*0x750)
    # Array publication is a fixture. The grid permutation itself is original.
    m.put(0x78A418, CONFIGS)
    m.put(0x78A41C, 4)
    m.run(0x4B628B, end=0x4B6299, ebx=setup)
    result = []
    for i in range(4):
        config = CONFIGS + i*0x750
        m.run(0x422ACA, end=0x422AEA, eax=config)
        result.append(dict(address=config, id=m.get(config+1, 'b'),
            role=m.get(config+4), grid=m.get(config+3, 'b'),
            context=m.get(config+0x735, 'b'), mode=m.get(config+0x736, 'b'),
            controller_selector=m.get(m.reg('esp'))))
    return result


class PairedController:
    """A conditional original-code caller contract, not a race session.

    Supplied poses are retail route samples with coherent tangent/velocity.
    Calls have dt0.02 and advance the 4000Hz clock by80. The call stream is
    explicit; no original frame scheduling or physical trajectory is claimed.
    """
    def __init__(self, image, retail, seed=12345, elapsed_ticks=240000):
        from controller293_planner import ActorController, TRACK
        self.actor = a = ActorController(image, retail, event_seed=seed)
        self.m = m = a.m
        self.elapsed_ticks = elapsed_ticks
        m.run(0x42B21E, end=0x42B22B, eax=0, ebx=REF)
        # Player/view ownership is an explicit binding, not RealController.
        m.put(REF+4, HUMAN)
        m.put(REF+0x10, 0)
        m.run(0x42B557, end=0x42B5B8, ebx=REF, edi=0)
        m.put(HUMAN+0x10, RIDE)
        m.put(HUMAN+0x20, RIDE+0x60)
        m.put(HUMAN+0x14, a.event['configs'][0]['address'])
        m.run(0x464860, (RIDE+0x60,), eax=HUMAN+0x40,
              ecx=HUMAN+0x540, edx=HUMAN+0x828)
        a.pose(30, 20.0, update_world=False, actor=HUMAN)
        m.run(0x4257F7, end=0x425805, ebx=HUMAN)
        m.put(0x1190000+0x10, 0x11AF000)  # second transform pool slot
        m.run(0x57A1B0, (HUMAN, 0))
        m.put(HUMAN+0x82C, m.reg('eax'))
        m.run(0x57A380, esi=m.get(m.get(HUMAN+0x82C)+0x20))
        m.put(HUMAN+0x24, a.event['configs'][0]['controller_selector'])
        m.run(0x425150, eax=HUMAN)
        m.put(HUMAN+0x28, m.reg('eax'))
        m.put(0x1180000+0x10, 0x11A6000)  # second route context slot
        m.run(0x59BFC0, (HUMAN_CONTROLLER,), ecx=HUMAN, budget=3000000)
        m.put(HUMAN+0x2C, HUMAN_CONTROLLER)
        m.put(HUMAN+0x30, m.get(HUMAN_CONTROLLER+8))
        m.put(HUMAN+0x3C, 0x11AA000)
        m.run(0x469B00, (0x11AA000, HUMAN), end=0x469D1E)
        # The flow state and race status are distinct original objects.
        # Stage4 is an explicit entry condition; its transition is not traced.
        m.put(0x7361F0, 0x11AD000)
        m.put(0x11AD000, 4)
        m.put(0x11AD004, 1)
        m.run(0x571FA0, esi=0x2E00000)
        m.put(0x7345D0, 0x2E00000)
        m.run(0x445720, esi=0x77B190)
        for tag, at, end in chunks(jdlz(retail['Tracks/TRACKB1003.lzc'])):
            if tag == 0x34146:
                m.run(0x441ED0, (TRACK+at,))
        m.run(0x434270, eax=STATUS, budget=3000000)
        for actor in (HUMAN, ACTOR):
            self.project(actor)
        # Actor registration list is supplied; partition and rows are original.
        nodes = [WORLD, HUMAN, ACTOR]
        for i, node in enumerate(nodes):
            m.put(node, nodes[(i+1)%3])
            m.put(node+4, nodes[i-1])
        m.run(0x445720, esi=WORLD+8)
        m.run(0x41FE50, esi=WORLD)
        m.run(0x435D30, (STATUS,), budget=3000000)
        m.run(0x434A80, esi=STATUS, budget=3000000)
        self.initial = dict(event=a.event, race_rows=[m.get(x+0x1C) for x in (HUMAN, ACTOR)],
            progress=[m.get(m.get(x+0x1C)+0x14, 'f') for x in (HUMAN, ACTOR)],
            physical_selectors=[m.get(x+0x28) for x in (HUMAN, ACTOR)],
            world_rng=m.get(WORLD+0x2D0), elapsed_ticks=elapsed_ticks)
        # Explicit resumed-clock input, no skipped simulation is claimed.
        a.tick = 500000
        m.put(0x73AD34, a.tick)
        m.put(STATUS+0x14, elapsed_ticks)

    def project(self, actor):
        self.m.run(0x443A10, (0x7372B0, actor+0x60), esi=actor+0x800, budget=3000000)

    def step(self, human_point, human_speed):
        from controller293_strategy import CONTROLLER, ACTION
        m, a = self.m, self.actor
        a.pose(human_point, human_speed, actor=HUMAN)
        self.project(HUMAN)
        m.run(0x4338C0, edi=m.get(HUMAN+0x1C), budget=3000000)
        m.run(0x434ED0, edx=STATUS)
        m.put(0x73457C, 0.02, 'f')
        m.put(0x73AD34, a.tick+80)
        self.elapsed_ticks += 80
        m.put(STATUS+0x14, self.elapsed_ticks)
        m.run(0x59C210, (bits(0.02),), ecx=HUMAN_CONTROLLER, budget=3000000)
        m.run(0x430758, end=0x4308D6, esi=REF, budget=3000000)
        result = a.step()
        return dict(human_input=dict(point=human_point, speed=human_speed),
            progress=[m.get(m.get(x+0x1C)+0x14, 'f') for x in (HUMAN, ACTOR)],
            history=m.get(m.get(REF+0xA8)+0xC, 'f'), history_timer=m.get(REF+0xAC, 'i'),
            history_count=m.get(m.get(REF+0xA8)+6, 'B'),
            action_catchup=m.get(ACTION+0x1F2, 'B'), reference=m.get(CONTROLLER+0x470),
            elapsed_ticks=self.elapsed_ticks, world_rng=m.get(WORLD+0x2D0), result=result)


def suite(image, retail):
    controller = PairedController(image, retail)
    frames = []
    for i in range(160):
        point, speed = ((20, 20.0), (30, 30.0), (37, 60.0))[min(i//40, 2)]
        frames.append(controller.step(point, speed))
    m = controller.m
    return dict(initial=controller.initial, frames=frames, hooks=m.events,
                visited=['%08X' % a for a in sorted(m.visited)],
                physical_integration=False, actual_event0_roster=False,
                original_runtime=None)

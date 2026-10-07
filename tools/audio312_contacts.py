"""Issue 312 continuation: original contact identity/controller witnesses.

Run with standalone Python, Unicorn 2.1.4 and Capstone 5.0.7. No game process,
PCM, or retail writes. Synthetic objects stop at named allocation/dispatch
boundaries; original decisions and numeric producers execute in Unicorn.
"""
import argparse
import json
from pathlib import Path
import struct

from audio312_native import Machine, u32
from audio312_trace import checked, output_path, require, sha, INPUTS, SOURCES
from hud254_trace import PE, manifest
from unicorn import x86_const as x


def vector(m, address, values):
    for k, value in enumerate(values):
        m.wf(address+4*k, value)


def byte(m, address, value):
    m.uc.mem_write(address, bytes([value]))


def pair_witness(image):
    m = Machine(image, [(0x466CE0, 0x466DE4), (0x468300, 0x46833F),
                        (0x467300, 0x46735F), (0x466600, 0x466651),
                        (0x441B90, 0x441C0C), (0x43FE70, 0x43FEA9),
                        (0x46A514, 0x46A531), (0x469850, 0x46985D),
                        (0x46A5D2, 0x46A5ED), (0x4697EA, 0x4697F3)])
    body_a, body_b, pair, bits = 0x1100000, 0x1101000, 0x1102000, 0x1104000
    m.wi(body_a+0x18, 5)
    m.wi(body_b+0x18, 17)
    m.wi(0x72CBFC, bits)
    m.wi(0x779BE0, 0x779BE0)
    m.wi(0x779BE4, 0x779BE0)
    # Null geometry shapes exclude geometric witness construction. Body IDs,
    # pair pointers, list insertion and symmetric duplicate bits are original.
    m.run(0x466CE0, (body_b,), {x.UC_X86_REG_EAX: body_a, x.UC_X86_REG_ESI: pair})
    found = []
    for a, b in ((body_a, body_b), (body_b, body_a)):
        m.run(0x468300, regs={x.UC_X86_REG_EAX: a, x.UC_X86_REG_ECX: b})
        found.append(bool(m.uc.reg_read(x.UC_X86_REG_EAX)))
    require(found == [True, True], 'symmetric body pair identity')
    require([m.ri(pair+o) for o in (0x258, 0x25C, 0x264)] == [body_a, body_b, 0],
            'pair owns bodies and a distinct initially absent record')
    # Non-overlapping AABBs: the retained record must bypass overlap testing.
    for off in (0x24, 0x28, 0x2C, 0x30):
        m.wf(body_a+off, 100)
        m.wf(body_b+off, 0)
    retention = []
    for record in (0x1108000, 0):
        m.wi(pair+0x264, record)
        m.run(0x467300, regs={x.UC_X86_REG_EDI: pair})
        retention.append(dict(record_present=bool(record),
                              removal_predicate=m.uc.reg_read(x.UC_X86_REG_EAX)))
    require([r['removal_predicate'] for r in retention] == [0, 1],
            'retained record bypasses failed geometric overlap')
    removal_paths = []
    for address, label in ((0x469801, 'retain_pair'), (0x4697F3, 'destroy_pair')):
        m.callbacks[address] = lambda cpu, label=label: (removal_paths.append(label), cpu.finish())
    for record in (0x1108000, 0):
        m.wi(pair+0x264, record)
        m.run(0x4697EA, regs={x.UC_X86_REG_EDI: pair})
    require(removal_paths == ['retain_pair', 'destroy_pair'], 'actual caller retirement branches')
    # Existing polygon-cache slot: follow raw polygon +6 slot through 441B90
    # and 43FE70, then execute the real track-body back-reference stores.
    pool, polygon, args, owner = 0x1110000, 0x1112000, 0x1113000, 0x1114000
    m.wi(0x736184, pool)
    m.wi(pool+0x24, 8)
    m.wi(pool+0x28, 0x40)
    m.uc.mem_write(polygon+6, struct.pack('<h', 2))
    cache = pool+0x30+2*0x40
    m.wi(args+0x10, polygon)
    m.callbacks[0x46A531] = lambda cpu: cpu.finish()
    m.run(0x46A514, regs={x.UC_X86_REG_EDI: body_b, x.UC_X86_REG_EBX: args})
    require(m.ri(body_b+0x60) == cache and m.ri(cache+0x10) == body_b,
            'polygon cache owns reusable track body')
    m.wi(owner+0x64, args)
    m.wi(args+0x20, cache)
    branch = []
    for address, label in ((0x46985D, 'evaluate_uncached'), (0x4698AC, 'skip_cached')):
        m.callbacks[address] = lambda cpu, label=label: (branch.append(label), cpu.finish())
    m.run(0x469850, regs={x.UC_X86_REG_EDI: owner, x.UC_X86_REG_EBP: 0x20})
    # The destructor fragment clears the back-reference before releasing cache.
    m.callbacks[0x46A5ED] = lambda cpu: cpu.finish()
    m.run(0x46A5D2, regs={x.UC_X86_REG_ESI: body_b, x.UC_X86_REG_EBP: args})
    m.run(0x469850, regs={x.UC_X86_REG_EDI: owner, x.UC_X86_REG_EBP: 0x20})
    require(branch == ['skip_cached', 'evaluate_uncached'], 'track body reuse/retirement')
    return dict(body_ids=[5, 17], pair_lookup_both_orders=found,
                retention=retention, removal_paths=removal_paths,
                polygon_cache_slot=2, candidate_paths=branch,
                cache_body_cleared=(m.ri(cache+0x10) == 0))


class Controller:
    """Initialized world controller + explicit active-list fixture."""
    def __init__(self, image, direction=(1, 0, 0), previous=(0, 1, 0),
                 point=(0, 0, 0), previous_point=(10, 0, 0), actor=True,
                 subtype=5, blocked=False, already_started=False, competitor=None):
        self.m = m = Machine(image, [
            (0x531FF0, 0x532269), (0x567370, 0x5673DC), (0x4655B0, 0x4655DC),
            (0x408180, 0x4081B2), (0x436D60, 0x436D81), (0x567280, 0x5672A9),
            (0x5325F0, 0x5325FD), (0x530240, 0x5302EF),
            (0x52FB30, 0x52FB57), (0x40A880, 0x40A883), (0x531150, 0x531160)])
        self.obj, self.record, self.other = 0x1100000, 0x1101000, 0x1102000
        self.manager, self.controller, self.actor = 0x1104000, 0x1105000, 0x1106000
        m.wi(0x7356A8, self.manager)
        m.wi(self.manager+0x80, self.controller)
        m.wi(0x6F1DD8, 1)
        m.wi(self.obj, 0x6BE520)
        m.wi(self.obj+0x21C, self.record)
        m.wi(self.obj+0x32C, self.actor if actor else 0)
        m.wi(self.obj+0x340, subtype)
        byte(m, self.obj+0x218, 1)
        byte(m, self.obj+0x219, int(blocked))
        byte(m, self.obj+0x34F, int(already_started))
        vector(m, self.record+0x70, direction)
        vector(m, self.record+0x30, point)
        vector(m, 0x736730, previous)
        vector(m, 0x736740, previous_point)
        head = self.controller+0x24
        items = [self.obj]
        if competitor is not None:
            items.append(self.other)
            byte(m, self.other+0x218, int(competitor.get('active', True)))
            m.wi(self.other+0x340, competitor.get('subtype', 5))
            m.wi(self.other+0x32C, self.actor if competitor.get('same_actor', True) else self.actor+0x100)
            vector(m, self.other+0x230, competitor.get('direction', (1, 0, 0)))
        nodes = [head]+[p+0x210 for p in items]
        for k, node in enumerate(nodes):
            m.wi(node, nodes[(k+1) % len(nodes)])
            m.wi(node+4, nodes[(k-1) % len(nodes)])
        queue = self.controller+4
        m.wi(queue, queue)
        m.wi(queue+4, queue)
        self.enqueued = []
        def observe_queue(cpu):
            self.enqueued.append(cpu.uc.reg_read(x.UC_X86_REG_EAX))
            cpu.return_stub()
        m.callbacks[0x52FB00] = observe_queue

    def admit(self):
        self.m.run(0x531FF0, regs={x.UC_X86_REG_ECX: self.obj})
        return bool(self.m.uc.reg_read(x.UC_X86_REG_EAX) & 255)

    def submit(self):
        self.m.run(0x530240, regs={x.UC_X86_REG_ESI: self.controller,
                                  x.UC_X86_REG_EDX: self.record})
        return dict(enqueue_observed=bool(self.enqueued),
                    rejection_latch=bool(self.m.uc.mem_read(self.obj+0x219, 1)[0]))


def admission_witness(image):
    cases = [
        ('fresh', {}, True), ('blocked', dict(blocked=True), False),
        ('no_actor', dict(actor=False, already_started=True), True),
        ('same_record_already_started', dict(already_started=True), False),
        ('same_actor_parallel_record', dict(competitor={}), False),
        ('same_actor_opposite_record', dict(competitor={'direction': (-1, 0, 0)}), False),
        ('same_actor_orthogonal_record', dict(competitor={'direction': (0, 1, 0)}), True),
        ('different_actor', dict(competitor={'same_actor': False}), True),
        ('inactive_record', dict(competitor={'active': False}), True),
        ('different_competitor_subtype', dict(competitor={'subtype': 4}), True),
        ('dot_below_threshold', dict(competitor={'direction': (.799, .602, 0)}), True),
        ('dot_above_threshold', dict(competitor={'direction': (.801, .599, 0)}), False),
        ('previous_vertical_nearby', dict(direction=(0, 0, 1), previous=(0, 0, 1),
                                        previous_point=(.5, 0, 0), subtype=3), False),
        ('previous_vertical_distant', dict(direction=(0, 0, 1), previous=(0, 0, 1),
                                         previous_point=(2, 0, 0), subtype=3), True),
        ('previous_horizontal_nearby', dict(previous=(1, 0, 0), previous_point=(.5, 0, 0)), True),
    ]
    results = []
    for label, fields, expected in cases:
        fixture = Controller(image, **fields)
        admitted = fixture.admit()
        submitted = fixture.submit()
        require(admitted == expected and submitted['enqueue_observed'] == expected and
                submitted['rejection_latch'] == (not expected), 'admission '+label)
        results.append(dict(case=label, inputs=fields, admitted=admitted, submission=submitted))
    fixture = Controller(image, competitor={})
    fixture.submit()
    byte(fixture.m, fixture.other+0x218, 0)
    require(not fixture.admit(), 'rejection stays latched after competitor leaves')
    results.append(dict(case='competitor_leaves_after_rejection', admitted=False,
                        note='newly initialized record/controller would clear latch'))
    return results


def retirement_witness(image):
    fixture = Controller(image)
    m, obj, record = fixture.m, fixture.obj, fixture.record
    m.allowed += [(0x420AD7, 0x420B20), (0x465700, 0x465764),
                  (0x466500, 0x46654F), (0x5302F0, 0x530338),
                  (0x5307E0, 0x530838), (0x532640, 0x5326EB)]
    pair, a, b, pool = 0x1120000, 0x1121000, 0x1122000, 0x1123000
    m.wi(record, record)
    m.wi(record+4, record)
    m.wi(record+0x64, pair)
    m.wi(pair+0x264, record)
    m.wi(pair+0x258, a)
    m.wi(pair+0x25C, b)
    for body in (a, b):
        byte(m, body+0x1C, 1)
    m.wi(pool+0x18, 1)
    m.wi(0x736004, 1)
    byte(m, obj+0x350, 1)  # Scrape active: exercise real retirement envelope setup.
    byte(m, obj+0x34E, 1)  # An impact tail is not cleared by record retirement.
    m.callbacks[0x420B20] = lambda cpu: cpu.finish()
    # Start after the previously proved age predicate; no repeated age sweep.
    # Body kinds zero exclude separate steep-track bookkeeping in 466500.
    m.run(0x420AD7, regs={x.UC_X86_REG_ECX: record, x.UC_X86_REG_EBP: pool})
    result = dict(pair_record=m.ri(pair+0x264), audio_record=m.ri(obj+0x21C),
                  retired=bool(m.uc.mem_read(obj+0x351, 1)[0]),
                  impact_active=bool(m.uc.mem_read(obj+0x34E, 1)[0]),
                  scrape_active=bool(m.uc.mem_read(obj+0x350, 1)[0]),
                  record_returned_to_pool=(m.ri(pool+0x10) == record),
                  pool_count=m.ri(pool+0x18), live_record_count=m.ri(0x736004),
                  body_record_counts=[m.uc.mem_read(body+0x1C, 1)[0] for body in (a, b)],
                  fade_segments=m.ri(obj+0x250+0x6C),
                  fade_start=m.rf(obj+0x250+0x1C), fade_end=m.rf(obj+0x250+0x34))
    require(result == dict(pair_record=0, audio_record=0, retired=True,
            impact_active=True, scrape_active=True, record_returned_to_pool=True,
            pool_count=0, live_record_count=0, body_record_counts=[0, 0],
            fade_segments=1, fade_start=1.0, fade_end=0.0), 'connected record/audio retirement')
    return result


def budget_witness(image):
    results = []
    cases = ((0, 0, 5, 10, True), (0, 0, 10, 5, False),
             (0, 0, 5, 5, False), (1, 0, 10, 5, True),
             (0, 1, 5, 10, False), (1, 1, 5, 10, False))
    for pending_priority, active_priority, pending_distance, active_distance, expected in cases:
        m = Machine(image, [(0x52FE41, 0x52FE9A)])
        pending, active, pending_node, active_node = [0x1100000+0x1000*k for k in range(4)]
        for obj, priority, distance in ((pending, pending_priority, pending_distance),
                                        (active, active_priority, active_distance)):
            m.wi(obj, 0x6BE520)
            byte(m, obj+0x344, priority)
            m.wf(obj+0x300, distance)
        byte(m, active+0x34E, 1)
        m.wi(pending_node+8, pending)
        m.wi(active_node+8, active)
        m.wi(m.stack+0x14, pending_node)
        calls = []
        for address, label in ((0x530E20, 'stop_active'), (0x531350, 'start_pending')):
            m.callbacks[address] = lambda cpu, label=label: (calls.append(label), cpu.return_stub())
        for address in (0x52FE9A, 0x52FEC6):
            m.callbacks[address] = lambda cpu: cpu.finish()
        m.run(0x52FE41, regs={x.UC_X86_REG_EBP: active_node, x.UC_X86_REG_EBX: 0})
        require(calls == (['stop_active', 'start_pending'] if expected else []) and
                bool(m.uc.mem_read(active+0x34E, 1)[0]) == (not expected),
                'pool replacement fragment')
        results.append(dict(pending_priority=pending_priority, active_priority=active_priority,
                            pending_distance=pending_distance, active_distance=active_distance,
                            calls=calls))
    return dict(config_address='006F7E94', initial_capacity=u32(image.read(0x6F7E94, 4)),
                boundary='replacement fragment only; preselected pending/active nodes; calls observed',
                cases=results)


def scrape_admission_witness(image):
    results = []
    for label, delta, active, expected in (
            ('below_motion_threshold', -.01, False, False),
            ('at_motion_threshold', 0, False, True),
            ('above_motion_threshold', .01, False, True),
            ('already_active', .01, True, False)):
        fixture = Controller(image, blocked=True)
        m = fixture.m
        m.allowed += [(0x530340, 0x530570), (0x530090, 0x5300FA), (0x531170, 0x53118F)]
        m.run(0x530340, (0,))
        m.wi(fixture.obj+0x2FC, 0x7367C0)
        threshold = m.rf(0x7367C8)
        m.wf(fixture.record+0x40, threshold+delta)
        byte(m, fixture.obj+0x350, int(active))
        m.run(0x530090, regs={x.UC_X86_REG_ECX: fixture.controller,
                             x.UC_X86_REG_EDX: fixture.record})
        requested = bool(m.uc.mem_read(fixture.obj+0x21B, 1)[0])
        require(requested == expected, 'scrape admission '+label)
        results.append(dict(case=label, threshold=threshold, motion=m.rf(fixture.record+0x40),
                            impact_rejected=True, scrape_already_active=active,
                            scrape_requested=requested))
    return results


def spatial_witness(image, point, actor_type=1, player_index=0, category=32767,
                    global_gain=32767, muted=False, state=0, listener=(0, 0, 0),
                    listener_forward=(0, 1, 0), actor_position=(0, 0, 0), executed=None):
    m = Machine(image, [(0x52E060, 0x52F2B5), (0x530F20, 0x530F79),
        (0x530740, 0x5307E0), (0x531350, 0x531455), (0x531F60, 0x531FD7),
        (0x532270, 0x5322A2), (0x531190, 0x5311C4), (0x52F500, 0x52F507),
        (0x567370, 0x5673DC), (0x564CD0, 0x564EF6), (0x674898, 0x67490D)])
    obj, record, provider, camera, actor = [0x1100000+0x1000*k for k in range(5)]
    manager, settings, view, descriptor, player, state_at = [0x1110000+0x5000*k for k in range(6)]
    m.wi(obj, 0x6BE520)
    m.wi(obj+0xBC, 0x6F47A0)  # Original world/car spatial profile, unchanged.
    m.wi(obj+0x204, provider)
    m.wi(obj+0xA8, 32767)
    m.wi(obj+0x21C, record)
    m.wi(obj+0x32C, actor)
    m.wi(obj+0x340, 5)
    m.wi(obj+0x240, 22000)
    m.wi(obj+0x248, 32)
    vector(m, record+0x30, point)
    vector(m, record+0x70, (1, 0, 0))
    m.wi(provider+0x60, camera)
    m.wi(provider+0x168, actor)
    vector(m, camera+0x40, listener)
    vector(m, camera+0x50, listener_forward)
    vector(m, actor+0x60, actor_position)
    vector(m, actor+0x90, (0, 1, 0))
    m.wi(actor+0x14, descriptor)
    m.wi(descriptor+4, actor_type)
    m.wi(actor+0x878, player)
    m.wi(player+0x264, player_index)
    m.wi(0x7356A8, manager)
    m.wi(manager+0xA0, view)
    m.wi(view+4, provider)
    byte(m, view+0x10, 1)
    byte(m, manager+0x84, int(muted))
    m.wi(0x73619C, state_at)
    m.wi(state_at, 1)
    m.wi(0x7356AC, settings)
    m.wi(0x7356B0, 2)  # Explicit fallback category for non-player actors.
    m.wi(0x7361F0, state_at+4)
    m.wi(state_at+4, state)
    for k in range(4):
        m.wi(settings+k*0xD10+0x858, category if k == max(0, min(player_index, 3)) else 8192)
    m.wi(settings+0x3E1C, global_gain)
    dispatched = []
    def observe_dispatch(cpu):
        dispatched.append({name: cpu.ri(obj+off) for name, off in (
            ('gain_q15', 0x304), ('pitch_q12', 0x314), ('pan', 0x30C),
            ('intensity', 0x31C), ('variant', 0x334))})
        cpu.return_stub()
    # Do not allocate an event or enter the backend; all preceding math and
    # original virtual dispatch execute, followed by original started latches.
    m.callbacks[0x530B80] = observe_dispatch
    m.run(0x531350, regs={x.UC_X86_REG_ECX: obj})
    require(len(dispatched) == 1, 'one impact dispatch boundary')
    if executed is not None:
        executed.update(m.visited)
    return dict(point=point, listener=listener, listener_forward=listener_forward,
        actor_position=actor_position, actor_type=actor_type, player_index=player_index,
        category=category, global_gain=global_gain, muted=muted, state=state,
        spatial_attenuation=m.ri(obj+0x50), distance_to_listener=m.rf(obj+0x90),
        distance_to_actor=m.rf(obj+0x80), event=dispatched[0],
        started=bool(m.uc.mem_read(obj+0x34F, 1)[0]))


def spatial_cases(image, executed):
    cases = [
        ('near_forward', dict(point=(0, 1, 0)), 32764, 0),
        ('near_positive_x', dict(point=(1, 0, 0)), 32764, 16384),
        ('near_negative_x', dict(point=(-1, 0, 0)), 32764, 49152),
        ('near_backward', dict(point=(0, -1, 0)), 32764, 32768),
        ('far_forward', dict(point=(0, 20, 0)), 24818, 0),
        ('far_side', dict(point=(20, 0, 0)), 6749, 16384),
        ('farther_forward', dict(point=(0, 60, 0)), 1386, 0),
        ('beyond_profile', dict(point=(0, 90, 0)), 0, 0),
        ('listener_near_actor_far', dict(point=(0, 20, 0), listener=(0, 19, 0)), 24818, 0),
        ('actor_near_listener_far', dict(point=(0, 20, 0), actor_position=(0, 19, 0)), 32764, 0),
        ('listener_rotated', dict(point=(0, 20, 0), listener_forward=(1, 0, 0)), 6749, 49152),
        ('half_category', dict(point=(0, 1, 0), category=16384), 32764, 0),
        ('half_global', dict(point=(0, 1, 0), global_gain=16384), 32764, 0),
        ('both_half', dict(point=(0, 1, 0), category=16384, global_gain=16384), 32764, 0),
        ('player_index_low', dict(point=(0, 1, 0), player_index=-2), 32764, 0),
        ('player_index_high', dict(point=(0, 1, 0), player_index=9), 32764, 0),
        ('actor_type_two', dict(point=(0, 1, 0), actor_type=2), 32764, 0),
        ('actor_fallback_category', dict(point=(0, 1, 0), actor_type=3), 32764, 0),
        ('manager_mute', dict(point=(0, 1, 0), muted=True), 32764, 0),
        ('mode_one_mute', dict(point=(0, 1, 0), state=1), 32764, 0),
        ('mode_seven_mute', dict(point=(0, 1, 0), state=7), 32764, 0),
        ('other_mode_unmuted', dict(point=(0, 1, 0), state=2), 32764, 0),
    ]
    results = []
    for label, fields, attenuation, pan in cases:
        result = spatial_witness(image, executed=executed, **fields)
        category = result['category'] if result['actor_type'] in (1, 2) else 8192
        gain = (22000*attenuation) >> 15
        if result['muted'] or result['state'] in (1, 7):
            gain = 0
        gain = (gain*category) >> 15
        gain = (gain*result['global_gain']) >> 15
        require(result['spatial_attenuation'] == attenuation and result['started'] and
                result['event'] == dict(gain_q15=gain, pitch_q12=4096, pan=pan,
                                        intensity=32, variant=6), 'spatial/category '+label)
        result['case'] = label
        results.append(result)
    return results


def instructions(image, executed):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    decoder, lines = Cs(CS_ARCH_X86, CS_MODE_32), []
    ranges = (
        ('pair constructor', 0x466CE0, 0x466DE4),
        ('duplicate lookup', 0x468300, 0x46833F),
        ('retained record predicate', 0x467300, 0x46735F),
        ('predicate caller', 0x4697EA, 0x469806),
        ('cache lookup', 0x441B90, 0x441C0C),
        ('cache indexed slot', 0x43FE70, 0x43FEA9),
        ('cache body assignment', 0x46A514, 0x46A531),
        ('cached candidate branch', 0x469850, 0x46985D),
        ('cache body retirement', 0x46A5D2, 0x46A5ED),
        ('expired record cleanup', 0x420AD7, 0x420B20),
        ('record destructor', 0x465700, 0x465764),
        ('retire matching audio', 0x5302F0, 0x530338),
        ('audio retirement', 0x5307E0, 0x530838),
        ('impact submission', 0x530240, 0x5302EF),
        ('scrape submission', 0x530090, 0x5300FA),
        ('scrape motion predicate', 0x531170, 0x53118F),
        ('world admission', 0x531FF0, 0x532269),
        ('rejection latch', 0x531150, 0x531160),
        ('impact start', 0x531350, 0x531455),
        ('attach snapshot and actor selection', 0x530840, 0x530A49),
        ('body actor flags', 0x52F650, 0x52F6E9),
        ('body actor marker', 0x4664B0, 0x4664FF),
        ('pool capacity gate', 0x52FB60, 0x52FBC4),
        ('pool replacement fragment', 0x52FE41, 0x52FE9A),
        ('record spatial update', 0x530F20, 0x530F79),
        ('category gain', 0x530740, 0x5307E0),
    )
    for label, start, end in ranges:
        data = image.read(start, end-start)
        decoded = list(decoder.disasm(data, start))
        require(sum(i.size for i in decoded) == len(data), 'instruction boundaries '+label)
        lines.append('\n%s %08X..%08X SHA256 %s' % (label, start, end, sha(data)))
        lines.extend('%08X %-8s %s' % (i.address, i.mnemonic, i.op_str) for i in decoded)
    # The spatial region mixes code and jump tables. Dump only instruction
    # boundaries actually executed, not a linear decode through embedded data.
    lines.append('\nExecuted impact/spatial instructions (union of cases; sorted, not a timeline)')
    for address in sorted(executed):
        decoded = list(decoder.disasm(image.read(address, 15), address, count=1))
        require(len(decoded) == 1, 'executed instruction %08X' % address)
        i = decoded[0]
        lines.append('%08X %-8s %s' % (i.address, i.mnemonic, i.op_str))
    return '\n'.join(lines)+'\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    workspace = Path.cwd().resolve()
    root = (workspace/args.root).resolve(strict=True)
    out = output_path(workspace, root, args.out)
    out.mkdir(parents=True, exist_ok=False)
    before = manifest(root)
    (out/'before.json').write_bytes(before)
    try:
        for name in ('src/collision_audio.cpp', 'src/race_audio.cpp', 'src/vehicle_track.cpp'):
            checked((workspace/name).read_bytes().replace(b'\r\n', b'\n'), SOURCES[name], name)
        blob = checked((root/'speed.exe').read_bytes(), INPUTS['speed.exe'], 'speed.exe')
        image = PE(blob)
        executed = set()
        report = dict(exe_sha256=sha(blob), source_hashes={k: SOURCES[k] for k in
                      ('src/collision_audio.cpp', 'src/race_audio.cpp', 'src/vehicle_track.cpp')},
                      boundary='synthetic pair/controller/listener state; no PCM or live race',
                      pair=pair_witness(image), retirement=retirement_witness(image),
                      admission=admission_witness(image), budget=budget_witness(image),
                      scrape_admission=scrape_admission_witness(image),
                      spatial=spatial_cases(image, executed),
                      spatial_profile=dict(address='006F47A0',
                          words=[u32(image.read(0x6F47A0+4*k, 4)) for k in range(28)]))
        (out/'instructions.txt').write_text(instructions(image, executed), encoding='ascii')
        (out/'report.json').write_text(json.dumps(report, ensure_ascii=True, indent=2)+'\n', encoding='ascii')
        print('PASS: native pair identity/cache reuse, record retirement and controller admission')
        print('PASS: pool replacement fragment; full pool ordering remains outside the witness')
        print('PASS: original spatial profile to impact dispatch boundary; synthetic listener/actor inputs')
    finally:
        after = manifest(root)
        (out/'after.json').write_bytes(after)
        require(before == after, 'complete retail manifest unchanged')
        print('PASS: complete retail manifest unchanged '+sha(after))


if __name__ == '__main__':
    main()

"""Bounded native minimap consumers. Synthetic state is not event0 footage.

Executes pinned scalar retail math. Graphics submission is captured, and
selection probes isolate the actor-list consumer with declared draw stubs.
"""
import struct

from hud254_native import Machine


OBJ, TRACK, ACTOR, POINT, OUT = 0x1001000, 0x1002000, 0x1003000, 0x1005000, 0x1006000


def fbits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def floats(machine, address, count):
    return list(struct.unpack('<'+'f'*count, machine.uc.mem_read(address, count*4)))


def prepare(image, metadata, style=3):
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESI
    m = Machine(image)
    # The executable's default thunks choose a CPU implementation using OS
    # imports. Select its scalar bodies explicitly; no math is emulated by us.
    m.write(0x6D4AC4, 0x5ABDB5)
    m.write(0x6D4ACC, 0x5AC6A2)
    m.run(0x5912B0, (0,))
    t = metadata['track']
    for off, value in ((0x8C,t['offset_x']),(0x90,t['offset_y']),(0x94,t['world_span'])):
        m.write(TRACK+off,fbits(value))
    for off, value in ((0x98,t['rotation_u16']),(0x9A,t['start_angle_u16']),(0x9C,t['finish_angle_u16'])):
        m.uc.mem_write(TRACK+off,struct.pack('<H',value))
    m.write(OBJ+0xA4,TRACK)
    m.write(OBJ+0x9C,ACTOR)
    m.write(OBJ+0xBC,1)
    m.write(OBJ+0xC0,style)
    m.write(OBJ+0xC4,style)
    for index, value in enumerate(metadata['placeholder']['native_center']):
        m.write(OBJ+0x68+index*4,fbits(value))
    for index, value in enumerate(metadata['placeholder']['size']):
        m.write(OBJ+0x70+index*4,fbits(value/2))
    m.write(0x78A320,1)
    m.run(0x565020,registers={UC_X86_REG_EAX:OBJ+0x20})
    m.run(0x40BAF0,(OBJ+0x20,OBJ+0x20),{UC_X86_REG_ESI:t['rotation_u16']})
    return m


def project(m, xy):
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESI, UC_X86_REG_EDI
    m.uc.mem_write(POINT,struct.pack('<3f',xy[0],xy[1],0))
    m.run(0x590AD0,registers={UC_X86_REG_EAX:POINT,UC_X86_REG_EDI:OBJ,UC_X86_REG_ESI:OUT})
    return floats(m,OUT,2)


def return_stub(m, cleanup=0):
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP
    esp = m.uc.reg_read(UC_X86_REG_ESP)
    m.uc.reg_write(UC_X86_REG_EIP,m.integer(esp))
    m.uc.reg_write(UC_X86_REG_ESP,esp+4+cleanup)


def quad(m, at):
    return dict(screen=[floats(m,at+0x10+i*16,2) for i in range(4)],
                uvq=[floats(m,at+0x50+i*12,3) for i in range(4)],
                colors=[list(m.uc.mem_read(at+0x80+i*4,4)) for i in range(4)])


def geometry(image, metadata, style, player, points, map_angle=0x8000, marker_angle=0, marker_sizes=None):
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
    m = prepare(image,metadata,style)
    m.uc.mem_write(ACTOR+0x60,struct.pack('<3f',player[0],player[1],0))
    submitted, clipped, binding_calls = [], [], []
    def hook(uc, at, size, user):
        esp = uc.reg_read(UC_X86_REG_ESP)
        if at == 0x58FB10:
            submitted.append(dict(kind=m.integer(esp+16),quad=quad(m,m.integer(esp+8))))
        elif at == 0x40BEF0:
            clipped.append(quad(m,uc.reg_read(UC_X86_REG_EAX)))
            return_stub(m)
        elif at in (0x414A10,0x414A60):
            binding_calls.append('%08X' % at)
            return_stub(m)
    handle = m.uc.hook_add(UC_HOOK_CODE,hook)
    center, boundary = 0x1007000, 0x1007100
    m.run(0x58F090,(OBJ,0,center,boundary,map_angle))
    route, route_clipped = list(submitted), list(clipped)
    samples = []
    for label,xy in points:
        submitted.clear()
        clipped.clear()
        m.uc.mem_write(POINT,struct.pack('<3f',xy[0],xy[1],0))
        marker_size = m.integer(0x737874+style*60)
        sizes = (marker_size,marker_size) if marker_sizes is None else tuple(fbits(v) for v in marker_sizes)
        m.run(0x58F640,(OBJ,0,POINT,center,map_angle,boundary,0x7313DD,5,0,
                       marker_angle,*sizes))
        samples.append(dict(name=label,xy=xy,submitted=list(submitted),clipped=list(clipped)))
    m.uc.hook_del(handle)
    return dict(style=style,map_angle=map_angle,marker_angle=marker_angle,marker_sizes=marker_sizes,
                player_xy=player,center=floats(m,center,2),
                boundary=[floats(m,boundary+i*8,2) for i in range(4)],
                route=route,route_clipped=route_clipped,markers=samples,binding_calls=binding_calls,
                limit='Native clipping; graphics submission captured at 0040BEF0, no pixel/GPU output')


def selection(image,metadata,eligible=(1,1),remote_human=False,style=3):
    """Native list producer and consumer; independent synthetic actor records."""
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_ESI
    m = prepare(image,metadata,style)
    manager,status,view = 0x1008000,0x1009000,0x100A000
    m.write(0x7361F8,manager)
    m.write(0x73619C,status)
    m.write(0x7361BC,view)
    m.write(view+4,ACTOR)
    m.write(0x7361C4,1)
    actors = [ACTOR,0x1010000,0x1012000,0x1014000,0x1016000,0x1018000]
    roles, ids = [1,2,2,2,4,3], [0,100,101,102,7,50]
    m.write(manager,actors[0])
    m.write(manager+8,manager+8)
    for index,actor in enumerate(actors):
        config = 0x1020000+index*0x1000
        m.write(actor,actors[index+1] if index+1<len(actors) else manager)
        m.write(actor+0x14,config)
        m.write(actor+0xC,config)
        m.write(config+4,roles[index])
        m.uc.mem_write(config+0x734,bytes((int(remote_human and index==0),)))
        m.uc.mem_write(config+1,bytes((ids[index],)))
        m.uc.mem_write(actor+0xA,struct.pack('<h',ids[index]))
        # Toggle one opponent, leaving all other records unchanged.
        m.uc.mem_write(actor+0x4D0,bytes((eligible[0] if index==1 else 1,)))
        m.uc.mem_write(actor+0x8A9,bytes((eligible[1] if index==1 else 1,)))
        m.uc.mem_write(actor+0x60,struct.pack('<3f',1000+index*10,500,0))
    m.run(0x41FE50,registers={UC_X86_REG_ESI:manager})
    partitions = [m.integer(manager+off) for off in (0x18,0x1C,0x20,0x24)]
    calls = []
    def hook(uc,at,size,user):
        esp = uc.reg_read(UC_X86_REG_ESP)
        if at == 0x58EFB0:
            m.write(m.integer(esp+4),0x8000)
            uc.reg_write(UC_X86_REG_EAX,1)
            return_stub(m,4)
        elif at == 0x58F640:
            position = m.integer(esp+12)
            owner = next((i for i,a in enumerate(actors) if a+0x60==position),None)
            calls.append(dict(fixture_id=ids[owner] if owner is not None else None,kind=m.integer(esp+32),
                              color_address='%08X' % m.integer(esp+28),
                              angle=m.integer(esp+40)&0xFFFF))
            return_stub(m,48)
        elif at == 0x58F090:
            return_stub(m,20)
        elif at == 0x58FA50:
            return_stub(m,12)
    m.uc.hook_add(UC_HOOK_CODE,hook)
    m.run(0x58E810,(0,),{UC_X86_REG_EAX:OBJ})
    return dict(opponent100_eligible=eligible,remote_human=remote_human,partitions=partitions,
                calls=calls,limit='Native partition/selection; synthetic actors; orientation/geometry/frame draws stubbed')


def visibility(image,stage,view_state,map_style,finished=False):
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX
    m = Machine(image)
    view,scene,status = 0x1001000,0x1002000,0x1003000
    for address,value in ((0x7361F0,scene),(scene,4),(0x73619C,status),(status,stage),
                          (0x7361C4,1),(0x78A30C,1),(0x78A31C,2),(0x78A320,1),
                          (0x78A324,3),(view+4,0x1004000),(view+0x58,int(finished))):
        m.write(address,value)
    m.uc.mem_write(view+0x20,bytes((view_state,)))
    m.uc.mem_write(0x78A3F6,bytes((map_style,)))
    stubs = (0x42AC10,0x42AC30,0x421A50,0x41F600,0x432A10,0x421A30,0x42AB30,0x42EBB0,0x43A2E0)
    calls = []
    def hook(uc,at,size,user):
        if at in stubs:
            calls.append('%08X'%at)
            uc.reg_write(UC_X86_REG_EAX,0)
            uc.reg_write(UC_X86_REG_EDX,0)
            return_stub(m)
    m.uc.hook_add(UC_HOOK_CODE,hook)
    m.run(0x42E620,(view,))
    low,high = m.uc.reg_read(UC_X86_REG_EAX),m.uc.reg_read(UC_X86_REG_EDX)
    return dict(stage=stage,view_state=view_state,map_style=map_style,finished=finished,
                low='%08X'%low,high='%08X'%high,map_enabled=bool(low&1),stub_calls=calls)


def orientations(image,metadata):
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDI, UC_X86_REG_ESI
    m = prepare(image,metadata,3)
    direction = [1.0,0.0]
    def hook(uc,at,size,user):
        if at == 0x5910F0:
            m.uc.mem_write(uc.reg_read(UC_X86_REG_EDI),struct.pack('<3f',*direction,0))
            uc.reg_write(UC_X86_REG_EAX,1)
            return_stub(m,4)
    m.uc.hook_add(UC_HOOK_CODE,hook)
    m.write(OBJ,fbits(1))
    rows=[]
    for name,tick,target in (('initial',0,(1,0)),('turn',67,(0,1)),
                             ('paused',67,(0,1)),('turn_continues',134,(0,1)),
                             ('reset_direction',201,(-1,0))):
        direction[:]=target
        m.write(0x73AD34,tick)
        m.run(0x58EFB0,(OUT,),{UC_X86_REG_ESI:OBJ})
        rows.append(dict(name=name,tick=tick,target=target,
                         angle=struct.unpack('<H',m.uc.mem_read(OUT,2))[0],
                         filter_state=floats(m,OBJ,4)))
    return dict(rows=rows,limit='Camera direction stub only; native filter/angle; no actual reset/pause scheduler')


def measure(image,metadata):
    m = prepare(image,metadata)
    styles = []
    for index in range(12):
        at = 0x737850+index*60
        styles.append(dict(index=index,tilt_u16=struct.unpack('<H',m.uc.mem_read(at,2))[0],
                           distance=floats(m,at+4,1)[0],offset=floats(m,at+8,2),
                           extent_add=floats(m,at+16,2),uv_extent=floats(m,at+24,2),
                           marker_sizes=floats(m,at+36,3),
                           flags=list(m.uc.mem_read(at+48,3)),
                           alpha=floats(m,at+52,1)[0],enabled=m.uc.mem_read(at+56,1)[0]))
    projections = [dict(name='node_%d'%r['index'],source_offset=r['offset'],xy=r['xy'],
                        uv=project(m,r['xy'])) for r in metadata['route_nodes']]
    grid = metadata['grid']
    # grid_records uses dictionaries with position/angle fields; retain its
    # full record separately so the sample identity cannot be mistaken.
    for row in grid:
        xy = row['raw_position'][:2]
        projections.append(dict(name='grid_%d'%row['marker_index'],xy=xy,uv=project(m,xy)))
    player = metadata['route_nodes'][11]['xy']
    seam = next(row['position'][:2] for row in metadata['lines'] if row['name']=='ForwardStartLine')
    points = [('same_position',player),('separation_10m',[player[0]+10,player[1]]),
              ('outside_map',[player[0]+5000,player[1]+5000])]
    geometry_cases = [geometry(image,metadata,s,player,points,a,h)
                      for s,a,h in ((1,0x8000,0),(3,0x8000,0),(3,0x4000,0x4000),
                                    (5,0x8000,0),(6,0x8000,0))]
    for name,xy in (('lap_before',[seam[0],seam[1]-0.25]),
                    ('lap_after',[seam[0],seam[1]+0.25]),
                    ('reset_to_player_grid',grid[1]['raw_position'][:2])):
        geometry_cases.append(geometry(image,metadata,3,xy,[(name,xy)]))
    masks = [visibility(image,stage,state,style,finished)
             for stage in (1,2,3,4) for state in (0,1,3)
             for style in (3,6) for finished in (False,True)]
    selections = [selection(image,metadata),selection(image,metadata,(0,1)),
                  selection(image,metadata,(1,0)),selection(image,metadata,remote_human=True)]
    return dict(styles=styles,world_matrix=floats(m,OBJ+0x20,16),projections=projections,
                geometry=geometry_cases,selection=selections,masks=masks,
                orientation=orientations(image,metadata),
                limits='Synthetic inputs and scalar CPU dispatch; not running-game, GPU, event0 option or lifecycle evidence')

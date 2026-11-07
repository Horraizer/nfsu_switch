"""Read-only producer witnesses for issue 254, Python 3.8+.

Pinned original instructions execute in synthetic memory, never a game process.
No live profile, GPU result, lifecycle capture or production readiness is implied.
"""
import argparse
import json
from pathlib import Path
import re
import struct

from hud254_trace import (PE, EXE_SHA, BUNDLE_SHA, LANGUAGE_SHA, chunks, fonts,
                          jdlz, manifest, output_path, resources, sha)
from hud254_native import Machine

SHADER_SHA = 'D1CDBD788D564C4CD408A060D0EDA4E99245F0EB54B18A4FCEE848D2966E6C98'
TOOLS = ('hud254_producers.py', 'hud254_producers_test.py', 'hud254_trace.py',
         'hud254_native.py')
RANGES = (
    ('profile_option_initializer', 0x4A9DD0, 0x4A9E28),
    ('profile_ctor_calls_options_context', 0x4AA200, 0x4AA20D),
    ('global_profile_ctor_context', 0x68AE60, 0x68AE6A),
    ('profile_slots_ctor_context', 0x4A9BDD, 0x4A9C08),
    ('profile_options_save_context', 0x4AAAD0, 0x4AAB31),
    ('profile_options_load_context', 0x4AAC70, 0x4AACBA),
    ('career_reset_entry_context', 0x4B37D0, 0x4B388B),
    ('profile_copy_lap_context', 0x4B4881, 0x4B4895),
    ('viewport_setter', 0x40ABD0, 0x40AC7B),
    ('render_target_dimensions_context', 0x40AA99, 0x40AAAF),
    ('feng_graphics_setup_context', 0x409CD0, 0x409EC2),
    ('effect_path_load_context', 0x40E2BC, 0x40E2DF),
    ('effect_decode_compile_context', 0x40E34D, 0x40E387),
    ('effect_technique_selection_context', 0x40E458, 0x40E500),
    ('resource_name_hash', 0x4F5360, 0x4F5487),
    ('resource_bind', 0x5003C0, 0x500410),
    ('parent_lookup_context', 0x50185F, 0x501887),
    ('package_attach_context', 0x5015C3, 0x5015DC),
    ('root_append', 0x500820, 0x500865),
    ('child_append', 0x4FBC30, 0x4FBC75),
    ('package_draw', 0x4F0390, 0x4F04E0),
    ('group_draw', 0x4F04E0, 0x4F0652),
    ('leaf_collect', 0x4F0660, 0x4F06F2),
    ('depth_sort', 0x4F16D0, 0x4F180D),
    ('reverse_visit', 0x4F5090, 0x4F50B8),
    ('group_callback', 0x4F5560, 0x4F5576),
    ('package_visibility', 0x4F56A0, 0x4F5730),
    ('leaf_visibility', 0x4F5640, 0x4F565C),
    ('cached_dispatch', 0x4F2CB0, 0x4F2D39),
)


def u32(data, at=0):
    return struct.unpack_from('<I', data, at)[0]


def package_objects(package):
    """Walk bounded enclosures, not a byte-pattern search inside strings/scripts."""
    top = list(chunks(package))
    if len(top) != 1 or top[0][0] != 0xE76E4546:
        raise ValueError('expected one FEng package')
    containers = [(begin+8, end+8) for kind, begin, end in chunks(package[8:])
                  if kind == 0xCC6A624F]
    if len(containers) != 1:
        raise ValueError('expected one object container')
    begin, end = containers[0]
    rows = []
    identities = set()
    for kind, first, last in chunks(package[begin:end]):
        if kind != 0xEA624F46:
            continue
        first, last = begin+first, begin+last
        parts = [(k, first+b, first+e) for k,b,e in chunks(package[first:last])]
        fields_parts = [(b,e) for k,b,e in parts if k == 0x446A624F]
        if len(fields_parts) != 1:
            raise ValueError('expected one ObjD')
        at, limit = fields_parts[0]
        fields = {}
        while at < limit:
            if at+4 > limit:
                raise ValueError('truncated object field')
            tag = package[at:at+2].decode('ascii')
            size = struct.unpack_from('<H',package,at+2)[0]
            if tag in fields or at+4+size > limit:
                raise ValueError('duplicate or truncated object field')
            fields[tag] = package[at+4:at+4+size]
            at += 4+size
        if any(key not in fields for key in ('Ot','On','OP','SA')):
            raise ValueError('missing required field')
        if len(fields['Ot']) != 4 or len(fields['OP']) != 16 or len(fields['SA']) not in (68,84):
            raise ValueError('unexpected field size: '+repr({k:len(v) for k,v in fields.items()}))
        identity = u32(fields['OP'])
        if identity in identities:
            raise ValueError('duplicate object identity')
        parent = u32(fields['PA']) if 'PA' in fields else None
        if parent is not None and (len(fields['PA']) != 4 or parent not in identities):
            raise ValueError('parent absent or not yet loaded')
        if parent is not None and next(r['kind'] for r in rows if r['id']==parent) != 5:
            raise ValueError('parent is not a group')
        if u32(fields['OP'],8) & 0x100000:
            raise ValueError('template inheritance is outside this witness')
        name = fields['On'].split(b'\0',1)[0].decode('ascii')
        rows.append(dict(name=name,id=identity,kind=u32(fields['Ot']),parent=parent,
            offset=first-8,sha256=sha(package[first-8:last]),
            flags=u32(fields['OP'],8),resource_index=struct.unpack_from('<H',fields['OP'],12)[0],
            sa=fields['SA'].hex(),has_script=any(k==0x70726353 for k,_,_ in parts)))
        identities.add(identity)
    if not rows or len(rows) > 256:
        raise ValueError('object count outside bound')
    return rows


class Probe(Machine):
    def __init__(self,image):
        super().__init__(image)
        self.executed = set()
        self.uc.mem_map(0,0x1000)  # Synthetic FS exception-chain storage.
        from unicorn import UC_HOOK_CODE
        self.uc.hook_add(UC_HOOK_CODE,lambda uc,at,size,user:self.executed.add(at))

    def ret(self,cleanup=0,result=None):
        from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EAX
        sp=self.uc.reg_read(UC_X86_REG_ESP)
        if result is not None:
            self.uc.reg_write(UC_X86_REG_EAX,result)
        self.uc.reg_write(UC_X86_REG_EIP,self.integer(sp))
        self.uc.reg_write(UC_X86_REG_ESP,sp+4+cleanup)

    def slice(self,begin,end,args=(),registers=None):
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_EIP
        def stop(uc,at,size,user):
            if at == end:
                uc.reg_write(UC_X86_REG_EIP,self.stop)
        handle=self.uc.hook_add(UC_HOOK_CODE,stop)
        try:
            self.run(begin,args,registers)
        finally:
            self.uc.hook_del(handle)


def profile_case(image,loaded):
    from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_EAX,UC_X86_REG_EBX
    m=Probe(image)
    m.uc.mem_write(0x7588D4,b'\xA5'*40)
    m.write(0x6EE6F0,0)  # Language is supplied, not read from a live registry.
    m.run(0x4A9DD0,registers={UC_X86_REG_ECX:0x7588D4})
    initialized=bytes(m.uc.mem_read(0x7588DA,1))[0]
    if loaded is not None:
        serialized=bytearray(4+13+40)
        serialized[4+13+6]=loaded
        m.uc.mem_write(0x1001000,bytes(serialized))
        m.slice(0x4AAC70,0x4AACBA,registers={UC_X86_REG_EAX:0x1001000,
            UC_X86_REG_ECX:len(serialized),UC_X86_REG_EBX:0x7588C4})
    m.run(0x432890,registers={UC_X86_REG_EAX:0x78A384})
    m.write(0x758948,0);m.write(0x7361A4,0)
    m.run(0x4B47A0,registers={UC_X86_REG_EAX:0})
    m.run(0x42BAC0,registers={UC_X86_REG_EAX:0x1002020,UC_X86_REG_ECX:0x78A384})
    return dict(loaded_option=loaded,initialized_option=initialized,
        selected_option=bytes(m.uc.mem_read(0x7588DA,1))[0],view_permission=m.integer(0x1002030),
        evidence='Synthetic profile initialization and optional serialized-option copy, then native view propagation',
        stubs=[],executed_instructions=len(m.executed))


def viewport_case(image,width,height):
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESI,UC_X86_REG_ESP
    m=Probe(image)
    target,device,vtable=0x1001000,0x1002000,0x1003000
    m.write(0x73636C,device);m.write(device,vtable)
    m.write(target+8,0x1111);m.write(target+12,0x2222)
    m.write(target+20,width);m.write(target+24,height)
    for offset in (0x94,0x9C,0xBC):
        m.write(vtable+offset,0x10FD000+offset)
    calls=[]
    def hook(uc,at,size,user):
        sp=uc.reg_read(UC_X86_REG_ESP)
        if at==0x40ED50:
            calls.append(dict(boundary='0040ED50',meaning='supplied empty pending-draw flush'))
            m.ret()
        elif at in (0x10FD094,0x10FD09C,0x10FD0BC):
            if at==0x10FD0BC:
                ptr=m.integer(sp+8)
                calls.append(dict(boundary='device+BC',viewport=list(struct.unpack('<4I2f',uc.mem_read(ptr,24)))))
                m.ret(8,0)
            else:
                count=3 if at==0x10FD094 else 2
                calls.append(dict(boundary='device+%X' % (at-0x10FD000),
                    args=[m.integer(sp+4*i) for i in range(2,count+1)]))
                m.ret(count*4,0)
    m.uc.hook_add(UC_HOOK_CODE,hook)
    m.run(0x40ABD0,(0,),{UC_X86_REG_ESI:target})
    return dict(supplied_size=[width,height],calls=calls,
        active_target_matches=m.integer(0x736458)==target,shader_context=m.integer(0x73640C),
        stubs=['0040ED50 empty flush','device+94 render target','device+9C depth target','device+BC viewport'],
        evidence='Original setter with synthetic render target and COM capture; no display or safe-area observation')


def native_resource_hashes(image,package):
    rows=resources(package)[0]['chunks']
    requests=next(r['records'] for r in rows if r['tag']=='52735271')
    m=Probe(image)
    table=0x1001000
    for index,row in enumerate(requests):
        if row['type'] not in (1,2):
            raise ValueError('unsupported resource type')
        name=0x1010000+index*512
        m.uc.mem_write(name,(row['name']+'\0').encode('ascii'))
        m.write(table+index*24+4,name)
        m.write(table+index*24+8,row['type'])
    m.run(0x4F5360,(0,len(requests),table))
    return [dict(index=i,name=r['name'],kind=r['type'],hash='%08X'%m.integer(table+i*24+16),
                 handle=m.integer(table+i*24+20)) for i,r in enumerate(requests)]


def native_bindings(image,rows,requests):
    from unicorn.x86_const import UC_X86_REG_ECX
    m=Probe(image)
    context,indirect,table,obj=0x1001000,0x1002000,0x1003000,0x1004000
    m.write(context+8,indirect);m.write(indirect,table)
    for row in requests:
        m.write(table+row['index']*24+16,int(row['hash'],16))
        m.write(table+row['index']*24+20,row['handle'])
    result=[]
    for row in rows:
        if not any(word in row['name'] for word in ('Lap','Laps')):
            continue
        m.uc.mem_write(obj,bytes(128))
        m.write(obj+24,row['kind'])
        m.uc.mem_write(obj+34,struct.pack('<H',row['resource_index']))
        m.run(0x5003C0,(obj,),{UC_X86_REG_ECX:context})
        result.append(dict(name=row['name'],index=row['resource_index'],
            bound_hash='%08X'%m.integer(obj+36),bound_handle=m.integer(obj+40)))
    return result


def traversal_case(image,rows,hidden=(),package_active=True):
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EDX,UC_X86_REG_ESP
    m=Probe(image)
    frontend,callback,context,package=0x1001000,0x1007000,0x1008000,0x100C000
    m.write(frontend+0x10C,callback);m.write(frontend+0xE4,package)
    m.write(callback,0x6C1B18);m.write(callback+12,context)
    m.write(package+12,0x100D000)
    m.uc.mem_write(0x100D000,b'HUD_SingleRace.fng\0')
    m.uc.mem_write(0x100D11E,struct.pack('<H',int(package_active)))
    m.write(0x736104,0);m.write(0x735784,0)
    m.write(0x6D4ACC,0x5AC6A2);m.write(0x6D4AC4,0x5ABDB5)
    m.write(0x7349B4,0x100D200);m.write(0x7349B8,0)
    for index in range(256):
        at=0x746118+8*index
        m.write(at,at);m.write(at+4,at)
    objects={};names={}
    for index,row in enumerate(rows):
        obj=0x1010000+index*0x200
        objects[row['id']]=obj;names[obj]=row['name']
        m.write(obj+12,row['id']);m.write(obj+24,row['kind'])
        m.write(obj+28,row['flags'] | (1 if row['name'] in hidden else 0))
        m.write(obj+44,obj+0x100)
        m.uc.mem_write(obj+0x100,bytes.fromhex(row['sa']))
        parent=row['parent']
        m.run(0x500820 if parent is None else 0x4FBC30,
              registers={UC_X86_REG_EAX:obj,UC_X86_REG_EDX:package if parent is None else objects[parent]})
    submissions=[];allocation=[0x1080000];sort_input=[];sorted_order=[]
    def hook(uc,at,size,user):
        sp=uc.reg_read(UC_X86_REG_ESP)
        if at==0x4F3F90:
            m.ret(4,0x100D100)
        elif at==0x567160:
            value=allocation[0];allocation[0]+=16
            if allocation[0]>=0x10E0000:
                raise RuntimeError('allocator bound')
            m.ret(result=value)
        elif at==0x4F16D0:
            ptr=m.integer(sp+4)
            sort_input.extend(dict(name=names[m.integer(ptr+4+i*8)],
                depth=struct.unpack('<f',uc.mem_read(ptr+8+i*8,4))[0]) for i in range(m.integer(ptr)))
        elif at==0x4F5090:
            ptr,count=m.integer(sp+4),m.integer(sp+8)
            sorted_order.extend(names[m.integer(ptr+i*8)] for i in range(count))
        elif at==0x4F2CB0:
            obj=uc.reg_read(UC_X86_REG_EAX)
            submissions.append(dict(name=names[obj],parent_index=struct.unpack('<H',uc.mem_read(obj+32,2))[0]))
            m.ret()
    m.uc.hook_add(UC_HOOK_CODE,hook)
    m.run(0x4F0390,registers={UC_X86_REG_EAX:frontend})
    return dict(hidden=list(hidden),package_active=package_active,sort_input=sort_input,
        sorted_order=sorted_order,submissions=submissions,
        stubs=['004F3F90 supplied package registry activation','00567160 bounded queue allocator',
               '004F2CB0 capture dispatch before geometry/cache/GPU'],
        executed_entries=['%08X'%at for at in (0x4F0390,0x4F04E0,0x4F0660,0x4F16D0,
            0x4F5090,0x4F5560,0x4F2E30,0x4F5640,0x4F2FD0,0x4F2EB0) if at in m.executed],
        evidence='Native traversal of reconstructed package links and unanimated SA fields; registry activation supplied')


def shader_contract(image,data):
    from unicorn.x86_const import UC_X86_REG_EBP,UC_X86_REG_EDI
    path=image.read(0x7014E0,128).split(b'\0',1)[0].decode('ascii')
    machine=Probe(image)
    machine.uc.mem_write(0x1001000,data)
    machine.slice(0x40E34D,0x40E35C,registers={UC_X86_REG_EBP:0x1001000,UC_X86_REG_EDI:len(data)})
    decoded=bytes(machine.uc.mem_read(0x1001000,len(data)))
    if decoded!=bytes(value^0xAA for value in data):
        raise ValueError('native effect decode mismatch')
    # The pinned file has a terminal decoded FF. Never silently discard an
    # arbitrary suffix or claim that compilation of this file was observed.
    if not decoded.endswith(b'\xff'):
        raise ValueError('unexpected effect terminator')
    text=decoded[:-1].decode('ascii')
    compact=re.sub(r'\s+','',text).lower()
    clauses={
        'vertex_position_wvp':'float4p=mul(in.position,worldviewproj);',
        'texture_offset':'out.tex=in.texcoord+textureoffset;',
        'programmable_rgba_modulate2':'float4diffuse=tex2d(samp,in.tex)*in.color*2;',
        'fixed_rgb_modulate2':'colorop[0]=modulate2x;',
        'fixed_alpha_modulate2':'alphaop[0]=modulate2x;',
        'linear_min':'minfilter=linear;', 'linear_mag':'magfilter=linear;',
        'linear_mip':'mipfilter=linear;', 'wrap_u':'addressu=wrap;', 'wrap_v':'addressv=wrap;',
    }
    if any(value not in compact for value in clauses.values()):
        raise ValueError('effect contract not found')
    return dict(path=path,decoded_sha256=sha(decoded),terminal_byte=decoded[-1],
        techniques=re.findall(r'technique\s+(\w+)',text),clauses=list(clauses),
        effect_context_index=0,native_decode=True,
        evidence='Static authenticated effect source and native decoder, no technique validation or GPU execution',
        unresolved=['Chosen device technique and effective shared parameters are not captured',
                    'Terminal FF compiler handling and compilation success are not observed',
                    'No texel values, filtering result, raster or final pixel equality is measured'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('root','reference','out'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--source',required=True)
    args=parser.parse_args()
    if not re.fullmatch('[0-9a-f]{40}',args.source):
        parser.error('source must be a full lowercase commit SHA')
    roots=[args.root.resolve(),args.reference.resolve()]
    if any(not root.is_dir() for root in roots):
        parser.error('missing input root')
    out=output_path(args.out,roots)
    import capstone,unicorn
    if (capstone.__version__,unicorn.__version__)!=('5.0.7','2.1.4'):
        parser.error('requires Capstone 5.0.7 and Unicorn 2.1.4')
    out.mkdir(parents=True)
    before=[manifest(root) for root in roots]
    for i,value in enumerate(before):
        (out/('before-%d.json'%i)).write_bytes(value)
    try:
        identities={'speed.exe':EXE_SHA,'Global/INGAMEB.BUN':BUNDLE_SHA,
                    'Languages/English.bin':LANGUAGE_SHA,'fx/world.efx':SHADER_SHA}
        inputs={name:(roots[0]/name).read_bytes() for name in identities}
        if any(sha(inputs[name])!=digest for name,digest in identities.items()):
            raise ValueError('unsupported input identity')
        image=PE(inputs['speed.exe'])
        package=jdlz(inputs['Global/INGAMEB.BUN'][0x16F40C:])
        rows=package_objects(package)
        requests=native_resource_hashes(image,package)
        font_rows=fonts(inputs['Languages/English.bin'])
        md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
        ranges=[];lines=[]
        for name,begin,end in RANGES:
            data=image.read(begin,end-begin)
            decoded=list(md.disasm(data,begin))
            if sum(i.size for i in decoded)!=len(data):
                raise ValueError('incomplete instruction range: '+name)
            ranges.append(dict(name=name,begin='%08X'%begin,end='%08X'%end,sha256=sha(data)))
            lines.append('\n%s %08X..%08X'%(name,begin,end))
            lines.extend('%08X %-8s %s'%(i.address,i.mnemonic,i.op_str) for i in decoded)
        (out/'instructions.txt').write_text('\n'.join(lines)+'\n',encoding='ascii')
        report=dict(source_commit=args.source,input_sha256=identities,package_sha256=sha(package),
            tools={name:sha((Path(__file__).parent/name).read_bytes()) for name in TOOLS},ranges=ranges,
            profiles=[profile_case(image,value) for value in (None,0,1,2)],
            viewports=[viewport_case(image,w,h) for w,h in ((640,480),(1280,720),(1920,1080))],
            objects=rows,resources=requests,bindings=native_bindings(image,rows,requests),fonts=font_rows,
            traversal=[traversal_case(image,rows),traversal_case(image,rows,('LapNumber','LapCounterBacking_2')),
                       traversal_case(image,rows,package_active=False)],
            shader=shader_contract(image,inputs['fx/world.efx']),
            callback_table=['%08X'%u32(image.read(0x6C1B18+i*4,4)) for i in range(16)],
            evidence='Authenticated static producer provenance and bounded synthetic native execution; no original runtime capture',
            presentation_ready=False,
            blockers=['Actual selected profile/options and complete movie/pause/retry/finish lifecycle',
                'Runtime package activation, animation state, resource replacements and parent mutations',
                'Actual render target dimensions, device-selected effect technique and shared parameters',
                'Injected installation patches are not executed or authenticated as stock behavior',
                'Original event0 frames and Switch comparison/owner hardware acceptance'])
        (out/'producers.json').write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n',encoding='ascii')
    finally:
        after=[manifest(root) for root in roots]
        for i,value in enumerate(after):
            (out/('after-%d.json'%i)).write_bytes(value)
        if before!=after:
            raise RuntimeError('retail/reference files or directories changed')
    print('PASS: bounded producer witnesses; full retail/reference manifests unchanged; no live runtime claim')


if __name__=='__main__':
    main()

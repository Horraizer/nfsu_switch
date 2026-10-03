"""Read-only upstream lap HUD contract, pinned PE, Python 3.8+.

No game launch, pixel extraction, output overwrite or inferred runtime defaults.
Requires Capstone 5.0.7 / Unicorn 2.1.4 in the caller's environment.
"""
import argparse
import json
from pathlib import Path
import re
import struct

from hud254_trace import (EXE_SHA, BUNDLE_SHA, LANGUAGE_SHA, PE, chunks,
                          feng_objects, fonts, jdlz, manifest, output_path, resources, sha)
from hud254_presentation_native import Renderer, hash_name, language_case, permission_case

GLOBAL_SHA = '39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9'
TEXTURES_SHA = '6A92604D9766557115DEE8F8CE18F392557AF1A8B0369E69B474F36DF2691615'

# Exact instruction boundaries, exclusive end. Context slices are named as such.
RANGES = (
    ('language_registry_context',0x40132B,0x40139B),
    ('language_bootstrap_context',0x448794,0x4487A9),
    ('language_setter',0x59FA20,0x59FB30),
    ('view_settings_init',0x432890,0x432902),
    ('race_settings_init_view_calls_context',0x431EBE,0x431EE4),
    ('profile_to_view_player0',0x4B47A0,0x4B48E2),
    ('career_profile_call_context',0x4B602B,0x4B604F),
    ('view_ctor_copy_context',0x42B1C2,0x42B245),
    ('view_settings_copy',0x42BAC0,0x42BB5E),
    ('inrace_lap_toggle_context',0x4BFD90,0x4BFE1B),
    ('per_view_update_context',0x430DBC,0x430DF2),
    ('font_loader',0x4F44C0,0x4F46AA),
    ('font_lookup',0x4F43D0,0x4F441C),
    ('Sj_fields',0x501AC0,0x501B77),
    ('transform',0x4F1810,0x4F1D76),
    ('sprite',0x4F2000,0x4F2278),
    ('text_submit',0x4F25F0,0x4F27CC),
    ('glyph_geometry',0x4F4790,0x4F48A7),
    ('text_layout',0x4F48B0,0x4F4CF2),
    ('text_width',0x4F4D00,0x4F4E9C),
    ('text_height',0x4F4F10,0x4F5000),
    ('quad_build',0x514530,0x5146CA),
    ('quad_submit',0x514B90,0x514BE7),
    ('quad_ndc',0x40BEF0,0x40C2F4),
    ('texture_material',0x40F3D0,0x40F6DB),
    ('queue_bucket',0x4F2FD0,0x4F3024),
    ('queue_append',0x4A9900,0x4A994C),
    ('queue_drain',0x4F2EB0,0x4F2FC2),
    ('visible_object_queue',0x4F5640,0x4F565C),
)


def textures(data, base=0):
    """Metadata only, recursively bounded known texture containers, no pixels."""
    result = []
    for kind, begin, end in chunks(data):
        if kind in (0xB3300000,0xB3310000):
            result.extend(textures(data[begin:end],base+begin))
        elif kind == 0x33310004:
            if (end-begin) % 124:
                raise ValueError('invalid texture info stride')
            for at in range(begin,end,124):
                blob = data[at:at+124]
                raw = blob[12:36]
                if b'\0' not in raw:
                    raise ValueError('unterminated texture name')
                name = raw.split(b'\0',1)[0].decode('ascii')
                result.append(dict(name=name,hash='%08X' % struct.unpack_from('<I',blob,36)[0],
                    offset=base+at,sha256=sha(blob),width=struct.unpack_from('<H',blob,68)[0],
                    height=struct.unpack_from('<H',blob,70)[0],alpha_test=blob[85],
                    alpha_blend=blob[86],layer_order=blob[81],layer_group=blob[84]))
    return result


def language_table(image):
    def string(at):
        return image.read(at,128).split(b'\0',1)[0].decode('ascii')
    return [dict(id=identity,name=string(name),file=string(filename))
            for identity,name,filename,_ in
            (struct.unpack('<4I',image.read(0x6EE650+i*16,16)) for i in range(10))]


def object_map(package):
    tables = resources(package)
    if len(tables) != 1:
        raise ValueError('expected one FEng resource table')
    requests = next(c['records'] for c in tables[0]['chunks'] if c['tag']=='52735271')
    result = {}
    for obj in feng_objects(package):
        fields={f['tag']:f for f in obj['fields']}
        index=fields['OP']['resource_index']
        if index != 0xFFFF:
            if index >= len(requests):
                raise ValueError('resource index outside table')
            obj['default_resource']=requests[index]
        result[obj['name']]=obj
    return result


def queue_case(image, depths):
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP
    from hud254_native import Machine
    m=Machine(image)
    m.write(0x7349B4,0x1001000)
    m.write(0x7349B8,0)
    alloc=[0x1006000]
    submitted=[]
    for index in range(256):
        at=0x746118+8*index
        m.write(at,at);m.write(at+4,at)
    def hook(uc,at,size,_):
        if at not in (0x567160,0x4F2CB0):
            return
        sp=uc.reg_read(UC_X86_REG_ESP)
        if at == 0x567160:
            uc.reg_write(UC_X86_REG_EAX,alloc[0]);alloc[0]+=16
        else:
            submitted.append((uc.reg_read(UC_X86_REG_EAX)-0x1002000)//256)
        uc.reg_write(UC_X86_REG_EIP,m.integer(sp))
        uc.reg_write(UC_X86_REG_ESP,sp+4)
    m.uc.hook_add(UC_HOOK_CODE,hook)
    for i,depth in enumerate(depths):
        obj=0x1002000+256*i
        m.write(obj+0x2C,obj+0x80)
        m.uc.mem_write(obj+0x80+0x24,struct.pack('<f',depth))
        m.run(0x4F2FD0,registers={UC_X86_REG_EAX:obj,UC_X86_REG_EDX:0x1008000})
    m.run(0x4F2EB0,(0,),{UC_X86_REG_EAX:0x1008000})
    return dict(depths=depths,submission_indices=submitted,
                stubs=['00567160 allocator','004F2CB0 capture object dispatch'])


def measure(image,bundle,language,global_bundle,language_textures):
    package=jdlz(bundle[0x16F40C:])
    if b'HUD_SingleRace.fng' not in package[:256]:
        raise ValueError('unexpected HUD package')
    objects=object_map(package)
    texture_rows=[]
    for origin,data in (('Global/INGAMEB.BUN',bundle),('Global/GLOBALB.BUN',global_bundle),
                        ('Languages/LanguageTextures.bin',language_textures)):
        texture_rows.extend(dict(t,origin=origin) for t in textures(data))
    selected=[]
    for name in ('3RDPERSON_BOXCORNER','WHITE16X16','CONDUITMDITC_TT14I','LCD_LET48','FONT_CONDUITMDITCTT38BI'):
        matches=[t for t in texture_rows if t['hash']=='%08X' % hash_name(name)]
        if len(matches)!=1:
            raise ValueError('texture missing or ambiguous: '+name)
        selected.append(matches[0])
    tex={t['hash']:t for t in selected}
    cases=[]
    base_texts=dict(CurrentLapTime=' 1:00.00',BestLapTime='--:--:--',
        CurrentLapTimeText='Current:',BestLapTimeText='Best:',LapNumber=' 1',NumberLaps='/ 2',LapText='Laps')
    trials=[(name,text,None,None) for name,text in base_texts.items()]
    trials += [('CurrentLapTime',' 1:00.00',sj,None) for sj in (0,1,2,3,4,8,12)]
    parent=dict(color=[128,192,256,128],matrix=[2,0,0,0,0,3,0,0,0,0,1,0,11,17,0,1])
    trials.append(('CurrentLapTime',' 1:00.00',None,parent))
    for name,text,sj,parent in trials:
        obj=objects[name]
        resource=obj['default_resource']['name'].replace('\\','/').split('/')[-1].rsplit('.',1)[0]
        texture=tex['%08X' % hash_name(resource)]
        r=Renderer(image)
        font=r.load_font(language,resource,(texture['width'],texture['height']))
        r.uc.mem_write(r.texture+85,bytes((texture['alpha_test'],texture['alpha_blend'])))
        r.object_fields(obj,text,sj,parent)
        output=r.render_text()
        cases.append(dict(name=name,text=text,sj_override=sj,parent=parent,font=font,
            texture=texture,output=output,stubs=sorted(set(r.stub_calls)),
            executed_instructions=len(r.executed),
            executed_entries=['%08X' % at for at in (0x4F44C0,0x4F43D0,0x501AC0,0x4F1810,0x4F25F0,0x4F48B0,0x4F4790,0x514530,0x514B90,0x40BEF0,0x40F3D0) if at in r.executed]))
    for name in ('LapCounterBacking_1','LapCounterBacking_2','LapCounterBacking_3'):
        obj=objects[name]
        resource=obj['default_resource']['name'].replace('\\','/').split('/')[-1].rsplit('.',1)[0]
        texture=tex['%08X' % hash_name(resource)]
        r=Renderer(image)
        r.uc.mem_write(r.texture+85,bytes((texture['alpha_test'],texture['alpha_blend'])))
        r.object_fields(obj,'')
        cases.append(dict(name=name,texture=texture,output=r.render_image((texture['width'],texture['height'])),
                          stubs=sorted(set(r.stub_calls)),executed_instructions=len(r.executed)))
    return dict(language_table=language_table(image),
        font_overrides=[dict(hash='%08X' % h,baseline=b,line_spacing_scale=s) for h,b,s in
            (struct.unpack('<Iff',image.read(0x6F9498+12*i,12)) for i in range(4))],
        languages=[language_case(image,i) for i in (0,1,2,3,4,5,6)],
        permissions=[permission_case(image,option,initial) for option in (0,1,2) for initial in (0,0x755,0xFFFFFFFF)],
        queue=[queue_case(image,depths) for depths in ([100,120,120,100],[-5,0,300,255],[100.25,100.75,101])],
        cases=cases,objects=[objects[name] for name in list(base_texts)+['LapCounterBacking_1','LapCounterBacking_2','LapCounterBacking_3']],
        textures=selected,font_metadata=fonts(language),package_sha256=sha(package))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('root','reference','out'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--source',required=True)
    args=parser.parse_args()
    if not re.fullmatch('[0-9a-f]{40}',args.source):
        parser.error('source must be full lowercase SHA')
    roots=[args.root.resolve(),args.reference.resolve()]
    if any(not p.is_dir() for p in roots):
        parser.error('input directory missing')
    out=output_path(args.out,roots)
    import capstone,unicorn
    if (capstone.__version__,unicorn.__version__) != ('5.0.7','2.1.4'):
        parser.error('expected Capstone 5.0.7 and Unicorn 2.1.4')
    out.mkdir(parents=True)
    before=[manifest(p) for p in roots]
    for i,value in enumerate(before):
        (out/('before-%d.json' % i)).write_bytes(value)
    try:
        inputs={name:(roots[0]/name).read_bytes() for name in
            ('speed.exe','Global/INGAMEB.BUN','Languages/English.bin','Global/GLOBALB.BUN','Languages/LanguageTextures.bin')}
        for (name,data),expected in zip(inputs.items(),(EXE_SHA,BUNDLE_SHA,LANGUAGE_SHA,GLOBAL_SHA,TEXTURES_SHA)):
            if sha(data)!=expected:
                raise ValueError('unsupported input: '+name)
        image=PE(inputs['speed.exe'])
        md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
        instructions=[]
        ranges=[]
        for label,begin,end in RANGES:
            blob=image.read(begin,end-begin)
            decoded=list(md.disasm(blob,begin))
            if sum(i.size for i in decoded)!=len(blob):
                raise ValueError('incomplete instruction range: '+label)
            ranges.append(dict(name=label,begin='%08X' % begin,end='%08X' % end,sha256=sha(blob)))
            instructions.append('\n%s %08X..%08X SHA256 %s' % (label,begin,end,sha(blob)))
            instructions.extend('%08X %-8s %s' % (i.address,i.mnemonic,i.op_str) for i in decoded)
        (out/'instructions.txt').write_text('\n'.join(instructions)+'\n',encoding='ascii')
        report=measure(image,inputs['Global/INGAMEB.BUN'],inputs['Languages/English.bin'],
                       inputs['Global/GLOBALB.BUN'],inputs['Languages/LanguageTextures.bin'])
        report.update(source_commit=args.source,input_sha256={name:sha(data) for name,data in inputs.items()},
            ranges=ranges,tools={name:sha((Path(__file__).parent/name).read_bytes()) for name in
                ('hud254_presentation.py','hud254_presentation_native.py','hud254_native.py','hud254_trace.py')},
            evidence='Synthetic native execution with real PE/package/font metadata; no original runtime capture',
            limits=['Supplied profile bytes, strings, selected language, allocated object/context and texture handles',
                    'Native font loader uses null optional callback table; scalar matrix dispatch selected explicitly',
                    'Identity parent unless case declares synthetic parent; no complete package hierarchy lifecycle',
                    'Lookup/allocator/render-state/COM stubs listed per case; no real GPU shader, viewport, safe area or raster proof',
                    'No proof that later runtime code never overrides fonts; English package bindings are conditional',
                    'No event0 movie, pause, retry or finish execution; mask evidence retains existing declared predicate stubs'])
        (out/'presentation.json').write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n',encoding='ascii')
    finally:
        after=[manifest(p) for p in roots]
        for i,value in enumerate(after):
            (out/('after-%d.json' % i)).write_bytes(value)
        if before!=after:
            raise RuntimeError('input files/directories changed')
    print('PASS: presentation producers, native glyph/sprite/queue submissions; full retail/reference manifests unchanged')


if __name__=='__main__':
    main()

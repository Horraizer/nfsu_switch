"""Verify first-capture sampler completeness from actual shader/recorder source."""
from pathlib import Path
import argparse,re

def check(root,source):
    start=source.index('bool record_frame(');first=source.index('emit_draw(cmd,draw',start)
    capture=source[start:first]
    safe='cmd.bindTextures(DkStage_Fragment,1,dkMakeTextureHandle(static_cast<std::uint32_t>(first_texture),2));'
    if safe not in capture:raise ValueError('first cube draw has undefined sampler1')
    upload=source[source.index('textures.resize(handles.size())'):source.index('// Legacy nearest stays available.')]
    nontexture=upload.index('if (r.kind != UploadResourceKind::texture)')
    select=upload.index('if(first_texture==kNoHandle)first_texture=i;')
    if nontexture>=select or 'continue;' not in upload[nontexture:select]:raise ValueError('safe texture is not an ordinary uploaded image')
    emit=source[source.index('void emit_draw('):source.index('static bool same_fixed_state(')]
    if 'out.bindTextures(DkStage_Fragment, 0,' not in emit:raise ValueError('base sampler not bound per draw')
    # Recorder binding trace: scene-global safe image then each real draw's
    # base image. Infer shader requirements from both compiled-stage sources.
    bound={int(x) for x in re.findall(r'(?:cmd|out)\.bindTextures\(DkStage_Fragment,\s*(\d+),',capture+emit)}
    for name in ['textured.frag.glsl','textured_opaque.frag.glsl']:
        shader=(root/'shaders'/name).read_text()
        required={int(x) for x in re.findall(r'layout\s*\(binding\s*=\s*(\d+)\)\s*uniform sampler2D',shader)}
        if not required or not required<=bound:raise ValueError('missing statically declared sampler: '+name)
    if 'static_cast<std::uint32_t>(handles.size()),2)' in safe:raise ValueError('safe image aliases active target')

def main():
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);root=p.parse_args().root
    source=(root/'switch/deko_renderer.cpp').read_text();check(root,source)
    safe='cmd.bindTextures(DkStage_Fragment,1,dkMakeTextureHandle(static_cast<std::uint32_t>(first_texture),2));'
    for label,bad in [('undefined slot1',source.replace(safe,'',1)),('capture-target alias',source.replace(safe,safe.replace('first_texture','handles.size()'),1)),('nontexture source',source.replace('if(first_texture==kNoHandle)first_texture=i;','',1))]:
        try:check(root,bad)
        except ValueError:print('PASS negative control: '+label)
        else:raise ValueError('negative control accepted: '+label)
    print('PASS actual first-capture recorder source defines all basic fragment samplers using ordinary uploaded image; not GPU execution')
if __name__=='__main__':main()

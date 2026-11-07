"""Bind executable scalar parity to the production shader and FE-only upload."""
import argparse
from pathlib import Path

def check(shader,renderer):
    branch=shader[shader.index('if (material.fallback.w == 3.0)'):shader.index('// This shader is selected only')]
    assert branch.count('frontend_glow_channel(')==3
    assert 'material.environment_color.rgb : inColor.rgb * 255.0' in branch
    assert 'frontend_glow_alpha(texel.a, material.alpha.x)' in branch
    assert 'return;' in branch
    sample='vec4 sampled = texture(baseTexture, uv);'
    quantized='uvec4 texel = uvec4(sampled * 255.0 + 0.5);'
    assert shader.index(sample)<shader.index(quantized)<shader.index('if (material.fallback.w == 3.0)')<shader.index('vec3 normal = cross')
    assert shader.index('if (frame.options.x != 0u)')<shader.index('uvec4 texel =')
    upload=renderer[renderer.index('// Copy a private GPU payload'):renderer.index('template<class Recorder>')]
    assert 'source.frontend_light_enabled && frontend_glow_fast(view,source.frame)' in upload
    assert 'auto material=view.material;' in upload and 'material.fallback[3]=3;' in upload
    assert '&material, sizeof(DrawMaterialUniform)' in upload
    loop=renderer[renderer.index('const auto first_dynamic = package ?',renderer.index('bool record_frame(')):renderer.index('cmd.barrier(DkBarrier_Fragments, 0);',renderer.index('const auto first_dynamic = package ?',renderer.index('bool record_frame(')))]
    skip='if(frontend && frontend_glow_no_output(view,commands->frame))continue;'
    assert skip in loop
    assert loop.index('const auto index = i - first_dynamic;')<loop.index(skip)<loop.index('emit_draw(cmd, view.definition')<loop.index('previous_dynamic = &view.definition;')

p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);root=p.parse_args().root
s=(root/'shaders/textured_opaque.frag.glsl').read_text();r=(root/'switch/deko_renderer.cpp').read_text();check(s,r)
for name,bad_s,bad_r in [
    ('base sampler binding lost',s.replace('vec4 sampled = texture(baseTexture, uv);','vec4 sampled = texture(reflectionTexture, uv);'),r),
    ('sample quantization bypass',s.replace('uvec4 texel = uvec4(sampled * 255.0 + 0.5);','uvec4 texel = uvec4(sampled);'),r),
    ('alpha rounding bypass',s.replace('frontend_glow_alpha(texel.a, material.alpha.x)','texel.a'),r),
    ('car modulation bypass',s.replace('material.environment_color.rgb : inColor.rgb * 255.0','inColor.rgb * 255.0 : inColor.rgb * 255.0'),r),
    ('global specialization',s,r.replace('source.frontend_light_enabled && frontend_glow_fast(view,source.frame)','true')),
    ('skip stale fixed state',s,r.replace('if(frontend && frontend_glow_no_output(view,commands->frame))continue;','previous_dynamic = &view.definition; if(frontend && frontend_glow_no_output(view,commands->frame))continue;')),
    ('uniform compaction',s,r.replace('const auto index = i - first_dynamic;','const auto index = 0;')),
]:
    try:check(bad_s,bad_r)
    except (AssertionError,ValueError):print('PASS negative control: '+name)
    else:raise AssertionError('negative control accepted: '+name)
print('PASS production glow shader/sampler/upload and stable emission binding; not hardware execution')

for filename in ('textured.frag.glsl','textured_opaque.frag.glsl'):
    shader=(root/'shaders'/filename).read_text()
    original=shader[shader.index('if(material.alpha.w == 8.0)'):shader.index('// Native skid ribbon:')]
    assert 'texel.rgb*(material.environment_color.rgb/255.0)*2.0' in original
    assert 'texel.a*material.alpha.x*2.0' in original
    assert 'return;' in original
scene=(root/'src/frontend_scene.cpp').read_text()
assert 'draw.material.environment_range[3]=2;draw.material.alpha[3]=8;' in scene
assert 'material.ordinary_world=draw.frontend_sprite_kind!=0;' in scene
assert 'material.alpha_scale=draw.material.alpha[0]*(material.ordinary_world?2:1);' in scene
print('PASS scoped FE car flares retain authenticated world.efx RGBA2x on CPU and both GPU variants')

for filename in ('textured.frag.glsl','textured_opaque.frag.glsl'):
    shader=(root/'shaders'/filename).read_text()
    neon=shader[shader.index('if(material.alpha.w == 7.0)'):shader.index('if(material.alpha.w == 8.0)')]
    assert 'uint rgb=uint(material.alpha.z);' in neon
    assert 'texel.rgb*tint*2.0' in neon and 'texel.a*material.alpha.x' in neon and 'return;' in neon
print('PASS FE neon packed-color controller has an actual generic GPU fragment consumer')

neon_reference=scene[scene.index('bool draw_frontend_neon('):scene.index('bool draw_frontend_road_sprites(')]
assert 'material.ordinary_world=true;' in neon_reference
assert 'options.explicit_camera' in neon_reference
assert 'clip[k].r=float((rgb>>16)&255)' in neon_reference and 'clip[k].b=float(rgb&255)' in neon_reference
print('PASS FE neon reference retains RGB2x and explicit current camera')

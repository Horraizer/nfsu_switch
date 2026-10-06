"""Tie executable race equations to all production uploads and shader states."""
import argparse
from pathlib import Path


def block(source, token):
    start = source.index(token)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def check(shaders, renderer):
    branches = []
    for index, shader in enumerate(shaders):
        branch = block(shader, 'if (race_fast)')
        branches.append(branch)
        assert branch.count('race_effect_channel(') == 3
        assert 'race_effect_alpha(texel.a, material.alpha.x)' in branch
        assert 'material.environment_color.rgb : inColor.rgb * 255.0' in branch
        assert 'frame.options.y != 0u || material.environment_range.w > 0.5' in branch
        assert 'bool race_fast = material.fallback.w == 4.0 || material.fallback.w == 5.0;' in shader
        early = block(shader, 'if (race_fast && (material.fallback.w == 4.0 || (frame.options.z & 1u) == 0u))')
        assert 'uint shade = 256u;' in early and 'return;' in early
        assert early.count('race_effect_channel(') == 3
        assert early[early.index('        bool modulate ='):] == branch[branch.index('        bool modulate ='):]
        assert shader.count('dFdx(inWorld)') == 1 and shader.count('dFdy(inWorld)') == 1
        for operation in ('vec3 normal = cross(dFdx(inWorld), dFdy(inWorld));',
                          'bool has_normal = dot(normal, normal) > 0.0;',
                          'normal = normalize(normal);',
                          'if (dot(normal, to_eye) < 0.0)', 'normal = -normal;'):
            assert operation in shader
        shade = block(shader, 'if ((frame.options.z & 1u)')
        assert '(material.fallback.w < 1.5 || material.fallback.w == 5.0) && material.environment_range.w < 0.5)' in shade
        assert 'float k = 0.35 + 0.65 * max(0.0, dot(normal, frame.light.xyz));' in shade
        assert 'shade = uint(floor(clamp(k, 0.0, 1.0) * 256.0 + 0.5));' in shade
        assert shader.index(early) < shader.index('vec3 normal = cross') < shader.index(shade) < shader.index(branch) < shader.index('float vdotn = 0.01;')
        assert shader.index('if (frame.options.x != 0u)') < shader.index('uvec4 texel =') < shader.index(branch)
        assert shader.index('material.environment_min.w>0.5') < shader.index(branch)
        if index == 0:
            assert shader.index('if (texel.a < frame.options.w)') < shader.index(branch)
            assert shader.index('if (material.alpha.w == 4.0)') < shader.index(branch)
    assert branches[0] == branches[1]
    upload = block(renderer, 'void write_uniforms(')
    assert 'source.frontend_light_enabled ? draw.material :' in upload
    assert 'race_effect_gpu_material(draw,draw.material)' in upload
    assert 'else if(!source.frontend_light_enabled)' in upload
    assert 'race_effect_gpu_material(view.definition,view.material)' in upload
    assert 'const auto index = i - first_dynamic;' in upload
    assert 'data + (2+2*index)*256' in upload
    static = block(renderer, 'bool prepare_static_commands(')
    assert 'source.frontend_light_enabled ? draw.material :' in static
    assert 'race_effect_gpu_material(draw,draw.material)' in static
    assert 'commands->frame' not in static
    visible = block(renderer, 'bool prepare_visible_commands(')
    assert 'race_effect_no_output' not in visible
    assert 'static_alpha_cutoff[slot] == commands->frame.options[3]' in visible
    assert 'if (unchanged) return true;' in visible
    assert 'static_uniforms.getGpuAddr() + (static_instances+index)*256' in visible
    loop = renderer[renderer.index('const auto first_dynamic = package ?', renderer.index('bool record_frame(')):]
    loop = loop[:loop.index('cmd.barrier(DkBarrier_Fragments, 0);')]
    skip = 'if(!frontend && !commands->frontend_light_enabled && race_effect_no_output(view))continue;'
    assert loop.index('const auto index = i - first_dynamic;') < loop.index(skip) < loop.index('emit_draw(cmd, view.definition') < loop.index('previous_dynamic = &view.definition;')
    assert 'previous_dynamic = &view.definition;' not in loop[:loop.index(skip)]
    # Keep reflected command counts/slots unchanged; specialize material only.
    assert 'reflected+(2+2*i)*256, &material, sizeof material' in upload
    assert 'race_effect_no_output' not in upload


p = argparse.ArgumentParser()
p.add_argument('--root', required=True, type=Path)
root = p.parse_args().root
shaders = [(root / 'shaders' / name).read_text() for name in ('textured.frag.glsl', 'textured_opaque.frag.glsl')]
renderer = (root / 'switch/deko_renderer.cpp').read_text()
check(shaders, renderer)
for name, old, new, shader_mutation in [
    ('alpha rounding bypass', 'race_effect_alpha(texel.a, material.alpha.x)', 'texel.a', True),
    ('vertex-color override', 'frame.options.y != 0u || material.environment_range.w > 0.5', 'true', True),
    ('flat lighting removed', 'normal = normalize(normal);', 'normal = vec3(0);', True),
    ('flat path bypass', 'race_fast && (material.fallback.w == 4.0 || (frame.options.z & 1u) == 0u)', 'race_fast', True),
    ('static frame dependence', 'const auto material=source.frontend_light_enabled ? draw.material :', 'const auto material=commands->frame.options[2] ? draw.material :', False),
    ('dynamic upload bypass', 'race_effect_gpu_material(view.definition,view.material)', 'view.material', False),
    ('stale skipped state', 'if(!frontend && !commands->frontend_light_enabled && race_effect_no_output(view))continue;', 'previous_dynamic = &view.definition; if(!frontend && !commands->frontend_light_enabled && race_effect_no_output(view))continue;', False),
    ('uniform compaction', 'const auto index = i - first_dynamic;', 'const auto index = 0;', False),
    ('static cutoff cache removed', 'static_alpha_cutoff[slot] == commands->frame.options[3]', 'true', False),
]:
    try:
        check([s.replace(old, new, 1) for s in shaders] if shader_mutation else shaders,
              renderer if shader_mutation else renderer.replace(old, new))
    except (AssertionError, ValueError):
        print('PASS negative control: ' + name)
    else:
        raise AssertionError('negative control accepted: ' + name)
print('PASS production race shader, static reuse, dynamic/reflection uploads and stable emission; not hardware execution')

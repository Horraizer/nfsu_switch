#version 460

// The textured material of the CPU reference renderer: one fragment of a
// draw whose binding is resolved, as Rasterizer::fill in src/render.cpp
// computes it. The reference's integer rounding is kept where the reference
// has it, so a backend has a stated target; docs/SHADER_PIPELINE.md lists
// where this still differs from the reference frame.
//
// The alpha test and the blend are not in this stage. Both are
// fixed-function state of the variant the program is drawn with (the
// manifest's variant records); this stage writes the source alpha that the
// test compares and the blend weighs.
//
// Interface: shaders/car_textured.manifest and track_textured.manifest.

layout (location = 0) in vec3 inWorld;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inTexcoord;
layout (location = 3) in vec3 inHeadlight;

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0) uniform Frame {
    vec4 eye;
    vec4 right;
    vec4 up;
    vec4 forward;
    vec4 projection;
    vec4 light;
    uvec4 options;
} frame;

// Per draw. fallback.frag.glsl declares the same block, so one layout is
// bound for both programs.
layout (std140, binding = 2) uniform Material {
    // rgb: fallback_color bytes; a: 2 for an unlit particle draw.
    vec4 fallback;
    // x: alpha_min, y: alpha_range, z: diffuse gate, w: lamp mode 0..3.
    vec4 alpha;
    // rgb: EnvmapMin and EnvmapRange.
    vec4 environment_min;
    vec4 environment_range;
    // rgb: the constant environment colour bytes (0..255); a: EnvmapPower.
    vec4 environment_color;
    vec4 surface_diffuse_min;
    vec4 surface_diffuse_range;
    vec4 surface_specular_min;
    vec4 surface_specular_range;
    vec4 surface_environment_min;
    vec4 surface_environment_range;
    vec4 surface_gates;
    vec4 uv_animation;
} material;

layout (binding = 0) uniform sampler2D baseTexture;
layout (binding = 1) uniform sampler2D reflectionTexture;

// Renderer-only marker fallback.w=3 selects a validated main-view FE glow.
// The executable parity test compiles these actual scalar GLSL functions.
uint frontend_glow_channel(uint texel, float vertex_byte)
{
    uint vertex = uint(clamp(floor(vertex_byte + 0.5), 0.0, 255.0));
    return (texel * vertex + 127u) / 255u;
}
uint frontend_glow_alpha(uint texel, float alpha_scale)
{
    return uint(clamp(float(texel) * alpha_scale + 0.5, 0.0, 255.0));
}

// Renderer-only markers 4/5 remove zero environment and angle-alpha terms.
// Marker 5 keeps the original geometric flat shade; 4 was already unlit.
uint race_effect_channel(uint texel, float vertex_byte, bool modulate, uint shade)
{
    uint vertex = uint(clamp(floor(vertex_byte + 0.5), 0.0, 255.0));
    uint color = modulate ? (texel * vertex + 127u) / 255u : texel;
    return min(255u, (color * shade + 128u) >> 8u);
}
uint race_effect_alpha(uint texel, float alpha_scale)
{
    return uint(clamp(float(texel) * alpha_scale + 0.5, 0.0, 255.0));
}

// Native world.efx base term; interpolation stays continuous until output.
float ordinary_world_channel(float texel, float vertex)
{
    return texel * vertex * 2.0;
}

layout (binding = 2) uniform sampler2D headlightTexture;
vec3 projectLamp() {
    float depth=inHeadlight.z;
    float x=(.5+.005*depth)*64.0-.5;
    // Linear interpolation of the original 64-texel falloff mask.
    // The three slopes meet at texels 33, 48 and 62.
    float mask=clamp(min(min(x-32.0,1.0-(x-48.0)*(16.0/255.0)),
                         (63.0-x)*(31.0/255.0)),0.0,1.0);
    if(mask<=0.0)return vec3(0);
    vec2 uv=.5+inHeadlight.xy/depth;
    if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))))return vec3(0);
    return texture(headlightTexture,uv).rgb*mask*vec3(.8,.8,.9);
}
vec3 projectedHeadlight() {
    if((frame.options.z & 16u)==0u)return vec3(0);
    vec3 color=projectLamp();
    return color;
}

// Original world texture transform. Keep negative fractional offsets signed.
float animatedWorldUvAxis(float uv, float rate, float period, float mode, float seconds)
{
    if (mode == 0.0) return uv;
    if (mode == 3.0) return uv + rate;
    precise float phase = seconds;
    if (mode == 2.0) {
        if (period == 0.0) return uv;
        phase = trunc(phase / period);
    }
    precise float value = phase * rate;
    return uv + (value - trunc(value));
}
// End native UV scalar
vec2 animatedWorldUv(vec2 uv)
{
    return vec2(animatedWorldUvAxis(uv.x, material.uv_animation.x,
        material.uv_animation.z, material.uv_animation.w, frame.light.w),
        animatedWorldUvAxis(uv.y, material.uv_animation.y,
        material.uv_animation.z, material.uv_animation.w, frame.light.w));
}

vec2 worldUv() {
    vec2 uv=animatedWorldUv(inTexcoord);
    if (frame.options.x != 0u) uv.y=1.0-uv.y;
    return uv;
}

// Native worldreflect receiver, effect selector 0x40. Diffuse and vertex
// alpha mask the reflection, and the stored vertex color is BGRA.
void shadeWorldReflect() {
    vec4 dc=texture(baseTexture,worldUv());
    vec3 reflected=vec3(0.0);
    if((frame.options.z & 2u)!=0u) {
        vec3 delta=inWorld-frame.eye.xyz;
        vec2 screen_uv=vec2(0.5)+vec2(dot(frame.right.xyz,delta)*frame.projection.x,
            -dot(frame.up.xyz,delta)*frame.projection.y)*(0.5/dot(frame.forward.xyz,delta));
        reflected=texture(reflectionTexture,screen_uv).rgb;
    }
    outColor=vec4(dc.rgb*(inColor.bgr+projectedHeadlight())*2.0+inColor.a*dc.a*reflected,1.0);
}

void shadeWorldDiffuse(vec4 sampled,uint texel_alpha) {
    outColor=vec4(ordinary_world_channel(sampled.r, inColor.b),
                  ordinary_world_channel(sampled.g, inColor.g),
                  ordinary_world_channel(sampled.b, inColor.r),
                  float(race_effect_alpha(texel_alpha, material.alpha.x)) / 255.0);
    outColor.rgb+=sampled.rgb*projectedHeadlight()*2.0;
}

// The Switch build selects this entry point for static world materials.
void raceWorldMain() {
    if(material.fallback.w==7.0) {
        shadeWorldReflect();
        return;
    }
    vec4 sampled=texture(baseTexture,worldUv());
    shadeWorldDiffuse(sampled,uint(sampled.a*255.0+0.5));
}
void main()
{
    // FE car flares use original world.efx Modulate2x in all four lanes.
    // FE neon uses the generic ground path; racing neon uses the projected
    // shadow shader. Both consume the same native packed RGBA controller.
    if(material.alpha.w == 7.0) {
        vec2 uv=inTexcoord;if(frame.options.x!=0u)uv.y=1.0-uv.y;
        vec4 texel=texture(baseTexture,uv);
        uint rgb=uint(material.alpha.z);
        vec3 tint=vec3(rgb&255u,(rgb>>8u)&255u,(rgb>>16u)&255u)/255.0;
        outColor=vec4(texel.rgb*tint*2.0,texel.a*material.alpha.x);
        return;
    }
    if(material.alpha.w == 8.0) {
        vec2 uv=inTexcoord;if(frame.options.x!=0u)uv.y=1.0-uv.y;
        vec4 texel=texture(baseTexture,uv);
        outColor=vec4(texel.rgb*(material.environment_color.rgb/255.0)*2.0,
                      texel.a*material.alpha.x*2.0);
        return;
    }
    // Native skid ribbon: texture with per-endpoint opacity; no lamp projection.
    if(material.fallback.w == 9.0) {
        vec2 uv=inTexcoord;if(frame.options.x!=0u)uv.y=1.0-uv.y;
        vec4 texel=texture(baseTexture,uv);
        outColor=vec4(texel.rgb*inColor.rgb*2.0,texel.a*inColor.a*material.alpha.x*2.0);
        return;
    }
    if ((frame.options.z & 1024u) != 0u) {
        vec2 uv = inTexcoord / inWorld.x;
        if (inWorld.y > 0.5) {
            vec2 local = (uv * vec2(1024.0, 512.0) - vec2(513.0, 1.0)) / 256.0;
            uv = (vec2(513.0, 1.0) + fract(local) * 256.0) / vec2(1024.0, 512.0);
        }
        outColor = texture(baseTexture, uv) * inColor;
        return;
    }
    // Original world.efx reflection-source material, with BGRA declaration.
    if (material.fallback.w == 8.0) {
        vec2 source_uv = animatedWorldUv(inTexcoord);
        if (frame.options.x != 0u) source_uv.y = 1.0 - source_uv.y;
        outColor = texture(baseTexture, source_uv) * vec4(inColor.bgr, inColor.a) * 2.0;
        return;
    }
    if (material.fallback.w == 7.0) {
        shadeWorldReflect();
        return;
    }
    vec2 uv=worldUv();
    // The nearest texel, wrapped; the manifest's sampler state is what makes
    // this wrap_texel. Held as bytes, as the reference holds it.
    // Original FE worldreflect.efx: alpha is1 after the pixel equation.
    // Retain original vertex alpha as mask, not doubled world alpha.
    if((frame.options.z & 8u)!=0u && material.environment_min.w>0.5) {
        vec4 diffuse=texture(baseTexture,uv);
        vec3 delta=inWorld-frame.eye.xyz;
        vec2 reflected_uv=vec2(0.5)+vec2(dot(frame.right.xyz,delta)*frame.projection.x,
            -dot(frame.up.xyz,delta)*frame.projection.y)*(0.5/dot(frame.forward.xyz,delta));
        vec3 reflection=texture(reflectionTexture,reflected_uv).rgb;
        outColor=vec4(diffuse.rgb*inColor.rgb*2.0+inColor.a*diffuse.a*reflection,1.0);
        return;
    }
    vec4 sampled = texture(baseTexture, uv);
    // Same original sprite material in both backend fragment variants.
    if (material.alpha.w == 5.0 || material.alpha.w == 6.0) {
        outColor=vec4(sampled.rgb*(material.environment_color.rgb/255.0)*2.0,
                      sampled.a*material.alpha.x);
        return;
    }
    uvec4 texel = uvec4(sampled * 255.0 + 0.5);
    if (material.fallback.w == 6.0) {
        shadeWorldDiffuse(sampled,texel.a);
        return;
    }
    if (material.fallback.w == 3.0) {
        vec3 vertex_byte = material.environment_range.w > 1.5 ?
            material.environment_color.rgb : inColor.rgb * 255.0;
        outColor = vec4(vec3(frontend_glow_channel(texel.r, vertex_byte.r),
                            frontend_glow_channel(texel.g, vertex_byte.g),
                            frontend_glow_channel(texel.b, vertex_byte.b)) / 255.0,
                        float(frontend_glow_alpha(texel.a, material.alpha.x)) / 255.0);
        return;
    }
    // This shader is selected only when the frame alpha cutoff is zero and
    // the material alpha test is off, so it has no fragment discard.

    bool race_fast = material.fallback.w == 4.0 || material.fallback.w == 5.0;
    if (race_fast && (material.fallback.w == 4.0 || (frame.options.z & 1u) == 0u)) {
        uint shade = 256u;
        bool modulate = frame.options.y != 0u || material.environment_range.w > 0.5;
        vec3 vertex_byte = material.environment_range.w > 1.5 ?
            material.environment_color.rgb : inColor.rgb * 255.0;
        outColor = vec4(vec3(race_effect_channel(texel.r, vertex_byte.r, modulate, shade),
                            race_effect_channel(texel.g, vertex_byte.g, modulate, shade),
                            race_effect_channel(texel.b, vertex_byte.b, modulate, shade)) / 255.0,
                        float(race_effect_alpha(texel.a, material.alpha.x)) / 255.0);
        return;
    }

    // The eye-facing geometric normal of the triangle, shared by the flat
    // shade and by vdotn. The reference takes it from the three placed
    // vertices; a fragment stage takes it from the screen derivatives of the
    // same world position.
    vec3 to_eye = frame.eye.xyz - inWorld;
    vec3 normal = cross(dFdx(inWorld), dFdy(inWorld));
    bool has_normal = dot(normal, normal) > 0.0;
    if (has_normal) {
        normal = normalize(normal);
        if (dot(normal, to_eye) < 0.0) {
            normal = -normal;
        }
    }

    uint shade = 256u;
    if ((frame.options.z & 1u) != 0u && has_normal &&
        (material.fallback.w < 1.5 || material.fallback.w == 5.0) && material.environment_range.w < 0.5) {
        float k = 0.35 + 0.65 * max(0.0, dot(normal, frame.light.xyz));
        shade = uint(floor(clamp(k, 0.0, 1.0) * 256.0 + 0.5));
    }

    if (race_fast) {
        bool modulate = frame.options.y != 0u || material.environment_range.w > 0.5;
        vec3 vertex_byte = material.environment_range.w > 1.5 ?
            material.environment_color.rgb : inColor.rgb * 255.0;
        outColor = vec4(vec3(race_effect_channel(texel.r, vertex_byte.r, modulate, shade),
                            race_effect_channel(texel.g, vertex_byte.g, modulate, shade),
                            race_effect_channel(texel.b, vertex_byte.b, modulate, shade)) / 255.0,
                        float(race_effect_alpha(texel.a, material.alpha.x)) / 255.0);
        return;
    }

    float vdotn = 0.01;
    if (has_normal && dot(to_eye, to_eye) > 0.0) {
        vdotn = max(0.01, dot(normalize(to_eye), normal));
    }
    // diffuse_scale.w. The off state and a retail draw with the default
    // coefficients hold 1 and 0; a closed diffuse gate holds 0 and 0.
    float alpha_scale = material.alpha.x + vdotn * material.alpha.y;
    uint source_alpha =
        uint(clamp(float(texel.a) * alpha_scale + 0.5, 0.0, 255.0));

    uvec3 color = texel.rgb;
    if (frame.options.y != 0u || material.environment_range.w > 0.5) {
        uvec3 vertex = uvec3(clamp(floor(inColor.rgb * 255.0 + 0.5), 0.0, 255.0));
        if (material.environment_range.w > 1.5)
            vertex = uvec3(clamp(floor(material.environment_color.rgb + 0.5), 0.0, 255.0));
        color = (color * vertex + 127u) / 255u;
    }
    uvec3 value = min(uvec3(255u), (color * shade + 128u) >> 8u);
    if (material.alpha.z != 0.0) {
        value = uvec3(0u);
    }
    // The car.efx environment identity with a constant colour in place of
    // the cube sample, times the texel alpha. All-zero unless the
    // environment gate keeps the term.
    vec3 environment = 4.0 * alpha_scale * material.environment_color.rgb *
        (material.environment_min.rgb +
         pow(vdotn, material.environment_color.a) *
             material.environment_range.rgb);
    vec3 sum = vec3(value) + floor(environment * float(texel.a) / 255.0 + 0.5);
    value = uvec3(min(sum, vec3(255.0)));
    uint lamp_mode = uint(material.alpha.w + 0.5);
    uvec3 lamp = lamp_mode == 1u ? uvec3(120u, 120u, 110u) :
                 lamp_mode == 2u ? uvec3(52u, 4u, 2u) :
                 lamp_mode == 3u ? uvec3(192u, 28u, 18u) : uvec3(0u);
    value += ((uvec3(255u) - value) * lamp + 127u) / 255u;

    if ((frame.options.z & 2u) != 0u && material.environment_min.w > 0.5) {
        vec3 eye_delta = inWorld - frame.eye.xyz;
        vec2 screen_uv = vec2(0.5) + vec2(dot(frame.right.xyz, eye_delta) * frame.projection.x,
            -dot(frame.up.xyz, eye_delta) * frame.projection.y) * (0.5 / dot(frame.forward.xyz, eye_delta));
        vec3 reflected = texture(reflectionTexture, screen_uv).rgb * 255.0;
        float wet = clamp(inColor.a, 0.0, 1.0) * float(texel.a) / 255.0;
        value = min(uvec3(255u), value + uvec3(floor(reflected * wet + 0.5)));
    }
    outColor = vec4(vec3(value) / 255.0, float(source_alpha) / 255.0);
}

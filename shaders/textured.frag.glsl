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
    // x: alpha_min, y: alpha_range, z: diffuse gate, w: lamp mode 0..3
    // or 4 for the car ground shadow world-effect branch.
    vec4 alpha;
    // rgb: EnvmapMin and EnvmapRange.
    vec4 environment_min;
    vec4 environment_range;
    // rgb: the constant environment colour bytes (0..255); a: EnvmapPower.
    vec4 environment_color;
} material;

layout (binding = 0) uniform sampler2D baseTexture;
layout (binding = 1) uniform sampler2D reflectionTexture;

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

void main()
{
    vec2 uv = inTexcoord;
    if (frame.options.x != 0u) {
        uv.y = 1.0 - uv.y;
    }
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
    // The manifest selects linear/repeat for the car and nearest/repeat for
    // the track. The reference holds the resulting sample as bytes.
    uvec4 texel = uvec4(texture(baseTexture, uv) * 255.0 + 0.5);
    // RenderOptions::alpha_cutoff, a diagnostic on the texel alpha.
    if (texel.a < frame.options.w) {
        discard;
    }

    // Retail world.efx:61 modulates the ground shadow by 0x80 vertex RGB
    // and two. Its runtime alpha comes from car height and alignment; the
    // shadow draw disables depth writes and uses source-over blending.
    if (material.alpha.w == 4.0) {
        outColor = vec4(vec3(texel.rgb) / 255.0,
                        clamp(float(texel.a) * material.alpha.x / 255.0, 0.0, 1.0));
        return;
    }

    if (material.fallback.w == 4.0 || material.fallback.w == 5.0) {
        uint shade = 256u;
        if (material.fallback.w == 5.0 && (frame.options.z & 1u) != 0u) {
            vec3 to_eye = frame.eye.xyz - inWorld;
            vec3 normal = cross(dFdx(inWorld), dFdy(inWorld));
            bool has_normal = dot(normal, normal) > 0.0;
            if (has_normal) {
                normal = normalize(normal);
                if (dot(normal, to_eye) < 0.0) {
                    normal = -normal;
                }
            }
            if (has_normal) {
                float k = 0.35 + 0.65 * max(0.0, dot(normal, frame.light.xyz));
                shade = uint(floor(clamp(k, 0.0, 1.0) * 256.0 + 0.5));
            }
        }
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
        material.fallback.w < 1.5 && material.environment_range.w < 0.5) {
        float k = 0.35 + 0.65 * max(0.0, dot(normal, frame.light.xyz));
        shade = uint(floor(clamp(k, 0.0, 1.0) * 256.0 + 0.5));
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

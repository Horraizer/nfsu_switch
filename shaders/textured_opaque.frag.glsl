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
    // x: alpha_min, y: alpha_range, z: diffuse gate, w: lamp mode 0..3.
    vec4 alpha;
    // rgb: EnvmapMin and EnvmapRange.
    vec4 environment_min;
    vec4 environment_range;
    // rgb: the constant environment colour bytes (0..255); a: EnvmapPower.
    vec4 environment_color;
} material;

layout (binding = 0) uniform sampler2D baseTexture;
layout (binding = 1) uniform sampler2D reflectionTexture;

void main()
{
    vec2 uv = inTexcoord;
    if (frame.options.x != 0u) {
        uv.y = 1.0 - uv.y;
    }
    // The nearest texel, wrapped; the manifest's sampler state is what makes
    // this wrap_texel. Held as bytes, as the reference holds it.
    uvec4 texel = uvec4(texture(baseTexture, uv) * 255.0 + 0.5);
    // This shader is selected only when the frame alpha cutoff is zero and
    // the material alpha test is off, so it has no fragment discard.

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

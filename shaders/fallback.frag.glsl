#version 460

// The fallback material of the CPU reference renderer: one fragment of a
// draw whose binding is unresolved, unusable or an invalid slot, as
// Rasterizer::fill in src/render.cpp computes it. Such a draw has no bound
// texture-info record and therefore no retail state: it is opaque and
// unblended in both material states, and nothing here reads an alpha.
//
// Interface: shaders/car_fallback.manifest and track_fallback.manifest.

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

// The same block textured.frag.glsl declares; this stage reads fallback only.
layout (std140, binding = 2) uniform Material {
    vec4 fallback;
    vec4 alpha;
    vec4 environment_min;
    vec4 environment_range;
    vec4 environment_color;
} material;

// checker_odd: the parity of floor(t * 8).
bool checker_odd(float t)
{
    return (int(floor(t * 8.0)) & 1) != 0;
}

void main()
{
    vec2 uv = inTexcoord;
    if (frame.options.x != 0u) {
        uv.y = 1.0 - uv.y;
    }
    uvec3 color = uvec3(material.fallback.rgb + 0.5);
    if (material.fallback.a != 0.0 && checker_odd(uv.x) != checker_odd(uv.y)) {
        color = color * 3u / 4u;
    }

    // The same eye-facing geometric normal textured.frag.glsl derives.
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
    if ((frame.options.z & 1u) != 0u && has_normal) {
        float k = 0.35 + 0.65 * max(0.0, dot(normal, frame.light.xyz));
        shade = uint(floor(clamp(k, 0.0, 1.0) * 256.0 + 0.5));
    }

    if (frame.options.y != 0u) {
        uvec3 vertex = uvec3(clamp(floor(inColor.rgb * 255.0 + 0.5), 0.0, 255.0));
        color = (color * vertex + 127u) / 255u;
    }
    uvec3 value = min(uvec3(255u), (color * shade + 128u) >> 8u);
    uint lamp_mode = uint(material.alpha.w + 0.5);
    uvec3 lamp = lamp_mode == 1u ? uvec3(120u, 120u, 110u) :
                 lamp_mode == 2u ? uvec3(52u, 4u, 2u) :
                 lamp_mode == 3u ? uvec3(192u, 28u, 18u) : uvec3(0u);
    value += ((uvec3(255u) - value) * lamp + 127u) / 255u;

    // Fallback variants write RGB only and never blend. Carry alpha through
    // the shared interface; it has no effect on their framebuffer contents.
    outColor = vec4(vec3(value) / 255.0, inColor.a);
}

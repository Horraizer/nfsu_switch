#version 460

// Recovered world.efx modulation: texture * vertex RGBA * 2.
// Byte rounding and nearest sampling follow the CPU reference approximation.
// The manifest selects each texture's alpha-test and blend state.

// The sky writes no depth, so testing depth before shading changes no
// pixel; it keeps the diagnostic discard below from forcing late depth
// tests, so the opaque world rejects every covered sky fragment (issue 227).
layout (early_fragment_tests) in;

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
    // rgb: the fallback_color bytes (0..255); a: 1 when the checker is on.
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

void main()
{
    vec2 uv = inTexcoord;
    if (frame.options.x != 0u) uv.y = 1.0 - uv.y;
    uvec4 texel = uvec4(texture(baseTexture, uv) * 255.0 + 0.5);
    // The diagnostic cutoff checks raw texel alpha in the CPU path too;
    // the material alpha test uses the doubled source alpha below.
    if (texel.a < frame.options.w) discard;
    uvec4 vertex = uvec4(clamp(floor(inColor * 255.0 + 0.5), 0.0, 255.0));
    uvec4 value = min(uvec4(255u), (texel * vertex * 2u + 127u) / 255u);
    outColor = vec4(value) / 255.0;
}

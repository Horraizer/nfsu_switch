#version 460
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inTexcoord;
layout (location = 0) out vec4 outColor;
layout (std140, binding = 0) uniform Frame {
    vec4 eye;
    vec4 right;
    vec4 up;
    vec4 forward;
    // focal_x, focal_y, near_plane, far_plane
    vec4 projection;
    // The normalized flat-shading light direction.
    vec4 light;
    // flip_v, vertex_color, bits (flat=1, wet=2, reflected source=4), alpha_cutoff
    uvec4 options;
} frame;

layout (std140, binding = 2) uniform Material {
    vec4 fallback;
    vec4 alpha;
    vec4 environment_min;
    vec4 environment_range;
    vec4 environment_color;
} material;

layout (binding = 0) uniform sampler2D baseTexture;
void main() {
    uint strip = uint(clamp(floor(inTexcoord.y * 3.0), 0.0, 2.0));
    if ((uint(material.alpha.y) & (1u << strip)) == 0u) discard;
    vec2 uv = inTexcoord;
    if (frame.options.x != 0u) uv.y = 1.0 - uv.y;
    vec4 texel = texture(baseTexture, uv);
    if (material.alpha.w == 7.0) {
        uint rgb = uint(material.alpha.z);
        vec3 tint = vec3(rgb & 255u, (rgb >> 8u) & 255u, (rgb >> 16u) & 255u) / 255.0;
        outColor = vec4(texel.rgb * tint * 2.0, texel.a * material.alpha.x);
    } else outColor = vec4(texel.rgb * inColor.rgb * 2.0, texel.a * material.alpha.x);
}

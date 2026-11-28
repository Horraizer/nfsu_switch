#version 460

// Selected only for depth-read-only, RGB-only src-alpha blended fallback9.
// Keep depth rejection before texture work even though the stage can discard.
layout(early_fragment_tests) in;
layout(location=1) in vec4 inColor;
layout(location=2) in vec2 inTexcoord;
layout(location=0) out vec4 outColor;

layout(std140,binding=0) uniform Frame {
    vec4 eye,right,up,forward,projection,light;
    uvec4 options;
} frame;
layout(std140,binding=2) uniform Material {
    vec4 fallback,alpha,environment_min,environment_range,environment_color;
    vec4 surface_diffuse_min,surface_diffuse_range,surface_specular_min,surface_specular_range;
    vec4 surface_environment_min,surface_environment_range,surface_gates,uv_animation;
} material;
layout(binding=0) uniform sampler2D baseTexture;

// BEGIN_PARTICLE_ZERO_EQUATION
bool particle_sample_is_zero(float sampled_alpha) {
    return sampled_alpha==0.0;
}
// END_PARTICLE_ZERO_EQUATION

void main() {
    vec2 uv=inTexcoord;
    if(frame.options.x!=0u)uv.y=1.0-uv.y;
    vec4 texel=texture(baseTexture,uv);
    // Test the actual filtered sample. Positive bilinear/anisotropic edge
    // contributions remain, however small; no alpha threshold is introduced.
    if(particle_sample_is_zero(texel.a))discard;
    outColor=vec4(texel.rgb*inColor.rgb*2.0,texel.a*inColor.a*material.alpha.x*2.0);
}

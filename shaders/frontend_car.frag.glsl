#version 460
// Native FE pixel product: separately interpolated alpha and specular.
layout (location = 2) in vec2 inTexcoord;
layout (location = 3) in vec3 inDiffuse;
layout (location = 4) in vec3 inAdditive;
layout (location = 5) in float inAlphaScale;
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

layout (std140, binding = 2) uniform Material {
    vec4 fallback;
    vec4 alpha;
    vec4 environment_min;
    vec4 environment_range;
    vec4 environment_color;
    vec4 surface_diffuse_min;
    vec4 surface_diffuse_range;
    vec4 surface_specular_min;
    vec4 surface_specular_range;
    vec4 surface_environment_min;
    vec4 surface_environment_range;
    vec4 surface_gates;
} material;
layout (binding = 0) uniform sampler2D baseTexture;
void main() {
    vec2 uv=inTexcoord;
    if (frame.options.x!=0u) uv.y=1.0-uv.y;
    uvec4 texel=uvec4(texture(baseTexture,uv)*255.0+0.5);
    if (texel.a<frame.options.w) discard;
    uvec3 color=texel.rgb;
    uvec3 value=uvec3(clamp(floor(vec3(color)*inDiffuse+float(texel.a)*inAlphaScale*inAdditive+0.5),0.0,255.0));
    uint lamp_mode=uint(material.alpha.w+0.5);
    uvec3 lamp=lamp_mode==1u ? uvec3(120,120,110) : lamp_mode==2u ? uvec3(52,4,2) :
        lamp_mode==3u ? uvec3(192,28,18) : uvec3(0);
    value+=((uvec3(255)-value)*lamp+127u)/255u;
    uint alpha=uint(clamp(float(texel.a)*inAlphaScale+0.5,0.0,255.0));
    outColor=vec4(vec3(value)/255.0,float(alpha)/255.0);
}

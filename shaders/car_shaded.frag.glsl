#version 460
// Static-environment approximation; see docs/CAR_SHADING.md.
layout (location = 6) in vec3 inCubeDirection;
layout (location = 7) in float inEnvironmentPower;
layout (location = 1) in vec3 inColor;
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
layout (binding = 1) uniform sampler2D environmentTexture;
vec3 cubeSample(vec3 d) {
    vec3 a=abs(d);float major;vec2 uv;int face;
    if(a.x>=a.y && a.x>=a.z) {major=a.x;face=d.x>=0.0?0:1;uv=vec2(d.x>=0.0?-d.z:d.z,-d.y);}
    else if(a.y>=a.z) {major=a.y;face=d.y>=0.0?2:3;uv=vec2(d.x,d.y>=0.0?d.z:-d.z);}
    else {major=a.z;face=d.z>=0.0?4:5;uv=vec2(d.z>=0.0?d.x:-d.x,-d.y);}
    if(major==0.0)return vec3(0);
    uv=clamp(uv/major*.5+.5,vec2(.5/128.0),vec2(127.5/128.0));
    // Fetch all four taps by bounded face-local integer coordinates. Even
    // atlas-UV rounding cannot sample a neighboring tile at a face edge.
    vec2 pixel=uv*128.0-.5;ivec2 a0=ivec2(floor(pixel));
    ivec2 a1=min(a0+ivec2(1),ivec2(127));vec2 f=fract(pixel);
    ivec2 origin=ivec2((face%3)*128,(face/3)*128);
    vec3 p00=texelFetch(environmentTexture,origin+a0,0).rgb;
    vec3 p10=texelFetch(environmentTexture,origin+ivec2(a1.x,a0.y),0).rgb;
    vec3 p01=texelFetch(environmentTexture,origin+ivec2(a0.x,a1.y),0).rgb;
    vec3 p11=texelFetch(environmentTexture,origin+a1,0).rgb;
    return mix(mix(p00,p10,f.x),mix(p01,p11,f.x),f.y);
}
void main() {
    vec2 uv=inTexcoord;
    if (frame.options.x!=0u) uv.y=1.0-uv.y;
    uvec4 texel=uvec4(texture(baseTexture,uv)*255.0+0.5);
    if (texel.a<frame.options.w) discard;
    uvec3 color=texel.rgb;
    if (frame.options.y!=0u) {
        uvec3 vertex=uvec3(clamp(floor(inColor*255.0+0.5),0.0,255.0));
        color=(color*vertex+127u)/255u;
    }
    vec3 captured=vec3(0);
    if((frame.options.z & 32u)!=0u)captured=4.0*material.surface_gates.z*cubeSample(inCubeDirection)*
        (material.surface_environment_min.rgb+inEnvironmentPower*material.surface_environment_range.rgb)*inAlphaScale;
    uvec3 value=uvec3(clamp(floor(vec3(color)*inDiffuse+float(texel.a)*(inAdditive+captured)+0.5),0.0,255.0));
    uint lamp_mode=uint(material.alpha.w+0.5);
    uvec3 lamp=lamp_mode==1u ? uvec3(120,120,110) : lamp_mode==2u ? uvec3(52,4,2) :
        lamp_mode==3u ? uvec3(192,28,18) : uvec3(0);
    value+=((uvec3(255)-value)*lamp+127u)/255u;
    uint alpha=uint(clamp(float(texel.a)*inAlphaScale+0.5,0.0,255.0));
    outColor=vec4(vec3(value)/255.0,float(alpha)/255.0);
}

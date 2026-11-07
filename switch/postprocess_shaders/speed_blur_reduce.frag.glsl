#version 460
layout(binding=0) uniform sampler2D sceneTexture;
layout(std140,binding=0) uniform Blur {
    vec4 params; // inverse output dimensions, byte alpha / 255, pass mode
    vec4 offsets;
};
layout(location=0) out vec4 outColor;
void main() {
    // Original 7024B8: four equal half-source-texel taps.
    // D3D9 fullscreen vertices are at clip +/-1 with no half-pixel inset.
    // Native pixel (0,0) therefore samples UV (0,0), not half a texel in.
    vec2 uv=(gl_FragCoord.xy-vec2(0.5))*params.xy;
    vec2 halfTexel=0.5/vec2(textureSize(sceneTexture,0));
    vec4 rgba=(textureLod(sceneTexture,uv-halfTexel,0)+
              textureLod(sceneTexture,uv+vec2(halfTexel.x,-halfTexel.y),0)+
              textureLod(sceneTexture,uv+vec2(-halfTexel.x,halfTexel.y),0)+
              textureLod(sceneTexture,uv+halfTexel,0))*0.25;
    outColor=rgba;
}

#version 460
layout(binding=0) uniform sampler2D sceneTexture;
layout(std140,binding=0) uniform Blur {
    vec4 params; // inverse output dimensions, byte alpha / 255, pass mode
    vec4 offsets;
};
layout(location=0) out vec4 outColor;
void main() {
    // Original 40D090: corner-interpolated offsets and four equal taps.
    // Keep this pass separate from reduction/copy: hardware readback showed
    // the combined shader losing all row variation in this branch.
    // D3D9 fullscreen vertices are at clip +/-1 with no half-pixel inset.
    // Native pixel (0,0) therefore samples UV (0,0), not half a texel in.
    vec2 uv=(gl_FragCoord.xy-vec2(0.5))*params.xy;
    // Captured vector interpolation forms lost the vertical corner addend.
    // Keep both contributions scalar and precise, without FMA contraction.
    precise float left=(1.0-uv.x)*offsets.x;
    precise float right=uv.x*offsets.z;
    precise float top=(1.0-uv.y)*offsets.y;
    precise float bottom=uv.y*offsets.w;
    precise float dx=left+right;
    precise float dy=top+bottom;
    vec2 delta=vec2(dx,dy);
    vec4 rgba=(textureLod(sceneTexture,uv,0)+
              textureLod(sceneTexture,uv-delta,0)+
              textureLod(sceneTexture,uv-2.0*delta,0)+
              textureLod(sceneTexture,uv-3.0*delta,0))*0.25;
    outColor=rgba;
}

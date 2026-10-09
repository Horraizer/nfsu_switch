#version 460
layout(binding=0) uniform sampler2D historyTexture;
layout(location=0) out vec4 outColor;
void main() {
    vec2 uv=gl_FragCoord.xy/vec2(textureSize(historyTexture,0));
    outColor=vec4(texture(historyTexture,uv).rgb,0.0);
}

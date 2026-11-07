#version 460
layout(binding=0) uniform sampler2D sceneTexture;
layout(std140,binding=0) uniform Blur {
    vec4 params; // inverse output dimensions, byte alpha / 255, pass mode
    vec4 offsets;
};
layout(location=0) out vec4 outColor;
void main() {
    // 40D6E0 uses constant-factor blending, not texture alpha as the factor.
    outColor=textureLod(sceneTexture,(gl_FragCoord.xy-vec2(0.5))*params.xy,0);
}

#version 460
layout(early_fragment_tests) in;
layout(location=1) flat in vec4 inColor;
layout(location=2) in vec2 inTexcoord;
layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D baseTexture;
void main() {
    vec4 texel=texture(baseTexture,inTexcoord);
    if(texel.a==0.0)discard;
    outColor=texel*inColor;
}

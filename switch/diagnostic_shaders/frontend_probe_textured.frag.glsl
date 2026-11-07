#version 460
// Texture-only stage: no derivatives, material UBO or reflection sampler.
layout(location=2) in vec2 inTexcoord;
layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D baseTexture;
void main() {outColor=texture(baseTexture,inTexcoord);}

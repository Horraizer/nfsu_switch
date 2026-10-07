#version 460
// Original FEng stores half-byte modulation; world.efx Modulate2x restores
// it. The overlay carries the restored, still-quantized values explicitly.
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inTexcoord;
layout (location = 0) out vec4 outColor;
layout (binding = 0) uniform sampler2D baseTexture;
void main() {
    outColor = texture(baseTexture,inTexcoord) * inColor;
}

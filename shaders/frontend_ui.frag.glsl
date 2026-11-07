#version 460
// Original FEng stores half-byte modulation; world.efx Modulate2x restores
// it. The overlay carries the restored, still-quantized values explicitly.
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inTexcoord;
layout (location = 3) flat in vec2 inFootprint;
layout (location = 0) out vec4 outColor;
layout (binding = 0) uniform sampler2D baseTexture;
void main() {
    vec2 uv = inTexcoord;
    if (inFootprint.x > 0.0) {
        vec2 size = vec2(textureSize(baseTexture,0));
        vec2 texel = uv*size - 0.5;
        vec2 weight = clamp((fract(texel)-0.5)/inFootprint+0.5,0.0,1.0);
        uv = (floor(texel)+weight+0.5)/size;
    }
    vec4 texelColor = texture(baseTexture,uv);
    if (inFootprint.x > 0.0)
        texelColor.a = clamp(1.25*(texelColor.a-0.5)+0.5,0.0,1.0);
    outColor = texelColor * inColor;
}

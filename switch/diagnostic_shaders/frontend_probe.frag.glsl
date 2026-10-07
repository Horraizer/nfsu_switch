#version 460
// Truly constant: no inputs, derivatives, uniforms, textures or materials.
layout(location=0) out vec4 outColor;
void main() {outColor=vec4(.25,.5,.75,1);}

#version 460
// Original 409BCA: D3DRS_BLENDFACTOR = 0x008296B4.
// ZERO source + BLENDFACTOR destination, once per rendered main view.
layout(location=0) out vec4 outColor;
void main() { outColor=vec4(130.0/255.0,150.0/255.0,180.0/255.0,0.0); }

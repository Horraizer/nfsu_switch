#version 460
// FXAA edge search and subpixel reconstruction, independently expressed from
// Timothy Lottes, NVIDIA FXAA white paper (2011), sections Implementation.
// https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf
// Input is display-referred RGBA8 UNORM; HUD is composited afterwards.
layout(binding=0) uniform sampler2D sceneTexture;
layout(location=0) out vec4 outColor;
float luma(vec3 rgb) { return dot(rgb,vec3(0.299,0.587,0.114)); }
vec3 sampleRgb(vec2 uv) { return textureLod(sceneTexture,uv,0.0).rgb; }
void main() {
    vec2 texel=1.0/vec2(textureSize(sceneTexture,0));
    vec2 uv=gl_FragCoord.xy*texel;
    vec3 center=sampleRgb(uv);
    float m=luma(center);
    float n=luma(sampleRgb(uv+vec2(0,-texel.y)));
    float s=luma(sampleRgb(uv+vec2(0, texel.y)));
    float w=luma(sampleRgb(uv+vec2(-texel.x,0)));
    float e=luma(sampleRgb(uv+vec2( texel.x,0)));
    float lo=min(m,min(min(n,s),min(w,e)));
    float hi=max(m,max(max(n,s),max(w,e)));
    float range=hi-lo;
    if(range<max(1.0/32.0,hi/8.0)) { outColor=vec4(center,1);return; }
    float nw=luma(sampleRgb(uv-texel));
    float ne=luma(sampleRgb(uv+vec2(texel.x,-texel.y)));
    float sw=luma(sampleRgb(uv+vec2(-texel.x,texel.y)));
    float se=luma(sampleRgb(uv+texel));
    float horizontal=abs(nw+sw-2*w)+2*abs(n+s-2*m)+abs(ne+se-2*e);
    float vertical=abs(nw+ne-2*n)+2*abs(w+e-2*m)+abs(sw+se-2*s);
    bool alongX=horizontal>=vertical;
    float negative=alongX?n:w,positive=alongX?s:e;
    float gn=abs(negative-m),gp=abs(positive-m);
    bool useNegative=gn>=gp;
    float pairMean=0.5*(m+(useNegative?negative:positive));
    float gradient=0.25*max(gn,gp);
    vec2 normal=alongX?vec2(0,texel.y):vec2(texel.x,0);
    if(useNegative)normal=-normal;
    vec2 tangent=alongX?vec2(texel.x,0):vec2(0,texel.y);
    vec2 edgeCenter=uv+0.5*normal;
    // Finite symmetric search. Unresolved ends do not invent an edge offset.
    float dn=0,dp=0,ln=0,lp=0;
    bool foundN=false,foundP=false;
    for(int i=1;i<=8;++i) {
        if(!foundN) { dn=float(i);ln=luma(sampleRgb(edgeCenter-tangent*dn))-pairMean;foundN=abs(ln)>=gradient; }
        if(!foundP) { dp=float(i);lp=luma(sampleRgb(edgeCenter+tangent*dp))-pairMean;foundP=abs(lp)>=gradient; }
        if(foundN && foundP)break;
    }
    bool nearerN=dn<dp;
    float edgeOffset=0;
    bool found=nearerN?foundN:foundP;
    if(found && (((nearerN?ln:lp)<0)!=((m-pairMean)<0)))
        edgeOffset=0.5-min(dn,dp)/(dn+dp);
    float average=(2*(n+s+w+e)+nw+ne+sw+se)/12.0;
    float subpixel=clamp(abs(average-m)/range,0.0,1.0);
    subpixel=subpixel*subpixel*(3.0-2.0*subpixel);
    subpixel=0.5*subpixel*subpixel;
    outColor=vec4(sampleRgb(uv+normal*max(edgeOffset,subpixel)),1);
}

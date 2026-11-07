#version 460
// No vertex buffers or uniforms: validate minimal shader/raster execution.
void main() {
    float x=gl_VertexID==0 ? -.5 : gl_VertexID==1 ? .5 : 0;
    float y=gl_VertexID==2 ? .5 : -.5;
    gl_Position=vec4(x,y,.5,1);
}

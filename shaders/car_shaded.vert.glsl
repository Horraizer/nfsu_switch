#version 460

// Opt-in car shading: smooth normals, GLOBALB material coefficients and a
// static analytical environment. This approximates the untraced harmonic
// lighting and live cube used by retail. Interface: car_shaded_textured.manifest.
// MeshVertex px, py, pz, in that memory order.
layout (location = 0) in vec3 inPosition;
// Vertex color bytes 0..2 as unorm. The reference multiplies by those three
// bytes only (RenderOptions::vertex_color).
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inTexcoord;

layout (location = 3) in vec3 inNormal;
layout (location = 6) out vec3 outCubeDirection;
layout (location = 7) out float outEnvironmentPower;
layout (location = 3) out vec3 outDiffuse;
layout (location = 4) out vec3 outAdditive;
layout (location = 5) out float outAlphaScale;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outTexcoord;

// The reference camera (CameraFrame) and the per-frame render options.
layout (std140, binding = 0) uniform Frame {
    vec4 eye;
    vec4 right;
    vec4 up;
    vec4 forward;
    // focal_x, focal_y, near_plane, far_plane
    vec4 projection;
    // The normalized flat-shading light direction.
    vec4 light;
    // flip_v, vertex_color, flat shading, alpha_cutoff
    uvec4 options;
} frame;

// The drawn copy's placement: row-major rows of the row-vector transform in
// the raw slot frame, row 3 being the translation. PreparedInstance::transform
// fills all four rows; TrackTransform fills rows 0..2 from `linear` and row 3
// from `translation`. A car profile with no instances draws with the
// identity, which lands every finite vertex where the null placement does.
layout (std140, binding = 1) uniform Instance {
    vec4 rows[4];
} instance;

layout (std140, binding = 2) uniform Material {
    vec4 fallback;
    vec4 alpha;
    vec4 environment_min;
    vec4 environment_range;
    vec4 environment_color;
    vec4 surface_diffuse_min;
    vec4 surface_diffuse_range;
    vec4 surface_specular_min;
    vec4 surface_specular_range;
    vec4 surface_environment_min;
    vec4 surface_environment_range;
    vec4 surface_gates;
} material;

float positivePower(float x,float p) { return p==0.0 ? 1.0 : pow(x,p); }

void main()
{
    // The raw slot order is (longitudinal, lateral, vertical) = (px, pz, py).
    vec3 slot = vec3(inPosition.x, inPosition.z, inPosition.y);
    vec3 moved = slot.x * instance.rows[0].xyz + slot.y * instance.rows[1].xyz +
                 slot.z * instance.rows[2].xyz + instance.rows[3].xyz;
    vec3 world = vec3(moved.x, moved.z, -moved.y);

    // project_point: a look-at basis and a D3D-style projection, so z is 0
    // at the near plane and w is the distance along forward.
    vec3 d = world - frame.eye.xyz;
    float vx = dot(frame.right.xyz, d);
    float vy = dot(frame.up.xyz, d);
    float vz = dot(frame.forward.xyz, d);
    float near_plane = frame.projection.z;
    float far_plane = frame.projection.w;
    gl_Position = vec4(vx * frame.projection.x, vy * frame.projection.y,
                       (vz - near_plane) * far_plane / (far_plane - near_plane),
                       vz);

    outColor = inColor;
    outTexcoord = inTexcoord;
    vec3 r0=instance.rows[0].xyz, r1=instance.rows[1].xyz, r2=instance.rows[2].xyz;
    vec3 a=cross(r1,r2), b=cross(r2,r0), c=cross(r0,r1);
    float det=dot(r0,a);
    vec3 n=vec3(inNormal.x,inNormal.y,-inNormal.z);
    if (abs(det)>1e-12) {
        vec3 m=(inNormal.x*a+inNormal.z*b+inNormal.y*c)/det;
        n=vec3(m.x,m.z,-m.y);
    }
    n=dot(n,n)>0.0 ? normalize(n) : vec3(0,1,0);
    vec3 view=frame.eye.xyz-world;
    view=dot(view,view)>0.0 ? normalize(view) : vec3(0,1,0);
    if (dot(n,view)<0.0) n=-n;
    float v=clamp(dot(n,view),0.01,1.0);
    vec3 refl=2.0*v*n-view;
    float diffuse=0.42+0.58*max(0.0,dot(n,frame.light.xyz));
    float spec=positivePower(max(0.0,dot(refl,frame.light.xyz)),material.surface_specular_min.w);
    float sv=positivePower(v,material.surface_specular_min.w);
    float ev=positivePower(v,material.surface_specular_range.w);
    float sky=clamp(refl.y*0.5+0.5,0.0,1.0);
    float horizon=max(0.0,1.0-abs(refl.y)*5.0);
    float band=max(0.0,1.0-abs(refl.x-0.35)*5.0)*horizon;
    vec3 environment=mix(vec3(0.055,0.045,0.035),vec3(0.20,0.27,0.38),sky)+0.18*horizon+0.25*band;
    bool captured=(frame.options.z & 32u)!=0u;
    outCubeDirection=vec3(0);outEnvironmentPower=0;
    if(captured) {
        vec3 rawEye=vec3(frame.eye.x,-frame.eye.z,frame.eye.y)-instance.rows[3].xyz;
        vec3 localEye=vec3(dot(r0,rawEye),dot(r1,rawEye),dot(r2,rawEye));
        localEye=dot(localEye,localEye)>0.0 ? normalize(localEye)*2.7799999713897705 : vec3(0);
        vec3 lv=localEye-slot;lv=dot(lv,lv)>0.0?normalize(lv):vec3(0);
        vec3 ln=vec3(inNormal.x,inNormal.z,inNormal.y);
        float nv=max(.01,dot(lv,ln));vec3 lr=2.0*nv*ln-lv;
        vec3 wr=lr.x*r0+lr.y*r1+lr.z*r2;wr=vec3(wr.x,wr.z,-wr.y);
        outCubeDirection=vec3(dot(frame.right.xyz,wr),-dot(frame.up.xyz,wr),dot(frame.forward.xyz,wr));
        outEnvironmentPower=positivePower(nv,material.surface_specular_range.w);
        environment=vec3(0);
    }
    float alpha=material.surface_gates.y*(material.surface_diffuse_min.w+v*material.surface_diffuse_range.w);
    outAlphaScale=alpha;
    outDiffuse=material.surface_gates.y*diffuse*(material.surface_diffuse_min.rgb+v*material.surface_diffuse_range.rgb);
    outAdditive=alpha*(material.surface_gates.y*spec*(material.surface_specular_min.rgb+sv*material.surface_specular_range.rgb)+
        4.0*material.surface_gates.z*environment*(material.surface_environment_min.rgb+ev*material.surface_environment_range.rgb));
}

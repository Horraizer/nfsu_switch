#version 460

// Native FE harmonic/specular equation. Startup input assumptions are
// explicit in frontend_startup_lighting; EnvMap uses the native logical faces.
// MeshVertex px, py, pz, in that memory order.
layout (location = 0) in vec3 inPosition;
// Vertex color bytes 0..2 as unorm. The reference multiplies by those three
// bytes only (RenderOptions::vertex_color).
layout (location = 2) in vec2 inTexcoord;

layout (location = 3) in vec3 inNormal;
layout (location = 3) out vec3 outDiffuse;
layout (location = 4) out vec3 outAdditive;
layout (location = 5) out float outAlphaScale;
layout (location = 6) out vec3 outCubeDirection;
layout (location = 7) out float outEnvironmentPower;
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

layout (std140, binding = 3) uniform FrontendLight {
    vec4 harmonic[10];
} frontend;
vec4 evaluateHarmonic(vec3 local) {
    // Native542400/541C90 supplies per-placement harmonic coefficients.
    // Equivalent contraction of the shared basis keeps rolling-wheel
    // lighting in its actual transformed normal/reflection frame.
    vec3 n=local.x*instance.rows[0].xyz+local.y*instance.rows[1].xyz+local.z*instance.rows[2].xyz;
    return frontend.harmonic[0]+frontend.harmonic[1]*n.x*n.x+
        frontend.harmonic[2]*n.y*n.y+frontend.harmonic[3]*n.z*n.z+
        frontend.harmonic[4]*n.x*n.y+frontend.harmonic[5]*n.z*n.x+
        frontend.harmonic[6]*n.y*n.z+frontend.harmonic[7]*n.x+
        frontend.harmonic[8]*n.y+frontend.harmonic[9]*n.z;
}
float positivePower(float x,float p) { return p==0.0 ? 1.0 : pow(x,p); }
void main() {
    vec3 slot=vec3(inPosition.x,inPosition.z,inPosition.y);
    vec3 moved=slot.x*instance.rows[0].xyz+slot.y*instance.rows[1].xyz+
        slot.z*instance.rows[2].xyz+instance.rows[3].xyz;
    vec3 world=vec3(moved.x,moved.z,-moved.y);
    vec3 d=world-frame.eye.xyz;
    float z=dot(frame.forward.xyz,d);
    gl_Position=vec4(dot(frame.right.xyz,d)*frame.projection.x,
        dot(frame.up.xyz,d)*frame.projection.y,
        (z-frame.projection.z)*frame.projection.w/(frame.projection.w-frame.projection.z),z);
    // Original car.efx works in raw local coordinates. No facing flip,
    // normal normalization, vertex-color modulation or upper vdotn clamp.
    vec3 n=vec3(inNormal.x,inNormal.z,inNormal.y);
    vec3 delta=material.environment_color.xyz-slot;
    vec3 view=dot(delta,delta)>0.0 ? normalize(delta) : vec3(0);
    float v=max(.01,dot(view,n));
    vec3 reflected=2.0*v*n-view;
    float sv=positivePower(v,material.surface_specular_min.w);
    vec4 h=evaluateHarmonic(reflected);
    float specLuma=positivePower(clamp(h.w,0.0,1.0),material.surface_specular_min.w);
    outDiffuse=material.surface_gates.y*evaluateHarmonic(n).rgb*
        (material.surface_diffuse_min.rgb+v*material.surface_diffuse_range.rgb);
    outAdditive=.5*material.surface_gates.y*h.rgb*specLuma*
        (material.surface_specular_min.rgb+sv*material.surface_specular_range.rgb);
    outAlphaScale=material.surface_gates.y*
        (material.surface_diffuse_min.w+v*material.surface_diffuse_range.w);
    outTexcoord=inTexcoord;
    vec3 rawReflected=reflected.x*instance.rows[0].xyz+reflected.y*instance.rows[1].xyz+reflected.z*instance.rows[2].xyz;
    vec3 worldReflected=vec3(rawReflected.x,rawReflected.z,-rawReflected.y);
    outCubeDirection=vec3(dot(frame.right.xyz,worldReflected),-dot(frame.up.xyz,worldReflected),dot(frame.forward.xyz,worldReflected));
    outEnvironmentPower=positivePower(v,material.surface_specular_range.w);
}

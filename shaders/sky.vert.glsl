#version 460

// World-placed SKYDOME, projected onto the background without depth writes.
// The placement is recovered; far-depth projection is an approximation.
// Interface: sky_textured.manifest. Evidence: docs/SKY_FOG.md.

// TrackVertex px, py, pz in memory order.
layout (location = 0) in vec3 inPosition;
// Vertex RGBA as unorm, including the blended cap alpha.
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inTexcoord;

layout (location = 1) out vec4 outColor;
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

    // Background depth; world placement stays fixed. See docs/SKY_FOG.md.
    gl_Position.z = gl_Position.w * 0.999999;
    outColor = inColor;
    outTexcoord = inTexcoord;
}

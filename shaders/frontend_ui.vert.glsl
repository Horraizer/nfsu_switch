#version 460


layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inTexcoord;

layout (location = 1) out vec4 outColor;
layout (location = 2) out vec2 outTexcoord;

layout (std140, binding = 0) uniform Frame {
    vec4 eye;
    vec4 right;
    vec4 up;
    vec4 forward;
    vec4 projection;
    vec4 light;
    uvec4 options;
} frame;

layout (std140, binding = 1) uniform Instance {
    vec4 rows[4];
} instance;

void main()
{
    vec3 slot = vec3(inPosition.x, inPosition.z, inPosition.y);
    vec3 moved = slot.x * instance.rows[0].xyz + slot.y * instance.rows[1].xyz +
                 slot.z * instance.rows[2].xyz + instance.rows[3].xyz;
    vec3 world = vec3(moved.x, moved.z, -moved.y);

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
}

# Prepared track GPU commands

The optimized race frame borrows an immutable command package from
`RaceGpuScene`. Each selected track draw carries a stable source group ID and
instance ID. The package exposes every track placement and validated group
definition, including groups absent from the first visible frame. The selected
order remains the cached reference order: track draws followed by car draws.

At renderer open, deko3d uploads one aligned placement per track instance and
one aligned material per track definition. It records one child command list
per definition with the shader, material, texture, pipeline, vertex and index
bindings and indexed draw. A frame binds its camera and selected placement,
then calls those child lists in selection order. Only the camera and the car's
placement/material values are copied to the two rotating frame slots. The car
path skips repeated fixed state when adjacent definitions have equal program,
texture and pipeline bytes; it always binds changing uniforms and mesh buffers.

The command lists have a separate command buffer and bounded backing memory.
The renderer does not clear that buffer between frames. Its shutdown waits for
the queue to become idle before list handles, command memory, static uniforms,
shaders or uploaded buffers are destroyed. The frame command buffer can clear
and reuse its own two fence protected slices independently.

The borrowed update path checks package identity, generation, frame shape,
capacity and each selected definition/placement identity. Immutable resources,
draw ranges and pipeline variants were validated during scene preparation and
renderer open. Owning offscreen commands continue to use full update
validation. `nfsu_race_run_tests` compares cached commands against the owning
reference recording and checks the static package mapping during the retail
replay. Switch build and CTest verify the deko3d command API. The coordinator's
console run is the source of image and frame time evidence.

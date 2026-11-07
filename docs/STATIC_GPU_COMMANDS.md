# Prepared track GPU commands

The optimized race frame borrows an immutable command package from
`RaceGpuScene`. Each selected track draw carries a stable source group ID and
instance ID. The package exposes every track placement and validated group
definition, including groups absent from the first visible frame. The selected
order remains the cached reference order: track draws followed by car draws.

At renderer open, deko3d uploads one aligned placement per track instance and
one aligned material per track definition. Each fence protected frame slot
caches the exact ordered visible track selection for invalidation. In the
race renderer, command emission visits depth-writing opaque track draws first,
then the remaining draws, preserving their order within each partition.
The separate sky (issue #184) is part of the package: its two definitions
and one placement, numbered one past the scenery instances. It is never in
the opaque partition, so the opaque world rejects every sky fragment it
covers (issue #227; see SKY_FOG.md). The
other Switch renderers retain the selected order. GPU ordering in the race
therefore needs its own image and timing check on hardware. A changed
selection re-records the chunks with static instance, material,
shader, texture, pipeline and mesh bindings; an unchanged selection reuses
its lists. The root frame calls one list per chunk, then records the car
directly. The shared GPFIFO budget bounds calls from the draw count and
rejects a frame that could exceed libnx's ring. Its conservative entry bound
and actual list count appear on the GPU profile page. Only the camera and
car placement/material values are copied to the rotating frame uniforms.
The car path skips repeated fixed state when adjacent definitions have equal
program, texture and pipeline bytes.

Each slot's command lists have a separate command buffer and bounded backing
memory. A slot is cleared only after its fence when its selection changes.
Renderer shutdown waits for the queue to become idle before list handles,
command memory, static uniforms, shaders or uploaded buffers are destroyed.
The frame command buffer can clear and reuse its own two fence protected
slices independently.

The borrowed update path checks package identity, generation, frame shape,
capacity and each selected definition/placement identity. Immutable resources,
draw ranges and pipeline variants were validated during scene preparation and
renderer open. Owning offscreen commands continue to use full update
validation. `nfsu_race_run_tests` compares cached commands against the owning
reference recording and checks the static package mapping during the retail
replay. Switch compilation verifies the deko3d command API; the desktop race
test and the runtime guard check the GPFIFO entry bound. The coordinator's
console run is the source of image and frame time evidence.

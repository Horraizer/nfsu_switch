# Train and Trolley animation controllers

## Scope and source

This continuation restores the group-zero TrainEngine, TrainCar and Trolley
controllers. It uses the PC 1.4 USA executable and original authored data.
The executable identity is SHA-256
`c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7`.
Retail inputs remain read-only.

The Ghidra investigation follows factories 0x57B6D0, 0x57B660 and 0x57B960,
constructors 0x57BF90 and 0x57BDD0, AnimationMover 0x579C00, the animation
clock 0x47B290, trigger loader/query 0x58DE20/0x58E2E0 and event 0x10006 in
0x4281E0. A missing MCP connection did not require guessed behavior: the
installed headless Ghidra opened the existing speed.exe analysis project.

## Authored movement

Train and Trolley movement is not an ordinary vehicle controller. The
0x3B300 records bind translation and quaternion channels from INGAMEB,
a placement matrix, clock flags and rate. Translation is DeltaF3; rotation
is DeltaQ. Fractional quaternion interpolation is normalized component
interpolation, without a shortest-sign rewrite. The ROOT skeleton is
validated before decoding. The animated child, not the identity parent,
supplies the pose.

The clock uses original sample units and the original channel end query.
Evaluation clamps to the last sample; the clock endpoint remains the source
frame count. Trolley loops. The train starts paused, wagons reserve their
constructor phase offsets and the engine starts ahead of the consist.
An endpoint-mode train stops without wrapping back across the track.

The event trigger uses the authored sphere and direction mask. The original
query predicts the point ahead using published velocity. Its release rule
uses route distance, speed, forward acceleration and the human driver's lane
context. This context is not the selected difficulty. The mover's prediction
fields remain separate from the animation phase.

The owner world order is physics, chronological contacts, actor acceleration,
animation movers, then track events. Releasing the train therefore changes
movement on the next animation update. Rival sessions do not advance their
own duplicate copies of the same world controllers.

## Collisions and models

These authored bodies have zero inverse mass and inertia. An impact changes
the car, not the train's animation. The original unbacked kind-4 getter still
returns node velocity. Clearing that velocity because inverse mass is zero
would incorrectly make a moving train behave like a static wall.
Translation delta over frame time supplies velocity; angular velocity is not
invented from the rotating matrices. The original small-frame-time guard
leaves the previously published velocity intact.

The shared contact queue uses the animation-node pose and the existing kind-4
response. It does not add a second integration or road solver. The current
retail train/Trolley records do not request the optional top support faces.

World models have no scenery-instance records. The bounded track loader
retains the named model solids and their textures. The renderer prepares
model commands against the same immutable track uploads, then updates only
their placement matrices. It does not fabricate scenery records or upload
new geometry every animation frame. Rendering and collision read the same
controller state. Per-frame triangle accounting and submission limits include
the dynamic models as well. Solver-only replays keep their explicitly empty live world.

## PC verification

`nfsu_world_motion_tests` pins original clock outputs, arrival calculations
and moving kind-4 contact responses. `nfsu_world_motion_retail` additionally
compares decoded child poses and matrices against original instructions,
steps the public RaceSession world update, checks train release and endpoint,
checks Trolley looping, and builds production GPU command frames for both
world model families. It verifies graphics/collision placement agreement and
validates the submitted resource namespace.

The native probe executes original instructions in synthetic mapped state:

    python tools/world_motion_native.py <retail-root> <new-output.json>

It authenticates the executable, refuses to replace an existing output and
forbids writing evidence under the retail root.
The animation allocator and the audiovisual tail after the physical response
are declared boundaries. It does not pretend to run a whole original game
frame. Its node velocity, pose, contact geometry and actor state are explicit
inputs. The allocator stub does not replace animation arithmetic.

## Corrections and remaining boundaries

Source record +116 creates a path/cache helper. It is not proof of independent
rigid-body backing. Source +144 selects a skeleton, not a parent pointer.
Parent binding is supplied separately to the constructor. Earlier fixed-body
wording that inferred backing from +116 was too strong.

This change restores authored movement, release, contact velocity and visible
models. It does not certify every original WorldObject factory, arbitrary
skeletons or parented movers. The specialized AI crossing-clear caller at
0x454A20 is a separate remaining integration; publishing the train mover's
prediction state does not certify that caller. Train sound and effects are
not newly synthesized here. Full original world allocator/list/callback
parity and Switch hardware behavior remain unverified by these PC witnesses.
Independent review is also separate from the worker's validation.

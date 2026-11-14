# Race tyre smoke and impact sparks

Issue #186 under #181. The CPU race reference and the deko3d race renderer
draw bounded, deterministic particles from the existing 120 Hz vehicle and
track-contact telemetry. `RaceParticles` is a visual owner; it does not write
to `RaceSession`, the vehicle, the route or the replay encoder. The on/off
control is `--particles=on|--particles=off` in both race applications. The
Switch defaults to on, and either value can be used with the same NRO and
`--replay --profile-frame`.

## Evidence and limits

The PC `speed.exe` used for the addresses below hashes to
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
Its image base is `0x00400000`; the cited file offsets equal virtual address
minus that base. The retail-original `Global/GLOBALB.BUN` hashes to
`39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9`.
These hashes identify inputs, not frame pins. The owner's modified
`GlobalB.lzc` and `GLOBALA.BUN` were not used. The retail file manifest is
compared before and after probe runs.

| Status | Evidence |
| --- | --- |
| OBSERVED | `speed.exe` strings `ROAD_SMOKE_1`, `ROAD_SMOKE_2` and `ROAD_SMOKE_3` are at `0x006B90EC`, `0x006B90CC` and `0x006B90AC`; pointers to them occur at `0x006EF520`, `0x006EF5B4` and `0x006EF648`. The car effect name `CARFX_SKID_SMOKE` is at `0x006BBFF0`, with its pointer at `0x006F1BE0`. These names establish available effect identifiers, not when the game emits them. |
| OBSERVED | At `0x0040283D..0x00402892`, the executable accepts chunk `0x00035021` version 4 and advances `0x220` bytes per effect record. At `0x0040289B..0x004028DC`, it accepts chunk `0x00035020` version 5 and advances `0xC0` bytes per record. `GLOBALB.BUN` contains the corresponding chunks at file offsets `0x203480` and `0x20F3E0`; their headers report 90 records. The first chunk contains `XEBO_ROAD_SmokeA`, `SMOKE_CS`, `SMOKEB_CS` and `XE_SPARKSA` strings. The second also names `XEBO_ROAD_SmokeA`. |
| INFERRED | The named records are part of the game's smoke and spark effect resources. Their presence does not identify the runtime wheel-slip test, emission-rate field, lifetime field or the texture used by a particular wall hit. |
| UNKNOWN | The exact retail slip quantity and threshold, wall-contact threshold, record-field-to-particle mapping, life/size/velocity curves, texture selection and fixed-function blend selection have not been traced to their consumers. The `SparkChatter` and `Sparks` strings alone are not evidence of a visual spark texture. |

The available simulation signals are `VehicleWheelTelemetry::slip_ratio`,
`slip_lateral` and `contact` for each wheel, and
`VehicleTrackTelemetry::impulses` and `impulse` for hull impacts. The latter
does not carry the contact point. `tests/recordings/event1.txt` produces tyre
smoke under the approximation below, but its full trajectory has zero hull
contacts. The assignment's assertion that it also contains a wall hit is
therefore not supported by this replay. The wall-hit image pin uses a
synthetic impulse in `nfsu_race_particles_tests`; the replay and its pins are
unchanged. Sparks are not seen in the real event1 replay. The synthetic
spark frame is only a two-pixel dot, so it does not establish the look of
a retail impact burst.

## Explicit approximations

- A contacting wheel emits smoke when horizontal speed exceeds 2 m/s and the
  larger absolute slip component exceeds 0.25. Each wheel emits on its own
  phase of a 4-step cycle. The smoke origin is its model wheel position,
  transformed by the car orientation. The 0.25 test, cadence, source point,
  size, drift, upward velocity, 60-step life and fading are approximations.
- A hull step with an impulse at speed above 1 m/s emits two sparks. The
  origin is estimated 0.85 m toward the struck side from the car centre,
  because the telemetry lacks a point. The two step-seeded directions,
  18-step life, size and fading are approximations. Only the horizontal
  impulse supplies a side: a purely vertical impact emits no sparks, and a
  nearly vertical one has an unreliable side.
- Smoke uses a generated soft grey RGBA8 disc with source-over blending;
  sparks use a generated amber disc with additive blending. These textures
  and blend selections are approximations. They contain no retail or derived
  image bytes. The CPU overlay and GPU commands use the same texel and
  opacity rules; GPU particle draws skip flat lighting. Both depth-test
  against the car and track but do not write
  depth. The CPU path takes the nearer of its separate car/track depth
  buffers; the GPU uses its combined buffer. This depth state is an
  approximation because the retail particle pass has not been traced.
- The seed mixer takes only the physics step, wheel or burst lane, and a
  fixed integer constant. No wall-clock input or mutable generator state
  reaches emission. Rendering can occur any number of times between steps
  without changing the particle list.

The fixed-array cap is checked by `nfsu_race_particles_tests`; the CPU sprite
bound is pinned by `nfsu_race_particle_frame_guards`. `nfsu_race_run_tests`
checks the recorded GPU sprite extents at the sampled smoke frame. A new
particle at capacity replaces the oldest of its type. The submission bound is
24 fixed draws and at most 55,296 covered pixels per frame (24 x 48 x 48);
dead slots have zero scale. This geometry bound is not a measured GPU time.
The paired handheld result for particles is still required. History: at
`3f53122`, the owner's handheld race replay GPU scene
measurement recorded mean 22.01 ms with the sky on, in [coordinator comment
5627](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/227#issuecomment-5627).
Particles default to on for the Switch, so that is the current baseline against
which their cost must be measured. The `--particles=off` argument allows a
paired handheld cost measurement on the same build.

`nfsu_race_particles_tests` pins the CPU smoke and synthetic wall-hit frames
by hash, with a reason at each assertion. `nfsu_race_run_tests` compares the
cached and reference command sequences with the effect enabled, including
the smoke step, and checks the unmodified replay hashes. The retail desktop
smoke frame is pinned by `nfsu_race_particle_frame_guards` when
`NFSU_RETAIL_ROOT` names the read-only install. A console photograph and
paired GPU measurement must be added from the owner's hardware before the
standing acceptance of #181 can be considered complete.

## Later solver-witness correction

The earlier body-side spark origin is historical. The world contact observer
now carries the actual prepared solver point through VehicleContactObservation.
RaceParticles selects a real current witness and emits no guessed burst when
one is unavailable. This is output-only: no contact order, impulses, physical
state or replay encoding is changed. The synthetic renderer test retains its
old supplied point and separately displaces only that witness. Triggers,
particle appearance and the complete original emitter runtime remain open.

## Authored atlases in the race renderer

The live CPU and deko3d renderers decode the original `SMOKE_CS` and
`XE_SPARKSA` from `Global/INGAMEB.BUN`. The procedural discs above
describe the earlier renderer and remain in the asset-free CPU test fixture.

In `speed.exe`, `0x402872..0x402883` resolves the texture hash at effect
record offset +0x0C. The `XETT_RoadSk_SmkA` and
`XECS_SolidWall_SprksA` records in `GLOBALB.BUN` bind those two atlases.
`0x403D2A..0x403D4C` derives the cell count from the log2 column and row
bytes at +0x19 and +0x1A. Smoke has a 4-by-4 atlas. Sparks have a 4-by-2
atlas. `0x403D50..0x403D73` turns +0x1B into the period in 1024 Hz ticks,
or zero for a fixed cell. `0x403E08..0x403E34` advances the stored cell
when the remaining lifetime crosses a frame boundary. Road skid smoke
has a fixed cell and solid-wall sparks run at 22 frames per second.

The port selects the initial cell deterministically for each emission.
The original also selects a cell during spawn (`0x40354D..0x403575`,
`0x4038C7..0x4038CB`), with its own random state. The port applies the
solid-wall atlas period to its current track-contact spark path and uses
its existing particle lifetime to calculate the remaining time. It still
uses the prior slip thresholds, spawn cadence, life, fade and trajectory.
Material-specific emitters, rival and traffic particle emission, and the
original particle simulation remain open.

## Wheel-slip admission

The two floats read by the original selector-3 producer at actor +0x374
are the physical slip velocities in wheel coordinates. At
`0x58746A..0x58747D` the tyre computes rim speed minus longitudinal
hub speed. `0x587681..0x58768F` publishes that result and lateral hub
speed to wheel +0x100/+0x104 in the sliding branch. The standing branch
zeros both at `0x58771C..0x587726`. `0x478BBE..0x478BE1` copies the
pair to the first wheel's owner snapshot at +0x334/+0x338, corresponding
to actor +0x374/+0x378. `VehicleWheelTelemetry::effect_slip_velocity`
exports that pair from the equivalent port producer without changing
the physics state or its encoding.

`0x4286A0` checks wheel contact and selects the surface row at
`0x6EF2D0`. It admits slip speeds strictly above the material minimum
and a camera distance strictly below 150. The grounded timer resets
below the minimum or outside the distance gate. It remains unchanged
while the wheel is airborne. Effect strength uses the material's
low/high speeds and the elapsed ramp. The port counts elapsed solver
seconds and uses the current chase view for the camera gate.

At `0x569B5C..0x569BEF`, selector 3 passes the negative of that strength
to the particle emitter. `0x402E6D..0x403975` treats its magnitude as a
burst budget against the interval of the effect record. Road, rough
road, dirt and grass skid records have the same unit interval. The
bounded port renderer applies the corresponding one/two-particle
budget to its staggered visual schedule. Its current generic atlas,
origin, motion, lifetime and fade still cover all of its skid smoke;
per-surface appearance and the original full emitter scheduling
remain separate work. The spark path retains its own trigger.

## Surface-specific presentation

The live renderers now read eight v4 effect records from the 0x35021 chunk
of GLOBALB.BUN. The wheel surface selects road, rough road, dirt, grass or
one of the three colored lane emitters. Wall contacts use the solid-wall
spark record. The texture hash and atlas layout in each record select
SMOKE_CS, SMOKEB_CS, SFX_SMOKE_A_CS or XE_SPARKSA from INGAMEB.BUN. The
Switch shares the uploaded SMOKEB_CS with nitrous purge.

The renderer evaluates the authored cubic RGBA and size at the lifetime
phase used by the native draw loop. It samples initial lifetime and launch
speed from the authored ranges, uses the atlas frame period, and advances
position using authored gravity and drag coefficients. It uses local seed
state, a body-up or wall-normal launch axis, and the port solver interval
for motion. The full native emitter transform, global random state and
packed update arithmetic are still open. Water has a separate original
spray producer; the current live skid-smoke path skips the two water rows.
Particle updates remain output-only with the existing draw and slot caps.

The original x86 curve and spawn slices are described in the r48 checkpoint.
The retail effect tests exercise each surface preset and check the matching
GPU draw state. The retail smoke frame guard pins the CPU smoke witness.

## Track environment emitters

PC 1.4 USA loader 402820 reads version 4 presets from TRACKB chunk
0x35021 and version 5 placements from 0x35020. The placement preset key
at +0x0C selects the definition key at +0x08 through 402B40. The original
update at 402D30 reads the placement origin at +0x60 and compares it to
the camera positions using the preset radius at +0x80.

RaceParticles reads both tables from the selected track bundle, including
reverse races and tournament rounds, and resolves only the presets used
by its placements. Empty tables have no environmental draws. The same
runtime feeds the CPU overlay and prepared GPU commands. INGAMEB supplies
SMOKE_CS and DUSTA_CS for steam, smoke and water, SFX_RJBSPARKS for
electrical sparks, FLOCKOFSEAGULLS for birds and FIREA_CS for flames and
embers. Electric sparks and fire use additive blending. Existing track
mesh animations and vehicle contact effects keep their existing owners.

The port uses the authored origin, direction, camera radius, start delay,
rate, life, atlas and appearance curves. The stationary emitters use a
deterministic visual RNG and the shared particle integration. Eight
nearby emitters per blend class, bounded particle pools and an inactive
draw skip cap frame work. Native random stream ordering, volume sampling,
bird alpha testing and exact particle integration are still separate
fidelity work.

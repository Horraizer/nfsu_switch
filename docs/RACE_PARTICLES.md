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

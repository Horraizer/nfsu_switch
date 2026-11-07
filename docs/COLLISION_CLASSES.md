# Collision classes: track, world objects, traffic, and opponents

Issue #252, research pass on the collision behavior requested after the
`c319a4b-red` Switch drive. This note separates four *encounter classes* from
the collision-body kinds in the retail executable. It is a static trace plus
reproductions of the existing one-car probe, not a retail runtime capture of
traffic or opponents. In particular, the owner's observation that these
encounters feel different does not establish four impulse formulas.

## Evidence and status

- The retail `speed.exe` used here is 3,178,496 bytes, SHA-256
  `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
  The executable was read only. Its `.text`, `.rdata`, and `.data` file offsets
  equal virtual address minus `0x400000`, as checked in
  [VEHICLE_TRACK.md](VEHICLE_TRACK.md). Addresses below are retail VAs.
- `docs/VEHICLE_TRACK.md` already traces a car against track polygons and
  labels the port's contact witness, pair order, and scheduling as
  approximations. This pass rechecked the named retail branches and reran four
  existing Release scenarios; it did not add a second actor to the port.
- The class values are established by the 35 retail car records in
  [PART_PLACEMENT.md](PART_PLACEMENT.md): class 0 for 20 racer models and class
  2 for 15 traffic models. A racer model is not by itself proof that an
  instantiated car is an opponent rather than the player.

## Body kinds and gates recovered from `speed.exe`

| Encounter | Retail evidence | Safe conclusion |
| --- | --- | --- |
| Track wall or road | `0x0046A477` pushes kind 3 into the polygon-body base constructor `0x00468340`. `0x00466770` and `0x00466780` answer zero inverse mass and inertia. | The static track is one collision body per polygon. A wall is a geometric classification of a kind-3 polygon, not a separate body kind. |
| Placed/world object | Constructors at `0x005922A9` and `0x005924AC` push kind 4; `0x005922F5` and `0x005924F8` install vtable `0x006B8D88`. `0x005927E0` returns inverse mass zero when body `+0x60` is zero; otherwise it reads the rigid pointer at `+0x6C` and caps inverse mass at `1/0.3` using constants `0x006CC7BC` and `0x006B5AA8`. | Kind 4 has unbacked and rigid-backed accessor branches. `+0x60` is a gate, not a proven pointer. A named event0 instance, activation writer, actual geometry and per-object descriptor selection remain unidentified; see [WORLD_OBJECT_BODIES.md](WORLD_OBJECT_BODIES.md). A scenery mesh is not automatically a collidable kind-4 body. |
| Traffic car | The car type's `+0xC54` class is read at `0x00425721`; nonzero skips the `+0x868` allocation at `0x0042572E` but reaches `0x00425751`. Both paths call `CarCollisionBody` constructor `0x00469B00` at `0x00425773`; that constructor calls `0x00468340` with kind 0 at `0x00469B2A` and installs vtable `0x006C8798` at `0x00469B74`. | The traced constructor gives a traffic car the same collision-body kind as a racer. Class 2 may change other car systems, but it is not a distinct collision-body kind here. |
| Race opponent car | A racer-model record has class 0, so it takes the extra `+0x868` branch before the same kind-0 `CarCollisionBody` construction. | The traced car-body construction does not distinguish player from opponent. Which racer instance is player or AI, and how their update order differs, remains untraced. |

`0x0046BAE0` resolves a closing contact through the bodies' virtual mass,
inertia, restitution, friction, and impulse methods. The analyzed instructions
do not contain a traffic-versus-opponent impulse switch. That is a bounded
negative finding about this solver path, not proof that AI control, body setup,
collision filtering, or later game logic cannot differ.

The same solver has material kind-dependent behavior:

1. `0x0046C620` recognizes a kind-0 car against a kind-3 polygon. If the
   polygon normal's vertical component is below `0.1` and the contact arm is
   sufficiently sideways to the car's forward axis (`|cos| < 0.9`), it asks
   `0x0046BDE0` to halve restitution (`0x006B7254` is `0.5`). A car-car or
   car-kind-4 contact does not enter this particular gate. The base
   restitution of both bodies is `0.1` in the traced path, per
   [VEHICLE_TRACK.md](VEHICLE_TRACK.md).
2. The car collision record calls `0x00465D00` from `0x004658B0`. When the
   current hit's impulse-vector magnitude is at least 30 t m/s
   (`0x006B72BC`), the block at `0x00465E75`..`0x00465EC3` can set car
   `+0x38` to 1 and zero `+0x4CC`. `0x00465E8F`..`0x00465EAB` accepts
   kind-0/kind-0 or kind-0/kind-4 pairs through `0x004663D0`; a track
   polygon is kind 3 and does not satisfy it. State 1 changes car
   inverse-inertia scaling from
   `0.4` to `1.0` at `0x0046A0C0` and enables the four-wheel lever block via
   collision-record `+0x10` at `0x00465EB4`. The state later decays through
   `0x0042A530` under its wheel/time gates, which this pass did not remeasure.
   `0x00465849`..`0x0046588B` also accumulates impulse components and
   magnitudes in the record, but `0x00465850` passes the current hit's vector
   to `0x00465D00`. Two aligned 20 t m/s hits can accumulate 40 in the record
   without either call crossing this 30 t m/s state gate.
3. A car's inverse mass is capped at `1/1.85` t at `0x0046A080` (constant
   `0x006B727C`), independent of the kind-3/4 distinction. For a moving
   world object, `0x005927E0` instead caps inverse mass at `1/0.3` t; for an
   immovable one it returns zero. The mass of a particular racer or traffic
   car still comes from its own handling record. The 15 traffic handling
   records differ from the 20 racer records; see
   [VEHICLE_TUNING.md](VEHICLE_TUNING.md).

The impulse accumulation path `0x00465830` calls `0x00465D00`, then consults
the surface table at `0x006EF2D0` (stride `0x94`, thresholds near `+0x80` to
`+0x90`) for effects and sounds. This does not identify a correct sound bank
entry for each encounter; that belongs to issue #222. It also does not imply
that all static scenery produces track-polygon contacts.

## Repeatable static scenarios in the current port

These are regressions for the *port's approximation*, not measured retail
outcomes. The existing Release `nfsu_probe` at source head
`c319a4b5833c66dc14025acf67b0daec2b4331fd` was run on 2026-10-01.
The SHA-256 telemetry pins are already recorded in
[VEHICLE_TRACK.md](VEHICLE_TRACK.md) and checked by the vehicle-track tests;
this table records that this pass reproduced them without copying the values.

| Scenario | Reproduced observation | Pin check |
| --- | --- | --- |
| `--proving-ground wall` | First kind-3 wall contact at step 331; car then travels along it; no reset. | `pinned: EQUAL` |
| `--proving-ground wall-glance` | First wall contact at step 377; 12 contact steps; no reset. | `pinned: EQUAL` |
| `--proving-ground wall-fast` | Starts at 35 m/s; first contact at step 89; 23 contact steps; no reset. | `pinned: EQUAL` |
| Event 1, track 1003, `wall` | First polygon 9374 contact at step 242 near 36 km/h; ends against it around 0.56 km/h; no reset. | `pinned: EQUAL` |

For a fresh Release build, set `NFSU_RETAIL_ROOT` to the retail installation,
then run the probe. The commands derive the retail location from the file's
directory, never from a constructed non-ASCII path:

```powershell
$retailExe = @(Get-ChildItem -LiteralPath 'E:/Games' -Recurse -Filter speed.exe -File | Where-Object Length -eq 3178496)
if ($retailExe.Count -ne 1) { throw 'Expected one retail speed.exe' }
if ((Get-FileHash -LiteralPath $retailExe[0].FullName -Algorithm SHA256).Hash -ne 'C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7') { throw 'Retail executable hash mismatch' }
$retailRoot = $retailExe[0].Directory.FullName
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DNFSU_RETAIL_ROOT="$retailRoot"
cmake --build build-release --target nfsu_probe
& ./build-release/nfsu_probe.exe vehicle-track --proving-ground wall
& ./build-release/nfsu_probe.exe vehicle-track --proving-ground wall-glance
& ./build-release/nfsu_probe.exe vehicle-track --proving-ground wall-fast
& ./build-release/nfsu_probe.exe vehicle-track (Join-Path $retailRoot 'Global/GLOBALB.BUN') (Join-Path $retailRoot 'FrontEnd/FrontB.lzc') (Join-Path $retailRoot 'Tracks/TRACKB1003.lzc') 1 wall
```

The `pinned: EQUAL` line in each result checks the recorded port telemetry.
It says nothing about a retail dynamic collision. The current
`VehicleTrackSession` has only one simulated car. There is no dynamic
traffic/opponent actor or world-object collision body in this path, so the
four scenarios above cannot stand in for those encounter classes.

## Gap by encounter class

| Encounter | Known retail behavior | Port behavior and missing evidence | Next proof |
| --- | --- | --- | --- |
| Track wall/road | Kind 3, immovable; sideways low wall can halve restitution; track-only hit does not trigger car state 1. | One-car track solver exists. Its witness and pair order are approximations, and a visible barrier can lack a collision polygon. No synchronized retail versus port drive trace exists for the owner's offending bend. | Capture the same initial car state and first-contact polygon in retail and port; compare witness, normal, separation, impulse, yaw, and speed for 15-, 45-, and 90-degree wall approaches. |
| Placed/world object | Kind 4; may be immovable or backed by a rigid body; strong car-kind-4 hit can set state 1. | No kind-4 objects in the race collision session, no decoded per-object hull/mass/activation. | Identify one named object and its retail constructor/data path, then capture static and movable contact cases with its body fields. |
| Traffic car | Class-2 model, kind-0 car body. Same traced generic impulse path as other cars; mass/handling and AI motion can differ. | No traffic actor, pair scheduling, or two-body contact. Class-specific controller and collision filters are untraced. | Capture a stationary and moving traffic car at fixed approach speeds; record both cars' pre/post velocity, yaw, masses, body kind, state, and pair order. |
| Race opponent | Racer-model class 0, kind-0 car body. The traced constructor gives no opponent-only kind or impulse formula. | No opponent actor, shared solver, or pair scheduling; racing-line work is separate in [RACING_LINE.md](RACING_LINE.md). | Repeat the traffic matrix with an opponent, compare matching mass/geometry, and trace any remaining difference upstream or downstream of `0x0046BAE0`. |

## Follow-up implementation tasks after the trace

1. **Trace and instrument dynamic pairs.** Recover broad-phase pair creation,
   ordering, contact witness (`0x00472830`, `0x00472950`), and the collision
   pass position relative to the car's 120 Hz model step and 60 Hz frame.
   Produce a retail capture matrix for kind 0/0, 0/4, and 0/3, recording both
   bodies and the collision state before and after. Dynamic behavior remains
   unaccepted until this can be compared to the port.
2. **Add world-object collision data and bodies.** Decode one bounded,
   identified track object first, including hull, static/movable switch,
   mass and inverse inertia, spawn, and reset. Keep it separate from static
   polygons. Prove collision and absence of collision where the retail data
   says each occurs.
3. **Add multiple car bodies with deterministic scheduling.** Instantiate a
   traffic car and an opponent in separate scenarios, use each model's actual
   handling data, and resolve both bodies through one contact. Preserve the
   one-car pins when actors are present but do not touch. Pin the touching
   cases on desktop and Switch only after retail captures exist; do not make
   up separate traffic and opponent impulse constants.
4. **Connect events to issue #222.** Feed body kind, surface, impulse, and
   contact lifecycle to the collision-sound work. Validate sound selection
   by listening to wall, placed object, traffic, and opponent contacts on
   Switch. An impulse event is not itself a known retail cue mapping.

This pass closes only the classification and static-trace part of #252.
Dynamic class responses and the owner's observed flight/no-flight difference
need controlled runtime evidence and implementation work.

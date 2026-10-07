# Original AI control producers

Bounded research for [issue293 assignment8064](https://git.accad.pro/murzintsevav/nfsu-switch/issues/293#issuecomment-8064).
Base: `31e3af4b665b9cd4cc93d9d13dccda505f301605`.
Actor context is [PR285](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/285)
at `7c6a5e30e977160105cad778b843323ac90d978e`; phase context is
[PHYSICS_DISPATCH_256.md](PHYSICS_DISPATCH_256.md).
This delta adds no production actors, path follower, solver or C++.
Its tests do not accept inherited C++/Release/Switch changes.

The new result is executable control production: the complete original driver
`0x0045E970`, including its lookahead, angular errors, history filters and
throttle/brake path, runs without helper hooks for the bounded fixtures below.
The upstream controller publication is also executable with a declared
strategy-cap hook. This resolves substantially more than object ownership.
Actual event0 action selection and original runtime parity remain open.

Evidence labels: **S** authenticated PE instructions/data; **E** original x86
interpreted by Unicorn on synthetic state; **R** original running game,
unavailable; **P** proposed port contract. E is not host-native execution,
a game process, a trajectory measurement, or a substitute for R.

## Reproduce

From the assigned worktree, using standalone Python3.8+:

```powershell
python -m pip install --target build/293-deps capstone==5.0.7 unicorn==2.1.4
$env:PYTHONPATH = Join-Path (Get-Location).ProviderPath 'build/293-deps'
python -B tools/controller293_trace.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --out build/293-evidence
python -B tools/controller293_test.py --report build/293-evidence/report.json
python -B tools/collision256_trace_test.py
```

Use a fresh output directory each time. Paths derive from `Path.cwd()` and
ASCII relative components; no encoded copy of the workspace path is needed.
The executable identity is 3178496 bytes, SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
No other retail file supplies probe inputs. Entire retail file/directory
size/mtime/content manifests bracket each run, including failures. Reparse
points, retail overlap, existing output and paths outside the workspace fail.
Reports stay local and ignored; no executable, geometry or derived asset is
exported. `source.txt` is decoded source evidence; `report.json` contains tool
hashes, source assertions, span hashes and E results. Tests own numeric pins.

Every executed original range is explicitly allowed. Unlisted execution,
memory faults, exhausted instruction budget and non-finite JSON results fail.
An output directory from a failed run must not be reused. The manifest PASS
alone does not imply a probe PASS. Regression checks also verify current tool
hashes and the matching manifests. Wrong grounded-branch and strategy-cap
branch opcodes are rejected using modified decoder input in memory; retail
files are never patched. This is not a mutation-sweep score.

## Controller to driver inputs

**S.** Ordinary rival controller3 constructs `0x004620C0`, installs
`0x006C89F0`, and constructs its separate driver at `0x0046219D`. Controller
virtual+0x0C is `0x00462430`; that calls the base action scheduler
`0x0044B1B0` at `0x004624B8`. The driver table `0x006C8C18` has virtual+4
`0x0045E970`. Neither the controller nor driver table is the physics table.
The factory's event0 role/config selection remains the pinned #268/#258
contract; this probe does not replay new-career construction or instantiate
the actual roster.

`0x0044C550` is the new concrete boundary between action selection and driver
inputs. It first sends action+0x20 to driver virtual+0x3C (`0x0045FAF0`).
That copies the path object into driver+0x10 and invokes `0x00459980` for
resampling. It then calls catch-up scale producer `0x0044BD10` and chooses:

| Source branch | Original input and publication |
| --- | --- |
| Empty current route (`route_state=[controller+0x3D8]`, `[route_state]==0`) | Desired speed, duration and auxiliary input become zero; enable defaults true. The route-state object itself must exist. |
| Action+0x1F0 is zero | Desired speed and duration come from action+0x1E4/+0x1E8. |
| Action+0x1F0 is nonzero, controller+0x478 is not -1 | `0x00442110` resolves a selected route point. Byte point+0x6A plus route-state index is converted to float and multiplied by controller+0x4B8. |
| Ordinary route branch, controller+0x478 is -1 | `0x004420E0` derives the point index. A timed RNG offset at controller+0x404 perturbs it; it is clamped to route count. `0x0059BB80` selects the category/direction byte index. Point stride is 0x88, data base route+0x27C. Byte at +0x6A receives an almost-zero fallback, context factor at `0x0077A00C + context*0x54`, then controller+0x4B8. |

On action/route branches, action+0x1EC supplies the auxiliary float and
action+0x1F1 supplies enable. Strategy object controller+0x3FC virtual+0x10
then limits desired speed. At `0x0044C7EE`, driver virtual+0x38 calls actual
setter `0x0045E180` with four arguments, writing driver+0x200 (desired speed),
+0x204 (duration), +0x208 (auxiliary) and +0x20C (enable). Do not infer that
the auxiliary float is a pedal value: this driver entry does not consume it.
Desired speed is compared with actor+0x3D4; the probes use that same raw domain
and do not assert a newly recovered physical-unit conversion.

**E.** Controller tests start at `0x0044C550` and stop immediately after the
actual setter returns, at `0x0044C7F1`. Action, selected-route, cap-binding and
null-route cases distinguish the branches. The route-byte fixture supplies a
synthetic point and scale. The only hook is the strategy virtual+0x10 returning
the declared cap. The path-copy/resampling entry and setter execute original
instructions. Catch-up returns through its actual null-actor-status gate.
The action scheduler, ordinary RNG-offset route branch and controller tail
are S only. These cases do not establish real event0 desired speeds.

## Lookahead and steering

**S/E.** Driver `0x0045FB30` computes lookahead before angular errors:

- Ordinary physics3/controller3 uses actual curve `0x00706118` evaluated on
  actor+0x3D0 and multiplies it by category curve `0x00706168`, using signed
  RideInfo+0x424. Category -1 invokes preparation `0x0057F2B0`; that branch
  is outside E, whose categories are explicit and already resolved.
- Other selector combinations use the indexed constants at `0x006B7418` and
  `0x006B7434`. Physics/controller selectors are distinct inputs.
- With at least two path points, `0x004590A0` walks/interpolates/extrapolates
  the synthetic polyline. `0x00459710` chooses a point using speed, velocity,
  position and a world-tick cache in path+0x1A0/+0x1A4. Fewer points select
  heading/velocity fallback routines; those branches are not E coverage here.
- The sampled XY target is stored at driver+0x1F4/+0x1F8. `0x0045E370`
  computes a wrapped heading-to-target error through actual angle function
  `0x00564DB0` and table helper `0x00564CD0`. It also produces a velocity-
  heading blend using curve `0x007060C0` and a lateral-path angle through
  `0x004592F0`. The lateral term is not the target-heading error.

When either first wheel-contact byte is set, `0x0045E970` maintains three
steering history objects at driver+0x230/+0x234/+0x238. Each holds samples,
elapsed-time sums, current/previous error and rate terms. Curve `0x007060A8`
blends angular and lateral error; speed/category curves select gains, and
the driver combines bounded error, integral and derivative contributions.
Airborne steering follows a separate branch at `0x0045F2B4`.
Steering is limited and slew-filtered, then converted into the wrapped word
at driver+0x1D0 (`0x0045FA53`). Treat it as a raw original encoding; it is
not a normalized joystick axis.

The startup routines `0x00689FD0` and `0x00689FF0` initialize globals
`0x0077A4A8` (point-selection distance threshold) and `0x0077A4A4` (history
integral bound). They execute in every fixture. Merely mapping the PE leaves
those BSS fields zero and gives a different steering history. This distinction
was found during exploration; the tests use the initialized original values.

## Throttle, brake and limited recovery evidence

**S/E.** Speed error is `abs(driver+0x200) - actor+0x3D4` at
`0x0045F3A4..B9`, stored through history object driver+0x23C. Actual
`0x0045E7B0` updates the separate wheel-slip history at +0x240. Driven-wheel
contact, speed sign and reverse-state hysteresis determine whether
`0x0045FC90` computes pedals or a brake/coast override is selected.

`0x0045FC90` has a low-target threshold branch, then history-based
proportional/integral/derivative gains selected by the original mode index.
It bounds the change using curve `0x0070617C`, adds it to the previous
throttle-minus-brake state, applies separate positive/negative bounds, and
splits the result into throttle and brake. `0x00460100` gates additional
wheel-slip limiting using hold state, role, race clock and finish place.
Final throttle is bounded by driver+0x1FC and brake by unity. The entry's
enable gate at `0x0045F8DD` can clear both despite computed errors.

Outputs are driver+0x1DC throttle, +0x1E4 brake, +0x1E0 handbrake, +0x1D0
steering word and +0x1D4 raw gear command. The reverse-input test changes
the raw gear command; no physical reverse trajectory is simulated.

**S/E, bounded.** A set driver+0x220 forces the later special pedal override
at `0x0045FA72`. We call this a special flag rather than claiming a complete
recovery-state identity. Action type0x11 installs table `0x006C92D4`; its
virtual+0x0C exit `0x00453830` follows route-state actor -> actor+0x30 and
clears that flag, sets +0x210/+0x21C timers negative, and clears actor+0x88C.
E executes the exit and subsequent driver updates. Clearing the flag does
not instantly recreate fresh-controller pedals: the previous pedal/history
state still affects the next update. The test pins that coasting transition.
Entry/trigger selection, obstacle escape, reposition and successful recovery
are not recovered or measured by this case.

Driver history rebasing is separate. `0x0045E650` obtains current angular
errors and repopulates steering error/rate buffers; `0x0045E750` does the
same for speed error. Their actual `0x005842D0` calls clear error-buffer
counts but preserve the time ring, and they do not rewrite published pedals
or steering. E asserts those distinctions. They are not a full race reset.

## Synthetic state and coverage

`controller293_native.fixture` is the complete input recipe, and the report
lists case-specific inputs. Actor/controller/driver/physics, RideInfo,
history and route memory is synthetic and initially zero unless assigned.
The fixture sets physics3/controller3, role2, resolved category, raw heading,
speed/velocity, wheel-contact flags and raw actor+0x3B8 above the final
steering-clear threshold. Wheel-slip values, actor status pointer, optional
timers, hold/override globals and unused object fields are zero. No live
event0 constructor, tuning vector, seed or actor address is claimed.

All nine driver cases run the whole entry to `ret 4`, repeatedly with positive
supplied dt and frozen actor pose/velocity. History and output state persist;
world ticks advance. This is an input-response experiment, not closed-loop
driving. The x87 control word is explicitly synthetic `0x007F`. Actual PE
curves, gains and angle tables are used. Driver cases, action exit and history
rebase have **no helper hooks**. The separate controller-publication cases
have exactly the one strategy-cap hook described above. Reports enumerate
encountered hooks and driver instruction addresses.

The regression scenarios cover straight acceleration, opposite corner signs,
overspeed braking, disabled inputs, airborne contact gates, a reverse command,
special-flag exit, history rebase, category sensitivity, and action-versus-route
speed publication with binding/nonbinding caps. No overtake/contact scenario
is called a PASS. There is no other actor, collision witness, solver or evolving
vehicle pose in these fixtures. Negative/zero/non-finite dt, fallback short
paths, selector1/4, category preparation, random action selection and full
restart are outside numerical coverage. The entry's local dt guard does not
make every internal division safe for an arbitrary zero dt.

## Producer status and proposed contract

| Input | Known producer | Remaining evidence |
| --- | --- | --- |
| Event0 roster/config ID/role/grid/tuning | #268 setup, pinned PR285 actor ownership | Actual resolved actor/config capture; no change to roster or tuning in this delta. |
| Desired speed, duration, auxiliary and enable | S/E controller publication and actual setter above | Actual action, strategy object/cap and sequence selected for each rival. |
| Driver path and lookahead target | S action path copy; E original sampler and target/error producer | Original racing-line/lane choice, path resampling for longer paths, obstacle/overtake planner. |
| Steering/speed/slip histories | S constructors and S/E actual update/rebase | Live initial state, x87 mode, dt sequence and all reset call sites. |
| Category/gains | S/E RideInfo+0x424 and original gain curves | Final event0 adaptive tuning/category and any mutable tuning overrides. |
| Context/difficulty | S `0x0044CA10` reads signed config+0x735, used by route speed and catch-up tables | Do not equate this context byte with the global profile difficulty without its setup producer. Runtime-initialized context tables and RNG offsets are not exercised. |
| Event AI scalar `0x0078A380` | #268 builder source; new S consumer `0x0043ED1A` in another progress calculation | No direct use by this driver or desired-speed setter established. Do not multiply pedal output by that scalar. |
| Catch-up multiplier | S `0x0044BD10` writes controller+0x4B8 using race/actor status, reference human, progress/speed differences and tables | E only takes its null-status early return. Live event0 values and ordinary numerical replay remain open. |
| Special flag/recovery | S/E type0x11 exit and driver override; S history reset routines | Trigger/action lifecycle, recovery geometry, RNG and successful physical outcome. |

**P: `controller293-v1`.** Use `(race_generation, signed_config_id)` as the
racer key, with a separate incarnation if an ID can be reused. The original
driver ID accessor `0x004600F0` reads actor+0x0A. Keep role-array index,
grid slot, rank and pointer identity separate; this preserves the pinned
#258 contract rather than inventing a persistent original generation field.

The actor owner owns configuration, RideInfo, independent driver/history and
controller action/route state. During #256's existing controller phase, publish
the action path and four driver inputs, preserving provenance and raw selector.
Then update every driver in original world-array order using the supplied
world dt. Publish all raw controls together. Physics consumes those controls
through the existing selected physics object and shared #257 solver. No
collision response belongs in this controls adapter. #258 publishes the final
pose/progress/status snapshot after the existing physics/contact/status phases.

A car discontinuity must explicitly select the recovered history rebase and
action-exit operations appropriate to its real call sites; do not erase the
whole driver or reset accumulated race timing merely because pose changes.
Full restart destroys/recreates owned controller/driver state and starts a
new port generation. This generation policy is P; actual original retry
callbacks and all reset side effects remain R/source acceptance work.

## Unmet original and hardware acceptance

No original game was launched, attached, patched or written. **R unavailable**:
there is no identified event0 profile/input recipe and live original control
trace for these scenarios. A later authorized capture must correlate actor ID,
config and resolved RideInfo, context/category, action type/strategy cap,
path points and selected line/lane, RNG state, world tick/dt, driver history,
entry/exit controls and subsequent physics/contact/progress by call identity.
Include straight/corner/braking, no-contact overtakes, contact, recovery,
pause and restart. Preserve the distinction between frozen-input E fixtures
and evolving original trajectories. The smallest missing input is that trace,
not permission to invent a controller from #63's project path follower.

Issue293 remains open for action/planner/difficulty research, independent
review and original comparisons; #258 retains actor integration and full-race
hardware acceptance. No Switch operation was performed. Research gates here
do not validate inherited production changes or waive scene-height540,
gpu-boost on and actual-clock measurements in the later hardware gates.

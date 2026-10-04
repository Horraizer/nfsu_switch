# Original AI control producers

Continuation for [issue293 assignment8442](https://git.accad.pro/murzintsevav/nfsu-switch/issues/293#issuecomment-8442).
Base: `ec21749671603a1505a63148feb3806d0a1c5059`.
The prior driver/publication witness is selectively imported from independently
reviewed [PR307](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/307),
`f5a8796e6859973cb5b1a10650d3ee71ed202fa9`. Imported dependencies are
`physics256_native.py`, `collision256_trace.py` and its output-path tests;
`world_body_trace.py` was already present, byte-identical to that source.
No cumulative PR307 stack or foreign worktree is required to reproduce.
Actor context is [PR285](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/285)
at `7c6a5e30e977160105cad778b843323ac90d978e`; phase context is
[pinned physics dispatch evidence](https://git.accad.pro/murzintsevav/nfsu-switch/src/commit/f5a8796e6859973cb5b1a10650d3ee71ed202fa9/docs/PHYSICS_DISPATCH_256.md).
This delta adds no production actors, path follower, solver or C++.
Its tests do not accept inherited C++/Release/Switch changes.

The continuation executes an initialized original planner and retained driver,
using authenticated retail route, AI policy, track policy, stock RideInfo and
convex hull inputs. `controller293_planner.ActorController` constructs the
original objects, publishes the self world object, generates action paths and
returns controls from original driver updates. No supplied path, substitute
curve, planner callback or prebuilt driver history enters this new chain.
The older frozen/supplied-path witnesses remain separate regression cases.
The v4 continuation under assignment8569 adds original event setup metadata,
grid permutation and a paired controller witness in `controller293_scene.py`.
The v5 continuation under assignment8601 adds original event preset/adaptive
RideInfo construction and a second paired witness using the event's player
INTEGRA and first rival ECLIPSE, including their retail hulls.
Its recursive retail chunk reader is the already tracked `event0_trace.py`;
no additional foreign-branch dependency is needed.
The original game runtime, actual event0 roster/scene and physical integration
remain outside this conditional executable adapter.

Evidence labels: **S** authenticated PE instructions/data; **E** original x86
interpreted by Unicorn on synthetic state; **R** original running game,
unavailable; **P** proposed port contract. E is not host-native execution,
a game process, a trajectory measurement, or a substitute for R.

## Reproduce

From a fresh checkout, using standalone Python3.8+ (adjust the interpreter path on other hosts):

```powershell
& 'C:/Users/horra/AppData/Local/Programs/Python/Python312/python.exe' -m pip install --target build/293-deps capstone==5.0.7 unicorn==2.1.4
$env:PYTHONPATH = Join-Path (Get-Location).ProviderPath 'build/293-deps'
& 'C:/Users/horra/AppData/Local/Programs/Python/Python312/python.exe' -B tools/controller293_trace.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --out build/293-evidence
& 'C:/Users/horra/AppData/Local/Programs/Python/Python312/python.exe' -B tools/controller293_test.py --report build/293-evidence/report.json
& 'C:/Users/horra/AppData/Local/Programs/Python/Python312/python.exe' -B tools/collision256_trace_test.py
```

Use a fresh output directory each time. Paths derive from `Path.cwd()` and
ASCII relative components; no encoded copy of the workspace path is needed.
The executable identity is 3178496 bytes, SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
The initialized planner additionally reads these authenticated retail inputs:

| Relative input | Bytes | SHA-256 |
| --- | --- | --- |
| FrontEnd/FrontB.lzc | 4182578 | 76F368D5E5E30F96A8182BF457DBA4491395E075C3F336D86A32012B56E16F90 |
| Tracks/TRACKB1003.lzc | 433352 | ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA |
| GLOBAL/GLOBALB.BUN | 2361176 | 39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9 |
| Cars/240SX/GEOMETRY.BIN | 4780816 | 002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619 |
| Cars/INTEGRA/GEOMETRY.BIN | 5780304 | C5E4808EBBEE5A5DD990A97E1BC035753042F55CFCC46678179F7441E49BFFB4 |
| Cars/ECLIPSE/GEOMETRY.BIN | 5923024 | F28688371D708189E83B152168486618D29436D9A109B54E63E0C40D908084BF |

Entire retail file/directory size/mtime/content manifests bracket each run,
including failures. Reparse
points, retail overlap, existing output and paths outside the workspace fail.
Reports stay local and ignored. No retail file or complete asset is exported;
reported path coordinates and bounds are bounded research measurements. `source.txt` is decoded source evidence; `report.json` contains tool
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
The paired witness below executes the event metadata/config selection, while
retaining conditional stock car inputs. It does not instantiate the actual
event roster or execute the complete new-career flow.

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
These legacy publication cases leave the action scheduler, ordinary RNG-offset
route branch and controller tail at S. The initialized planner witness below
executes the ordinary route/RNG branch. Neither establishes actual event0 inputs.

## Lookahead and steering

**S/E.** Driver `0x0045FB30` computes lookahead before angular errors:

- Ordinary physics3/controller3 uses actual curve `0x00706118` evaluated on
  actor+0x3D0 and multiplies it by category curve `0x00706168`, using signed
  RideInfo+0x424. Category -1 invokes preparation `0x0057F2B0`; that branch
  is outside the legacy driver fixtures. The initialized planner now executes
  original RideInfo preparation before category use.
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

The inherited regression scenarios cover straight acceleration, opposite corner signs,
overspeed braking, disabled inputs, airborne contact gates, a reverse command,
special-flag exit, history rebase, category sensitivity, and action-versus-route
speed publication with binding/nonbinding caps. No overtake/contact scenario
is called a PASS. There is no other actor, collision witness, solver or evolving
vehicle pose in these fixtures. Negative/zero/non-finite dt, fallback short
paths, selector1/4, category preparation, later random action selection and full
restart are outside numerical coverage. The continuation below adds conditional
initial strategy/action selection and longer changing-input paths. The entry's local dt guard does not
make every internal division safe for an arbitrary zero dt.

## Producer status and proposed contract

| Input | Known producer | Remaining evidence |
| --- | --- | --- |
| Event0 roster/config ID/role/grid/tuning | #268 setup, pinned PR285 actor ownership | Actual resolved actor/config capture; no change to roster or tuning in this delta. |
| Desired speed, duration, auxiliary and enable | S/E publication, actual setter, initial action constructor and original strategy1/2/3 cap | Actual event0 mode flags and subsequent action/route sequence for each rival. |
| Driver path and lookahead target | E original copy/resampling of longer synthetic paths and target/error producer | Original racing-line/lane choice and obstacle/overtake planner output. |
| Steering/speed/slip histories | S constructors and S/E actual update/rebase | Live initial state, x87 mode, dt sequence and all reset call sites. |
| Category/gains | S/E RideInfo+0x424 and original gain curves | Final event0 adaptive tuning/category and any mutable tuning overrides. |
| Context/difficulty | S `0x0044CA10` reads signed config+0x735, used by route speed and catch-up tables | Do not equate this context byte with the global profile difficulty without its setup producer. Runtime-initialized context tables and RNG offsets are not exercised. |
| Event AI scalar `0x0078A380` | #268 builder source; new S consumer `0x0043ED1A` in another progress calculation | No direct use by this driver or desired-speed setter established. Do not multiply pedal output by that scalar. |
| Catch-up multiplier | S `0x0044BD10` writes controller+0x4B8 using race/actor status, reference human, progress/speed differences and tables | E only takes its null-status early return. Live event0 values and ordinary numerical replay remain open. |
| Special flag/recovery | S/E type0x11 exit and driver override; S history reset routines | Trigger/action lifecycle, recovery geometry, RNG and successful physical outcome. |

**P: `controller293-v2`.** Use `(race_generation, signed_config_id)` as the
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

## Continuation: original strategy and first-action producer

**S/E.** `controller293_strategy.py` starts the scheduler slice at
`0x0044B831` and stops at `0x0044B85D`. If controller+0x3FC is null, it
calls controller virtual+0x60 (`0x004623D0`); if controller+0x3F8 is null,
it calls the resulting strategy virtual+0x18 and stores the action.
This executes the actual `0x00567160` allocator against explicit synthetic
free lists. No allocator, constructor, virtual call or scalar result is hooked.
The full controller constructor and the scheduler before/after this slice are
not executed by this continuation.

Selection reads the actual PE branches, using raw names where semantics have
not been established:

| State | Original strategy kind / first action kind |
| --- | --- |
| global byte0x78A344 == 0, dword0x7361A8 == 0, and special condition below false | strategy1 / action7 |
| Either mode global is nonzero, actor config+4 == 1 | strategy3 / action8 |
| Either mode global is nonzero, actor config+4 != 1 | strategy2 / action8 |
| Both mode globals zero, track0x78A2F0 == 0x44B, global0x735FB0 nonzero and global0x735FB8 == 4 | same role-dependent strategy2/3 branch |

The event0-shaped fixture uses track1003 and role2 with both mode globals
zero. These remain supplied state, not captured event0 values. Tests include
both alternative mode globals, role1 and the special condition's adjacent
nonmatching cases; they must not be described as profile-difficulty tests.

The factory at `0x0044AA10` calls constructors `0x0044A150`, `0x0044A190`
or `0x0044A1C0`. The installed strategy tables all point virtual+0x10 to
`0x0044A1F0`, which loads the PE scalar at `0x006CC98C`. Thus a port need
not obtain an injected cap for these strategies. The binding-cap experiment
uses an intentionally extreme synthetic catch-up scale solely to distinguish
the original minimum branch. It does not claim that such a scale occurs in a
race. Existing injected-cap cases remain as isolated branch tests.

Strategy1's initial-action method `0x0044DCE0` constructs action7 with table
`0x006C9374`. Strategies2/3 use `0x0044E260`, action8/table`0x006C920C`.
The common constructor `0x0044E2B0` initializes the action path, speed inputs,
flags, priority and lifetime through actual instructions. Numeric assertions
live in `test_real_strategy_selection_and_first_action_construction`.
Crucially, the constructed action has an empty path. Construction alone is
insufficient to drive a rival, and the publication fixture explicitly supplies
its own path after construction.

**E, lifetime bound.** A second scheduler-slice entry with both objects still
present reuses them without allocating or reconstructing either. Each actor
must retain its own strategy/action/driver histories between updates. The
slice assumes initialized nonempty allocator pools for construction; failure
handling and destruction are not replayed here. Do not treat an empty pool or
a null action as permission to invent a default action. A generation reset
still requires the real destruction/recreation call sites or an explicitly
separate port lifetime policy; history rebasing is not such a reset.

**E, longer input response.** The new publication witness uses the action's
original route-speed flag and original strategy cap, actual path installation,
resampling and setter. Its route byte and catch-up scale remain synthetic.
`test_changing_synthetic_pose_speed_and_long_path` separately runs original
path installation and complete driver updates on a curved polyline with
changing external pose, heading, velocity, scalar speed, target and world tick.
The mirrored path produces wrapped opposite steering; braking and acceleration
transitions retain previous driver state. These states are hand-specified,
not observed original frames or physics integration. No contact, overtaking,
collision solver or successful closed-loop driving is claimed.

## Initialized planner and executable actor contract

`controller293-v3` adds `ActorController(image, retail)` with retained
`pose(point_index, speed)` and `step(dt=0.02)` operations. It is an original-x86
research oracle implemented in Python/Unicorn, usable on the supported inputs;
it is not the production C++ RaceSession actor. The trace entry authenticates
the executable and all retail files before constructing it. A caller using the
class directly must use the same authenticated `Image` and `inputs(root)`.
The bounded instruction allowlist intentionally rejects other execution paths.

**S/E producer chain:**

| Input/object | Original producer executed |
| --- | --- |
| Route manager, geometry, links and preprocessing | `442B00`, `4434D0`, `442DD0`, `4430B0`; TRACKB1003 chunks34121..34124 and80034147 |
| Track policy and AI policy | GLOBALB chunks34201 and34B00; `442040`, track selector`4484F0`, AI loader`462070` |
| Stock240SX RideInfo and generated performance curve | `417600`, `57D160`, `57F2B0`; GLOBALB CarTypeInfo entry18 |
| Actor physical/state links | actor+20=RideInfo+60 from store`4255C8`; state constructor`464860`, called with actor+40/+540/+828 by`42538F..4253A4` |
| Self world object and pose publication | original type registration`577B00`/`577C30`, factory`57A1B0`, publication`57A380`; actor+82C store`425715` |
| Convex hull and local bounds | retail hull loader`472B00`, body constructor`469B00` through`469D1E`, including`472E40` and`468A50` |
| Controller, driver and histories | whole `4620C0` and `45DBB0` constructors; strategy/action dispatch`44B831..44B85D` |
| Selected route/context | route reset`44C250`, context update`44CF30` and self-world query`57A910` |
| Action/path lifecycle | whole action7 update`450210`, planner`45B050`, route path producer`45AE10`, original path copy`45032D` |
| Controls | publication`44C550..44C7F1`, cap`44A1F0`, setter`45E180`, driver`45E970` |

The context must select the actual published self entry and the action must
return a usable path. Empty context/path cannot report success. Initial action
and driver paths are empty from their constructors; tests verify transition to
generated paths, changing pose, retained histories, braking and the original
action refresh interval. `PlannerTests` owns frame/path/control pins. The old
`planner_boundary` case still demonstrates its deliberately incomplete fixture;
it is no longer the furthest recovered boundary.

The selected track policy was recovered after the higher-speed case accessed
`[736174]+B8` in `4628C7`. `4484F0` selects the matching track ID at record+6C
from the original loader's GLOBALB table. This input is retail data rather than
a manually chosen branch flag. Complete manifests confirm retail files remain
unchanged; mutable copies live only in emulator memory.

### Caller-owned scene inputs and limits

The adapter supplies one isolated role2 actor, controller3/physics3 selectors,
stock240SX, track1003, normal mode, lap0, grounded contact flags and no player or
rival catch-up target. It supplies the status records, initial speed scale and
zero RNG state explicitly. These are conditional scene choices, not observed
event0 values. Original RNG instructions subsequently advance retained state;
no random value or catch-up result is hooked. The no-reference catch-up branch
executes and returns its original scale. These are the v3 isolated cases;
the v4 paired case below exercises reference-dependent catch-up separately.

`pose` selects a real route1 sample for position, forward vector and progress;
velocity and signed/absolute speed are supplied. Original atan2 produces the
heading. The caller's original distance-accumulator slice`426A5C..426A95`
executes using supplied speed and dt. Each step advances the game clock at
4000ticks/second. Repeated/finitely jumped pose samples are deliberately
exogenous and do not constitute a physically consistent trajectory. A future
RaceSession caller must provide actual integrated pose/velocity/contact state
and preserve the controller's retained history; it cannot feed these fixture
poses into production as an AI implementation.

The returned snapshot contains raw steering word, gear command, throttle,
brake, handbrake and target point, together with generated path, selected route,
desired speed, action tick and input provenance. The same racer key and phase
contract described above still applies. There is no implemented RaceSession
hook and no claim that this Python object supplies one.

External service hooks are restricted to heap allocation/release and a disclosed
zero `QueryPerformanceCounter` with divisor1, separate from the supplied game
clock. This does not establish timer independence for other hull-query code.
Free-list storage, memory placement, SEH chain and empty graphics
registries are fixture infrastructure. Original scalar D3DX dispatch initializes
math operations. The body constructor stops before spatial world registration;
this is sufficient for the recovered local-bounds consumer, not a collision
world. No semantic AI, path, control or policy function is hooked.

`controller293_planner_ranges.py` freezes exact decoded instruction blocks.
`PlannerMachine` checks every instruction's start and length. Unexpected code,
NULL data accesses, exhausted budgets and non-finite outputs fail. A mapped
NULL page permits only the explicit FS:[0] SEH chain; this guard found missing
original state/type producers during recovery. Heap bounds also fail closed.
The legacy supplied-path evolving fixture now uses the same coherent clock/dt.

## Event metadata and paired original controller witness

**S/E.** `controller293_scene.PairedController` adds these original producers:

| Input or lifecycle | Executed producer |
| --- | --- |
| Event table and event0 setup | FrontB loader`5A00A0`, career reset core`5A0A26..5A0B30`, selection`5A0CE0`, copy`5A2350`, setup`5A2930` |
| Settings and explicit seed | settings reset`431DF0`, human count store`4B5E36`, career builder`4B5E61..4B602B`; world initialization core`41F7C9..41F891` derives world+2D0 and advances original RNG |
| Config metadata and grid | caller slices`4B6102..4B611C`, `4B41EB..4B4227`, `4B42FC..4B4321`; constructor`5809C0`; event grid swap`4B628B..4B6299` calling`4B6300` |
| Controller/physics selectors | role consumer`422ACA..422AEA`, physics selector`425150` |
| Player reference and history | registry slice`42B21E..42B22B`, history construction`42B557..42B5B8`, RealController`59BFC0`, update`59C210`, sampling`430758..4308D6` and`584270` |
| Race rows and initial route | marker loader`441ED0`, race status`434270`, role partition`41FE50`, route assignment`435D30`, row registration`434A80`, projection`443A10`, row refresh`4338C0`, sorting`434ED0` |
| Nearby human admission | original state`464860`, world object`57A1B0`/`57A380`, constructor active stores`4257F7..425805`, eligibility`57A300`, context query`57A910` |
| Catch-up and retained action | reference-dependent`44BD10`/`44C010`, original action flag/path update`450210`, publication and driver chain used by the isolated witness |

`SceneTests` owns the metadata, grid, seed, history, context and complete frame
pins. Native config construction preserves the event's IDs, roles, context and
mode bytes and final grid permutation. The original caller RideInfo argument
is explicitly replaced with the previously constructed stock240SX RideInfo;
resolved event presets/adaptive performance are not claimed. Four metadata
records are constructed, but only the human and first rival are instantiated
in this witness. It is not the event's complete field or traffic population.

The seed before settings reset is a declared input. Both settings and world
initialization execute the original RNG transforms. The world initialization
core executes before controller construction. The later resumed world-clock
and race-elapsed values are explicit snapshot inputs; no intervening simulation
is implied. Each subsequent call advances both clocks consistently with dt.

The player/view object referenced through`7361BC` is distinct from the human's
RealController. Its actor link and registry ownership are explicit bindings.
The race status and flow object are also distinct. Stage4 is an entry
condition; its transition has not been recovered here. Empty replay state is
constructed by`571FA0`. The actor linked list and config array publication are
fixture ownership; original code partitions roles and creates/ranks race rows.
The initial lap/progress projection is produced by that registration code.

The caller supplies coherent position, heading, tangent and velocity snapshots
through the shared `pose` method. Their time evolution is deliberately
nonphysical. Nearby human admission required recovering actor+4D0 from the
original constructor: `57A300` excludes inactive or actor+8A8-marked actors,
apart from the query's special self handling. No eligibility callback or
prebuilt context row is supplied. The tests exercise both a self-only context
and a context containing the nearby human, followed by original action refresh.
They do not establish general overtake or collision behavior.

Human history samples on its own original countdown. The fixture never forces
that countdown or the action catch-up flag to refresh. Race progress, rank and
human history reach the original catch-up comparator; resulting scales and
driver desired speeds vary. The action and driver retain their state across
the supplied call stream. Complete hooks, visited addresses, input metadata
and raw result frames remain in the report.

### Caller contract and remaining integration work

The executable research entry is `PairedController(image, retail, seed,
elapsed_ticks)`, followed by `step(human_point, human_speed)`. The explicit
snapshot fields and returned controls describe a conditional original-code
oracle, not a C++ or RaceSession hook. A production caller must replace route
sample snapshots with the same actor identity's integrated state, bind actual
resolved RideInfo/config/world ownership, provide original phase/clock/RNG
state and preserve histories across calls. It must not copy these fixture
poses, partial field or selected call cadence into gameplay.

The v4 exact instruction additions are frozen in
`controller293_scene_ranges.py`; the same boundary/length and NULL guards apply.
Source assertions include active admission, original seed storage, history
countdown and metadata stores. A modified admission branch is rejected in the
source guard tests. No semantic AI, path, catch-up, RNG or controls are hooked.

The initialized planner context and positive returned path are recovered for
these conditional scenes. Remaining work is complete scene ownership and
scheduler/action selection beyond the retained first action, actor lifecycle,
spatial registration, real collision/physics/progress
integration, and original correlated runtime comparisons. Missing prepared
fixtures alone do not establish an external blocker: these producer paths can
continue to be investigated from authenticated code and retail data. This
change establishes an executable original oracle and a concrete conditional
actor contract; it does not justify inventing a production AI approximation.

## Original event RideInfo and config ownership

**S/E.** `controller293_event_rides.py` removes the v4 stock RideInfo substitution
in a separate `event_scene` witness. The v4 stock scene remains a regression
case. `PairedController(..., event_rides=True)` selects the new witness.

| Producer | Original execution |
| --- | --- |
| Preset registry | `4B25B0` loads FrontB chunk`30220` into its original linked registry |
| Car types, variations and parts | whole loader`57C8B0` consumes GLOBAL chunks`34600`, `34601`, `80034602`; it publishes and relocates original tables in the mutable emulator copy |
| Player preset and performance | builder slice`4B6071..4B6102` derives visual seed fields, calls full preset lookup/apply`4B2640`/`4B26F0`, and executes the event0 level stores; original`57F2B0` rebuilds physical data/rating |
| Rival presets and adaptive tuning | caller slice`4B40A0..4B4197` initializes RideInfo, formats/hashes visual seed names and applies each event preset; `4B41EB..4B42BE` invokes full`57F700` and its original physical/rating rebuilds |
| Config copies | original`5809C0` calls`4323F0` to copy each resolved RideInfo into its own config record |
| Actor ownership | constructor source`425374..425382` derives config+10; original ID stores`425598` and RideInfo/physical stores`4255AB..4255CB` execute for both actors |
| Body inputs | the original hull loader`472B00` consumes each car's own GEOMETRY chunk`80034020`; original body constructor through`469D1E` consumes the corresponding actor RideInfo |

All four event preset RideInfo objects are constructed, including original
adaptive outputs for the three rivals. Only the player and first rival are
bound into the paired actor scene. `EventRideTests` owns their type IDs,
performance vectors, visual seed fields, physical/category/rating assertions,
opaque slot-binding hashes and complete control-frame pins. No tuning vector
is assigned by Python and no preset, part-selection, rating or adaptive helper
is replaced. Slots are processed by the original full preset loop, including
its special paint/vinyl/decal/shared-part branches and enabled-part updates.
The report hashes their resulting binding words without interpreting every
slot as the same kind of catalog pointer. Rendering/compositing is not tested.

The original caller's visual seed fields are distinct from the world/global
RNG state. The tested adaptive path runs with the declared original state;
its vectors are conditional outputs, not a universal event tuning table.
Config copying and lazy rebuild may relocate internal physical-data pointers,
so a raw physical-memory hash before copy need not equal one after copy.
The native actor stores bind actor+10 to config+10 and actor+20 to RideInfo+60;
the actor ID comes from that same config. Actual rebuilt category and curves
then feed the existing original planner, cap and driver consumers.

Preset diagnostics required two authenticated Windows pointer-probe imports,
`IsBadReadPtr` and `IsBadWritePtr`. Their explicit external services check the
emulator's mapped ranges and access flags; NULL data remains invalid. The
report records pointer, size and result for every executed probe. Native
diagnostic formatting remains original code. These services do not choose a
part, skip a preset branch or supply a physical/AI value. Existing heap and
external counter fixtures remain disclosed. The new exact execution blocks
are frozen in `controller293_event_rides_ranges.py`, under the same fail-closed
instruction and NULL guards. Source checks authenticate the import names and
IAT entries as well as preset stores, performance stores and actor ownership.

This closes the stock-car substitution for the new paired witness. It does
not close complete event actor/world construction: only two actors run, body
spatial registration and physics phases remain outside this oracle, poses and
resumed clocks are supplied, and flow-stage entry remains conditional. The
complete new-career/profile/options lifecycle, full field/traffic and actual
integrated original-game trajectory are still required for full parity.

## Unmet original and hardware acceptance

No original game was launched, attached, patched or written. **R unavailable**:
there is no identified event0 profile/input recipe and live original control
trace for these scenarios. A later authorized capture must correlate actor ID,
config and resolved RideInfo, context/category, action type/strategy cap,
path points and selected line/lane, RNG state, world tick/dt, driver history,
entry/exit controls and subsequent physics/contact/progress by call identity.
Include straight/corner/braking, no-contact overtakes, contact, recovery,
pause and restart. Preserve the distinction between frozen-input E fixtures
and evolving original trajectories. That trace is needed for runtime parity. Static producer recovery and actor
integration may continue independently; #63's project path follower is not
evidence for the missing original behavior.

Issue293 remains open for action/planner/difficulty research, independent
review and original comparisons; #258 retains actor integration and full-race
hardware acceptance. No Switch operation was performed. Research gates here
do not validate inherited production changes or waive scene-height540,
gpu-boost on and actual-clock measurements in the later hardware gates.

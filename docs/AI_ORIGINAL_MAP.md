# Original rival AI function map

Scope is the US PC 1.4 executable and the single-player campaign. Addresses are
32-bit virtual addresses from speed.exe, SHA-256
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
The [event matrix](AI_CAMPAIGN_EVENTS.md) enumerates all campaign events and
tournament rounds from authenticated data. The round manifest was checked
without driving. 4B41EB..4B4227 was executed separately on every physical
rival row on all three difficulties, confirming the decoded context and
behavior mode for the full set.

## Decision graph

~~~mermaid
flowchart TD
    Event["5A00A0 / 4B5DF0: connect event, round, track, difficulty"] --> Setup["4B41EB: roster, context, action mode, tuning selector"]
    Event --> Rules["78A400/404: comparison and rank weight; 78A300: traffic; 78A40C: throttle"]
    Setup --> Choice{"Mode and entry"}
    Choice -->|physical, adaptive| Adapt["57F700: player rating to rival parts and ratings"]
    Choice -->|physical, fixed| Fixed["57F970: authored setup selector"]
    Choice -->|drift| Drift["43DD40: score entrants, RNG factors, performance normalization"]
    Choice -->|time trial| Solo["No rival racer controllers"]
    Drift --> Zone["43EB00: score on type-6 zone and lap visit"]
    Adapt --> Spawn["434A80/4333C0: actor and standings registration"]
    Fixed --> Spawn
    Rules --> Manager["598600: traffic admission, relocation and intersection maintenance"]
    Spawn --> World["420B40: gameplay world phase"]
    Spawn --> TrafficGrid["422B36 / 598BF0: initial traffic placement"]
    TrafficGrid --> Appearance["59B590 / 4216B0: shared world appearance draw"]
    TrafficGrid --> FilmChoice["421F00/422EB0/47BB10: choose road start film on shared world RNG"]
    FilmChoice --> World
    World --> Manager
    World --> GridPedals["434B50/45DB80: held pedals; publish open throttle at GO"]
    Manager --> Registry["41FE50/41F680/41F690: indexed role-3 registry"]
    Manager --> Cadence["598440/59AE60: initial cursor and mode interval"]
    World --> SceneCar["47C310/47C580: bind and animate intro cars with physics selector 0"]
    SceneCar --> SceneGear["44B3E3/426110/46D436: reattach selector 1 and sample gearbox RNG"]
    SceneGear --> Rivals
    SceneCar --> SceneReturn["47C9E0/435D30: return animated cars to simulation and restore roads"]
    SceneReturn --> Rivals
    World --> TrafficScene["47F0A7/47F187: park and restore the retained traffic pool"]
    TrafficScene --> Cadence
    Registry --> TrafficSweep["5988C0: pending, path validity and racing proximity"]
    Cadence --> TrafficSweep
    TrafficSweep --> Placement["599830: authored or fallback placement"]
    Placement --> Appearance
    Placement --> TrafficDirection["5994D6/59A890 -> 583250: store adopted direction and update direction object"]
    TrafficDirection --> PaceGate["582F50/598A50: direction and published speed comparisons"]
    PaceGate --> TrafficSweep
    Placement --> Rivals["420C8A: role 2 controllers in actor order"]
    Rivals --> Timer["44B1B0: launch, recovery and physics-model timers"]
    Timer -->|drag launch hold or rev| Grid["44B27E/44B2E8: send launch command and heading target, return"]
    Grid --> Other
    Timer -->|ordinary before racing, 78A368 unset| Hold["44B344: hold command, heading and support timer, then return"]
    Hold --> Other
    Timer --> Schedule{"44B56D: initial action or scheduled update?"}
    Schedule -->|full| Road["450210/44F070: authored road category; road links and neighbor input"]
    Schedule -->|blocking or human-side refresh| Refresh["44B707: scoped refresh"]
    Schedule -->|retained| Other
    Road --> Joins["458BD0/462840: branch direction, cache, TrackInfo+B8 and 435CA0"]
    Joins --> Owner["44B94E/44F700/44BC30: update or replace owned action"]
    Owner --> Strategy{"Strategy query order"}
    Strategy -->|ordinary| Ordinary["44D8B0: recovery, obstacle, contact, traffic, passing, rollover, blocking, following"]
    Strategy -->|drag| Drag["462430/44B1B0: drag controller and lane/obstacle actions"]
    Ordinary --> Path["45AD30/45AE10/45B150/45BAE0: admit, extend, choose paths"]
    Drag --> Path
    Refresh --> Path
    Path --> Publish["45FAF0/459980/459870: publish and resample selected path"]
    Rules --> Catchup
    Publish --> Catchup["44BD10/44C010: ordinary catch-up; drag bypasses it"]
    Catchup --> Speed["44C550: speed, jitter, duration and action flags"]
    Speed --> Model["44B060: select the eligible far-rival path body"]
    Model -->|new or refreshed path| Spline["463B30/463910: resample and project to spline"]
    Spline --> Cap["456830/4638A0: gearing limit, drag and slope"]
    Cap --> Other
    Model --> Other["420CCA/420D0A: traffic controllers, then human controllers"]
    Other --> Drivers["420DA7: all drivers; 45E370 steer, 45FC90 pedals"]
    Rules --> PedalCap["422523: event selector to driver+1FC"]
    PedalCap --> Drivers
    GridPedals --> Drivers
    Drag --> GearBounds["476D6C/476E0D: drag shift-error bounds"]
    Drivers --> Physics["4267A0 + collision dispatch + 426930: body and progress publication"]
    Physics --> Gear["46D760: full-physics gearboxes share the world RNG"]
    GearBounds --> Gear
    Physics -->|path body| Departure["463D50/463F20: predict departure, arm timer, publish motion"]
    Departure --> Standings
    Physics --> Standings["435570: updated places, completed racers, finish transitions"]
    Standings --> World
    Standings --> Finish["4350D0: finish and results"]
~~~

A full controller update, a path refresh, the driver step and a body step
are different boundaries. A driver still reads its retained command between
full decisions. A rival that wins a road/path query changes the world seen
by the next controller in the same phase. Approximate scheduling can change
both passing and which actor consumes a given RNG value.

## Function-to-port crosswalk

"Connected" describes the production call site. A native fixture can prove
a function over supplied inputs while the connected call still obtains a
different input from its upstream producer.

| Boundary in speed.exe | Input, decision and output | Port and evidence |
| --- | --- | --- |
| 4B5DF0, 4B5F47, 422410 | Install round, traffic density, catch-up comparison flag and rank weight, pedal selector. Rank weight does not disable the distance/history terms. | Connected in [race_run.cpp](../src/race_run.cpp) and [race_events.cpp](../src/race_events.cpp). All playable round identities agree with the executable's retail input table. |
| 4B41EB, 4B420B | Read authored signed difficulty selector for that rival, clamp invalid to -1, clamp action mode outside 0..2 to mode 1, route tuning selector to the matching producer. | Connected in [event0_setup.cpp](../src/event0_setup.cpp) and [intro_grid.cpp](../src/intro_grid.cpp). |
| 57F2B0, 57F700, 57F970 | Build acceleration rating and performance category from the ride. Adaptive rivals use human tune/rating as reference and keep their own model, fixed rivals take authored package selector. | [race_adaptive.inc](../src/race_adaptive.inc), [intro_grid.cpp](../src/intro_grid.cpp). [race_difficulty_native.py](../tools/race_difficulty_native.py) and [career_fixed_native.py](../tools/career_fixed_native.py) compare retail routines with the port. An additional bounded comparison covered all three event 19 rival models against Tiburon, Miata, Civic and Golf with packages 0, 1 and 3. All 36 cases agreed on selected parts, ratings and three aero values. |
| 4B3C60, 4B4590, 4B4380, 57D860 | Quick Race separately chooses roster, difficulty context distribution, adaptive setup and cosmetics. The campaign uses authored entries. | [QUICK_RACE_RIVALS.md](QUICK_RACE_RIVALS.md) records the bounded extraction and remaining presentation/RNG limits. |
| 422A70, 422410, 425350, 41FE50 | Create actors in final grid order; partition the live registry by role while retaining order inside each group. Actor ID, grid slot, role range index and standings rank are separate. | [intro_grid.cpp](../src/intro_grid.cpp) traverses final grid slots. [EVENT0_ACTORS_258.md](EVENT0_ACTORS_258.md) documents the original constructor and scheduler witnesses. |
| 431DF0, 41F7C9..41F891 | Initialize race settings from the pre-race generator and derive WORLD+2D0 before creating controllers. | [controller293_scene.py](../tools/controller293_scene.py) executes original transforms from a declared initial seed. The production port uses its own deterministic race seed. Restoring the transform alone does not establish the pre-race seed. |
| 420C8A..420E05, 4216F0 | Manager first, rival/traffic/human controllers, all drivers, then preparation and contact; integer and float consumers share world RNG. | [EVENT0_ACTORS_258.md](EVENT0_ACTORS_258.md) documents the native calls. [race_run.cpp](../src/race_run.cpp) places ordinary traffic admission before plans. The road NIS now keeps a separate pre-film world phase, parks traffic motion during the film, and enables relocation after authored scene retirement. Source call coverage still differs across actor and audio owners. |
| 44B56D..44B70F | Full on missing action or when configured actor slot matches the signed frame cadence and at least 0.2 s have elapsed. Branches for blocking and retained target refresh do not run the factory. 44B754 suppresses both partial publications for selector 4. | [ai_strategy.cpp](../src/ai_strategy.cpp), [intro_ai.cpp](../src/intro_ai.cpp). [ai_controller_cadence_vectors.py](../tools/ai_controller_cadence_vectors.py) supplies original states. |
| 442B00, 442DD0, 4430B0, 44CF30 | Load TrackB roads, joins, intersections and placement rows; project line and join progress; locate the current road context. | [racing_line.cpp](../src/racing_line.cpp), [ai_road.cpp](../src/ai_road.cpp), [ai_traffic_intersection.cpp](../src/ai_traffic_intersection.cpp). [CONTROLLER_293.md](CONTROLLER_293.md) records native input boundaries, and the event matrix counts the rows by course. |
| 458BD0, 458CC0, 462840, 435CA0 | Use direction/type/cache and TrackInfo+B8 before probabilistic joins. -1 uses the elapsed-from-start predicate. A literal value uses the race start clock. 435CA0 disables the hold in drag. | [ai_road.cpp](../src/ai_road.cpp), [ai_recovery.cpp](../src/ai_recovery.cpp), [race_events.cpp](../src/race_events.cpp), [intro_ai.cpp](../src/intro_ai.cpp). Both race modes forward TrackInfo+B8 and the elapsed time of the published race start. |
| 450210, 44F070, 45AD30, 456780 | Ride category selects road columns; retain road chain/category as policy permits; validate current target, lane width and lateral clearance before admission. | [intro_ai.cpp](../src/intro_ai.cpp), [ai_road.cpp](../src/ai_road.cpp), [ai_path_choice.cpp](../src/ai_path_choice.cpp). Native fixtures cover selected road and admission cases. TrackB 34121/34122 provide the line and direction-specific links. |
| 4576B0, 45A1E0, 459710, 45A5B0 | Predict from published pose/velocity and lane/action state; evaluate other bodies along the candidate geometry. | [ai_path_choice.cpp](../src/ai_path_choice.cpp) and [ai_neighbors.cpp](../src/ai_neighbors.cpp). Collision/ranking primitives are connected, but their live input can diverge with world phase and state publication. |
| 44B94E, 44F700, 44BC30 | Complete/abort predicates, explicit action request versus automatic candidate, clean up and retain current action at equal priority. | [ai_action.cpp](../src/ai_action.cpp), [ai_rival_action.cpp](../src/ai_rival_action.cpp). [AI_RIVAL_ACTION_RUNTIME.md](AI_RIVAL_ACTION_RUNTIME.md) describes the connected owner and fixture boundaries. |
| 44D8B0, 44C2E0 | Lazy ordinary strategy. Rollover/wall/recovery/contact state competes with passing, blocking, following, and route/neighbor availability. Target reactions consume the shared random stream even with a busy timer. | [ai_strategy.cpp](../src/ai_strategy.cpp), [ai_rival_action.cpp](../src/ai_rival_action.cpp), with [ai_strategy_vectors.py](../tools/ai_strategy_vectors.py) and [ai_reaction_vectors.py](../tools/ai_reaction_vectors.py). |
| 45AE10, 45B150, 45BAE0, 45C550 | Rebuild or retain/extend path; select among neighbors for passing; following searches one target; lateral offset is per sample, not per wall-clock second. | [ai_action.cpp](../src/ai_action.cpp), [ai_path_choice.cpp](../src/ai_path_choice.cpp), [ai_rival_action.cpp](../src/ai_rival_action.cpp). Native-side helpers do not prove the entire input geometry. |
| 450730, 57B480, 4508F0, 450A70 (Xbox 59FD0, 136A60, 5A1E0, 543B0) | Following compares the signed lateral difference in the active road category. An absent target immediately satisfies abort. Completion has its own timer and can select another target. | The original action owner executes both predicates. The production controller and its next action are exercised in `tests/ai_strategy_tests.cpp`. |
| 45FAF0, 459980, 459870, 583F20 | Action path publication, arc-length resampling and cursor invalidation; preserve metadata and publish empty paths when appropriate. | [ai_driver.cpp](../src/ai_driver.cpp), [intro_ai.cpp](../src/intro_ai.cpp). Boundary tests compare captured polyline and exact generated cases. |
| 430778, 43080A, 584270 | Human speed/road-speed ratio producer and ten-entry history, with a time gate. | [ai_catchup.cpp](../src/ai_catchup.cpp), [intro_ai.cpp](../src/intro_ai.cpp), native history fixtures. Port road update and history consumer do not run in a fully identical world clock. |
| 44BD10, 44C010, 4B562D | Compare unwrapped distance, rank and relative speed, then apply speed history. Quick Race Catch Up Off bypasses the distance/rank comparison. Selector 4 uses forward speed; selectors 1/3 use magnitude. A local player view skips the refresh; finishing by itself does not. Drag/special contexts have separate gates. Physical effect is max(1, scale) while target speed can fall below the base. | [ai_catchup.cpp](../src/ai_catchup.cpp), [intro_ai.cpp](../src/intro_ai.cpp), [AI_DIFFICULTY.md](AI_DIFFICULTY.md). See the catch-up unit tests and the Quick Race retail Sprint. |
| 44C550, 44A1F0, 45E370, 45FC90 | Publish explicit action limit or authored road target with context, point offset and catch-up. The driver steers and regulates the pedals against the target speed. | [ai_action.cpp](../src/ai_action.cpp), [ai_driver.cpp](../src/ai_driver.cpp), [race_session.cpp](../src/race_session.cpp). |
| 4B5F73, 4B5951, 422523, 434B50, 45F745, 45FC90 | Copy the campaign event selector or the Quick Drag difficulty selector to driver+1FC. While the grid holds, throttle and brake are both full. On GO the race stage setter releases both brakes and publishes full throttle. The subsequent moving update clamps to driver+1FC. | [race_run.cpp](../src/race_run.cpp), [race_session.cpp](../src/race_session.cpp), [ai_driver.cpp](../src/ai_driver.cpp). Native execution covers all three selectors in both modes and all three Quick Drag difficulties. |
| 47C310, 47C580, 427C50, 47C9E0, 44B3E3, 426110, 46D436 | Attach intro car tracks by slot, suspend physics during animation and return the car to simulation. Reattaching selector 1 constructs a gearbox and consumes two shared RNG draws. | [start_crowd.cpp](../src/start_crowd.cpp) reads the car slot mask and the authored half-speed release pose. [race_session.cpp](../src/race_session.cpp) publishes the grounded release pose and includes the player gearbox's two draws when it restores selector 1. The restored boxes publish zero shift offsets in neutral. Road scenes still render the prepared grid before the handoff. |
| 44F6C5, 450393, 462B30, 44C7FB, 45F909 | The action updates its nitrous request only on specific branches. Following checks an ahead target's relative speed. Avoidance checks an urgent obstruction. Full speed publication reads the retained request until the action replaces it or ends. A partial update that only publishes a path cannot start a burst. | See `src/ai_rival_action.cpp`, `src/intro_ai.cpp` and `src/race_session.cpp`. The resource still applies support, wall, slip, gear and RPM gates. Whole-race burst timing remains under comparison. |
| 44B060, 44BA05, 425150/426110, 463B30/463910/463F20 | Select far-body model 4 at a full controller decision when timers, speed >=5 m/s, support, action, context, race age >=30 s, active-anchor distance >=120 m and standings allow. Move through the published path and publish displacement-derived velocity. | [race_session.cpp](../src/race_session.cpp), [ai_track.cpp](../src/ai_track.cpp). The port now uses the pre-physics human-car anchor for the 120 m gate rather than the chase camera. Additional path validity and fallback rules remain. Continuous equality is open. |
| 44B1FF..44B238, 4216F0, 435F80 | Drag samples a launch offset on the first controller call. Negative lead can publish a rev target in countdown. Nonnegative offset can suspend the speed command after GO, with the command disabled and the driver applying neither pedal. The stage gate also defers path selection before GO. | [intro_ai.cpp](../src/intro_ai.cpp). The port takes the draw after traffic staging and enters path selection on the first eligible frame. The two branches return before action work. [ai_rival_path_gates_native.py](../tools/ai_rival_path_gates_native.py) executes the original checks. The archive for event 9 confirms the empty rival paths before GO and a published path on the first active update. |
| 476D6C, 476E0D, 46D760 | Full physics gearboxes redraw on the original gear state transitions using shared RNG. Drag takes shift error bounds from the context. Ordinary races use the standard bounds. | [player_powertrain.cpp](../src/player_powertrain.cpp), [race_session.cpp](../src/race_session.cpp), [drag_world_rng_native.py](../tools/drag_world_rng_native.py). Initial seed and global call schedule remain open. |
| 4B3B20, 57D000, 57CFA0, 564A80, 4B3B7D | Build the traffic model roster from weighted authored models, duplicate retry and one additional skin draw on the global generator. | [traffic_model.cpp](../src/traffic_model.cpp), [intro_grid.cpp](../src/intro_grid.cpp), [TRAFFIC_MODEL_SELECTION.md](TRAFFIC_MODEL_SELECTION.md). The original draws match at equal supplied seed. The port's initial seed and caller lifetime differ. |
| 5992F0, 59A7C0, 59B5D0, 59B590, 57D100, 4216B0, 57D120 | Traffic construction and successful relocation both select an appearance on the shared world RNG. The relocation draws even if the result equals the current appearance. A zero variant count bypasses the draw. | [race_traffic.cpp](../src/race_traffic.cpp) restores the draw after successful placement. The selected variant is retained with the live traffic actor. The current renderer still uses its preloaded traffic appearance. |
| 598600, 59AE60, 5988C0, 599830 | Manager starts ordinary/drag relocation sweeps at 1.0/0.3 s with up to 1/22 admissions. It scans eligible actors and branches on drag for placement. Intersections and controller actions have separate owners. | [race_traffic.cpp](../src/race_traffic.cpp), [race_traffic_actions.inc](../src/race_traffic_actions.inc). Production drag continuation still uses one slot at 60-step intervals. Authored drag start placement is a separate boundary. |
| 434A80, 4333C0, 4338C0, 435570, 4350D0 | Register race status rows, write progress, sort previous order, assign places, finish/retire racers. Lifecycle affects subsequent neighbors, actions and tournament rank. | [race_timing.cpp](../src/race_timing.cpp), [race_session.cpp](../src/race_session.cpp). 435730/435740 remove a racer in knockout. The remaining actors use ordinary controllers with a shrinking active roster. |
| 43DD40, 43E04C, 43EB00, 42AC60 | Drift entrants initialize RNG factors and normalized performance factor. Type-6 zone visits use highest lap, course direction, first/later lap table, vehicle grade and event scalar. | [race_drift.cpp](../src/race_drift.cpp), [DRIFT_SCORING.md](DRIFT_SCORING.md), [original_feedback_native.py](../tools/original_feedback_native.py). The port uses supplied human acceleration rating for the performance scale. Native builds the active actor standings first and keeps the lowest category and highest normalized acceleration rating from that scan. It then creates virtual opponents using that minimum category; their display models do not contribute ratings to the first scan. The ordinary one-human case therefore reduces to that human's values. The port owns the initial seed. |

## How a road fork is picked

1. 458BD0 rejects the wrong direction and type 3. It looks up the link index
   in an eight-slot decision cache before invoking the policy.
2. 462840 accepts the mandatory type 2 and the human-only type 8 branch.
   Disabled links and types 5/7 are rejected. The type-9 obstacle goes
   through its separate availability path.
3. Other eligible links apply the signed time policy at TrackInfo+B8. With
   -1 the 435CA0 predicate uses elapsed time and actor identity. With a
   literal threshold, 462932 compares the published race clock and
   requires it to be strictly greater.
4. An eligible type 4 has the explicit half-range test. Other probabilistic
   branches read their cutoffs from the actor's context row. The draw
   advances WORLD+2D0. Re-reading a cached decision consumes no new draw.
5. Accepted marker type 2 can retain the target speed marker (4629E0).
   458CC0 invalidates the consumed road decisions as progress advances.

These are racing-line joins. Passing and following can choose an adjacent
maneuver even when the underlying road node is unchanged. The path solver
has a third responsibility after either decision: moving a body along the
published path when the current physics selector accepts it.

## Action ownership

Ordinary probing starts at the top. The priority comparison is an additional
gate after candidate admission and may preserve an existing action. The port
also has separate drag, finish and wall-follow handling.

| Action | Original meaning and key condition |
| ---: | --- |
| 0 | Follow one admitted target. Build its path using 45BAE0's ordered search. |
| 1 | Passing. Validate target, room, side geometry and cooldown. 45B150 resolves candidates across neighbors. |
| 2 | Special traffic response. 452B40 tests target strategy virtual 0C. Normal traffic policy 4 denies this forcing branch; policy 5 or special context can admit it. |
| 4 | Fallback when no workable action/path can be used; includes immediate preceding-racer speed comparison. |
| 5 | Blocking. Authored mode controls its allowed priority and length. Path may refresh without a new full decision. |
| 7 | Stopped/default action record; can reopen a forward path. |
| 8 | Drag forward lane/track path owner. |
| 10 | Recovery from stuck or obstructed motion. Checks contact orientation, movement and target, with separate cleanup. |
| 12 | Physical contact action, admitted from stored pair endpoints and target state. |
| 14 | Rollover; uses chassis orientation and support, not a generic track-speed penalty. |
| 16 | Follow a wall escape path and release when wall contact ends. |
| 17 | Finish action. Retires a racer from active avoidance when race status makes the body a permanent ghost. |
| 19 | Track obstruction. Uses authored type-9 links and live rail status when bound. |

Excluded kinds in 44B060 are 9, 10, 11, 12 and 14. Seeing a kind in a gate is
not evidence that a campaign owner naturally creates that action. Likewise,
the existence of an original address for a helper is not evidence of
matching connected delivery to it.

### Strategy order and priority

| Query order | Ordinary strategy | Drag strategy |
| ---: | --- | --- |
| 1 | Recovery 10 (500) | Recovery 10 (500) |
| 2 | Obstacle 19 (600) | Obstacle 19 (600) |
| 3 | Contact 12 (400) | Wall-contact 16 (300) |
| 4 | Special traffic 2 (400) | Contact 12 (400) |
| 5 | Passing 1 (300) | Special traffic 2 (400) |
| 6 | Rollover 14 (500) | Default drag path 8 (0) |
| 7 | Blocking 5 (from mode table) | |
| 8 | Follow 0 (200) | |
| Default | Stopped/default 7 (0) | |

These numbers are factory candidate priorities, not a sorted evaluation list.
Recovery is conditional. A found candidate may not preempt: at equal
priority the owner keeps the running action and its timer. Action 4 is an
additional fallback when publication or path state cannot be used.
[ai_strategy_tests.cpp](../tests/ai_strategy_tests.cpp) checks the native
drag and ordinary order under supplied query outcomes.

## Recovered selector-4 caller behavior

PC 420B8B..420C09 copies the human actor position array into WORLD+280
before either controller dispatch or physical advancement. Empty slots
receive the sentinel encoded by 4B18967F. 421400 finds the nearest populated
distance. For one human, the active anchor is the human car, independent
of the camera.

44B754 skips the blocking and human-side partial path publications if
actor+28 is 4. The full decision uses the ordinary cadence and runs
44B060 at 44BA05. A native branch witness executed 44B754 with four body
selectors and seven current action types, using observation endpoints
without replacing its decision instructions. Only selector 4 took the
retained path in all cases. The same witness also executes the 44B060
and 421400 admission thresholds without replacing any callee. It exercises
the 462840/435CA0 opening-gate boundary as well. Run
[ai_rival_path_gates_native.py](../tools/ai_rival_path_gates_native.py) with
a Python installation that has Unicorn. The port applies this gate before creating
the partial plan in intro_ai.cpp. The distance gate in race_session.cpp
uses the snapshotted human actor position. The distance condition is
checked at the full controller decision and uses the race clock.

This admission correction alone does not reproduce the entire selector4
implementation. The port retains input validation on path count and
driver state. The later retained-body implementation below removes the
extra offset, displacement and yaw rejection guards. 44B060 also reads
native actor+828 independently from its two controller timers. 425398
passes its address into464860, and42543D initializes it to zero. The
placement and prepared-force section records its connected wheel-query
producer and tested lifetime boundaries.

### Path-body functions after admission

463B30 copies points from the Driver path after 459980 resamples it and
initializes the spline with 59CB20. 463910 uses 59D430/59D540 for nearest
point and parameter and 463800 for pose/offset. Internal speed and
acceleration use a path-point conversion. The target uses driver virtual
70 and the ramp duration uses virtual 74, with 0.5 s for nonpositive
duration. The 456830 cap reads normalized speed (speed divided by the
physical catch-up factor), the acceleration curve, drafting/drag and a
slope contribution from 4638A0 (negative gravity times the normalized
spline tangent Z). 581C5A publishes RideInfo+42C, the speed at the end
of the curve generator's gear scan. At or above that normalized speed,
456830 limits drive acceleration to the drag term if the curve would
otherwise exceed it. 463910 initializes from the published signed forward
speed (+3D0). It uses the acceleration cap first and the -20 m/s2
floor in the other branch.

The port now consumes these inputs in ai_track.cpp, including the gear
limit from the shared acceleration curve generator in ai_powertrain.cpp.
The old port evaluated drag at the unscaled speed and omitted the slope. Native 456830/4638A0 ran with uphill, flat and
downhill splines, two catch-up scales, drafting on/off and both gear
limit branches. The native witness validates the formula with supplied
spline and handling state. The 59D350 native interior point and tangent
match the port's cubic on the non-linear supplied open spline. The port
resamples the driver path again before projecting the actor. The nearest
parameter and lateral/vertical blending continue to use port adapters.

463D50 samples at one tenth through nine tenths of the supplied horizon.
For a 0.5 s horizon this is 0.05 s through 0.45 s. It compares the next
spline height to the projected vertical trajectory using tangent velocity
and the handling/gravity term. In 463F20 a report above
1.0 arms the original controller's +424 timer for one second. The native
threshold witness checked below, at and above 1.0 with a supplied
prediction value. 44B4E8..44B51C drops the path body at the next
controller pass with that timer active. 4645D0 feeds 59D350 the parameter
from retained speed, acceleration and time. The actual update advances
pose, wheel and driveline channels and publishes displacement-derived
linear velocity at 4642C6. Additional wheel updates at 46458A depend
on actor flags and a separate 200 m human-anchor threshold.

A native run of 463D50 produced zero on the flat examples and generated
a takeoff report when the spline dropped away. The port now evaluates the
same nine time offsets and retains the path mover until the next decision
after a reported departure. Verification uses the original path-body
fixture; the recorded event19 opening does not exercise this branch.
The later retained-body witness covers heading, slip and ground placement.
Native wheel visual updates within200m and the complete downstream
collision lifecycle remain outside that bounded witness.

## Drag caller and the third round of event 9

44B240/435F80 splits the drag launch. During the negative lead window,
44B27E sends a rev command and publishes a heading target through 44BCB0.
A positive delay takes the separate disabled command at 44B2E8. Both return
before evaluating roads or actions. Outside those windows 44B3F0 can still
defer strategy selection until GO. The archived event 9 uses that gate.
Its three driver paths are empty during countdown and present on the first
moving update.

Draws now occur at the first eligible controller call after grid traffic
placement. The first active controller decision initializes the rival's
authored starting road and nearest lane even if the human's drag controller
already ran. The third-round regression test still requires all three rival
paths to finish with traffic disabled. Matching a runnable path does not
establish native trajectories from end to end.

## Pre-race gearbox draws in the event 19 capture

421F00 constructs the world and invokes 422A70 to place racers and traffic.
The mode-1, event-1 dispatch enters 422EB0 and calls 47BB10 from 422F7F.
47BD0E passes the authored playlist count to 4216B0. It uses the incoming
world seed to choose a scene for the racer count and player grid slot, and
advances the seed before the first 420C8A controller promotion. Each ordinary
rival promoted from path physics to full physics then initializes its gearbox.
The first observable prerace world poll can precede or follow those promotions.
The road start path in RaceRun now places the grid and traffic, draws the
scene on this world RNG, and loads its authored timing before starting the
drivers. The retail start-camera check covers scene choice and outgoing seed.

47C310 binds the authored ZPM_Car tracks to indexed race cars. 47C580
temporarily selects physics 0 before applying the animated pose. During
the following controller phase, 44B3E3 restores selector 1 and 46D436 calls
46D760 while constructing its gearbox. The captured ordinary rivals each
consume two world RNG values during every observed replacement. Retaining
an existing selector-1 body consumes none at that boundary.

The archived selector transitions and seeds agree with separate execution
of 46D760 for each replacement. 47C9E0 restores simulation at the end of the
car scene and publishes the starting road. During this capture there are
replacements on the first two countdown updates as well as in prerace.
The raw globals identify scene 2303, whose four animated car slots map
to WORLD+28 indices 0 through 3. The authored GO-minus-release interval
is 138 frames at 30 Hz, matching the recorded 4.6 s countdown. The
recorded opening itself spent roughly 2 s in stage 0 with moving traffic
and 2.5 s in stage 2 with traffic parked. Those two intervals must be
accounted for separately in a replay.
The port retains its prepared rival bodies across these phases. Arrival at
GO with a different RNG state can change later traffic placement and
the rival's route and speed decisions.

After GO, the event 19 capture also shows the first application of first
gear taking two draws per full-physics body in the order Golf, player,
240SX, Peugeot. Executing 46D760 against the recorded gearbox bounds and
incoming seed reproduces both stored RPM offsets and the outgoing seed
for every observed full-physics change up to the first braking zone.
The port now connects ordinary full-physics boxes to the race RNG,
using the same path as drag.

## Human steering and the first passing decision

The player's future position is one input to the rival passing planner.
460B69 reads the last admitted contact tick stored by the collision body
at +14C. 467F8B and 4681E8 stamp it before contact solving. Using the last
gear change instead attenuated the player's steering in an empty lane and
could make the rival choose a different route. See STEERING_RECOVERY.md for
the dispatch, world tick wrap and contact classes. The event 19 differential
in docs/checkpoints/root-ai-audit-r99.md distinguishes player input from
the earlier independent 240SX body divergence.

## Rival body heading after a physics change

The driver reads the last published Actor+3B4 before steering. The two
physics components write it differently. At 478AA0 the full body truncates
the two forward-axis components to fixed point and uses the integer angle
lookup. At 4747F6..474806 the reduced body passes the original float
components to 564DB0. Use the actual component's publication even when the
world position and linear velocity are identical. The path-following body
has its own published heading. The reduced-body publication is exercised
by nfsu_ai_track_tests.

In the reduced body's wheel feedback, 475D00 calls 5ABDAF twice per
wheel. The recorded PC dispatch at 5EBCE3 rotates the lever arm and the
force with the same packed multiply. It adds X and Y, then adds Z plus
the translation row (zero for these vectors). Scalar fallback 5ABDB5
groups the terms differently. tools/ai_powertrain_vectors.py runs the
packed target on the original executable to generate the inputs and
expected results checked by nfsu_ai_powertrain_tests. See the R56
comparison in the AI checkpoint for the effect on the race.

In the full body wheel update, 4778B0..477951 routes service brake per
axle. On the start grid the rear service brake is zero. On the road,
simultaneous throttle above 0.8 and brake above 0.5 below 10 mph uses
signed speed times 0.05 on the rear axle. The normal bias applies outside
that branch. The original also reads a separate timed front-brake latch.
The rear-axle routing is covered by nfsu_player_powertrain_tests and the
240SX countdown in the event 19 PC capture.

On a breakable scenery hit the common solver can request a second pass
after the object starts moving. At 46C601 it branches to 46BB94, past
initialization of the two contact arms. Both bodies keep the arms from the
first pass, including the shortened arm of an upright fast car. This affects
both the contact-point velocities and rotational inertia in the repeated
solve. The two-pass PC example is checked by nfsu_car_contact_tests.

## Traffic relocation seen by the rival

41F8C0 creates the manager. 598440 sets cursor +4 to zero and enables
the manager at +8 after initializing its interval via 59AE60. The first
nonempty sweep therefore starts at slot 1, before it wraps to slot 0.
598600 decrements timer +0 in race stage 4, only when traffic is enabled,
the role-3 registry is nonempty and at least one human is registered.
Each sweep resets to 1.0 s with one admission, or 0.3 s with up to 22
admissions in drag. The interval is replaced, so elapsed overshoot is
discarded. It scans the role-3 partition via 41F680/41F690 and advances
its cursor even over
ineligible slots. The admission budget counts accepted candidates even
if their placement fails. Failure invokes 5832A0 and sets the controller
pending byte +4D1 so the slot can be considered in a later sweep.
5832A0 parks the body, while path exhaustion and unsuccessful road
continuation can set the same controller byte at 583467/5834A6.
The successful reactivation tail at 583772 clears it. This allows an
original indexed actor to remain in the scan while its body is parked.

- 44D650 dispatches strategy virtual +0C through controller+3FC.
  The TrafficController constructor chooses policy 4 via 583230/44AA10.
  Its 6C97D4 vtable uses 5314D0 at +0C, which returns false.
- When that virtual returns true, 5988C0 can take the +4D0 and 598B60
  speed/distance shortcut. Otherwise that arm admits a pending actor or
  one outside the 250 m participant window (59AC80 and 421550).
- The normal policy-4 false arm evaluates 598A50 (relative direction
  and speed to the selected human) and 59AC20 (stage-4 proximity veto).
  A pending actor still qualifies. The value read by 421960 here is the
  race status from 73619C, separate from physical-body selector 4.
  59AD90 compares published participant progress or the selected human
  under the native network conditions. The 250 m threshold is strict.
- 598A50 uses the traffic controller direction at virtual +40. 582F50
  reads the retained +4D4 integer. 5994D6/59A890 call 583250 to write
  it and update the direction object through 457060. It is not a test
  of the current physical body's heading. At a gap of zero or behind
  the human, equal speed and equal direction do not admit the actor.
- 41FE50 builds the indexed role partition from the linked actor list.
  The exact indexed partition is present in the event 19 raw world
  dumps at race stages 0, 2, 3 and 4: it contains actors 50 through 61
  in order. The manager pointer at WORLD+2F0 is present, but the
  pointed-to block is absent, so the live cursor was not captured.
  The linked-list walk has a different order from the indexed partition.
- The twelve model IDs in the recording and the current port differ
  at seven slots. The original weighted selector at 4B3B20 and skin draw
  at 4B3B7D reproduce the port's roster when supplied its fixed seed.
  A separate bounded rewind of the captured stage-2 global seed gives
  a single matching input for the full retail roster within 4096 reverse
  steps of the original transition. It establishes a usable input for a
  roster-only differential. A second check ties it to the stage-0
  settings and world blocks. The captured settings seed reaches the
  matching roster seed after six original RNG steps. The observed
  stage-0 world value is consistent with one global advance between
  roster completion and 41F7C9 and 5939 world steps after the native
  constructor. The earlier global consumers and individual staging
  draws still require attribution to the native callers.
- The recording has all 12 traffic bodies at selector 2 in race stage 0,
  selector 0 in stage 2, and selector 2 again in stages 3 and 4. This
  matches the pre-race owner calls at 47F0A7/47F187: 59B710 parks the
  bodies and disables the manager, then 59B790 re-enables the manager
  and restores physical selector 2 where the body validity test passes.
  The capture moves traffic during stage 0, freezes XY during stage 2,
  then resumes traffic in stage 3. The port keeps the traffic intersection
  manager ticking during the film and uses the authored elapsed clock
  for retirement. Relocation first runs once the scene is destroyed and
  the new interval expires. The very first film selection still uses a
  separate frontend stream in the port.
  At GO only WORLD+2C0 holds participant progress in the single-human
  capture. +2C4/+2C8/+2CC contain the unused-slot sentinel.

59B3E0 derives relocation spacing from the current type-4 zone and
traffic density. 59B8D0 ignores pending traffic while counting stopped
traffic in the 250 m window, and 59B3E0 keeps its sentinel when no
zone is available or a stopped actor blocks it. 599830 draws the
reference human, tries the authored 599C80 window according to signed
forward speed, and falls back to 5999C0 in drag or 59A2C0 in ordinary
racing. The live port implements parts of this placement after admission.

The 5988C0 caller decisions and child call order were checked under
supplied child results. The 598A50 fixture additionally runs the actual
progress subtraction and strict relative-speed tests with supplied
direction virtuals and the selected human. The 598600 fixture executes
the original timer, budget and indexed scan with supplied admission and
placement outcomes, including a failed placement that consumes a slot
and writes pending. Ordinary port traffic uses the two branches and
retains a pending latch and authored exit for each slot. Policy 4 uses
strict comparison against the human pace. Policy 5 also admits an
authored actor once its road cursor passes the direction's exit and no
slow human is within 50 m. The port uses its existing raw road cursor,
path progress and a single-human race context as inputs to the gate.
The two worlds still differ at GO in initial models, traffic positions and
active bodies. Matching the eligibility predicate does not synchronize
the initial phase, pending publications or authored-record lifetime.
The seed-only diagnostic replay, before the world appearance draw was
restored, got all twelve models right and spawned actor 50 at the bend
around 6.10 s. Executing the original 59B590 inside the traffic
construction fixture changes later placement choices. The same startup
seed is still insufficient. With a small search over candidate startup
states, the fixture can reproduce all recorded config+17 variant
values and all twelve stage-0 XY positions within 0.31 m after using
the captured direction weight at 78A304 and the original lane
projection. The final RNG state still differs by seven advances, and
several initial states converge to the same placements. Appearance
and placement constrain the sequence without identifying its owner. Production
uses its session seed without offsets inferred from the recording.

### Event 19 differential replay

The local event 19 archive records the three rival controllers on the
original frame intervals through the selected Stadium round. The diagnostic
feeds the captured controller inputs, frame intervals and initial RNG
values to the portable race. It applies the recorded start-scene skip;
later rival commands, traffic placements and NOS use the simulation.
Before the first recorded world frame, the diagnostic uses the native
two-second interval with synthesized frame durations. A separate controlled
case provides the original seed immediately before each of the first two
traffic relocations to disambiguate the placement code from earlier draws.

The ordinary strategy's passing branch calls 44C2E0 once before
admission. The port removed a second call after successful admission.
The first traffic relocation now occurs on the original world tick. A
controlled comparison at equal placement inputs should also compare the
human controller's raw road reference and the actual traffic position.
At the first relocation the captured placement lane and point differ from
the autonomous replay at least by the raw road point. The actor's later
position can still lie outside the Golf's collision-lookahead threshold.

The checkpoint records the full-run result and the earlier comparison
with a different scene-release clock. No owner drive is required to run
these diagnostics again.

## What this means per mode

- Circuit and sprint both use ordinary racing controllers. Context, traffic
  density, lap/point-to-point bank, route direction and authored passage
  choices change the output. The course table lists the start gate used
  for these decisions by each event and direction. Sprint does not become drag just because it is
  point to point.
- Knockout uses the ordinary controllers. Removing a racer changes the
  active obstacle and opponent set. At tournament boundaries the current
  round's route and progress lifetime are installed again.
- Drag uses its own launch, shift and lane logic, with ordinary catch-up
  bypassed. Traffic admission and rail crossing can change whether a rival
  ever reaches the finish. Raw shift offsets are not guaranteed finish times.
- Drift opponents accrue virtual scores. Track zone crossings and the
  selected difficulty control the opposition even if there are no rendered
  opponent cars. Time trials have no competitive opponent controller.

## Event 19 and vehicle choice

Event 19 is 1001 forward, three laps, traffic selector 2 and rank weight 1.
Its three authored rivals are Jose (Golf), Bryce (Peugeot) and Brian (240SX). Their contexts on Hard
are 5, 5 and 4. The setup is adaptive for each. Tiburon, Miata, Civic and Golf
can therefore produce different original rival ratings/parts and different
driver road categories. The event still retains the same authored roster and
difficulty contexts. Once racing, changing the human's position, pace and
history also changes catch-up and what the opponents see in their paths.

The original code provides no guaranteed winning outcome for every vehicle
on Hard. The matched setup cases rule out the tested 57F700 tuning inputs as
an explanation by themselves. Track 1001 has TrackInfo+B8 = -1. The
fresh join decision holds probabilistic branches through 435CA0's
opening window. The native limits for actor IDs 100, 101 and 102 are
12, 11.5 and 11 seconds. Mandatory links and cached decisions are
handled before this gate. This proves a route decision discrepancy,
and the path-model correction proves a separate physical boundary
discrepancy. Their contribution to the race result is still open.
Live traffic, the shared RNG stream and complete selector-4 dynamics
can still affect the race result.


### Interpreting the recorded comparison

The event 19 diagnostic replays the recorded human commands and original
world intervals. It does not overwrite the human or rival trajectories.
The pre-capture interval is reconstructed, not recorded physical history.
An early human wall contact changes later traffic and rival decisions, so
a complete replay is not an identical-input proof of the AI functions.

Actor identity is explicit: configuration 100 is Golf, 101 is Peugeot,
102 is 240SX, and the human is Tiburon. Earlier narrative comparisons
incorrectly called configuration 102 Golf. Those attributions are withdrawn;
use the recording identity and the comparison artifacts named in the
[active checkpoint](checkpoints/root-ai-audit-r99.md).

Published actor RPM and slip are separate from physical component fields.
Compare Actor+3E0 with published RPM, and Actor+374 pairs with published
wheel slip. Comparing these fields with engine angular speed or internal
wheel slip can falsely identify a downstream driver error.

The opening replay and identical-input native witnesses serve different
purposes. The former locates divergence in connected producers. The latter
executes authenticated original functions over declared input states and
checks the corresponding portable implementation. Neither alone proves the
whole race, the unseen pre-capture state, or console behavior.

### Physical-component lifetime and publication

- 426110 replaces components before the next action/driver use. 4782C0
  constructs fresh engine, gearbox, wheels and struts, importing published
  actor RPM, gear and wheel spin. Old cutoff, heat, clutch, compression and
  torque history do not survive. The gearbox construction consumes the
  shared world random stream. Rigid import uses 584B10/584B80 with the
  original normal inertia; do not reinterpret the imported velocity again
  as an external edit on the next derivative.
- Driver45E7B0 and NOS44CCA0 consume actor wheel-slip publication. Full
  physics publishes wheel100/104, not raw wheel160/164. Selector3 publishes
  retained engine94/98 pairs, including no-contact retention. Selector4
  publishes 464405..46445A velocity projections. Component refresh alone
  cannot replace actor publication before completion.
- 45F170..45F364 changes grounded-front steering on selector1 for a recent
  strong wall contact found by468820. Both pair endpoints, the plane normal,
  contact age and magnitude matter. The result still reaches the common
  steering clamp and slew. Original whole-driver cases live in
  `tests/ai_driver_contact_native.inc`, generated by
  `tools/ai_driver_contact_vectors.py`.
- Selector4 retains the published wheel-contact bytes and support count.
  Its 427A30 publication owns a retained speed filter at Actor4C0, original
  global gear/RPM bands and per-call slew. It does not derive the displayed
  RPM from the current car's physical gear ratios. Original outputs live in
  `tests/ai_path_powertrain_native.inc`, generated by
  `tools/ai_path_powertrain_vectors.py`.
- Actor4C0 is initialized by4257BE and serialized at425D44..425D4C. It
  survives both component replacement and427C50 actor placement. Importing
  published gear through47839B..4783A4 does not clamp it. Retail race cars
  have sufficient physical slots for the publication; an artificial
  two-speed fixture is not evidence for adding a non-original clamp.

The connected implementations are `src/ai_track.cpp`,
`src/vehicle_track.cpp`, `src/ai_powertrain.cpp`, `src/ai_driver.cpp`,
`src/intro_ai.cpp` and `src/race_session.cpp`. The independent review and
current gate status are recorded in the active checkpoint. No whole-race
parity claim follows from this crosswalk.

### Road placement and prepared component forces

427C50 places the actor before replacing its components. 464C80 supplies
neutral gear and1000RPM. Ordinary recovery takes the retained controller
road/point; it does not substitute the human nearest-node reset. The
4273D0 support queries use authored hub coordinates and the original angle
table. All four hits form the supported pose; a missing hit retains the
authored position and yaw. The native cases are generated by
`tools/ai_placement_vectors.py` and consumed by `nfsu_ai_track_tests`.

Selector3 construction calls474CA0 twice through474640 and4743E0. Both
callbacks refresh struts, rest, bump stops, wheel feedback, aero and body
stabilization. Actor34 is not installed yet, so42A860 cannot publish the
component back to the actor. The first integration step consumes those
prepared forces; importing the old public velocity again would discard
constructor stabilization. Real external edits of position, orientation or
momentum invalidate the derivative, including edits already synchronized to
the public rigid-body cache. The component body is the comparison of record.

Actor828 follows the last actual wheel support query. A cached query that
does not execute a new cast retains it. This dynamic-body candidate flag
participates in selector4 admission; the owner recovery timer itself gates
the reset request, not an extra physical-solver lock.

Retained neighbor identity is distinct from traffic activity.5832A0 parks
an Actor and59A7C0 reuses that same Actor on relocation.57A300 filters a
new neighborhood; neither57B520 nor4576B0 filters a cached entry again.
The port keeps slot identity and order until that neighborhood is rebuilt.

57A530 copies the raw Driver path before the manager and controller phases.
4576B0 starts every pair query from that saved path, not the latest Driver
and not an already predicted path. It reads live traffic pose, action and
road state for the outside-road branch and45A510 refresh. RaceTraffic keeps
this publication separately and refreshes it every world epoch, even when
the neighborhood itself is retained. Placement queries read that current
publication while refreshing live actor inputs. The parked/reused/publication boundary
is exercised by `nfsu_traffic_reaction_retail` alongside original full-pair
vectors in `nfsu_ai_path_tests`.

The native world-contact fixture sets raw actor velocity and identity axes
before426110, leaving scalar owner speed unchanged. Its port fixture must do
the same. `nfsu_race_world_contact_retail` keeps its original output vectors;
changing a fixture's public quaternion alone leaves a different constructor
input in the owner's retained axes.

### Retained selector4 body motion

463260 constructs separate channels for lateral displacement, heading and
slip. 463B30 copies the Driver path into the physical component's spline;
463910 updates its path parameter, acceleration and lateral channel while
retaining the heading, slip and curvature history. A Driver path update
must not reconstruct that component.

463F20 advances those channels through593EC0, samples forward velocity and
curvature, and obtains lateral slip from463BC0. The friction curve uses
the previous sliding state; the transition changes the channel duration
immediately and the selected curve on the following call. 4278D0 then
samples all four wheel hubs and uses4273D0 to fit the body to the ground.
One missing support keeps spline height and the yaw-only basis. The raw
fitted basis survives publication without a quaternion round trip.

463D50 checks predicted departure before the current step advances its
elapsed time. Its original timer selects another physical component on
the following controller update; it does not discard the current pose.
The port's former tangent-only heading and additional offset/heading
rejection gates did not implement this retained motion.

The spline evaluator preserves both scalar59CDB0 matrix products and their
component-specific summation order. Simplifying the polynomial can erase
signed float residues which567370 subsequently normalizes into a direction.
The native fixture covers both an exact-zero horizontal derivative and a
constant nonzero coordinate that retains such a residue.

59D430/59D540 projection also preserves float differences, square-root
rounding and the original Z/Y/X summation order. Double intermediates
can select a different secant result on near-vertical splines at large
coordinates. The production helper and the original native projections
are compared directly by `nfsu_ai_track_tests`. These synthetic inputs
exercise the numerical boundary; they do not prove the event19 opening.

The complete463910 publication uses the stored binary32 path rate divided
back by points-per-metre before456830 applies the physical speed scale.
583EA0 truncates the table index but takes the floor for its fraction; a
small negative coordinate therefore samples the first segment rather than
clamping to the first value. 4638A0 normalizes the spline tangent in binary32
before multiplying by gravity. The shared speed-target helper preserves
these steps, the gearing limit, drafting drag, default transition duration
and upper-cap-before-braking-floor order. Whole native463910 witnesses in
`nfsu_ai_track_tests` cover positive and negative signed forward speeds.

Departure downforce uses the speed saved at publication (rate divided by
points-per-metre), even between path refreshes. The spline derivative uses
its own timed speed. Velocity publication takes the resized-displacement
branch at the exact dt cutoff as well as below it. A stationary zero-vector
and zero requested speed is explicitly kept finite in the port; original
masked x87 produces NaN for this degenerate input.

### Checking yaw, traffic projection and steering recovery

The path-body publisher takes a positive time step. Its race caller,
`VehicleTrackSession::step_rival_path`, rejects zero, negative and nonfinite
steps before passing control to the publisher. The small-step branch above
changes how displacement becomes velocity. Yaw still uses the elapsed time.
A direct call outside the positive-step contract can return a nonfinite yaw.

For traffic probes, run `tools/traffic_physical_native.py <retail-root>`
and `nfsu_ai_path_tests`. The former executes PC 1.4 routine 44F1B0 with
supplied positions, velocities and box bounds. The latter passes the same
inputs to the port and compares both distance and time by their float bits.
Keep the scalar column projection when working with the temporary matrix in
`ai_physical_obstacle`. Changing it to row products reverses the answer for
some headings. This adapter supplies the physical probe for civilian traffic.

For steering recovery, `tools/ai_reverse_vectors.py` executes the original
45E650 reset and 45E970 drive step. The port checks the subsequent steering
words and pedals in `nfsu_ai_driver_tests`. Resetting an error history seeds
its samples and sum, clears its count and retains its cursor and cached
average. The first subsequent derivative average can consequently be large.
The steering calculation clamps the blended histories and limits the rate
of change of the steering word. To investigate a maneuver, compare the
published steering word together with the history and the reset trigger.

427C50 reconstructs selector4 from the placed actor's heading, magnitude
and yaw only after placement succeeds. A failed resolver retains the old
spline and channel history even though the frame reports a reset reason.
The integrated adapter regression exercises both outcomes through run_tick.

The implementation is `src/ai_path_body.cpp` and the selector4 adapter in
`src/ai_track.cpp`. `tools/ai_path_body_vectors.py` executes authenticated
original constructor seeds, retained motion, path republication and wheel
placement. Its only semantic hook supplies explicit road heights; the
dynamic-object list is empty. `nfsu_ai_track_tests` compares the resulting
positions, basis, heading, channels, sliding state and post-fit velocity/yaw
using the checked-in
`tests/ai_path_body_native.inc`. The witness stops before wheel visual
effects and does not assert the complete controller or whole-race input
sequence. Those limits also apply to variable-step and missing-wheel cases.

## Passing action factory and lifetime

The ordinary strategy44DAA6 calls44F8A0 for a collision prediction and a
path choice. These are different outputs. The forecast reaches the target
admission virtual at44DB09. The constructor call44DB43 instead converts the
path-search result byte to a float for44F750: action+204 stores that value
and action+214 stores it plus two seconds. A retained side descriptor
supplies a successful search result. The previous port incorrectly supplied
the collision prediction to the constructor, expiring or retaining passing
maneuvers at different full controller epochs.

The constructor's next argument is the independent side-admission result,
initially stored at+208. Its+218 side-score/ignore input starts at zero.
After a new45B150 search, +208 is the path result and+218 is side admission.
44FAF0 completes on a strict ordered greater-than comparison of the signed,
wrapping tick difference times the original float tick scale. A partial
44B81F path publication does not evaluate this predicate.

`tools/ai_passing_lifecycle_vectors.py` executes the original factory tail,
constructor and predicate dispatch without semantic hooks. The connected
`nfsu_ai_strategy_tests` uses its native predicates after entering an actual
passing action through the production query/factory. It covers successful
and rejected searches, the clock boundary, wraparound, and partial versus
full updates. `tests/ai_passing_lifecycle_native.inc` owns these pins.

Conditional event19 replays supply recorded actor motion to the world
snapshot while retaining portable actions and paths. They isolate decision
logic; they do not reconstruct all physics, contacts or traffic and are not
whole-race parity. The action cleanup's original Actor+88C override flag is
not yet mapped to a demonstrated live rival gameplay consumer.

## Static reproduction

Run from the port checkout with the retail root read-only. This asks the PC
test binary for a static round manifest and does not start the game.

~~~powershell
python -B tools/campaign_ai_audit.py --root $retail --probe build-release/nfsu_career_races_tests.exe --output out/campaign-ai-events.md
~~~

Whole-race parity requires the unresolved producer and owner boundaries
above to match. The function crosswalk names each recovered branch, its
evidence and its remaining inputs. The event census covers all campaign data described above.
Other original AI functions and naturally reached branch combinations
remain open.


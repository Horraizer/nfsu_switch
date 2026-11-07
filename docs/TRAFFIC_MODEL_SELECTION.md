# Traffic model and road-decision audit

## Original boundaries

CarTypeInfo records from GLOBALB chunk34600 drive57CFA0 and57D000. The
iterator checks class2, enable byte40 and positive signed variant countD9.
The track selects a weight column atC60. Track IDs at least1000 use column0;
legacy region6 uses column4 and other legacy regions use region minus1.
The weighted selection retains original record order and binary32 operations.
It must not filter zero-weight records before the subtract/accept walk: an
exact-zero random sample can accept a leading zero-weight record.

The4B3B20 stage rejects an already configured type when the prior config's
first byte is zero. It retries at most100 times and then accepts a duplicate.
This replaces the port's bag without replacement.4B3B7D consumes another
sound-RNG draw for the authored skin selector. In this PC executable57D160
does not store that first stack argument; the port does not fabricate a skin
texture alias. Both the retry and skin draw affect subsequent random states.

`traffic_model_tests.cpp` compares the production decoder and chooser against
`traffic_model_native.inc`. `traffic_model_native.py` regenerates/checks that
fixture by executing actual57D000/57CFA0/564A80 and both4B3AC0 stages, with
supplied track, seed and prior configs and no hooks. Retail input hashes are
checked before execution. Tests own the row counts and expected values.

## Live integration limits

`load_race_grid` uses the chooser for each actual traffic asset/handling/hull.
The frontend still supplies an explicit deterministic port seed. The witness
does not execute actor construction between choices or the entire frontend
and global sound-RNG lifecycle. It is not a claim of an identical natural
roster on every run. Existing GLM data and retail files are read-only.

Traffic road callback583BB0 consumes event byte0C and the world generator.
Type2 is forced, type4 uses a strict sample less than0.5, other types decline.
`ai_traffic_road_decide` and `traffic_junction_native.py` preserve this small
boundary, including RNG advancement and the exact half-range case. The live
port still owns its separately documented road admission/cache/generator.

## Live spawn-point admission

The traffic owner now applies original4420B0 before placement. Raw point+84
bits25/26 admit forward/backward spawning; enabled header lanes alone are
insufficient. In the observed drag data, racer-route lane groups overlap in
world space for opposing travel, but their raw points deny traffic spawning.
Do not replace this point permission with a RouteKind filter: legitimate
main-road traffic corridors also have the racing record kind.

Drag5999C0/599B20 searches table slots0..98 and599C40 requires ten authored
points remaining along travel. Forward index<count-10 is strict; backward
index>=10 is inclusive. RaceSession passes actual drag mode to the owner.
The ordinary teleporter is a separate producer and keeps its wider admission.
The density native verifier also executes the original point flag and endpoint
functions; ai_traffic_action_tests.cpp compares every emitted boolean. The
endpoint function defines AL, so its unrelated upper EAX bits are excluded.
Tournament path tests check newly activated traffic against the raw authored
point permission, drag table range and endpoint margin.

This restores authenticated admission rules. The remaining spatial search,
random-choice/lifetime adapter and avoidance strategy are separate limits;
legal placement alone does not establish whole-world traffic parity.

## Unfinished ordinary traffic strategy

Traffic driver vtable6B96B0 creates policy4 through583230/44AA10/44A200.
Policy vtable6C97D4 selects action18 via4543D0 at priority600, then action3
via453920 at priority300, then default action8 via44E260. The live port's
finite obstacle-braking corridor is not this complete strategy and remains
an open correction. Selection alone does not reconstruct action paths.

583910 additionally combines the owner's4DC speed cap with native road-speed
interpolation5A6B00 and upcoming drop5A6BC0.453920 uses44F470 prediction and
44EFD0 admission. These real producers and action lifetimes must be restored
before removing the finite corridor; invented coefficients are not evidence.
This mapping does not establish the cause of a particular queue or impact.

## Collision and runtime acceptance

Recovered ordered hull queries and common car/static response queues already
exist. They must not be replaced on the basis of older historical checkpoint
notes that predate their implementation. The traffic-impact viewer's finite
proving ground now supplies the retail route image name separately; its first
run failed because the synthetic name was not a retail route file. This probe
checks wall retention, not original natural car/car behavior.

Natural original collision/order capture is pending an original PC runtime
that the owner cannot launch now. Console presentation, traffic paths, impact
recovery, listening and performance remain unaccepted. No console delivery,
Git publication or full-parity declaration follows from the bounded tests.

## Intersection geometry and isolated identity

The34123 header uses two signed16 raw route-point indices at8/A. It is not
a timer word. ai_traffic_intersection_extent follows5AAB20 on raw roads;
the other road always uses its raw forward edges, while self direction
selects its forward/backward projection basis. ai_traffic_intersection_distance
follows5A6E10 from an admitted first_limited cell and its self-neighbor epoch.
An absent producer refuses instead of fabricating a clear road. Arrival
583C80 uses signed speed, including its original low-speed sentinel, and
401C40 maximum; it does not round. The named compiled intersection retail
check and traffic_intersection_geometry_native.py own the expected outputs.

The isolated traffic fixture now supplies actual owner4 and trafficID50
before construction/world registration. Physical3 remains explicit, not
natural admission from nominal physical2. Older runtime witnesses with ID0
are retracted:583350 clears intersection slots50..71 without an ID guard,
so ID0 could corrupt preceding data. See the r75/r76 checkpoints. Bounded
context and geometry equality does not close live actions8/3/18, world RNG,
teleporter admission or shared-world update order.

## Stop-action controls and lifetime

ai_traffic_stop_update follows454490 after actual first_limited/raw-road
resolution and44F470/44EFD0 prediction. It preserves the original independent
obstacle limit, shared right-of-way reservation, mandatory stop for kind3
and the second route of kind2, strict stop-line/speed latch and final control
flags. The final auxiliary1 deliberately overwrites its intermediate obstacle
value. The actor's four-float override is retained without assigning it an
unverified horn/headlight meaning. The original45AE10 path stage remains a
separate caller responsibility; the kernel does not synthesize a clear path.

The reference producer resolves44D570 from the raw point mask. A known
absent first_limited is a valid absence. An invalid admitted index, point or
table refuses; it does not become absence.4543D0 offers priority600 only for
a present crossing.454420 retains an unchanged crossing, completes on absence
and resets the stopped latch/mode on a changed one. With this PC1.4 executable's
constants,454330 chooses mode1 but still consumes the shared WORLD draw.

tools/traffic_stop_action_native.py executes original constructors, queries,
path output, offers and completion with authenticated external inputs. It
observes44F470/44EFD0 transparently in a clear scene and a real registered
second static stock shape, with explicit owner4/ID50/physical3. The compiled
traffic-action test owns numeric/state expectations. These are bounded input
scenes, not a complete natural action/spawn/world/physics acceptance. The live
traffic corridor remains until real actions8/3/18 and their caller order are
connected as a coherent replacement.

## Lane-divider permission

ai_road_lane_transition follows5A7700/5A7820 after real destination admission.
It walks every adjacent lane edge on1206/1214, in both source directions,
using the executable's fixed rows. The progress intervals include both
endpoints. It is not a parser for road-divider object geometry. Ordinary
categories, retained lanes, rejected destinations and unrestricted tracks
keep the source's early exits before consuming divider progress.

The live drag planner now supplies actual track, direction and raw progress.
Owner2's5A75C0 header admission bypasses future compact-chain flags, while
other controller kinds retain that flag gate. Disabled header lanes remain
blocked. The traffic context's adjacent-lane admission uses the same divider
permission; nearest-lane fallback remains its separate original branch.
The full traffic runtime still awaits actions8/3 and shared world admission.

tools/traffic_lane_permission_native.py executes the actual whole predicate,
the actual adjacent fixed-table walk and actual5A75C0 admission on separately
mapped synthetic route/cell/actor inputs. It reads only the authenticated
executable. Exact interval neighbors, disabled destinations and owner2's
flag bypass are pinned by the compiled road tests, not this document. These
checks do not certify complete natural drag steering, paths or contacts.

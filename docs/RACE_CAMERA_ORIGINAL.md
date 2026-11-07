# Race camera and tournament command capacity

## Scope and original-code evidence

This correction addresses the owner's event6 return to the menu and distant
start/race views. The preceding garage-original-r35 changes are included.
The older controls-camera document describes the legacy drive-probe projection;
production races now opt into ChaseFovMode::retail instead of that widening.

Authenticated input: PC USA1.4 speed.exe SHA256
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
All original files are read-only. tools/race_camera_native.py executes the
original camera cycle42E020 and primary projection40DD00 on supplied memory.
Its execution is bounded before scene submission, with original scalar math,
no callback/arithmetic hooks, and no claim of a running original race.
The resulting fixture lives in tests/race_camera_native.inc and is checked by
nfsu_drive_tests. Special high-view outputs are retained as evidence; only the
ordinary cycle is exposed by the current control.

42E020 compares the current POV against the player's four-byte list, skips
the fourth slot in ordinary mode, and excludes the cockpit POV. 4B6390 and
its default fields at6F1D68..6F1D70 establish the near/bumper/high entries;
4BE6C0 independently restricts the ordinary options to the first three.
The normal port cycle is therefore far chase, near chase, bumper, far chase.
Explicit high-view selection remains available to diagnostic tools.

489690 uses the body frame for the fixed view instead of its chase-heading
spring. The bumper adapter carries body pitch and roll into the renderer,
uses the selected CarTypeInfo anchor and suppresses the human body/lamp
sprites while retaining command identities. Opponents, traffic, projected
headlights and the car's shadow are retained. Runtime camera record loading
uses the selected car, not the built-in240SX table. Civic and240SX anchors
are identical; their table identity alone did not cause the distant framing.

40DD00 consumes the FOV word published at48ACA4. Its primary-view path scales
and truncates independent horizontal and vertical half-angles before native
sine/cotangent evaluation. The viewport ratio and globals6B7C6C/70/74 enter
before truncation. The production race camera now uses that projection.
Frustum culling receives the same independent focal ratio and camera roll.
This replaces the approximate preservation of a4:3 vertical tangent that
made the car appear smaller. The test fixture pins the original focal
scales at both race FOV words and multiple viewport sizes.

Full original mover equivalence is not claimed: legacy chase spring,
collision retraction and look-back adapters retain their documented limits.
The fixed view's complete update, live original tournament start animation
and final Switch framing still need hardware/reference acceptance.

## Event6 failure and capacity correction

The owner's difficulty-r34 run3 log ends with
`original-distance frame exceeds reserved draws; no distance fallback`.
The race reports failure and returns to the frontend. This terminal path
contains no allocation failure. Sampled heap statistics alone cannot prove
that no other allocation issue exists elsewhere.

The nearby-view retail probe reproduced the same refusal before correction.
It covers the event6 grid, rivals and traffic with full original-distance
geometry and visual effects. A wider view exceeded the immediate recorder's
old draw ceiling. Empty geometry groups were also included in the raw list
capacity calculation although they emit no hardware draw.

Prepared scenery selection now uses a separate bounded command capacity;
the immediate diagnostic recorder retains its old bound. Empty groups still
participate in native bucket ordering but do not consume hardware slots.
The combined bound includes actual shadows, actor draws, glows and crowd.
No far-plane fallback or object omission was introduced.

The root GPU command reservation counts the dynamic tail and static child
list calls. Static draw instructions are stored in separate visible-track
buffers, rather than reserved again at full per-draw size in both roots.
The existing chunk size and conservative GPFIFO bound remain enforced in
deko_gpfifo_budget.h. Capacity remains bounded; arbitrary scenery/rosters
are not promised to fit. nfsu_tournament_camera_capacity_retail checks the
real complete package, GPU execution plan and nearby views.

## Delivery and remaining acceptance

All changes are local under the owner's Git publication hold. The source
freeze, full required gate commands/results, script ASCII checks and NRO
identity are recorded in build/tournament-frame-r36/artifacts/BUILD_RECEIPT.json
when sealing succeeds. No console upload is implicit in this task.
An SDK packaging pass is not evidence of Switch play. A new event6 drive,
bumper/near/far camera inspection and independent review remain required
before claiming the owner has accepted this correction.

## Startup input lifetime correction

The camera-reference replay exposed another startup lifetime defect in
TrackResidencySession: RaceRun supplied a temporary cost vector and local
options, but its persistent Streamer borrowed both. A later zone rebuild
read expired configuration; gdb located the access violation in Streamer::step.
The live session now owns these small immutable inputs. Track tables and
zone reports retain their documented borrowed lifetime. This adds startup
storage only, with no per-frame allocation or streaming-policy change.
nfsu_track_residency_tests invalidates caller configuration after start and
compares every incremental transition with the batch model. The long native
texture-filter reference and full replay gates cover the real caller.

## Authored road startup and results overlay

The owner's tournament-camera-r36 log reaches a valid first-place finish in
round0 of event6, then records post.failed with HUD image: GPU memory budget
exceeded. The failure is the frontend overlay allocation, not a failed race
or a command-list ceiling. The old path uploaded all accumulated garage and
career art while the race resources remained resident for the live background.
FrontendArt::prepare_post_race now packs an independent result atlas with the
two original fonts, White16x16 and45Corner. The race background and source
frontend art stay resident and unchanged. nfsu_frontend_retail checks all five
pages against the original CPU draws and GPU geometry, colors and texels.
It also checks the result overlay allocation bound. No GPU limit was raised.

Original47BC63 selects the single-player start playlist by car count and
human grid position. In a four-car race the mapping is3->0,1->1,2->2,4->3;
INGAMEB34036 supplies the scene IDs. The port picks a stable variant by round
index from that original playlist. It does not reproduce the original world's
RNG history. The road path loads the selected34035 camera channel hashes,
ROOT skeleton and EAGL camera clips from the selected region, using bounded
chunk reads instead of retaining another complete world stream. Road camera
loading adds no GPU uploads or actor geometry. Existing UG0 and drift crowd
paths remain separate. Full road crowd and scripted car-motion reconstruction
is not claimed by this camera correction.

The camera uses the authored pose and lens, anchored at ForwardStartLine(0)
or BackwardStartLine(0). Header release/go/end frames control the cinematic,
countdown and release. Every rival receives the same cinematic interval as
the human; otherwise rivals reached GO during the human's intro and ran with
an incompatible planner stage. The last clip pose is held when its samples
end before the header release, as47B290 flag20 does. The normal camera returns
at release; changing POV during a cinematic cannot hide the human body.

48E62F converts the authored focal length with original564DB0 and401C10.
The port uses the already recovered angle lookup for that conversion, then
rebuilds the original40DD00 independent projection. This replaces stale chase
projection overrides that previously masked authored lens changes.
tools/race_start_native.py executes authenticated original playlist selection
up to its RNG call and lens conversion up to view submission, without hooks.
tests/race_start_native.inc pins its observations; this is bounded native
execution with supplied storage, not a live original-game startup capture.

nfsu_race_start_camera_retail checks camera motion, release, stable resources,
all four human grid positions and both marker directions. The three
nfsu_tournament_start_round*_retail tests load the production race scene,
roster and original camera through the complete intro and synchronized GO.
Local startup image captures exercise the same decoded camera against the
real track and cars. Switch camera framing and advancing from the tournament
result menu still need an owner hardware run; no build or host test substitutes
for that acceptance.

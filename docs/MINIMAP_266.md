# Original event0 minimap: bounded research and integration contract

Research for [issue266](https://git.accad.pro/murzintsevav/nfsu-switch/issues/266),
batch20261003 slot4. The assigned development substrate is
`8090cb058c658089c83cc101299861754e03504e`. This document and the three
`minimap266` Python tools add no production HUD, actor, simulation or renderer
changes. They do not close issue266 or establish event0, Switch, visual or
performance acceptance. The substrate itself is unaccepted.

Evidence labels: **R** recovered bytes plus identified consumer; **M** measured
by executing bounded retail functions with the stated fixture/stubs; **O**
observed in a named original still; **I** inference; **U** unresolved; **P**
proposed port interface. M is not a running-original observation.

## Reproduce without extracting images

Run in this research worktree. Use a standalone Python 3.8+ with Capstone 5 and
Unicorn 2. This worker used Python3.8.4, Capstone5.0.9 and Unicorn2.1.4. If
Unicorn is installed in the local ignored dependency directory, add it only
to this shell's Python path. No retail executable is launched.

```powershell
$env:PYTHONPATH = Join-Path (Get-Location).ProviderPath 'build/266-s4-deps'
$reference = Join-Path (Get-Location).ProviderPath '../../../reference'
$source = git rev-parse HEAD
python -B tools/minimap266_trace.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --reference $reference --out build/266-minimap-evidence --source $source
python -B tools/minimap266_test.py --report build/266-minimap-evidence
```

The relative reference location applies to `build/day20261003-b1-s4-minimap266`.
For another checkout, provide the real existing reference directory or omit
`--reference`. Do not derive a Unicode workspace path from native stdout.
The full SHA produced by Git above is ASCII, not a filesystem path.
Install missing dependencies into your own ignored directory, not another
worker's tree. Use a fresh `--out` on each run; the tool never deletes outputs.

The probe refuses output overlap with either input root, existing output and
non-ASCII new output components. Full before/after manifests include directory
and file names, mtimes, sizes and file SHA-256; all must match. A manifest
match alone is not success: the final PASS is printed only after every trace
and report completes. `data.json` gives source-addressed fields and resource
identities; `instructions.txt` gives bounded disassembly; `native.json` gives
inputs, native outputs, tool hashes and limitations. No pixels, images or
texture payloads are written. These reports remain local and reproducible;
this PR uploads no original references or retail assets.

| Pinned input | SHA-256 |
| --- | --- |
| `speed.exe` | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |
| `Global/GLOBALB.BUN` | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` |
| `Global/INGAMEB.BUN` | `044724AB4C167831587E80F29961A1E7D8404C9DD5266533F3563C555A997299` |
| `Tracks/TEX1003TRACK.BIN` | `081B4D966C154D7B9823E70DD5031269A8D3293F8CD13ED515EF644F09573ED5` |
| `Tracks/TRACKB1003.lzc` | `ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA` |

All executable addresses are PE32 virtual addresses mapped through the section
table. Ends in `RANGES` are exclusive. Names ending `_context` explicitly
identify search context rather than established function boundaries. Probe
guards cover malformed records and output isolation; native assertions require
a report and never silently skip. Expected native numbers live in
`tools/minimap266_test.py`, not a second measurement table here.

## Resource, map coordinates and streamed coverage

**R.** The constructor `0x0058E540` formats `TRACK%d_MAP` from track-record
`+0x6C` at `0x0058E62D..0x0058E63B`, hashes it and looks up a texture with
`0x005461C0` at `0x0058E651`. For event0, #268 establishes track1003/forward.
Its route artwork is `TRACK1003_MAP` in `Tracks/TEX1003TRACK.BIN`, raw pack
root0, info record840, A8R8G8B8, one mip. The resource hash, dimensions,
payload offset and payload identity are reported and pinned by
`NativeEvidence.test_source_resource_and_native_placeholder` in
`tools/minimap266_test.py`. It is a texture, not the lap spline or an AI
racing line.

The same constructor resolves these INGAMEB resources by name:

| Resource | Hash | Info record | Format / size | Consumer field |
| --- | --- | ---: | --- | --- |
| CAR_MARKER | `135663D6` | 37412 | DXT3,16x16 | `+0x7C` |
| CAR_MARKERB | `7E22DED8` | 46340 | DXT3,32x32 | `+0x80` |
| TRACKMAPFRAME | `E4B9DA3D` | 54772 | DXT1,256x256 | `+0x84` |
| MAPBACK | `D86EB8CE` | 54276 | DXT1,8x8 | `+0x88` |
| START_LINE | `72974A14` | 44728 | DXT1,16x16, two mips | `+0x94` |
| BKGBMP | `526F7C72` | 41876 | DXT1,8x8 | `+0x98` |

**R.** World-to-texture helper `0x00590AD0..0x00590B48` reads horizontal car
slots `+0x60/+0x64`; it negates the second coordinate, applies the object's
matrix at `+0x20`, subtracts track `+0x8C` from x, adds track `+0x90` to y,
and divides both by track `+0x94`. For GLOBALB track1003 record2102144 those
fields are `604.030029296875`, `796.5999755859375`, `1202`. The constructor
builds the matrix from track u16 `+0x98` via `0x0040BAF0`; the track1003
stored angle is zero. The idealized expression is

```text
q = retail_rotation(track.rotation_u16) * (raw_world_x, -raw_world_y, 1)
map_u = (q.x - track.offset_x) / track.world_span
map_v = (q.y + track.offset_y) / track.world_span
```

**M.** The scalar retail sine approximation makes its zero-angle matrix
slightly different from an exact identity. Replacing it with textbook sine
or collapsing away the rotation changes the pinned native samples. The probe
selects the executable's scalar vector/matrix dispatch targets, then executes
the actual rotation and projection instructions; it does not stub their math.
This is a scalar-CPU oracle, not evidence that the running original selected
the same CPU implementation or that a GPU port is bit-identical.

**R/M.** The probe projects every track1003 path-node record and all forward
grid markers. It also samples the actual `ForwardStartLine` marker, not node0:
the extra closed-path node is not identical to the first node. The map helper
uses world pose, with no lap fraction or wrap. A car reset changes the pose;
the map must not move it by inventing a progress value or interpolating across
a teleport. The actual reset/filter lifetime remains U.

**R.** These map functions read a track-wide texture and the GLOBALB track
record. They do not construct artwork from currently resident streamed road
meshes. **P:** retain the map resource for the event generation independently
of geometry residency; unloading a render tile must not erase route artwork.
Use actual published actors, not streamed mesh centroids or the AI spline, for
markers. **U:** texture load/unload failure handling, active-actor eligibility
writers and UV sampler address/filter modes still need tracing. In particular,
centered map styles can request UVs outside `[0,1]`; do not guess wrap versus
clamp from the texture's presence.

## Native rectangle, style, orientation, clipping and alpha

**R.** The constructor looks up `track_map_placeholder` at `0x0058E592`.
`HUD_SingleRace.fng` is JDLZ data at INGAMEB `0x16F40C`. The placeholder
object is at package `0x32C0`, SA field `0x3310`.
`0x0058E5B2..0x0058E5E7` adds `(320,240)` to its position and halves its
dimensions. `NativeEvidence.test_source_resource_and_native_placeholder`
pins the resulting center and dimensions; `test_geometry_clips_and_culls_outside`
pins the clipping rectangle. `0x0058FB10` clips against that native screen
rectangle. This recovers the package/controller coordinate space, not final
1280x720 placement.

**U.** The enclosing FEng/display/aspect/safe-area transform has not been
followed to final pixels. Do not simply double native coordinates for the
Switch HUD. The existing 1280x800 stills show a left-side map with a roughly
upward player triangle, but do not identify event0 or the selected map style.
`retail-01.webp` and `retail-05.jpg` are O only; neither establishes absent
opponent markers, lifecycle or a universal anchor. No screenshot fit has been
promoted to a recovered final transform.

**R.** Twelve 60-byte style records at `0x00737850` are initialized by
`0x005912B0..0x005919A8`. The constructor starts with index6, but
`0x0042EC10` subsequently selects the style. The ordinary single-view setting
is `0x0078A3F6`; other paths use `...F7/F8`. `0x0042E4F0` validates the
selection; style1 falls back to3 when `0x00701084` is zero. Index6 disables
route/marker output. Thus the constructor's initial6 is not an event0 default.

Relevant style fields, relative to one 60-byte record:

| Offset | Recovered consumer |
| --- | --- |
| `+0x00` u16 | x tilt passed to `0x0040B990` by screen projection |
| `+0x04` float | perspective distance in `distance/(distance+z)` |
| `+0x08/+0x0C` | native anchor adjustments |
| `+0x10/+0x14` | added to placeholder half-extents |
| `+0x18/+0x1C` | local UV extents around the view-car projection |
| `+0x24` | car-marker size; start-line sizes are `+0x28/+0x2C` |
| `+0x30` byte | camera-direction/filter branch enabled |
| `+0x31` byte | centered/cropped route branch enabled |
| `+0x32` byte | CAR_MARKERB and heading-aware marker branch |
| `+0x34` float | route vertex-alpha multiplier |
| `+0x38` byte | route/marker enable |
| `+0x39` byte | backing/frame draw enable |

The initialized styles differ materially:1 is tilted and camera-driven,3 is
near-flat and camera-driven,5 shows the full UV square with fixed map
orientation,6 is disabled. Native comparisons exercise these branches;
they do not choose a default for event0. All initialized backing/frame enable
bytes are zero, so loading TRACKMAPFRAME/MAPBACK alone does not prove they
appear. Later runtime edits to the table are not ruled out by initialization.

**R.** `0x0058EFB0` obtains camera direction through `0x005910F0`, filters
its two components using `0x005919D0`, and converts the filtered vector to a
u16 angle through `0x00564DB0`, adding `0x8000`. Its dt is elapsed world
ticks times the recovered tick conversion and0.9. Without the orientation
flag it returns `0x8000`. The camera helper visits view-camera state and has
a race-stage4 fallback to its previous direction; a different fallback reads
view-car direction slots `+0x90/+0x94`. A map driven directly by vehicle yaw
would omit the camera/filter dependency.

**M.** With an explicitly stubbed camera direction, equal world ticks retain
filter state/angle; changed direction changes it gradually. This is consumer
evidence for a supplied pause or turn, not proof of the actual pause scheduler
or reset notification. Tests pin the native filter sequence.

**R/M.** `0x0058F090` constructs the route UV quadrilateral; `0x0058F640`
projects each marker and first calls point-in-polygon `0x005679B0` against
the map's UV coverage. It culls an outside center, rather than clamping that
marker to a border. `0x00590B50` rotates local rectangles. `0x00590DD0`
tilts and perspective-projects them, then `0x0058FB10` clips polygons to the
native rectangle and interpolates UVQ/alpha before graphics submission.
The probe executes this clipping and captures the final submissions at
`0x0040BEF0`. Tilt can produce several submitted pieces, not just one quad.
Pixel rasterization, sampler behavior and GPU blending are outside that hook.

Route alpha has multiple inputs: source A8R8G8B8 texel alpha, native
depth-dependent vertex alpha from `0x00590DD0`, and the style multiplier
applied by `0x0058F48D..0x0058F512`. Marker colors come from initialized
three-byte tables and receive native vertex alpha; the marker routine does
not apply the route's style-alpha multiplier. The existing raw texture
records report blend mode1 for the map/car-marker resources. That metadata
alone does not prove the final render-state chain. Keep numeric native
color/alpha expectations in the tests/report and verify GPU material binding
before matching visible intensity.

## Actual opponent consumer and identity requirements

**R.** `0x0041FE50..0x0041FF47` builds manager `0x007361F8`'s pointer table
at `+0x28` in role order, reading actor configuration through actor `+0x14`:

| Pointer-table range | Configuration `+4` | Map consumer |
| --- | --- | --- |
| `[0,+0x1C)` | role1 humans | view player separately; extra flagged-human loop |
| `[+0x1C,+0x20)` | role2 opponents | `0x0058EA20..0x0058EAF7` |
| `[+0x20,+0x24)` | roles4/5 | `0x0058EB10..0x0058EBA6`; not event0 rivals |
| `[+0x24,+0x18)` | role3 traffic | no marker loop in `0x0058E810` |

Offsets in the ranges are stored counts, not byte offsets into the table.
The producer rewrites actor `+8` to the current table index. **Do not use
that index as stable racer identity.** #268's actor-config IDs0,100,101,102
identify event0's human and three rivals; names, grid and table order do not.
The lookup at `0x0041FF50` compares actor signed ID `+0x0A`, while the map
draw itself follows actor pointers rather than joining a lap/rank list.

Each opponent requires actor bytes `+0x4D0` and `+0x8A9` to be nonzero
(`0x0058EA26..0x0058EA3C`). Its position is the actual actor `+0x60/+0x64`.
When heading-aware markers are enabled, angle is track u16 `+0x98` minus
actor u16 `+0x3B4` minus filtered map angle plus the native90-degree
conversion (`0x0058EA57..0x0058EA80`). Normal opponent color table is
`0x007313DD`, submission kind5. A selected actor pointer at map `+0xA8`
chooses a different color/kind; this field is cleared at the start of the
traced draw, so a persistent selected-opponent highlight is not established.

Single-view player drawing follows `0x007361BC -> +4` at `0x0058EBB9`,
uses the same two eligibility gates and heading expression, and normally
selects table `0x007313D4`/kind1. Actor `+0x0C -> +0xC54 == 1` changes
its color table. The extra role1 loop at `0x0058EE82` additionally requires
config `+0x734 != 0`. Its runtime meaning is not inferred from its value.

**M.** The synthetic fixture runs the real role-partition producer, then
the real map selection consumer. Independent ID100/101/102 records get
marker requests; changing either gate on100 removes only100; role3 ID50
does not get a request. A role4 fixture is distinguished from opponents.
Geometry probes then independently show culling outside the map polygon.
These fixtures validate selection logic; they are not simulated competitors,
event0 gameplay, evidence of live flag values or proof that every opponent
is visible at every race phase.

**U.** Writers/lifetime of `+0x4D0/+0x8A9`, player color selector, dynamic
camera/style changes, spectator/finish filtering and final event0 values
still need runtime evidence. Do not rename an unknown gate `is_finished`,
`is_loaded` or `is_visible` merely to make an API convenient. Traffic's lack
of a marker in this recovered routine does not authorize omitting traffic
from physics or displaying roles4/5 as event0 rivals.

## State/visibility evidence and limits

**R.** Low visibility-mask bit1 controls the map's lifetime:
`0x0042EC10` allocates it on the rising edge and frees it on the falling
edge. `0x00409ED0..0x00409EF5` draws each view's live map object under an
additional global render gate. This is separate from the shared FEng
start/finish/backing component described in #268.

On the ordinary mask branch `0x0042E8D9..0x0042E907`, selected style6
omits bit1; non6 sets it. Other branches differ, so this is not a universal
`style != 6` visibility function. Synthetic whole-mask probes retain those
differences and finish suppression, with non-map helper predicates explicitly
stubbed to zero. Their expected branch matrix is in the test.

| Scenario | Evidence delivered | Still unresolved |
| --- | --- | --- |
| Grid/start | R source grid/ForwardStartLine and projected native coordinates | U event0 style, camera flyby and exact first visible frame |
| Turns/heading | M rotated UV boundary, marker heading, separation axis and native camera filter | U continuous original camera/marker agreement |
| Lap seam | M samples around actual forward start marker remain continuous; no progress wrap input | U event0 lap transition's mask/camera lifecycle |
| Reset | M authoritative player-grid pose reprojects to the same centered marker rule | U real reset timing, filter reset/persistence and flash/gate writers |
| Racer separation | M separate role2 IDs and positions, independent eligibility and polygon culling | U actual race-wide snapshot/update order and offscreen retention |
| Outside route/map | R/M no clamp-to-route; world pose projects even off the path; center outside visible UV polygon is culled | U reset threshold/actor-gate policy, UV sampler beyond artwork |
| Pause/retry | M equal tick holds the supplied direction filter; creation/destruction is mask-controlled | U real pause/retry generation and camera sequence |
| Finish | M supplied finished view removes map bit in exercised mask branches | U original event0 player-before/after-rivals and results transitions |

## Immutable integration handoff for the next batch

**P.** Contract `minimap266-v1` is this document plus its three tools at the
research PR's exact head. Its input dependencies are immutable source versions
in substrate `8090cb058c658089c83cc101299861754e03504e`:
`docs/EVENT0_SETUP_HUD.md` (#268), `docs/HUD_253.md`/`docs/RACE_HUD.md`
(#253), `docs/HUD_254.md` (#254). #258 and #269 are future producers,
not implemented dependencies silently supplied by this research. Any producer
change must name a new full SHA and reconcile the contract explicitly.

The later map renderer should consume an immutable snapshot, published after
the same simulation/status tick used by timing and standings:

| Input | Producer/meaning | Required handling |
| --- | --- | --- |
| `session_generation`, `tick`, `event_id`, `track_id`, `direction` | #268/#258 setup and race session | Reject stale generation; event0/track1003/forward is explicit |
| `map_resource`, `map_transform` | validated track-wide retail metadata/resource loader | Own once per event, independent of streamed geometry; fail unsupported resources |
| `view_actor_id`, `actors[]` | #258 stable ID/role, raw world position, angle convention, pose discontinuity | Read actual published state; never synthesize competitors from a racing line |
| `map_eligibility` with validity/provenance | #258 adapter for recovered original gates | Keep unavailable distinct from false; trace writers before production defaults |
| `camera_direction`, `world_ticks`, `map_style` with provenance | view/session owner and verified options | Native orientation needs camera/filter state, not only player yaw |
| `visibility_mask`, `finished_view`, `view_state` | #268/#269 lifecycle adapter | Map applies its bit; renderer does not recompute standings or advance time |
| `presentation_transform`, clip and material policy | shared HUD integration owner | Establish final coordinates/sampler/blending; keep #253/#254 layout intact |

World-coordinate convention must be explicit at the adapter boundary:
`raw_world_x/raw_world_y` are the retail horizontal slots, not blindly the
renderer XYZ axes. Convert once using the existing verified track/vehicle
coordinate adapters. Carry heading as the traced u16 convention until its
adapter from authoritative vehicle state is proved. Marker positions do not
come from lap counts, closest AI points, name order or standings indices.

The renderer may retain presentation filter state keyed by generation/view;
it must never mutate vehicle, timing, ranking or eligibility producers.
Reset/discontinuity behavior is a pending evidence decision, not permission
to smooth a teleport through the whole map. On retry, discard prior-generation
presentation state; prove the original within-generation reset policy before
claiming parity. Keep finish visibility separate from per-opponent eligibility.

Suggested bounded implementation sequence:

1. Trace missing camera/option/eligibility/sampler/final-transform producers and
   collect the exact original event0 lifecycle. Choose supported styles from
   evidence, not from the screenshots' apparent shape.
2. On a coordinator-approved integrated SHA after #253/#254, add a separate
   map resource/projector and fixed-capacity draw output. Register atlas or
   separate-texture requirements explicitly; do not overwrite the gauge/lap
   atlas while appending the map. Retain native projection/clipping or document
   and independently measure any changed numeric implementation.
3. Bind #258's real coherent roster/poses to stable IDs and verified gates;
   bind #269's view/finish mask. Player-only output is intermediate and cannot
   close event0. Compare actual original/port positions across the scenario
   matrix above. Preserve existing gauge and lap/time behavior.
4. Run applicable default/Release plain retail CTest with zero skipped retail
   tests, Switch/shader/NRO checks, ASCII gates and actual CPU/GPU evidence.
   Obtain owner visual acceptance and paired map-on/map-off hardware cost at
   `--scene-height 540 --gpu-boost on`; record measured clocks separately.
   Full-field performance must be measured with real opponents.

## Blocking original evidence and PR scope

The missing original clip remains the request in
[#268 comment7020](https://git.accad.pro/murzintsevav/nfsu-switch/issues/268#issuecomment-7020).
For this map follow-up, preserve all HUD corners and record selected map/camera
options, new-career entry through grid/GO, turns with separated rivals, lap
crossing, a safe reset/out-of-route sample, pause/retry, and finishes before
and after opponents. Match the executable/build and source settings. A still
from another race cannot establish the event0 option/flag lifecycle. The
coordinator schedules that evidence; no Switch artifact is deployed by this
research and no private owner images are uploaded.

The research delta is this document and three Python tools. Relative to
current `origin/main` at `6e65e30e43b1393ee5e32d5a075c86778e45603c`, the
assigned substrate already contains pending integration changes. A PR to
main therefore also displays that inherited delta; it is not a fresh
validation of those C++/Switch changes or a merge-ready combined release.
The coordinator must reconcile/sequence the substrate before merging the
research branch. Review this new delta against the assigned full substrate
SHA, while also measuring the required fresh `origin/main...head` scope.
No inherited default/Release/Switch PASS is claimed. For the research-only
delta, the native probe/tests and ASCII gate apply; no C++/CMake, shader,
Switch or kanban file is changed by this worker.

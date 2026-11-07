# Car ground shadow fidelity, issue 267

Research only. Source inspected and desktop binaries measured at
`f5b08a3a3f698be23ac6bc58a861e8c3527f329d` on 2026-10-02. No renderer,
physics, default, frame pin or comparison tolerance changes are proposed for
this PR. [Issue 267](https://git.accad.pro/murzintsevav/nfsu-switch/issues/267)
follows [188](https://git.accad.pro/murzintsevav/nfsu-switch/issues/188) under
[181](https://git.accad.pro/murzintsevav/nfsu-switch/issues/181).

The current shadow is a deliberately planar approximation. The strongest
new findings are a recovered geometry-bounds footprint producer, explicit
terrain rejection rules, world-vertical fade/alignment rather than the
port's road-relative values, and a CPU/GPU shadow sampler mismatch. These
are reproducible algorithm/state differences. Their visibility in the
owner's original game and Switch gameplay is still missing evidence.

## Evidence and reproduction

Evidence classes used below:

- Observed: bytes, instructions, source, or output of the named command.
- Deduced: arithmetic/control-flow consequences of those observations.
- Approximate: a stated substitution in the current port.
- Unknown: runtime state or matched visual/hardware evidence not collected.

The five input identities are checked by
`tools/car_shadow_fidelity_trace.py`: `speed.exe`,
`Cars/240SX/TEXTURES.BIN`, `fx/world.efx`, `Global/GLOBALB.BUN` and
`Cars/240SX/GEOMETRY.BIN`. Their sizes and SHA-256s are the provenance in
[CAR_SHADOW.md](CAR_SHADOW.md), [MATERIALS.md](MATERIALS.md) and
[PART_PLACEMENT.md](PART_PLACEMENT.md), and the helper's `IDENTITIES` table.
The executable is the same PE32 image, base `0x00400000`, with equal raw
and virtual section offsets. Addresses below are virtual addresses.

Run from this worker's repository root, including when its ancestry contains
non-ASCII characters. Only output path components created by these commands
are ASCII. Use a fresh output directory for every run:

```text
python tools/car_shadow_fidelity_trace.py --root <retail-root> --out out/267-trace --prior
```

Requires a real Python 3.8+ interpreter, Capstone 5 (measured with 5.0.7),
and `cmp` on PATH or in Git for Windows as required by the prior trace.
`--prior` reproduces the existing issue-188 helper before the follow-up.
The output contains an ASCII instruction/numeric trace, before/after
manifests of every file and directory, and the nested prior file manifests.
The outer finally block compares manifest bytes even on failure. It rejects
an existing output directory and output inside retail before reading input.
No decoded asset is written by this helper. Its solid-header inventory is
numeric, not an installed-part selector or a replay of a live RideInfo.

Historical run at the inspected SHA, Windows, Python 3.8.4, Capstone 5.0.7:
the prior trace printed `PASS: bounded shadow evidence read` and
`RETAIL MANIFEST EQUAL 476 cmp exit 0`. The follow-up printed
`RETAIL FILE/DIRECTORY MANIFEST EQUAL 558 entries` and
`PASS: trace saved; retail unchanged`. These counts describe that input
installation, not a required count for other owners' installations.

For desktop evidence, put the configured compiler's libraries on PATH.
This run used `C:/Strawberry/c/bin` before `C:/msys64/mingw64/bin`:

```text
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DNFSU_RETAIL_ROOT=<retail-root>
cmake --build build-release --target nfsu_race nfsu_probe nfsu_race_run_tests -j 4
python tools/frame_budget_evidence.py <retail-root> out/267-replay-bracket run -- ./build-release/nfsu_race.exe --root <retail-root> --input tests/recordings/event1.txt --out out/267-port-frames --frames 1,180,600,6000 --particles=off
python tools/frame_budget_evidence.py <retail-root> out/267-run-bracket run -- ./build-release/nfsu_race_run_tests.exe tests/recordings/event1.txt <retail-root> --sample-draws
python tools/frame_budget_evidence.py <retail-root> out/267-texture-bracket run -- ./build-release/nfsu_probe.exe export-textures <retail-root>/Cars/240SX/TEXTURES.BIN out/267-textures tga
```

These unchanged-source Release targets built with GCC 13.2.0. Both replays
finished, returned zero, and produced nonempty outputs without error lines.
`nfsu_race_run_tests` printed
`PASS: exact per-step stream and result; frames 815 max draws 1831`.
Each wrapper printed `manifest files 476 equal True`. The frame directory
was fresh; all four requested TGAs were newly written. This is bounded
desktop evidence, not a full C++ default/Release gate claim. Current replay,
startup, command, triangle and resource pins remain in
`nfsu_race_run_tests` / `nfsu_race_run_retail` and
`nfsu_race_replay_check`, not copied here as current document pins.

The exported texture is local evidence only. At the inspected SHA with the
verified stock pack, its decoded RGB was black everywhere and alpha spanned
0..255. One horizontal edge on row 3 was alpha 0 at x=54 and alpha 17 at
x=55. This is a historical asset observation, independently reproducible
with the existing exporter and an RGBA image reader; no TGA belongs in Git.

## Footprint producer and transform

Observed producer chain, newly resolved:

1. Constructor `0x0056A130` saves its first argument through `ebx+8` and
   loads it into `esi` at `0x0056A163`. It is CarRenderInfo, not CarTypeInfo.
   Consequently, the later `[ebx+8]` reloads at `0x0056AB64` and
   `0x0056AB99` refer to the same render object. Its `+8` separately holds
   CarTypeInfo (`0x0056A364`). This distinction matters for producer tracing.
2. `0x0056A919..0x0056A93A` initializes render `+0x10/+0x14/+0x18` to
   positive maximum floats and `+0x20/+0x24/+0x28` to negative maxima.
3. After resolving each slot's four LOD models, `0x0056AAFC..0x0056AB70`
   accepts only slot indices 0..11. It takes the first model's solid
   pointer, copies solid `+0x20..+0x28` and `+0x30..+0x38`, or supplies
   zero vectors when that pointer is absent. Helper `0x00567890` reduces
   these into component-wise minima/maxima. It does not average them or
   compute half extents. The first model is LOD A by the slot/LOD layout
   already established in CAR_SCENE.md; resolving all LODs is not unioning
   all LODs.
4. `0x0056AB99..0x0056ABEC` subtracts/adds float `0x006CC7B8` (0.1) from
   all three bound axes. Slots include base, bumpers, mirrors, body,
   wide-body, roof, hood, trunk, skirt and spoiler; wheels, exhaust and
   lamp slots do not enter this reducer. An absent slot can include origin.
5. Loader `0x00540004..0x0054001E` makes the aligned `0x00134011` payload
   the solid object itself. Thus these are on-disk solid-header bounds,
   not material bounds from `0x00134B02`. The new helper reports their
   source offsets and values for stock/adjacent A meshes.
6. Draw `0x0056DED2..0x0056DEDF` retains render `+0x20` and `+0x10`;
   `0x0056EB61..0x0056EBD3` scales their X/Y coordinates about local origin
   by `1.05 * (1 + 0.75*f)` in the normal in-game branch. It steps from
   minimum to maximum in thirds. This retains asymmetric bounds and their
   offset. It is not `+-handling.length/2, +-handling.width/2`.

The union is recovered for this constructor path. All possible subsequent
writers, live tuning/reload timing, and the exact original installed RideInfo
in the owner's photos were not exhaustively recovered. A later implementation
must use the installed-part selection, not union every KIT in the pack.
For example the helper prints different front and rear bumper bounds;
their asymmetry alone does not establish a live combined footprint.

Transform `0x005ABDAF` dispatches through `0x006D4AC4`. The x87 body at
`0x005ABDB5..0x005ABE3D` computes row-vector position multiplication, with
translation at matrix `+0x30/+0x34/+0x38`. Matrix `+0x28` is the coefficient
of local Z in world Z: the local-up/world-vertical component. Under a rigid,
normalized car transform it is the cosine of tilt from world up. Static
instructions prove that component, not that every runtime matrix is rigid.
No shadow-local unit conversion occurs in this path. Bounds, wheel table,
road heights, padding, lift and fade thresholds share the game's raw slot
coordinate units. Interpreting those as metres follows the independent wheel
and geometry scale evidence in PART_PLACEMENT.md; no separate shadow unit
system was found.

Each local `(x,y,0)` grid point is transformed before its world Z is
replaced by terrain height. Its XY therefore follows the body matrix,
including pitch/roll foreshortening. The port instead projects body-forward
onto the inferred road plane, normalizes it, and scales a symmetric grid
along road tangents. At a crest or while the body tilts relative to the
surface these are different operations even if footprint dimensions matched.

## Terrain taps, rejection and contact loss

Observed in the normal branch with a road context:

- Four wheel positions come from the RideInfo wheel table, transform by
  the car matrix, and query `0x00440A40` at `0x0056EE20..0x0056EEA0`.
  These are independent query success flags. No port-style suspension
  `WheelState.contact` mask is read here.
- Each of the sixteen transformed grid points queries the same helper at
  `0x0056EEB0..0x0056EEC7`. Its cached-plane fast path evaluates
  `z = (d - nx*x - ny*y)/nz`. The slow path checks candidates and containment;
  on total failure `0x00440C32..0x00440C71` returns input Z, clears success,
  and supplies fallback outputs. Returning a height alone is not success.
  The cache/candidate population policy is not a proved general ray caster.
- On a failed grid query, the preassigned wheel's successful road plane can
  extrapolate Z (`0x0056EEDD..0x0056EF10`). In flattened grid index order the
  wheel map is `[2,2,1,1,2,2,1,1,3,3,0,0,3,3,0,0]`.
- `((i >> 1) ^ i) & 5 == 5` selects indices 5,6,9,10, the inner four.
  They get another 0.08 before the validity tests.
- Helper `0x0056CE90` accepts finite ordered height deltas whose absolute
  value from reference Z is <=4. Helper `0x0056CEB0` accepts absolute
  `dot(point-reference, reference_normal)` <1.5. Equality is rejected by
  the final `test ah,5` / `jp` branch. The latter is a plane
  expression; a distance interpretation assumes a unit normal. NaN and
  flag semantics should be tested directly before a faithful port.
- A successful point must pass both checks. Its lifted height becomes the
  last accepted Z, then another 0.04 is added for drawing. Otherwise the
  point uses last accepted Z plus 0.04 and increments its row's invalid
  count (`0x0056EF61..0x0056EF88`). The initial last Z is transformed origin
  Z (`0x0056ED80..0x0056EDB6`), not a fitted suspension plane.
- Strip r spans grid rows r/r+1. If their invalid-count sum is >=4, the
  entire strip is omitted (`0x0056F280..0x0056F295`). It is not a
  per-triangle cutout or a rule that every failed tap hides the whole shadow.
  A successful fallback need not count as invalid. Both layers share this grid.

The port does no terrain taps in `place_car_shadow`. It selects the first
three active wheel centres for a plane, averages radius-shifted active
centres for an anchor, and projects the body origin onto it. Two or one
contacts use body up as the plane normal. Zero contacts immediately return
zero alpha. GPU still submits its resident draw; CPU skips rasterization.
The existing contact-mask tests check centre stability, not retail projection.

Concrete consequences, conditional on the specified surface/query state:

- Crest/depression/curb under the footprint but outside the supporting wheels:
  let true surface height differ from the fitted plane by more than the
  uniform clearance. The port penetrates a higher surface or floats over a
  lower one. Valid retail taps follow those individual heights, with its
  inner lift. This is an algebraic counterexample, not an observed Switch curb.
- Road edge with four rejected points across two adjacent rows: retail omits
  that strip; the port still covers it. With fewer rejects, retail can carry
  forward last height; assuming every failed tap disappears is also wrong.
- Losing the third active contact changes the port's fitted normal to body
  up; losing the last changes visibility to zero. This can pop even while
  the retail grid still has valid road queries. A four-to-three transition
  can also change the chosen triplet. The sample replay does not prove those
  contact transitions occur at a particular recorded tick.
- Low airborne car over queryable road: retail has height fade without a
  wheel-contact gate; the port hides immediately on zero contacts. Landing
  reverses that discontinuity. If retail context/taps reject the road or
  height fade reaches its upper threshold, retail can also vanish. "Airborne
  always casts a shadow" is not established.

## Fade, color and the second effect

Normal in-game fade is `f = clamp((origin.z - road_z)/3, 0, 1)` from
`0x0056E3AA..0x0056E42C`, with lower/upper floats 0/3. A missing road
context/front-end mode takes separate branches; globals can also affect
this function. The port measures `max(0, dot(origin-road, road_normal))/3`.
For a 30-degree planar slope and an origin vertically one unit above it,
the retail gap is 1 while the port normal gap is cos(30 degrees). The fade
and footprint expansion differ without any terrain curvature.

`0x0056EFD9..0x0056F05A` produces normal in-game RGB `0x80` and alpha
from `114 * abs(matrix[+0x28]) * (1-f)`, converts to an integer, and clamps
to 0..254. `world.efx:61` then multiplies texture, vertex color and two.
The port folds doubling into continuous `228/255 * alignment * (1-f)`.
For three or more contacts its alignment is body up against fitted road
normal, whereas retail uses world vertical. On a body parallel to a
30-degree slope the port alignment is 1; the retail rigid-matrix component
is cos(30 degrees). Below three contacts the port changes that alignment
reference again. The combination can make its shadow darker on slopes;
these equations do not fix a net pixel difference at an unmatched pose.

Retail quantizes vertex alpha before texture modulation; CPU rounds scaled
texel alpha. The default GPU non-cutout shader also rounds scaled alpha,
whereas the cutout shader's special shadow branch writes fractional scaled
alpha. Thus the continuous fold is not bit-identical to retail even on a
flat road. Integer conversion
mode and final device blend rounding need runtime parity evidence. The
RGB-unity substitution has no effect on the measured all-black SHADOWIG
texture. Source-over black is ground darkening; switching to a different
blend-factor pair or tuning opacity by eye has no supporting evidence.

Constructor `0x0056AC73..0x0056ACBD` resolves `%s_NEON` (string
`0x006BBD0C`) into render `+0x4BC`, unlike `%s_SHADOWIG` into `+0x4B4`.
The second draw uses separately animated color at
`0x0056F061..0x0056F22F`, and initialization assigns its runtime row 4,
versus shadow row 2. It is an underbody neon effect, identified by producer
string and separate pointer/color, not a second ground-darkening pass.
Its full color-controller/settings behavior remains outside this task.
Existing lamp glows are not evidence that this adjacent effect is reproduced.

## CPU and deko3d audit

| State | Retail normal path | CPU race shadow | deko3d race shadow | Conclusion |
| --- | --- | --- | --- | --- |
| Texture | Stock SHADOWIG, sampled soft alpha | Same decoded texture | Same RGBA top level | Identity verified; no invented ellipse |
| Blend | SRCALPHA/INVSRCALPHA from texture +86 | source_over | source_over variant | No confirmed factor defect |
| Depth | enabled, LESSEQUAL; immediate draw disables writes | shared track depth; no write | less_equal; no write | Local state agrees; different caller/flush state unproved |
| Cull | NONE in world.efx | race options default none | override none | No confirmed winding/cull defect |
| Sample | WRAP in world.efx; device MIN/MAG LINEAR setup | RasterMaterial default nearest/repeat | car_textured manifest linear/repeat | Confirmed CPU/GPU mismatch |
| Alpha | integer vertex alpha then texel modulation x2 | continuous placement, byte rounded sample/alpha | default shader rounds scaled alpha; cutout branch uses fractional alpha | Retail quantization and diagnostic-path differences remain |
| Lift | +0.04, inner points another +0.08 | uniform +0.12 along fitted normal | same transform | Same approximation on both paths |
| Ordering | runtime row 2, column 0 in a shared fill | all track, shadow, separate car then compose | opaque-first partition then stable remainder | Whole-frame parity is not established |

MINFILTER=2 at `0x00408BD2..0x00408BD8` and MAGFILTER=2 at
`0x00408C21..0x00408C27` go through device vtable +0x114 for sampler 0;
the other initial samplers get the same setup. These are D3D9 LINEAR values.
`world.efx` declares wrap but no filter override. This proves default state
in the traced path, not every later device setting. Port GPU sampler selection
comes from `car_textured.manifest`, and CPU `render_car_shadow` leaves
`RasterMaterial::linear_filter=false`. Track filtering work in
[262](https://git.accad.pro/murzintsevav/nfsu-switch/issues/262) is separate.

A shadow-only sampler counterexample uses the actual alpha edge reported
above. At `u=55/128, v=3.5/64`, CPU nearest selects alpha 17; linear mixes
0 and 17 at the halfway point and rounds the sampled byte to 9. Black RGB
is identical. This predicts a different darkening edge without a glass
pixel, a different road shader or altered opacity. It is an arithmetic
prediction from the exported pixels, not a GPU readback measurement.

CPU race renders the full track and shadow into background, renders the car
over its own clear color/depth, then `compose_race_frame` copies car pixels
whose depth is no farther than track depth. Transparent glass can therefore
already have blended with the car framebuffer before composition. GPU uses
one scene framebuffer. Its cached command stream is track, shadow, car;
`deko_visit_opaque_first` moves depth-writing non-cutout opaque draws before
the rest, preserving each partition. Consequently opaque car pixels precede
and depth-reject the shadow; car cutouts/glass follow it, but later blended
track entries still precede shadow. With opaque-first disabled, all car draws
follow shadow. Neither is the complete shared retail bucket walk.

Retail shadow texture row/group are overridden to 2/0 at
`0x0056B099..0x0056B0A2`. Inside a common fill, opaque row-0 car/world precede
shadow and later glass/neon/world rows follow it. Multiple flush cycles
remain unresolved as documented in MATERIALS.md. Do not globally sort all
world and car draws based solely on this local bucket evidence.

Source-over operations are not commutative: a black shadow after a later
bright blended road layer attenuates that layer, while shadow before it
allows the layer to add/blend over the darkened ground. An overlapping,
depth-passing later layer is the concrete failure condition. Whether a
particular street lamp patch meets it needs a command/flush trace. The
known car-glass discrepancy cannot be attributed to shadow without an
on/off causal comparison restricted to ground and glass regions separately.

The actual GPU shader selection is another important boundary:
`DekoDrawRenderer::emit_draw` chooses `textured_opaque.frag.glsl` for the
shadow when alpha testing is off and frame alpha cutoff is zero. The
`material.alpha.w == 4` shortcut exists in `textured.frag.glsl`, but is
absent in that default non-cutout shader. In the latter, black texture RGB
stays black through ordinary shade/vertex modulation, its environment
constants are zero, lamp mode 4 contributes zero, and alpha.y is zero.
It therefore still darkens correctly apart from sampling/rounding. This
is agreement through the black asset/default constants, not execution of
the intended shortcut on both shaders. It is not a confirmed color defect
for this asset. A diagnostic with nonzero alpha cutoff selects the other
shader and changes rounding; a future nonblack shadow asset would need an
explicit world-effect path in both shaders. Parity fixtures must name the
actual shader and cutoff rather than assume the manifest source alone is
the executed shader.

## Reference comparison and missing observations

The reference directory is beside the original repository. Resolve it from
the original checkout's runtime location, then append ASCII `reference`;
never reconstruct its path from Git stdout. For this worker tree under
`build/267-gpt6-worker`, `Path.cwd().parents[2] / 'reference'` resolves that
directory. Inputs were read only. The thirty images include duplicates,
menu/cinematic views and gameplay shots. They are not thirty independent
shadow/contact observations.

| Condition | Original references inspected | Port evidence at inspected SHA | What is established / missing |
| --- | --- | --- | --- |
| Rest/countdown | retail-01.webp / retail-05.jpg / retail-06.jpg show orange car near the same landmark; timing/identity unverified | fresh event-1 frames 1 and 180 | Port displays road darkening at startup; no claim that original first pixels or footprint match |
| Turning | retail-07.jpg / retail-08.jpg near the intersection | frame 600 | Same area is recognizable; car finish, exact camera, pose and elapsed time differ; no pixel metric |
| Slope/crest | retail-16.jpg / retail-19.jpg / retail-30.jpg are moving, different locations; some duplicates | frame 6000 shows a later street view | No matched crest pose or known wheel/road heights |
| Road edge/curb | retail-21.jpg / retail-23.jpg suggest edge/intersection positions | no matched controlled curb run | No query success flags or curb cross-section from a photo |
| Partial contact | no independently confirmed contact state | source contact-mask coverage only | Need reproducible pose and query/contact telemetry |
| Airborne/landing | no matched sequence | no controlled airborne sequence | Need low jump/landing motion; still cannot recover internal alpha from a photo alone |

For provenance of selected original references, SHA-256s observed here:

```text
retail-01.webp 9cb81ca0b68862b31b9bc57e4fbdf27dee94d025062172fa6e8af666368a05bd
retail-07.jpg 477c94bc366294ea31261d8d226b1a25fdc6a55f23214fa97e77cdc9c2c8d830
retail-08.jpg 5f75b5722907de0d186901bb29eed252199f1524117507869a61e4f6684e8c1a
retail-19.jpg a0edbdc87a266947d46ec9ada3fb9bdb3e3131a1d650d7728676f8c953d73672
retail-21.jpg 347ab6b92689586dfd0c7148502240b1947280435e0b1ae37c2cee65d90e0dc9
retail-23.jpg c347aeec8f494e117e362a30bf692bb1b8532e43da45b4ce214771db5f09d15a
```

No image-based fit of alpha, dimensions or ground height was performed.
No matched-original PSNR, console parity or console frame-time PASS is
claimed. FRAME_COMPARISON.md defines fixed-resolution GPU/CPU criteria;
agreement between two planar implementations would still not validate retail
terrain projection. A passing whole-frame score can conceal a small shadow
edge, so future diagnostics need separate shadow-region reporting without
weakening the existing whole-frame thresholds.

## Ranked differences and bounded next work

Confidence is in the algorithm difference, not an unmeasured screen defect.

| Rank | Retail evidence | Current implementation | Concrete difference / impact | Confidence | Smallest proposed action and test |
| --- | --- | --- | --- | --- | --- |
| 1 | Per-point road queries, rejection and strip masks | One contact plane; uniform clearance | Nonplanar footprint or rejected adjacent rows: clipping/floating/coverage across an edge | High static; visual severity unknown | Render-only bounded grid sampler, wheel-plane fallback and three strip masks; synthetic crest/curb/invalid-row cases plus retail replay |
| 2 | Road-query success independent of suspension contacts | Zero contacts hides; fewer than three uses body plane | Low flight/landing and contact-count transitions: vanish/pop or tilt jump | High conditional | Share sampled road/fade policy with rank 1; masks 0..15 with independently successful/failed terrain, contact transition sequences |
| 3 | World Z gap and matrix world-up component | Normal distance and road-relative alignment | Body following a slope: different fade, expansion and strength | High static | Port recovered normal-branch formula with quantization evidence; flat/30-degree slope, pitch/roll, upper/lower boundaries and tilt/contact combinations |
| 4 | Constructor union of installed LOD-A solid bounds plus padding | Symmetric authored handling length/width | Asymmetric bumper or wider kit: wrong size/centre even at rest | High producer; runtime refresh incomplete | Load-time installed-part bounds helper; stock/tuned/asymmetric/absent-slot numeric tests; exclude non-body slots; validate actual RideInfo before adoption |
| 5 | Default linear/wrap world texture state | CPU nearest, GPU linear | Soft-alpha edge samples disagree | High source; hardware unmeasured | Shadow-only CPU linear selection and dedicated same-buffer parity test; keep #262 world/car filtering policy out of the patch |
| 6 | Row-2 shadow in a shared retail fill | After all blended track; CPU separate car framebuffer | Overlapping later bright layer gets darkened; glass background differs | High source difference; scene/flush causality unresolved | Trace one shared fill with overlap, isolate shadow on/off. Correct only proved local ordering; car compositor repair needs separately authorized scope |
| 7 | Vertex alpha integer then modulation | Continuous fold and different final rounding | Small edge alpha error on flat ground | High equation difference; conversion/rounding unknown | Recover conversion/device behavior and test alpha boundaries before selecting rounding; no opacity tuning |
| Keep | Retail asset, source-over, local depth/cull | Same asset/factors/local state | No demonstrated local-state defect | High bounded | Preserve these states; no blend rewrite, blur pass or shadow map |

All corrections require a later implementation assignment. Rank 1/2 can be
one bounded rendering change, with no solver or wheel-contact changes. Rank
4 can run at load/part change. Rank 5 is a small independent CPU parity fix.
Rank 6 must not expand into an unbounded renderer ordering rewrite. Source
repinning requires actual changed-frame review; no reference pin is changed
in this research.

## Cost and paired Switch GPU SCENE experiment

Historical structural census at the inspected SHA: one resident shadow draw,
16 vertices at 36 bytes, 54 indices at 2 bytes, and 128x64 decoded RGBA8
top level (576 + 108 + 32768 payload bytes, excluding alignment/objects).
The per-frame transform/material contents are 64/80 bytes; deko3d reserves
256-byte aligned uniform entries. Resident payload upload is at load, not
a per-frame texture upload. Zero-alpha GPU draws still incur geometry/state
work; zero-alpha CPU draws exit before rasterization.

Current placement does constant small wheel/vector work and no ground queries.
Retail normal branch issues sixteen grid calls, four wheel calls and up to
two reference/fade calls. These are helper invocations, not necessarily that
many expensive traversals: the helper has a cached-plane fast path. The port
contact API currently allocates temporary vectors per query. A naive sixteen
query addition has unknown Switch cost and cannot inherit retail efficiency.

Proposed implementation budget: fixed sixteen position outputs, fixed four
fallback records, three strip flags, one bounded reused query context or
scratch area, no renderer-driven physics mutation or retained frame allocation.
Upload only changed grid positions and masks, fenced per presentation slot.
One triangle-list draw can use bounded index ranges or degenerates for omitted
strips; three strips need at most three draws per car. Position-only payload
is 192 bytes per updated grid; uploading full MeshVertex arrays is 576 bytes
per slot. Those are prospective size calculations, not achieved allocations
or GPU timings. Decide the index strategy from a test of shared-edge coverage
and upload/deko3d synchronization, not a claimed speed benefit.

Paired measurement design for a later authorized diagnostic:

1. Pin one exact source SHA and NRO SHA-256. Add a diagnostic shadow-on/off
   selection that changes only the shadow draw at command submission. Keep
   the placement CPU calculation in both variants first. In a second pair,
   bypass preparation as well to isolate its CPU cost. Neither pair changes
   simulation, residency, geometry selection, texture policy, sky, glass,
   particles, camera or HUD. Add counts proving exactly which draw changed.
2. Use the same recording `tests/recordings/event1.txt`, fixed camera policy
   and exact sample window after countdown. Run ON/OFF/OFF/ON, fresh process
   for each, >=60 seconds driving per measured run, warmup excluded equally.
   Verify replay state/trajectory equality, draw difference and no input errors.
   If the existing report cannot export a matched window, add a bounded
   capture/report facility before claiming a pair.
3. Handheld gameplay baseline is explicit `--scene-height 540 --gpu-boost on`
   (960x540 scene, requested existing 460.8 MHz GPU profile). Keep sky on and
   all gameplay effects/settings equal. Record page 1 actual mode, requested
   configuration and observed GPU/CPU/memory clocks when available. A profile
   request does not prove the measured clock. Docked uses its native scene
   and existing clock policy in a separate pair. No handheld 720p gameplay
   or boost-off run is required first.
4. Retain page 7 GPU SCENE window count, mean, p95, maximum and worst step,
   plus full-frame pacing, CPU preparation/upload stages, retained/peak memory
   and draw/triangle/upload counts. Report paired deltas with repeat spread;
   a delta below variability is unresolved, not zero cost. The scene timestamp
   includes upscale/barriers at 540p. Whole-frame DRAW/fence wait is not
   isolated GPU service and cannot establish a shadow speed gain.
5. Capture resting, turn, crest, curb and low jump/landing views for appearance
   in both variants. Use fixed-resolution offscreen comparison separately
   for CPU/GPU parity, with shadow-only and glass-only difference regions and
   existing whole-frame criteria. Do not infer gameplay performance from the
   offscreen capture/fence path.

No suitable shadow toggle or exact matched-window report exists in the
inspected gameplay binary, and no temporary measurement variant was authored
here. Therefore no executable/hash is offered as a ready shadow A/B build.
The [HANDHELD_FRAME_BUDGET.md](HANDHELD_FRAME_BUDGET.md) and
[SWITCH_PERFORMANCE.md](SWITCH_PERFORMANCE.md) measurements are context, not
shadow cost. Issue 55 retains performance acceptance.

The coordinator-facing CONSOLE_REQUEST accompanying this PR asks first for
matched original captures with identified car/kit, track/event, camera,
graphics mode, countdown/rest, slow turn, curb/crest and low flight/landing.
Internal original query flags/matrices need a separate read-only runtime trace
or equivalent evidence; photographs cannot supply them. A later diagnostic
assignment must produce the exact NRO/source hashes and ready commands before
the paired Switch experiment can be scheduled. Exit with Plus for the port
diagnostic; original capture exits via its normal pause/quit controls. The
worker neither deploys nor closes the parent tasks.

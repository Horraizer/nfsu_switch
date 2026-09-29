# Sky and fog research (issue #184)

This document distinguishes RECOVERED instructions/data from APPROXIMATION.
The current evidence is static analysis, not a capture of retail GPU state.

## Evidence

`speed.exe`, SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`,
was disassembled with Capstone 5.0.7. Addresses below are virtual addresses;
in this image their file offset is VA minus 0x400000.
`fx/world.efx`, 2404 bytes, SHA-256
`D1CDBD788D564C4CD408A060D0EDA4E99245F0EB54B18A4FCEE848D2966E6C98`,
is XOR-0xAA source with a trailing decoded 0xFF byte.
Retail inputs are read only; research runs compare before/after manifests
with `cmp`. No decoded asset is checked into Git.

## Sky placement and submission

RECOVERED: 0x004450AB..0x004450BA initializes a separate SKYDOME object,
hash 0xBE43EDBB, through 0x005715D0. Its object is at 0x0073C1C8 and
its matrix at 0x0073C220. This explains the general scenery loader's
intentional skip of that hash at 0x0044590B.

0x005715ED resolves the solid. 0x00571602..0x00571634 initializes an identity
matrix. 0x00571637 calls 0x00445CA0, whose loop walks loaded scenery sections
and their instances and returns the first instance with the requested slot-0
hash (0x00445CD0..0x00445CDA). 0x00571649 decodes that instance's matrix;
0x0057164E..0x00571668 copies its three position components. This is a
world placement, not a camera-position addition at this initialization site.
No claim is made that a runtime debugger has excluded every later write.

0x00571870 draws the separate object for view IDs 1..3, gated by 0x006EEF60;
0x0057188A..0x00571897 passes the matrix, object and view to 0x0040C910.
That function submits the mesh material ranges through the common queue at
0x0040CA54. Texture selection follows each material's texture slot:
0x0040C9F8..0x0040CA03. There is no time-of-day texture selector at these
sites. Other views 10..15 select a different reserved object.

For the main-view path, 0x004098FE submits the sky, 0x00409972 flushes the
queue, 0x0040997E submits scenery and 0x00409983 flushes it. Thus sky is
submitted before scenery. The auxiliary-view path explicitly disables
ZENABLE and ZWRITEENABLE at 0x004096B0/0x004096C2 around the sky submission,
but restores them at 0x004096DB/0x004096ED before its queue flush. These
calls alone do NOT prove that queued sky fragments run with depth disabled.
The common material state can also change ZWRITEENABLE; see
[TRACK_MATERIALS.md](TRACK_MATERIALS.md).

The sampled event-1 permanent set has one SKYDOME instance in section 2600.
Its mesh has two material groups. It is already decoded and uploaded by the
track loader, but ordinary scenery placement intentionally leaves it unplaced.
The separate sky pass must use the first matching instance, not make one sky
copy for every scenery reference and not infer an orbit from the camera.

## Fog evidence and limits

RECOVERED: global graphics setup 0x00408B84 clears EDI, then
0x00408BAC..0x00408BB0 calls SetRenderState(28, EDI): FOGENABLE is false.
The decoded `world.efx` vertex function (lines 47..56) outputs position,
UV and vertex colour. Its pixel function (58..65) computes texture times
vertex colour times two; neither function calculates fog. Its two techniques
(67..122) do not declare a fog render state.

The executable contains names FogOffset, FogScale, FogFalloffY and FogColor
at 0x00702120, 0x00702160, 0x007021A0 and 0x007021E0. They belong to the
0x40-stride effect parameter-name table: 0x0040E458..0x0040E47A queries
every name between 0x00701860 and 0x00702420 for each effect. The presence
of a queried name does NOT demonstrate a live shader parameter or a value.

Inspection of the seven shipped effect sources found no fog-distance or
height-falloff formula. `filter.efx` has a FOG output assigned constant 1;
that is not a distance-fog equation. The direct state-call byte-candidate
scan found the disable above and no immediate fog start/end/density setter;
this scan is not a proof about indirect or computed state calls.

Unknown: a live fog colour, start/end/density, an attribute-to-renderer path
from GLOBALB, and any per-track/time-of-day fog selection. It would be false
to publish invented values as recovered retail fog. Fog remains disabled
pending a traced rule or an explicitly requested visual approximation.

## Implementation plan and GPU cost, before implementation

The intended sky pass borrows the existing mesh and textures, uses its world
placement, and samples the texture with unlit vertex-colour modulation. The
existing nearest sampler and byte rounding remain APPROXIMATION relative
to the effect's linear filtering. For the bounded race camera, projecting
the dome onto the far-depth background without writing depth is also
APPROXIMATION; the retail main-view depth behaviour is not yet fully traced.

The pre-implementation estimate at base 3b7691f was two extra submissions,
720 input triangles, no extra texture upload, and one sample plus colour
modulation per covered pixel. Current inventory is checked by
`tests/sky_fog_retail.cpp`; the estimate is not a console measurement.
Drawing before blended materials preserves sky behind glass and foliage
holes. No full-screen fog pass is planned while fog is untraced. Millisecond
cost is unknown until the coordinator measures GPU SCENE on the same console,
route, docked/handheld mode and baseline/revision pair. Handheld has no
established GPU headroom; desktop timings cannot substitute for this gate.

Alpha-tested foliage retains GREATER 0xAA and opaque depth writes. Sky must
leave depth untouched so foliage and opaque scenery can occlude it; blended
world draws must see sky in their destination colour. These are composition
requirements, not a newly recovered fog interaction.

## Implemented scope and approximations

The first matching slot-0 instance supplies `sky_transform`. The original
ordinary-scenery skip stays in place. `RenderOptions::sky` enables the separate
pass in the race and track slice; old isolated diagnostic renders opt in
explicitly. The sky borrows the existing vertex/index/texture uploads.
An unresolved sky texture skips only its sky group; resolved world groups
still render. `nfsu_track_render_tests` covers that fallback case.

APPROXIMATION: both renderers place sky immediately before the first blended
world draw (or after the opaque world if there is no blend), project it to
normalized depth 0.999999, use LEQUAL, disable depth writes and culling. This
puts sky behind glass. In the retail bucket order the first blended draw
comes early, so in list order only the few depth writers before it reject
hidden sky; the Switch race renderer therefore submits the sky after its
whole opaque pass (see GPU cost below).
It deliberately differs from the recovered sky-first queue order. It is not
proof of retail depth/cull behavior. The fixed world placement does not make
the sky follow the camera; the approximation is intended for the race route.

RECOVERED DATA, bounded to event 1 / track 1003: the selected groups bind
SKY_OVERCASTA and SKY_OVERCASTA_CAP from TEX1003TRACK.BIN. Their texture
hashes are pinned by `tests/sky_fog_retail.cpp`. `nfsu_probe pack-set`
reports info offsets 716 and 592,
respectively; the data spans are 470784+349184 and 121344+349440. Both are
A8R8G8B8. The main texture has alpha-test/blend bytes 0/0; the cap has 2/1
(alpha test disabled, source-over). These are the material texture choices,
not a time-of-day rule inferred from the other SKY_* names in the pack.
The selected instance is section 2600, instance 0, stream offset 245952,
with identity matrix and raw translation approximately (-692.102,2132.55,0).
No assertion about every track is made.

Both renderers use texture * vertex RGBA * 2 from world.efx, including cap
alpha. Alpha-tested foliage still rejects alpha <= 0xAA before depth writes;
its holes show the background. World blending uses that background as its
destination. Fog stays disabled: no fog tint, density or start/end is invented.
Thus the sky implementation is ready for review, but the requested active-fog
frames are NOT delivered. Coordinator acceptance or further runtime tracing
is needed to resolve that part of #184. Console photo/performance is also
pending; a desktop hash cannot discharge either requirement.

## GPU cost (issue #227)

History: at 2c1b9a7, handheld replay, the sky build fell from the 30 Hz
step to the 20 Hz step on a large share of frames; coordinator comment 5487
on pull request #227 records the console result.

The camera is always inside the dome, so the sky rasterizes over the whole
screen in both of its groups. `nfsu_sky_fragment_probe` replays event 1
through the RaceGpuScene frames the Switch executes and counts, per frame,
the sky fragments that pass the depth test in each submission order: `list`
(the renderer before #227, and `--sky early` approximately), `on` (the race
renderer's opaque-first order, sky after the opaque pass), and the uncovered
floor. It also reports the texel footprint of the uncovered sky. It is
fragment accounting, not a timing:

```sh
python tools/frame_budget_evidence.py "$RETAIL" out/sky-cost run -- build/nfsu_sky_fragment_probe "$RETAIL" tests/recordings/event1.txt 60
cmp out/sky-cost/before.json out/sky-cost/after.json
```

History: at 52fe3ca, desktop Debug build, the command above over the whole
replay (805 frames, 1280x720): per frame 1397184 sky fragments rasterized;
798148 pass depth in `list` order, 823083 in `early`, 185724 in `on`;
162794 are uncovered by any depth-writing draw. The uncovered sky has a
mean log2 texel footprint of -2.06 (magnified); 3.7 percent of it is
minified.

The cause is order, not texture sampling or upload. In list order the sky
is submitted before most opaque scenery, so most sky fragments are shaded,
blended and then overdrawn. The texture is magnified almost everywhere, so
mip levels would not change the cost; no per-frame upload or re-record is
involved (the sky lives in the static track package with its placement).

Implemented for the Switch race renderer:

- The sky's two definitions and its placement are part of the static track
  package; `deko_draw_in_opaque_pass` keeps the sky out of the opaque pass,
  so every depth-writing opaque draw is submitted before it.
- `shaders/sky.frag.glsl` declares `layout (early_fragment_tests) in`. The
  sky writes no depth, so this changes no pixel; it keeps the diagnostic
  alpha-cutoff discard from forcing late depth tests.
  `nfsu_shader_source_guards` requires the declaration.
- `--sky on|off|early` selects the mode for console A/B runs; see
  [SWITCH_RACE.md](SWITCH_RACE.md).

The look is unchanged: both sky textures, both layers and the modulation
stay as above, and the CPU reference and its pinned frames do not change.
The GPU order differs from the CPU reference only where an alpha-tested or
opaque draw follows the first blended draw; opaque-first ordering already
made that difference in PR #221. The console A/B is the acceptance evidence.

## Reproduction and frame pins

From the checkout, set RETAIL to the read-only install path. Each command
below is wrapped with before/after manifests; compare `before.json` and
`after.json` with `cmp` after each run. Output directories must be new.
Capstone 5.0.7 is required only by the disassembly helper.

```sh
python tools/frame_budget_evidence.py "$RETAIL" out/sky-trace run -- python tools/sky_fog_evidence.py "$RETAIL"
cmp out/sky-trace/before.json out/sky-trace/after.json
python tools/frame_budget_evidence.py "$RETAIL" out/sky-frames run -- build/nfsu_sky_fog_retail "$RETAIL" out/sky-images
cmp out/sky-frames/before.json out/sky-frames/after.json
python tools/frame_budget_evidence.py "$RETAIL" out/sky-replay run -- python tools/race_replay_check.py --exe build/nfsu_race --exe build-release/nfsu_race --root "$RETAIL" --out out/sky-replay-images
cmp out/sky-replay/before.json out/sky-replay/after.json
```

Windows executables have the `.exe` suffix. `tests/sky_fog_retail.cpp` owns
the overview and street camera definitions, inventory and TGA hashes. Both
are new pins: the world-placed sky replaces the clear background. It renders
each camera with sky off as well, requires a visible contribution, unchanged
world depth and a complete CPU/GPU draw trace. Debug and Release must agree.
`tools/race_replay_check.py` owns each replay frame pin with its individual
reason; simulation/result/trajectory pins are unchanged. The draw-count pin
in `tests/track_slice_tests.cpp` changes only for the sky groups; the cache
byte pin in `tests/race_run_tests.cpp` changes only for their diagnostic names.

`nfsu_track_render_tests` checks world placement, far clipping, depth/owner
preservation, foliage holes, glass, vertex/texture alpha in the cap, GPU state
and submitted-triangle limits. The Switch gates compile/check the sky shader
interface and NRO contents. A TGSI full vec4 omits its component suffix;
the parser now preserves its default xyzw mask, with a regression in
`nfsu_shader_source_guards`. The unlit fragment needs no driver constants,
which `nfsu_uam_derivatives` checks without weakening the derivative guards.

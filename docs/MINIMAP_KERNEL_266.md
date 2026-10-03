# Bounded original minimap resource and geometry stage

Issue #266, CHECKPOINT7369, batch20261003 slot2. Assigned base:
`798bd8e6570b8e84043a3c4577b01da34b057bff` (pending HUD prerequisite).
No HUD renderer, defaults, actor simulation, camera producer, GPU binding or
console deployment is added. This stage does not close event0 or issue #266.

## Source attribution and evidence classes

The four research files were recovered through Git from independently reviewed
[PR284](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/284), exact head
`f81b9320065a22d37aaecc4b8887714d58283c8b`, authored by the batch1 slot4
worker. [MINIMAP_266.md](MINIMAP_266.md) remains that historical research
contract, including its original substrate and scope. The native helper gains
only an optional explicit marker-size pair for the new differential.

Actor identity and gate-writer qualification were read through Git from
[PR285](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/285), exact head
`7c6a5e30e977160105cad778b843323ac90d978e`. Those files are not imported.
In particular, actor +0x8A9 is not defined as !finished: the ordinary finish
passes zero for the hide argument. Raw gates retain their source offsets.

Recovered executable consumers are static evidence. Unicorn runs are bounded
synthetic execution with explicit scalar dispatch and captured graphics and
binding calls. The C++ differential is geometric measurement against those
runs, not original gameplay, screenshots, GPU rasterization or hardware parity.
No original texture payload, derived image or private capture is committed.

## Usable API

`load_minimap_resource` authenticates the complete retail texture pack and
GLOBALB identities from the research, parses actual TRACK1003_MAP metadata and
track1003 fields, and returns a metadata-only descriptor. Retail inputs remain
read-only. It does not decode/copy artwork into the gauge atlas or retain
streamed road geometry. Missing, changed or ambiguous resources fail clearly.
The source texture is a separate resource; its raw blend bytes are metadata,
not a verified material policy.

`project_minimap_world` reads that descriptor and explicit retail horizontal
slots (x,y). It applies the recovered negation, rotation, offsets and span,
with native scalar sine rather than textbook sine. It reads no lap progress.

`build_minimap_geometry` takes a const resource, explicit native style record,
explicit already-filtered camera/map angle, native placeholder rectangle,
caller-supplied affine final-screen transform, view pose and immutable marker
inputs. Output is a fixed-capacity list of four-vertex pieces with homogeneous
UVQ, native vertex color/alpha, resource hash, kind and stable actor ID. A final
triangle repeats its last vertex, as the native submission does. Native clipping
precedes the supplied final transform. Failed calls clear the entire output;
they never silently truncate. Unknown style, camera, presentation, texture or
eligibility inputs remain different from false eligibility and capacity failure.

The measured branches are styles1,3,5 and disabled6. No event0 default is
selected. Style11's separate cropped rule, selected-target zoom, other styles,
backing/frame output and dynamic style overrides remain unsupported. Callers
must supply valid source inputs; the enabled flag is not a visibility-mask
implementation. Geometry accepts independently identified markers, and does
not partition an actor list, synthesize rivals or choose rank/name order.
The actor adapter owns roster/role selection, raw gate validity and stable IDs.
START_LINE can use its explicit resource hash, native size pair and supplied
heading; its caller owns its source marker and eligibility, distinct from cars.

The scalar sine polynomial follows 0x00564B10, including float constants and
float stores. Intermediate double arithmetic approximates x87 retained
precision; no bit-identical claim is made for every native intermediate or GPU.
The clipping fan's start follows 0x00590830's y/x rule. The fresh differential
pins tolerances in code for projection, UV coverage, UVQ and color, comparing
triangle samples independently of fan decomposition. The report records observed
maxima, driver/tool identities and source head. Expected values stay in tests.

## Reproduce from this workspace

Dependencies are standalone Python3.8+ with Capstone5 and Unicorn2, installed
in this worker's ignored build directory when needed. Native compiler DLLs
must be on PATH. Derive the reference root through runtime filesystem APIs:

```powershell
$project = (Get-Location).ProviderPath
$reference = Join-Path (Split-Path $project -Parent) 'reference'
Set-Location build/day20261003-b3-s2-minimap-projector266
$env:PATH = 'C:/Strawberry/c/bin;' + $env:PATH
$env:PYTHONPATH = Join-Path (Get-Location).ProviderPath 'build/266-b3-s2-deps'
$source = git rev-parse HEAD
$retail = 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground'
python -B tools/minimap266_trace.py --root $retail --reference $reference --out build/266-kernel-native-fresh --source $source
python -B tools/minimap266_test.py --report build/266-kernel-native-fresh
python -B tools/minimap266_diff.py --root $retail --report build/266-kernel-native-fresh --driver build/nfsu_minimap_tests.exe --out build/266-kernel-diff-fresh
```

Use fresh output paths on each run; the tools refuse overwrite. The trace
brackets full retail/reference manifests. The differential authenticates PE
bytes, re-executes geometry for nodes, all grid/start records, turns, outside
centers and native UV boundaries, and brackets full retail manifests around
its own reads and C++ invocations. Nonzero/empty/error driver output fails.
Its outputs contain numbers and identities only. No retail executable launches.

Default and Release plain CTest include `nfsu_minimap_tests` plus the
manifest-fixtured `nfsu_minimap_retail`. The library compiles on Switch through
the shared CMake list and is covered by its object FMA census; no console
application links or enables it. Run the full protocol gates independently.

## Next integration and remaining acceptance

This output enables a separate map-resource/material adapter and renderer once
verified production style/camera/final-transform inputs are available. Bind
actual #258 actor snapshots and #269 lifecycle at their coherent publication
boundary. Retain descriptor/resource for the race independently of resident
tiles; generation/retirement and stale-snapshot checks belong to that adapter.
Gauge/lap geometry and baselinesteering remain unchanged.

Unknowns remain: authoritative live eligibility and indirect writers; actual
camera/filter/reset lifecycle; event0 options; sampler/filter/address modes;
render-state/blending and final display/aspect/safe-area transform; visibility
through pause/retry/finish; live separated opponents. No material or default
is invented to fill them. Owner matched original/port visual evidence and paired
map-on/off hardware cost remain required, at scene-height540/gpu-boost on with
actual clocks recorded separately. This PR is a functional resource/geometry
prerequisite, not complete minimap or playable event0 acceptance.

The PR also inherits its assigned pending HUD base. Review the new delta against
that exact base and fetch before measuring `origin/main...head`; the coordinator
must sequence prerequisite review/merged-result gates before merge.

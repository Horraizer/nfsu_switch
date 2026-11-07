# Event0 setup descriptor input stage

Issue [258](https://git.accad.pro/murzintsevav/nfsu-switch/issues/258),
assignment [7371](https://git.accad.pro/murzintsevav/nfsu-switch/issues/258#issuecomment-7371).
This bounded implementation depends on the reviewed setup research at
`798bd8e6570b8e84043a3c4577b01da34b057bff`,
[EVENT0_SETUP_HUD.md](EVENT0_SETUP_HUD.md), and the actor ownership research
in [PR285](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/285), head
`7c6a5e30e977160105cad778b843323ac90d978e`.
These substrates are pending integration, not merged or hardware accepted.

## Callable input and owned output

`load_event0_setup(front, global, track)` in
[event0_setup.h](../include/nfsu/event0_setup.h) reads exactly the caller's
three files. Supply FrontEnd/FrontB.lzc, Global/GLOBALB.BUN and
Tracks/TRACKB1003.lzc from the pinned retail installation. The shared
race-events pack reader handles JDLZ and filesystem paths. Decoded SHA-256
identities must match the recovered substrate; the semantic evidence is the
pinned speed.exe identified in EVENT0_SETUP_HUD.md. The loader authenticates
data packs, not a running executable or a current profile.

The structural `parse_event0_setup` entry accepts bounded decoded images for
tooling and malformed-input tests. It records their hashes but never asserts
authentication. Both entries reuse `parse_race_event_table`,
`parse_track_info_table` and the shared bounded chunk locator. No second event
parser, decompressor or vehicle-performance implementation is added.

A successful result owns a `shared_ptr<const Event0SetupDescriptor>`. It
contains copied event/track/race provenance, raw event words, the new-career
difficulty producer address, configuration-order actor records, grid-order
config IDs, all ordered visual pairs including unused/zero/repeated entries,
named human performance levels, rival difficulty contexts and tuning selectors,
raw per-racer markers, and the track's traffic request policy. Human naming
remains a profile input. No pointer into an input image survives. No retail
image, derived pixel, actor/body/controller, installed mesh or mutable session
resource is owned by this stage.

Pair categories only identify the recovered vinyl/decal dispatch and base
paint identity; they do not resolve catalog hashes, paint composition, active
geometry or materials. All other pairs remain unresolved. Order and raw
values are preserved for the future full preset importer, including special
shared-part lookup and mesh-disable behavior. Do not pass these pairs to a
generic tuned appearance and call that original preset actuation.

`descriptor_ready` means the input descriptor is available. `runtime_ready()`
remains false with typed unsupported reasons for adaptive rival performance,
RNG sequence, preset composition, mesh-disable rules, ground/body pose,
actuation and traffic model/route/activation. Rival performance and live
traffic count are absent, rather than zero or a fabricated fixed vector.
Raw marker height/angle are not final CG/orientation. Desired player grid,
initial grid and swapped final grid are distinct. Factory grid order is not
asserted to be the controller/physics update or standings registration order.

Malformed/truncated/duplicate chunks, events, preset identities and marker
indices are refused. The pack cap, aggregate chunk budget, nesting depth,
table-record budget, finite scalars/coordinates and signed configuration-ID
limits bound work and storage. Unsupported event/race/grid/tuning policies
return no descriptor. Missing nonzero presets never fall back to profile or
random cars. No input is written and no production selector, default steering,
player-only RaceSession or replay behavior is changed.

## Reproduction and evidence limits

Run from this checkout, including its actual non-ASCII workspace path. Native
toolchain libraries must be on PATH. First run the normal default/Release
retail gates from AGENTS.md. `nfsu_event0_setup_retail` is inside the existing
whole-root manifest fixture. It authenticates and loads the original inputs,
checks ownership/readiness, then tests malformed variants only in memory.
`nfsu_event0_setup_tests` also exercises null/short/duplicate/deep/budget cases
without retail inputs. Regression values live in these tests.

Generate fresh source evidence and compare the descriptor against it:

```powershell
python -B tools/event0_trace_test.py
python -B tools/event0_trace.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --out build/258-s4-fresh-trace
python -B tools/event0_descriptor_check.py --driver build/nfsu_event0_setup_tests.exe --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --trace build/258-s4-fresh-trace/data.json
```

The trace needs Capstone 5.0.7. Use a fresh output directory on every run;
the probe refuses existing output. The comparison checks input and report
provenance, every ordered pair and identity, event words, per-grid raw
transforms, context selectors and traffic policy against the fresh trace,
and brackets the C++ invocation with full retail manifests. The trace output
and build artifacts remain ignored and must not be committed.

This is data extraction from authenticated original bytes and identified
consumers, plus port parser/descriptor execution. It is not an original
event0 runtime trace, gameplay, appearance, AI, contact or hardware PASS.
The next integration can consume real actor configuration identities and
source preset/grid/performance inputs without duplicating setup discovery.
It still needs resolved preset actuation, adaptive tuning/seeds, real
controller/route/solver ownership and final initial poses before creating a
playable field. PR285's actor ownership and the #256/#257/#63 evidence remain
dependencies. Finish/results and the rest of #258 acceptance stay open.
There is no event0 runtime mode in this change and no console deployment.

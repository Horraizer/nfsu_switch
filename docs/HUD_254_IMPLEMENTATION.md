# Gated lap HUD consumer

Partial implementation for [#254](https://git.accad.pro/murzintsevav/nfsu-switch/issues/254)
originally on the explicitly approved development base
`8090cb058c658089c83cc101299861754e03504e` from
[PR281](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/281).
Review-fix integration adopts that prerequisite at
`10563e2555270cb564fac910f1f2e1ba9569d4e4`, including its independently reviewed
texture diagnostic fixture separation. The HUD delta does not repin visual
fixtures; the PR handoff records gates on the combined head. The prerequisite
is unmerged and the candidate is not an accepted event0 or hardware release.

## Supported boundary

`race_lap_hud_snapshot` copies one actor from a completed authoritative timing
state. It captures the existing restart counter, step, world tick, highest lap,
target, start/best and held completed lap. No pointer to the live arrays survives.
The adapter rejects missing actors, unsupported boundaries, free-lap modes and
highest laps outside the native signed-byte range. It is the current per-actor
boundary, not a claim to implement the race-wide setup contract in
[EVENT0_SETUP_HUD.md](EVENT0_SETUP_HUD.md).

`build_race_lap_hud_panel` is a pure value consumer: padding and separators come
from the native formatter; zero and sentinel best differ; invalid current time
retains the localization hash instead of inventing a translation. The completed
lap hold uses the native float tick constant multiplied in double precision to
preserve the x87 comparison. The next lap clock continues while held. Highest
lap determines the displayed lap, with no clamp at finish. Reading a paused or
finished snapshot changes no clock, record or source state. A car reset keeps
race timing; a retry supplies a new snapshot and clears the old hold.

`format_lap_hud_ticks` is distinct from the diagnostic `format_race_ticks`.
It preserves native hour templates and negative template truncation. Native
integer-negation overflow is explicitly unsupported. Expected strings and hold
boundaries live in `nfsu_race_lap_hud_tests` and `hud254_trace_test.py`.

`race_lap_hud_bindings` preserves the package's local SA coordinates, color,
scale, raw Sj, default OP/RsRq font requests and static-label hashes. Inspect
the compiled metadata with `nfsu_race_lap_hud_tests --bindings`; compare it to
the read-only package report with `tools/hud254_consumer_test.py`. These are
default requests, not evidence of final font overrides or screen coordinates.

`build_race_hud_frame` captures this panel from the same completed session used
by the gauge. It explicitly uses unknown format style, because the event0
option producer has not been recovered. The car-only entry point clears prior
panel data. Existing atlas, gauge/needle vertices, shaders and quad capacity
are unchanged. No lap panel is submitted to CPU or GPU drawing.

## Evidence gate and remaining work

`presentation_ready` stays false. Resolving it requires source-addressed event0
view/permission producers and lifecycle, the format option producer, runtime
font overrides, Sj mapping, anchor/safe area and backing material/draw order.
The existing native mask matrix is conditional branch evidence; ordinary panel
stills do not prove default event0 visibility. Review of the pinned complete
consumer, text-submit, glyph and mask ranges in [HUD_254.md](HUD_254.md) leaves
those producers outside the recovered consumer contract. No always-visible
event0 panel, finish clamp, countdown, rank/result, opponent or map is inferred.

The follow-up [presentation producer research](HUD_254_PRESENTATION.md) traces
the language selector, profile-to-view permissions and bounded option updates,
native Sj/glyph transforms, texture metadata and submission order. It narrows
the unknowns but changes none of this consumer's runtime behavior. Actual event0
profile/lifecycle, effective hierarchy/fonts, viewport/safe area and shader
behavior remain outside the established production contract.

The default bindings and local coordinates are usable evidence; a screen-space
quad pass with an invented anchor/material would not be. This partial PR
therefore delivers the timing/text/binding subset and records drawing as blocked,
rather than offering a diagnostic appearance as production acceptance.

Reproduction from the assigned worktree (all output is fresh and ignored):

```text
python -B tools/hud254_trace.py --root <retail-root> --reference <reference-root> --source <head> --native --out out/254-fresh-evidence
python -B tools/hud254_trace_test.py --report out/254-fresh-evidence
python -B tools/hud254_consumer_test.py --probe build/nfsu_race_lap_hud_tests.exe --report out/254-fresh-evidence
ctest --test-dir build --output-on-failure
ctest --test-dir build-release --output-on-failure
ctest --test-dir build-switch --output-on-failure
```

The native report compares synthetic records and package bytes, not a running
retail game's full lifecycle. Paired synthetic sessions in the compiled test
drive through start, lap and finish while repeatedly reading the HUD in only
one session; their replay records must remain equal. Retail replay and frame
gates additionally guard inherited behavior. The PR handoff records exact heads,
commands, results and the separate inherited gate failure.

Independent review, original event0 footage and stock-retail-versus-Switch
captures, actual event0 setup/actors, owner visual feedback and paired HUD
performance remain required. Console request identity is this PR's full head
and its `nfsu_switch_race.nro` SHA-256, with explicit
`--scene-height 540 --gpu-boost on`, a 1280x720 HUD canvas and actual clocks
measured separately. The current NRO cannot test the absent lap geometry;
original lifecycle/anchor evidence must precede that acceptance request.
The coordinator owns scheduling. This worker does not deploy or claim 60 fps.
#266 retains minimap ownership; #269 retains standings/results ownership.

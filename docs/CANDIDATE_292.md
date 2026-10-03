# Candidate292 owner test checklist

This is a cumulative software candidate for issue292, assignment8283.
It is an event1 player-only diagnostic, not completed introductory Underground
(event0), real opponents or the full original HUD. No deployment is authorized
until the owner confirms netloader readiness. Target session: 2026-10-04
09:00 Europe/Moscow. A checklist is not console acceptance.

## Included and excluded

| Component | Status and evidence boundary |
| --- | --- |
| Reviewed PR281/282 substrate | Inherited through the pinned PR287/289 histories; existing event1 runner, gauge and texture diagnostics |
| PR287 b50ea050e8ac030049a4e96bf8f8b136363fa125 | Included: authenticated event0 setup descriptor; metadata only; no live actor actuation |
| PR289 065baa39e875d52a57eb99cfd4bb0cf9cecc8908 | Included: authenticated minimap resource and bounded geometry kernel; no production map renderer or real opponent markers |
| PR310 182eafdd0fbf7ea4e93665361c0a90de271c23e3 | Excluded pending published exact-head independent review; no adapter activation assumed |
| PR308/309 research | Excluded; not required in this runnable candidate |
| Full event0, opponents, traffic, actor contacts | Not implemented by these deltas; #258/#256/#257 acceptance remains open |
| Original lap/time panels, minimap, results | Not established as live rendering; presentation_ready gates remain closed |
| Handling/tuning #250/#251 | Owner criteria remain open; baseline steering retained |
| Texture policy | Legacy default retained; old #262 f61380672d48 diagnostic matrix is separate evidence |

Merge resolution: cmake/race_run_libraries.cmake keeps both nfsu_event0_setup
and nfsu_minimap in the Switch target-properties list. No other manual source
resolution was needed for PR287 plus PR289. Source history is preserved with
a merge commit. The exact final source, build tag, NRO hashes/sizes, toolchain,
commands and complete results belong to the PR handoff and ignored manifest;
read those before testing. Prior input passes do not validate this combination.

## Identity and launch

Use full-memory Homebrew Menu, handheld mode, existing read-only retail SD
inputs. Check the visible build tag against the manifest and the host NRO
SHA256 before upload. Record requested settings and observed CPU/GPU/EMC clocks
separately; unavailable clocks are unavailable, not inferred boosts.

The live baseline arguments for nfsu_switch_race.nro are exactly:

```text
sdmc:/nfsu --live --scene-height 540 --gpu-boost on --steering baseline --texture-filter legacy --sky on --particles=on --car-shading off --wet-road-reflections off
```

For uninterrupted performance replay, replace --live with --replay and append
--profile-frame. Keep --hud-trace off. Do not use --panel for these runs.
For optional paired effect tests change one supported flag, keeping artifact,
car, tick window, resolution and measured clock conditions constant.

## Morning cases

1. Launch, loading, car/track visibility, countdown, input, pause/resume and
   normal exit. Run three separate launch/drive/exit cycles and record each
   cycle's load/run memory pages and errors. This bounds the observed window;
   it does not establish unlimited leak freedom or an in-app retry flow.
2. Straight acceleration/braking, gentle/sharp turn, curb approach, controlled
   wall contact, slide and Minus reset. Record location, direction, indicated
   speed, ground contact and input sequence for unexplained launch/rollover.
   Assess steering only after disruptive physics is absent in the scenario.
   Do not claim traffic/opponent/prop contacts when those actors are absent.
3. Isolated low/high-speed impacts and sustained wall scrape; repeat through
   pause/resume/reset. Record absent, repeated, clipped or inappropriate audio
   independently of physics. #222/#202 original fidelity remains open.
4. Current gauge, gear, needle, sky and enabled effects. Missing original lap
   panels, map, opponents and full event0 HUD are remaining scope, not a PASS.
   Geometry/descriptor availability cannot substitute for rendered consumers.
5. Uninterrupted replay with --profile-frame. Record active-frame CPU/GPU
   statistics, debt, dropped/invalid GPU queries, CPU stages, GPU scene,
   load/run memory and observation window. Finish/menu timing is not race
   timing; panel/readback timing is not direct gameplay; GPU memory is already
   included in total heap. Missing data is unavailable, not zero.
6. Separate texture diagnostic: nfsu_switch_texture_compare.nro at fixed
   1280x720. Record mode, shading, step, build and numeric results. Preserve
   the previous f61380672d48 matrix independently; its results do not transfer
   to this NRO. Complete legacy baseline/glass residual rows remain separate
   #262 criteria. Do not compare reference/render/readback cost with race FPS.

For each case record exact SHA/NRO hash, flags, hardware mode/clocks, owner
result, reproduction, observed issue, evidence locator and remaining criteria.
Owner captures stay private unless separately authorized for export. Technical
transcriptions may be recorded with artifact identity. Stop a failed case
once its identity and useful reproduction are recorded. Software review,
build success, console execution, original parity and owner acceptance are
separate. Preserve all unresolved parent issues and #250/#251 feedback.

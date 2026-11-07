# UG0 standings and first performance breakdown

Owner scope: finish the position/opponent panel, then start optimization.
Solo work, scoped development checks, no publication or merge.
Parent: f0f39ee0ed155a7b1c2f295b9ff1b0f1919768a5.

## Implemented

- HUD_SingleRace.fng POSITION/RacePositionNumber/RacePositionNth/NumberRacers,
  LBData_1..4, LeaderText_1..4 and their separate backings. Original offsets,
  dimensions, FNTF fonts, BGRA colors and pi-rotated PositionBacking;
  right-edge anchoring follows this port's existing widescreen HUD policy.
- SetPlayerColor uses the original animation's settled BGRA value:
  base(-63,56,-24,255) plus time-zero key(255,72,24,0).
- Real opponent names and competitive ordering after each completed world
  update. Equal progress retains previous order; completed pairs compare
  finish places. Physical motion after finishing does not change HUD rank.
- The start-line approach retains lap -1. The old position-only label
  clamped this to zero, temporarily putting cars behind the start line ahead
  of a car that had crossed it.
- Full ASCII name alphabet in the atlas's unused bottom strip. No larger
  texture or additional HUD draw call; the frame still uses one atlas.

Initial implementation boundaries (superseded below for UG0 gaps and mode
cycling): this implements the original names presentation, not the complete
leaderboard mode cycle. Retail 4A2B10 uses ten-second gap phases and alternating
two-second driver/car-name phases. Live gaps require the 432E80 road-derived
256-bin progress/time map consumed by 433340/4353E0. This port does not yet
provide that map or localized selected-car labels, so it keeps names visible
instead of inventing gaps. Player profile naming is absent; the row says YOU.
Online, drag, split-screen and optional profile visibility are not reproduced.
Initial equal-progress order is port actor order; original registration order
is not established. No claim of complete original leaderboard parity.

## Evidence and validation

Reproduce the bounded authenticated layout/source extraction:

```
python -B tools/standings_hud_trace.py --root <retail-root> --out <new-directory>
```

It authenticates PC1.4 USA speed.exe and INGAMEB.BUN before decoding and
checks the same inputs afterwards. Existing hud254 parser behavior is retained
by default. Evidence is under the root-owned ignored directory
build-artifacts/draw-distance-20261008-root/standings-r1.

- Native targeted build: nfsu_intro_grid_viewer, nfsu_race_hud_tests,
  nfsu_race_session_tests.
- New standings cases: start, overtake, tied progress, finish order and reset.
- PC shared RaceRun/selector3 scene at 600 ticks: load, render, HUD capacity,
  real names, player highlight and recorded GPU package checked.
- The initial PC image exposed missing glyphs; fixed before delivery.
- Existing nfsu_race_hud_tests has two failures in its manual gear-change
  fixture. The identical parent test source, rebuilt against the unchanged
  vehicle libraries, produces the same two failures. This does not test the
  new standings behavior; it remains recorded, not repinned or hidden.
- Full Default/Release/retail matrices are not run, per owner development
  instructions. Final targeted results, SDK package checks and artifact
  identity are recorded in the adjacent ignored receipt.
- Actual Switch appearance and sampled performance remain pending hardware.

## Optimization started

Optional clock injection divides simulation into planner, driver controls,
rival physics, player physics, pair contacts and timing/camera. Null profiling
reads no clock. The PC viewer accepts a final `profile` argument; paired
6000-tick runs with and without clocks had identical final sequence hashes.

For the local parent-plus-standings candidate, PC Release, 6000 ticks:
most measured simulation time was rival/player physics, with path planning a
small fraction. This is a PC observation, not Switch timing or a 60 FPS claim.
The viewer also measures 120 warmed stationary command-preparation frames;
these are explicitly not a whole-course GPU benchmark.

The Switch candidate records one CPU probe per 60 presentations, excluding
held-grid simulation. It reports sums/maxima only when leaving the race,
resets on pause/resolution measurement reset, performs no extra GPU query,
and keeps detailed AB's minimal mode free of these probes. Renderer child
costs are labeled: reflections/visuals must not be added to their combine
parent. Sampling maxima are not frame-time percentiles or guaranteed spikes.

Next: run UG0 on handheld, inspect CPU_PROBE_SIM/CPU_PROBE_RENDER alongside
the existing full frame-time distributions. Physics investigation starts at
wheel/body track queries; resolve_body currently constructs/reserves a local
hit vector on every call and probes every hull point. Measure before changing
query results, call cadence or collision behavior. Preserve the accepted
ug0-path-r1 binary and its hardware log for comparison.


## UG0 gap and mode cycle completion

Local successor of 8097ea1aeb2469353bd7201e8ebdf05bd161dda2 implements the
432E80/433340 road-speed time map and 4353E0 signed relative gaps. Map creation
and English car-label loading happen once before the race. The HUD retains
one atlas and one draw; no road searches or file reads occur in HUD updates.
The map retains original binary32 stores and x87 intermediate ordering.

The 4A2B10 cycle displays gaps for ten seconds, names for two, gaps for ten,
then car models for two, repeating. Pause holds the simulation clock; a fresh
session resets the cycle. Finished rivals display the original Finished label.
Player naming remains YOU. Scope is the existing forward UG0 circuit; other
race modes, online/profile options and original registration order remain
outside this change.

Validation (owner requested focused development checks only):

- Native build: nfsu_intro_grid_viewer, nfsu_race_gap_check and
  nfsu_race_hud_tests. Targeted nfsu_race_standings_tests passed.
- The standings cases cover ordering, ties, overtakes, finished order,
  lap-unwrapped gaps, signs, one/both finished formulas, text formatting,
  cycle boundaries, pause and reset.
- Actual four-car PC RaceRun captures show gaps, names and car models.
  Load/render/command validation succeeded; inspected images fit the HUD.
- Authenticated PC 1.4 USA executable: original map execution for all four
  performance categories matches every output binary32 value. All 256
  original gap-consumer comparisons match integer ticks exactly. This is a
  bounded native-code comparison: spline projection 443A10 is hooked with
  the existing port's raw coordinate results. It does not independently
  establish original projection or full live-game trajectory equivalence.
- Switch SDK NRO build passed. Focused nfsu_switch_fma_census,
  nfsu_switch_race_nro and nfsu_switch_frontend_profile_contract passed.
- No full Default/Release matrices or delegated review, per owner instructions.
  New Switch appearance and behavior remain pending an actual hardware run.

Exact commands, preserved initial failed expectations/precision comparison,
final logs, captures, native probe and package receipt are in the ignored
build-artifacts/draw-distance-20261008-root/standings-gaps-r1 directory at
the primary workspace. Artifact tag: standings-gaps-r1. No push or merge.

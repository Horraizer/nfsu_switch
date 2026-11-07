# UG0 polish: grid engine, 2026-10-12

Assignment: owner requests original-like first-race UI, sound and behavior.
Solo sequential work, no delegated review. Parent:
89e35440054a6818e49bb074304a505d208ce690 (perf-cpu-r1).
Publication: local checkpoint only. Hardware verification pending.

## Ordered remaining scope

1. Human countdown engine revving: implemented and software checked here;
   owner still needs to check throttle/release, sound, neutral display and GO.
2. Original starting-grid people and their disappearance/scene lifecycle.
3. Original finish indication on the minimap.
4. Original race sounds, engine/exhaust/shift and nitrous sound behavior.
5. Original nitrous acceleration presentation, including blur and camera.
6. Opponent difficulty and other first-race UI/behavior gaps against reference.

Do not claim complete UG0 parity. Preserve the successful CPU optimizations.
Work one feature at a time; no broad assertion/review loops. Existing owner
videos/stills and authenticated PC 1.4 USA inputs are the reference sources.

## Original evidence and implementation

Authenticated speed.exe SHA256:
C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7.
434B50 stage setter calls human driver vfunc1C for held stages and vfunc20
for stage4. The actual vtable at6C8AD0 resolves these to4607F0 and461510.
4607F0 sets driver278=1. 4616B4..4616D6 then requests neutral1 and handbrake1,
without clearing processed throttle1DC. 461510 clears278 and dispatches
4615B0, restoring automatic request-1 or manual first2. Two controlled native
executions verify hold, neutral request, handbrake, retained throttle and GO
request restoration. Full original scene/drivetrain timing is not inferred
from these bounded controller executions.

The port already starts directly at countdown, skipping flyby/prerace. Its
adapter therefore begins with settled neutral. It advances the existing
original-derived engine torque/integration/clamp/cutoff and RPM publication
at the normal four solver slices per presentation. It does not integrate the
body, wheels, contacts or route on the grid. Engine time advances while body
step counters stay zero, and continues across GO. Release requests first gear
without resetting physical engine RPM, allowing the existing clutch phases.
Grid input consumes shift edges with reverse assist suppressed. Nitrous is
still disabled during countdown. AI pre-race throttle policy is unchanged.

Audio snapshots consume countdown RPM/throttle through the existing path.
This fixes frozen sound input, not the existing sample/mixing fidelity gaps.
The desktop viewer accepts grid-revs for a real UG0 preload/rev/launch smoke.

## Verification

Commands from owned tree, native toolchain on PATH:

```
cmake --build build --target nfsu_race_session_tests nfsu_race_audio_tests nfsu_intro_grid_viewer -j 4
ctest --test-dir build -R '^nfsu_race_(session|audio)_tests$' --output-on-failure
build/nfsu_intro_grid_viewer <retail-root> <fresh-prefix> 90 grid-revs
build/nfsu_intro_grid_viewer <retail-root> <fresh-prefix> 600 grid-revs
cmake -S . -B build-switch -DNFSU_RACE_BUILD_TAG=grid-engine-r1
cmake --build build-switch -j 4 --target nfsu_switch_race_nro
ctest --test-dir build-switch --output-on-failure -R '^(nfsu_switch_fma_census|nfsu_switch_race_nro|nfsu_switch_frontend_profile_contract)$'
```

Native build PASS; targeted tests2/2 PASS. Countdown scenario checks gas,
release, second blip, cutoff, exact grid position, zero wheel spin, neutral,
GO retention and actual launch. Audio test checks changed sound-layer inputs,
no false collision/NOS emission and release response. Real retail UG0 viewer
both scenarios PASS, including four-car rendering. PNG countdown inspected:
neutral N, raised needle, zero speed; no hardware image equivalence claim.
SDK build PASS; targeted tests3/3 PASS. Full matrices omitted per owner.

Root evidence: build-artifacts/draw-distance-20261008-root/
grid-engine-r1-original.json and root-grid-engine-r1-original.py;
grid-engine-r1-build-final.log; grid-engine-r1-tests-final.log;
grid-engine-r1-pc-90.log and grid-engine-r1-pc-600.log with fresh image outputs.
SDK logs: owned build/grid-engine-r1/. Exact NRO/source receipt is in root
artifacts/grid-engine-r1-checkpoint.json (same evidence directory above).
Retail reads only; helpers are ASCII and executed from actual workspace.

Next: owner hardware check of the first feature; starting-grid crowd is the
next implementation item. No deployment, main merge or release publication
performed for this checkpoint.

## Combined pass ready for hardware

CHECKPOINT
role: worker
agent: Codex root, owner-directed solo work
assignment: One combined tachometer audit, starting crowd and minimap pass.
phase: awaiting-evidence
base: 0b5b91863ec81d45648b1601cec874b986f34cb1
branch: codex/draw-distance-original
worktree: build/day20261008-root-combined
saved: Local source commit and NRO receipt; no push, merge or deployment.
verified: Native affected build, focused HUD/minimap tests, crowd lifecycle,
  actual UG0 PC before/after-GO captures and command validation; SDK NRO
  and three mandatory package/FMA/profile checks passed.
decisions: Original neutral slew retained; clutch-phase gear color fixed;
  original start/finish minimap marker; retail DDay crowd before GO only.
remaining: Owner hardware check; full parity limits in the audit below.
next: Owner verifies one combined ug0-polish-r3 NRO on Switch.
blockers: Hardware acceptance not performed.
ownership: Root solo; no reviewer/worker delegation per owner.

See [complete comparison](root-tachometer-audit-20261012.md). Exact source,
NRO hash and local logs are recorded by the root receipt under
build-artifacts/draw-distance-20261008-root/ug0-polish-r3.
Earlier r1/r2 results above are preserved history. Full matrices were not
repeated per owner instruction; this is not a whole-game acceptance.

## R3 hardware refusal and R4 correction

Owner reported return from race preparation. Preserved nxlink run shows
`HUD staging: GPU memory budget exceeded`: requested 2097152 bytes at
277274624 resident against 279089152 effective limit. This was the r3
hardware run of 7afd2fb, not an accepted build. The PC scene test did not
model this full console initialization peak.

R4 shares one immutable index buffer per animation clip and selects each
pose through the GPU base-vertex operand already supported by deko3d.
Range validation includes base+index, signed-offset bounds and the existing
whole-buffer bound. No budget increase or animation/texture reduction.
Actual PC captures at 90 and 600 ticks equal r3 byte-for-byte. The local
frame-comparison receipt records the hashes and payload reduction.

A second source defect was found before resending: the prepared renderer
required an unchanged dynamic draw count, so retiring the 66 crowd draws
at GO would fail. R4 permits retirement of a suffix while preserving every
remaining definition's identity and allocated capacity. The same shared
validator now runs in the PC viewer through GO; foreign identities remain
rejected. This does not permit reordered or inserted dynamic definitions.

Native affected build, HUD/minimap/draw-execution tests and real UG0
before/after-start viewer checks passed. R4 SDK/package checks and exact
NRO/source identity are in the r4 receipt. Hardware acceptance remains
pending; no automatic acceptance from software checks.

Evidence: build-artifacts/draw-distance-20261008-root/ug0-polish-r4 and
ug0-polish-r3-failure-complete.log in its parent directory. Work remains
solo, local only; no source push or merge. Next: launch r4 through Netloader
and confirm race entry plus the first GO transition on Switch.


## Crowd scene lifetime correction after owner R4 run

Owner confirmed R4 runs, but rejected immediate crowd removal on GO.
This supersedes the earlier "before GO only" implementation decision.

Original PC 1.4 USA executable identity is recorded above. STREAML1RA.BUN
chunk 34035 at file offset 0x1D46D80 has scene ID 2338 (DDay start).
47B890 binds its payload directly. Payload +64 is scene-end frame 215,
+68 is countdown-end frame 75, and +6C is controller-release frame 0.
47F0AC converts frames at 30 Hz, and sets countdown step to one third of
(countdown end - release). 47F4C0 completes at time >= scene end; its caller
423611 destroys the controller. 47F150 releases the scene actors and
sections. The crowd therefore outlives GO by (215 - 75) / 30 seconds.
These numbers identify the retail descriptor, not independently tuned delays.

Bounded execution of original 47F4C0 with supplied scene time at frames
0, 74, 75, 76, 214, 215 and 216 returned the expected completion predicate.
This verifies the actual original predicate; it is not a live game capture.
Disassembly and authenticated native results are preserved in the local
crowd-lifetime-r1 evidence directory below.

The port reads this scene's end/countdown-end fields at load and retires the
crowd from the existing simulation GO clock, including pause/restart behavior.
The cinematic is still skipped and the existing port countdown is unchanged.
Animation resources, memory budgets, actor meshes and shading are unchanged.
The affected native build, draw-execution test, retail crowd lifecycle check,
and UG0 viewer at 90/600 ticks passed. The real GPU package path is exercised
through GO and eventual suffix retirement. No broad matrices repeated.

Traffic finding: original UG0 setup requests six role3 traffic configurations
from the track policy; this does not prove six simultaneously visible cars.
See EVENT0_SETUP_HUD.md, Traffic, and src/event0_setup.cpp. The current race
runtime implements the player and three racers, not live ambient traffic.
No traffic implementation was bundled into this crowd correction.

Evidence: build-artifacts/draw-distance-20261008-root/crowd-lifetime-r1.
Exact local source/NRO identity and SDK results are in receipt.json there.
Next: owner hardware check of crowd removal after GO. No deployment, source
push, merge or independent reviewer; owner-directed solo work continues.

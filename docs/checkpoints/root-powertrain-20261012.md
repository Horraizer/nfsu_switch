# UG0 powertrain comparison and two corrections, 2026-10-12

Owner-directed solo continuation of root-ug0-debts-20261012.md.
Base bf0d0ec; accepted hardware candidate standings-gaps-r1 is preserved.
Scope: tachometer/acceleration inspection, first demonstrated discrepancies.
No sound, AI or traffic changes. No delegated review, push or merge.

## Corrections

- Original46CC37..46CCAB starts the limiter only above max RPM minus50
  and clears it only after the expiry time. Both port comparisons included
  equality. Actual original execution differed in15/36 supplied boundary
  cases before correction; all36 now match. Tests preserve equality and
  above-boundary witnesses with original neutral duration. This is a bounded
  timing predicate correction, not proof of a large visible RPM-decay change.
- Original4A0FBC reads actor3D4, the state394 speed publication from478A47.
  The port instead recomputed horizontal velocity magnitude. HUD now reads
  the existing published three-dimensional magnitude and performs the
  original binary32 unit conversion/store before truncating to integer.
  Ten actual original consumer executions agree. New tests distinguish
  vertical motion and completed publication from freshly sampled velocity.
  MPH remains selected; units preference and first-person dial variants
  remain open. The existing three-digit capacity clamp is unchanged.

No engine torque, inertia, grip, gear ratios or acceleration tuning changed.

## Reproduce selected-car evidence

From the owned tree, with native runtime libraries on PATH:

```
cmake --build build --target nfsu_race_powertrain_check -j 4
build/nfsu_race_powertrain_check <retail-root> <fresh-build-directory>
python -B tools/race_powertrain_native.py --root <retail-root> --captures <fresh-build-directory> --out <new-report-json>
```

The diagnostic loads the actual event0 human Integra and decoded selected
performance setup. It writes the encoded handling input and explicit engine,
cutoff and HUD inputs/results. Three independent fifteen-second PC runs use
normal pad processing, countdown gas/release, GO, automatic shifts, road-driver
steering, a coast interval and NOS. No rival contact influences these
isolated comparisons. CSV records applied gas, physical/published RPM, speed,
gear/clutch, cutoff, wheel RPM, NOS and resets. They are port measurements,
not recordings of the original game's controller or full physics trajectory.

The native tool authenticates USA1.4 speed.exe, executes original constructor,
derivative, Euler/component update, RPM export, cutoff and HUD conversion
instructions using Unicorn's initialized007F precision mode. No external
function hooks. Selected handling is supplied from the port: it does not
independently verify original adaptive setup selection. Compare original
execution on matching operands, not a reimplementation of its equations.
Fresh native report: engine108, cutoff36 and HUDspeed10 cases all exact.

At bf0d0ec plus this local diagnostic, original-controlled-input port runs
measured0-100km/h at5.05s without NOS and3.65s with NOS requested from2s.
These are historical fixture measurements including the neutral-to-first
launch, not assertions of retail acceleration performance. Baseline and
successor CSV files remain separate in the evidence directory.

## Validation and limits

- One final affected-target native build passed for the diagnostic, player
  powertrain, HUD, session, boost and four-car viewer.
- Targeted CTest player_powertrain/race_hud/race_session/race_boost passed4/4.
- Shared RaceRun four-car PC viewer,650ticks with grid-revs: passed, rendered
  two cameras, validated GPU commands. Chase image inspected.
- Real Switch SDK race NRO build passed; FMA census, race package and frontend
  profile contract passed3/3. Tag powertrain-r1. Hardware pending.
- Full Default/Release matrices not run, per owner development instruction.
- Original live input/clock alignment, whole launch/gear/body trajectory and
  full selected setup fidelity remain unaccepted. The video lacks an input
  trace; its timestamps cannot alone prove identical throttle/NOS scenarios.
- No claim that the user's perceived faster RPM decay is fully resolved.
  Next bounded gap: original world-clock ownership around limiter/shift
  updates and matched launch/coast behavior; preserve this candidate first.

Evidence, relative to primary workspace:
build-artifacts/draw-distance-20261008-root/powertrain-audit-r1.
Preserved failures include the initial local HUD variable-name compile error
and original-versus-parent boundary mismatches. Final native report, source
excerpts, telemetry, PC captures and SDK logs are retained. Package receipt
names exact commit, bytes and SHA256. Retail inputs were only read.

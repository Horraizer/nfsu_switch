# Native operation and integration regression contracts

The mandatory Default/Release suites include original-code operation fixtures,
port integration regressions and reference-render comparisons. A green suite
is not original whole-game, independent-review or Switch acceptance.

## Mechanical phase migration

The former vehicle suite contained calibrated requirements of its fitted
solver. The current selector1 integration uses independent service/parking
capacity, force-vector limiting, retained wheel torque history and delayed
force preparation. End-step telemetry is not the input of that step's wheel
integration. A scalar torque clamp, a same-record torque balance, mandatory
rear rolling under parking brake, and a compulsory snap-spin window are not
contracts of that native system.

`nfsu_player_powertrain_tests` retains its independent original numeric cases.
`nfsu_vehicle_tests` observes the actual runtime callback operands, combined
force limit before lateral drag, previous prepared derivative, capacity lock,
raw-slip classification and sign-clamped integration through complete scripts,
including driven airborne wheels. The retired approximate traction option
must not alter the native solver. Rest checks retain nominal preload/strut
checks and also verify the actual frozen-rest state. A frozen body need not
solve the fitted solver's exact static force equilibrium.

The unit suite keeps explicit port regression envelopes for unguided motion,
paired signed turns and displaced binary32 world coordinates. The half-car-
width and accumulated-coordinate-ULP allowances are diagnostic policies,
not native route widths, mathematically rigorous global error bounds or a
measurement of original handling. Matched pedal and aero counterfactuals
replace unsupported fitted acceleration-time and rolling-resistance claims.
Actual acceleration remains printed for diagnosis; it is not a retail oracle.

## Stability scope

`tests/flat_roll_native.inc` stores supplied inputs and independently executed
original body-only results. `tools/flat_roll_native.py` authenticates the retail
inputs and reruns the original constructor, body component Euler/refresh and
normalization operations without replacement hooks. The fixture disproves the
old unconditional calibrated up-axis limit. It does not prove that those
states are reachable through the original world/controller/ground system.

The synthetic no-contact matrix checks deterministic motion, clearance and
absence of overturning. Moving fixtures resume their correctly initialized
road gear after GO; a neutral countdown must not replace that moving state.
The native low-support curve/jump pins, actual one-wheel activation in every
steering mode, wall/drop/reset matrix and curb cases remain active.

The seeded retail input is an uncontrolled full-input barrier-impact stress.
It is not a clean driving line and cannot promise that impacts never overturn
or recover a car. Unhit motion must not overturn. After an actual hull impact,
configured port rollover recovery must have its slow/inverted prior interval;
unconfigured stuck recovery is forbidden. That port recovery remains an
approximation. Manual reset and complete paired state equality remain checked.
The clean complete event replay separately retains its strict per-slice
support, upright/road alignment, speed and zero-reset requirements.

## Host pilot and rendering coverage

`RaceRouteDriver` is a host regression pilot, not the retail opponent AI.
Drift arenas use a shorter pursuit chord so the target does not cut tight
corners. Every drift load/drive test still requires a complete race within the
input budget, unchanged reset ceiling and valid later draw updates. Authored
campaign/event data, live player controls and physical coefficients are not
changed by this pilot correction.

The ordinary-world test horizon covers all required placed object witnesses.
The original color/material/sampler/alpha/cull/vertex, car/sky and reflection
checks remain required; missing coverage still fails the test.

No memory ceiling, source-derived numeric fixture or clean-replay physical
bound is relaxed by this migration. Hardware behavior and visual matching
remain explicit acceptance work after the mandatory matrices pass.

## SDK and directory encoding

The native Windows RomFS directory scanner rejects an absolute checkout path
with non-ASCII components, even when the ELF and NACP can be opened. Asset
folder properties are relative to the packaging command's binary directory.
CMake copy/dependency paths and SDK files are unchanged. Fresh package checks
must run from the actual workspace; a cached NRO is not a packaging smoke test.

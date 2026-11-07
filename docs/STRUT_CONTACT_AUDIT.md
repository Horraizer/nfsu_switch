# Strut contact and crest code audit

## Authenticated original

PC1.4 USA speed.exe SHA-256:
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7

Retail input is read-only. Ghidra decompilation is checked against original
instructions and bounded execution; decompiler expression ordering alone is
not evidence of the x87 accumulation order.

## Confirmed correction

Original58860A..58867F clamps compression and contact against axle limits.
The excess above the bump limit is separately saturated using6F0740 before
588A5D multiplies it by BSS734538. The former port left that excess unbounded.
player_strut_compression now supplies the original cap, contact boundary and
cached-rate behavior, and VehicleModel consumes its result. The existing
sequential partner epoch remains unchanged.

tests/strut_refresh_native.inc comes from original instructions via
tools/player_strut_refresh_native.py. It covers front/rear supplied setup,
droop, bump and excess saturation, retained and updated rates, previous
compressions, partner terms and contact/no-contact force. Numeric pins live
in that fixture. The actual VehicleModel consumer test failed before the
correction: deeper supplied compression beyond saturation increased all
wheel loads and the prepared body force/moment. It passes after correction.

## Dependent landing fixtures

The proving-ground and retail-circuit drop hashes change with the recovered
cap. Their existing airborne, supported landing, hull penetration and reset
checks remain. Captured strut inputs from both the no-input and brake-held
proving-ground drops were replayed through original compression and force
instructions. The former uncapped producer disagrees during deep compression;
the corrected producer agrees on the captured cases. Bump coefficient and
cached rate are supplied in this comparison; it is not a live race trace.

Original47AA40 calls42AA70, which accepts any positive published support.
A brake-held body can freeze with fewer than four contacts before exact
spring equilibrium. The paired settling test now checks that native support
predicate while retaining its original time, speed, spin and rest-state
requirements. Its unbraked four-wheel ride-height and penetration assertions
are unchanged. nfsu_vehicle_track_tests and nfsu_probe_vehicle_track_guards
pin the proving-ground result; nfsu_vehicle_track_circuit_retail pins the
retail result. Numeric pins remain in their source fixtures.

## Road projection and airborne boundary

Original587070 obtains cached static height from440A40, considers577060
object surfaces, adds441D80 material height and projects along live body-up.
The port static-plane projection is isolated in player_strut_ground_projection
and checked by tests/strut_projection_native.inc, reproduced with
tools/player_strut_projection_native.py. Supplied planes include combined
slopes, tilted body-up and refused alignment. Both generators execute original
instructions without semantic hooks. This projection witness uses an empty
object-surface list; it does not prove dynamic-object support.

Existing nfsu_vehicle_track_tests checks the static cached selector and the
transition from supported to beyond-droop contact. nfsu_player_powertrain_tests
also checks body momentum/matrix integration, force application, aerodynamic
preload and low-support torque, with their distinct callback/publication epochs.
These are bounded component checks, not a full live crest trajectory proof.

## Explicit remaining gaps

The isolated witnesses supply BSS734538 explicitly. Owner live recordings
now establish zero after race initialization and during UG0, ordinary circuit,
drag and drift play. The supported original executable is authenticated by the
SHA-256 above. Sessions session-20261025T160045Z-284ae406,
session-20261027T044603Z-e6771001 and session-20261027T045456Z-98c62171
are the read-only inputs; their manifests identify individual captured files.
VehicleApproximations therefore defaults to zero, rather than the former
invented extra spring. Explicit nonzero diagnostic coefficients still exercise
the overflow cap and representability guards. nfsu_vehicle_tests checks the
live default through a deep-compression consumer; nfsu_player_powertrain_tests
retains the independent original instruction witnesses.

These observations do not establish the coefficient for every unrecorded game
mode or prove that this one correction explains a specific airborne incident.
Whole recorded trajectories also require synchronized component, controller,
contact and RNG state; sampled controls alone do not supply those states.

The577060 list is built by577450 for qualifying578060 physical objects.
Its additional object-surface height path remains absent from the production
wheel provider. It is not an established cause of a terrain crest problem.

The shared normal tuning wrapper is documented in PLAYER_SETUP_AUDIT.md.
Its successful setup comparison does not certify drift or a complete live
input/terrain/contact trajectory. Hardware acceptance remains separate.

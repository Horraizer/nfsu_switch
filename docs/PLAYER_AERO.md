# Bounded player aero adapter

`VehicleModel` retains the selected record's `+0x374`, `+0x378` and
`+0x37C` through `VehicleHandling`, decode, encode and `VehicleSpec`.
`make_race_vehicle_spec` already applies tuning before decoding. The original
`0x00575B30` writer applies table B before A with binary32 rounding; the
adapter uses those resulting fields rather than stock coefficients.
The stock numeric table's provenance is checked by `test_record_source` in
`nfsu_vehicle_tests` against `kVehicle240sxRecordBytesSha256`.

`player_aero_forces` implements the three applications from `0x00462F90`:
world drag, front downforce and rear downforce. Its source float order,
support-dependent gain and drag arm, cosine fourth power, up factor,
transforms and separate zero-support shares are retained.
`player_aero_apply` routes each application through the existing
`0x00584CD0` force/moment limiter. Disabled aero leaves the body and optional
drag output untouched. All primitive quantities use tonnes, kN and metres;
the model converts the completed accumulator to SI at its existing boundary.

`player_aero_preloads` implements `0x00477100`, `0x00584B10` and
`0x00588520`. It uses the prior published signed forward speed, clips its
speed contribution at zero, and adds it to source gravity/mass before the
front/rear lever shares. The nominal strut law, queries, load conversion,
contact point and sequential partner epoch remain separate existing adapters.

## Explicit configuration and timing

`PlayerInitialAeroConfiguration` names the supported single-car,
closed-speed-scale context. Original owner reset `0x00464C80` initializes
actor `+0x404` to one; actor constructor `0x00425350` initializes drafting
`+0x47C` to zero. The single-self world branch of `0x00426450` computes zero
drafting. In this context `0x00479160` leaves cache divisor, drag scale,
body, engine and wheel scales at one. The initialized `0x007000EC` and
`0x007000F0` operands are explicit supplied configuration. Later live global
changes are not recovered.

At outer selection the model latches the supplied processed throttle from
`VehicleInput`, prior published support times the original quarter factor,
and prior published owner speed. These reproduce the demonstrated
`0x00477BED` and preload writer adapter and stay held across inner slices.
The completed outer publication updates support and speed for the next call.
The supplied moving-reset adapter seeds initial support from its ground
probes before its first force preparation. Authentic actor activation and
moving-spawn force timing remain unproved.

The common kernel consumes the preceding prepared force before preparing
the next force. Aero torque survives into the tyre/contact accumulator.
Held cache/preload/readiness fields are included in the appended telemetry
suffix, preserving the earlier byte prefix. Existing replay hashes have not
been replaced to accommodate the schema or behavior change.

## Unsupported domains and acceptance

Active `0x00429B10` scaling needs initialized race curves, route/group inputs
and its complete coupled body/engine/wheel update. Traffic drafting needs
the corresponding actor/world inputs. Those modes are excluded from this
model adapter; noninitial cached scales are refused. Executable-loaded zero
runtime BSS curves are not production defaults. Full actor/driver/selector,
export/control provenance and indirect/global writer coverage remain open.

`nfsu_player_powertrain_tests` pins independent original force/preload
witnesses and checks disabled output semantics, held outer inputs and the
refusal of uncoupled scaling. `nfsu_performance_levels_tests` checks that
selected tuned coefficient bytes reach model handling.
`nfsu_player_aero_setup_tests` independently exercises the B-before-A
selected/spec/encode path and discriminates omitted coefficients in actual
model callbacks. The unrelated legacy
decoded-only grip assertion remains unresolved. The straight-brake criterion,
other integrated assertions and existing replay pins remain unchanged.
Final default/Release/retail, merged-result, Switch and hardware acceptance
are still required before a functionality claim.

## Road-only seam check and original comparison limits

At `23749d3`, the unchanged absolute-height assertion in `test_seams`
failed after aero integration. The original failed test and supplementary
evidence remain immutable. After the independent A1 supplement, the
coordinator explicitly authorized replacing that road-only obligation with
the height difference from an enabled flat-plane reference. This changes
what the seam test measures, not its threshold or the production physics.

`test_seams` in `nfsu_vehicle_track_tests` uses the same enabled stock spec,
spawn and input sequence in the track session and flat reference. It checks
every paired step, road contacts, no hull collision and actual crossings.
The original sampling window and limit are retained in the test. Absolute
height excursion is still printed as a diagnostic. Raised and lowered
valid-format slab fixtures must violate the same residual predicate while
the contact and crossing checks continue to pass. The focused command is
`nfsu_vehicle_track_tests --seams-only`.

Both runs share `VehicleModel`, so agreement is a road-query/geometry check,
not independent handling fidelity. Original-backed aero/tyre/suspension
comparisons, strict straight braking and all other behavior criteria and
gameplay pins remain separate obligations.

The native sag supplement at `23749d3` supplied identity axes read from the
port's unready rigid cache. The port imports its pitched public quaternion
before preparing forces; the native fixture prepared with identity instead.
Any earlier claim of the same initialized physical pose is withdrawn. The
experiment demonstrates original method coupling in that declared context,
not equal initialized orientation or whole-trajectory equivalence. Later
gear divergence, explicitly supplied controls/publication and differing
flat-plane float context remain limitations. The prior packets are preserved
unchanged; no stronger pose comparison is claimed or remeasured here.

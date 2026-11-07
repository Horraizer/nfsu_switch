# Low-support vehicle stability, issue #275

This is one bounded correction, not complete launch/rollover acceptance.
The older approximation register in VEHICLE.md/HANDLING_GAPS.md predates
this change. The static kind-3 solver and its response constants are unchanged.
No dynamic car pair, traffic, opponent or placed-body response is introduced.

## Recovered contribution

The read-only speed.exe identity is SHA-256
C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7.
At 0x00474DDE..0x00474DF4, physics+0x34 points to the car. Car+0x38
state 1 bypasses this pass. Otherwise car+0x3CC below 2 selects the
angular contribution at 0x00474DFD..0x00474E85. Body angular velocity is
+0xC0..+0xC8; the magnitude goes through pair interpolation 0x00583F20
with descriptor 0x00705E98, three binary32 pairs at 0x00705E80. The result
is multiplied by body mass +0x144, negated, multiplied by each world angular
velocity component, and added to world torque +0x120..+0x128.

vehicle_low_support_torque implements that selector3 contribution in SI
units and preserves its collision-state and support gates. Its numeric
curve and native contexts are pinned in nfsu_stability_tests. Selector1
VehicleModel instead uses player_low_support_apply: original4791F0 reads
a distinct table705E14, bypasses owner states1/2, and adds torque through
584E00 in binary32. Its source pins live in nfsu_player_powertrain_tests.
The static-only model supplies owner state0 and prior published support.
As VEHICLE_TRACK.md records, kind-3 contacts do not set car state 1. A
future dynamic-body integration must carry the actual state into this gate;
it must not assume this static-only call site covers car/kind-4 contacts.

The branch at 0x00474E8B starts a separate zero-wheel alignment pass.
Its orientation/speed gates and application are not recovered here. The
portable integration remains the existing approximation. There is no
new angle clamp, grip/restitution reduction, reset or jump suppression.

## Evidence and reproduction

At base 6e65e30, the explicit seed7 input below rolls recovered0/1 on
TRACKB1003. The source-pinned board evidence records the measured sequence.
Its support loss and suspension spike precede a later wall hit; low-support
angular velocity then persists through free flight. Applying only the native
torque contribution eliminates this particular rollover. This is causal
port evidence, not a matched retail driving episode or proof of the precise
maneuver in the owner's report. A broader seed4 case remains unresolved.

nfsu_stability_tests --root <ASCII-retail-root> --out <fresh-ASCII-file>
executes paired deterministic sessions in all three steering modes. The
seed is unsigned32 7, advanced every 120 display ticks by
rng = rng * 1664525 + 1013904223, with axis = ((rng >> 16) % 7 - 3) * 10000.
The first 163 ticks are neutral; throttle is then held. Steering stays zero
through tick399. It runs 720 ticks and checks a manual reset afterwards.
Expected stability values live in that test, not this document.

Without --root, the test checks the native table/torque contexts, independent
linear jump motion and the existing proving-ground matrix: walls square,
oblique and glancing at several speeds, slope/crest, drop/landing, ground
loss, route/reset zones and manual/reset semantics. It also reaches curb
faces at several heights, approach angles and speeds in paired sessions.
Existing planted pins
must remain unchanged. Airborne pins and recordings move because the old
stream omitted the recovered torque; each changed pin needs fresh evidence.

python tools/low_support_compare.py --root <ASCII-retail-root>
  --probe build-release/nfsu_low_support_probe.exe --out out/275-native-new
requires Unicorn 2 and standalone Python 3.8+. It hashes and maps the PE,
checks the curve bytes against the compiled port, executes the original
branch and interpolation under an instruction ceiling, and stops before
the alignment pass. All arithmetic is native; no helper or OS call is
stubbed. x87 FPCW 0x037F is an explicit context, not a measured game startup
word. Complete finite port output is compared against each native torque.
Full retail tree manifests bracket the run. Outputs remain in ignored
scratch; raw game bytes and disassembly never enter Git.

## Diagnostics and remaining work

The optional test output includes pose, pre/post collision velocity and
angular velocity, steering target/actual angle, speed/gear, wheel support,
compression/normal/ground point, and the applied low-support torque.
Separate contact rows identify order, polygon, hull point, surface, depth,
closing speed, normal, segment plane point, arm, separation, pre/post state,
current raw/applied impulse and accumulated applied impulse. These are the
existing hull-ray approximation, not the retail geometric witness of #256.
The diagnostic list is bounded by the existing 256 hull-point cap; all new
diagnostic fields are omitted from legacy binary telemetry encoding.

Matched original footage/runtime contact order, zero-wheel alignment,
suspension spike/bump-stop/application-point questions and the residual
seed4 case remain open. The owner Switch drive still needs the agreed
960x540 scene and GPU boost on, measured clocks and full frame pacing.
This inherited event-1 diagnostic cannot close event-0 acceptance (#268).
#222 owns impact audio; current-hit and accumulated impulses remain distinct.
Resume #250/#251 driving comparisons after the remaining physics work.

# Camera comparison at the n25 source freeze

Scope: the worker checkout starts at
`bdda1226701879858e1fee3083de8c9874289d57`. The production checkout is not
modified. Publication and Switch execution are outside this assignment.
All retail inputs and archived captures are read-only. No Dolphin or PC game
process was started for these measurements.

## Projection and camera ownership

The live widescreen lens previously followed third-party PC Widescreen Fix
operands. Those operands are not the unmodified original4:3 lens.
`tools/race_camera_native.py` independently executes authenticated original
40DD00, including its angle truncation and scalar math. The executable is
PC USA1.4 SHA256
`c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7`.
The native fixture retains the separate widescreen-fix witness for provenance;
the live projection regression uses the unmodified original viewport instead.
The installed PC copy also contains Widescreen Fix with FixFOV=1 and Scaling=0.
The archived recorder authenticated speed.exe on disk, but did not inventory
loaded ASI modules or record the post-hook projection matrix. Its camera poses
therefore cannot identify the live projection as unmodified. The separate
`apex_widescreen_report.json` records installed file identities and compares
stock4:3, stock16:9, supplied fix operands and both portable lenses. The new
framing targets stock4:3 and can differ from the owner's modded PC image.

The live view now preserves original4:3 vertical framing and derives the
horizontal focal from the output viewport for square pixels. The original
independent horizontal operand is still available in the diagnostic retail
projection. This is a defined widescreen adaptation, not a claim that the
original projection itself has square pixels or that rendered pixels match.
For local split screens the reference viewport is the corresponding portion
of an original4:3 display. The two layouts therefore have different original
viewport aspects. Start and finish shots also sample their authored lens in
the reference viewport before widening, instead of retaining a vertical lens
computed from the output aspect.

Native47D680 selects the second CarTypeInfo camera list for layout3. Both
local human cameras now select that list; a network peer's full-screen view
continues to use the single-player list. Runtime camera records still belong
to each selected car. The ordinary cycle exposes far, near and bumper; drift
also exposes high chase. The unused cockpit anchor is not a supported driving
view and is not added by this correction.
The network scene explicitly selects the live widescreen projection, even
when its separate effects policy omits the full single-player visual package.
The real UDP regression covers host and guest presentation after authority
snapshots and checks all three ordinary POVs against the native lens fixture.

PC42DA40 and GC8002D494 also reject cinematic requests when two local player
views exist. The GameCube witness executes the actual load, comparison and
branch from its boot DOL with supplied local-player counts. Local split views
retain ordinary impact shake and reject both car-impact and periodic airborne
cutaways. A full-screen network peer is not a second local player view.

The type17 chase basis previously published a downward `up` vector. The
renderer reconstructed its own upright basis, hiding the sign error in the
ordinary image, but impact and countdown-purge effects consumed the original
vector. The corrected cross-product order matches original48BCB0. The native
acceleration witness now exports the basis as well as the eye position.
Original483370 look-back also mirrors the fixed-view anchor about the actor.
The bumper adapter previously rotated its direction after placing the eye,
leaving that eye at the forward anchor. The corrected order mirrors both.
The native fixture covers hold and release through original48B410/483370/
48BCB0; the controlled real-pose evidence includes both chase views and bumper.

Garage cameras have a separate native controller, focus presets and manual
orbit. Their displayed main view now samples original4:3 vertical framing
before widening too. Cube and floor capture projections retain their own
native viewports. The low-level garage matrix and original-projection
witnesses remain independent of the displayed widescreen adaptation.

## Evidence and boundaries

Historical measurements at the base SHA are saved under
`build/apex-n25-camera` in this checkout. `apex_capture_stats.json` identifies
the archived genuine Golf and Tiburon camera polls, rejected cross-frame
polls, original mover types and observed speed ranges. The two full source
directories and metadata hashes are in that inventory. A complete original
event19 capture without camera fields is explicitly recorded as such.

`apex_mover_report_base.json` and per-view CSVs compare original48BCB0 with
the portable mover on identical original actor snapshots and game-clock
deltas. These are controlled function executions, not a deterministic replay
of the entire original game. Ground material, road-height noise, wall
clearance, continuous shake and impacts are explicitly excluded. The first
sample seeds synthetic controller history; maxima that include that warm-up
are not steady-state error bounds. High chase and bumper have initialization
transients in that supplied history.

`apex_live_same_frame.json` separately compares actual rendered original
camera polls with the controlled oracle at exactly matching frame and clock.
Those residuals include omitted world/effect/history inputs. They must not be
reported as millimetre-level whole-game camera equivalence. Camera-poll
stability does not make the original multi-read recorder atomic.

The coverage matrix and machine-readable handoff in the same evidence folder
distinguish original live observations, bounded original-code witnesses,
portable runtime checks and unverified transitions. In particular, original
PC has no retail local split gameplay capture here; native layout3 selection
and projection are bounded witnesses. No Switch hardware acceptance is
implied. The diagnostic replay camera retains its documented type1 path;
it is not an original in-game replay-camera implementation.

## Reproduction

Run the original witness from this checkout with a fresh output directory:

```text
python -B tools/race_camera_native.py --root <retail-root> --out build/camera-native-check
```

`nfsu_race_camera_tests` checks the projection fixtures, split reference aspects,
POV transitions and native chase basis. The complete `nfsu_drive_tests` still
runs these and its separate mechanical replay pin. `nfsu_lan_tests` checks actual local
RaceSession camera distances for both players and both chase POVs.
`nfsu_frontend_retail` checks the displayed garage projection and its native
controller paths. `nfsu_crash_camera_tests` and `nfsu_race_session_tests` cover
flight/impact admission, return, timing, collision clearance and effect
ownership. The full default and Release gates, source identity and exact
results belong to the private handoff receipt, including any pre-existing
failure. A successful camera regression does not turn an unrelated failing
full-suite pin into a pass.
`nfsu_lan_camera_retail` exercises real loopback UDP lobby/input/snapshot
packets through both full-screen LanScene participants, including independent
POV selection and single-view anchor ownership. Its caller-owned clock is
controlled so asset load duration is not treated as a network timeout.

# Owner request closure audit

CHECKPOINT
role: worker
agent: Codex root solo
assignment: Owner rejected early delivery and requested verification and correction of all earlier requests using the original executable.
phase: investigating
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
base: ea4d472c9c399265370fa10be94736b1121771ad
worktree: build/day20261008-root-combined
saved: Local dirty integration; sealed r26 preserved. Initial source comparison is build/owner-audit-r27/root-assignment-source.json with no differences.
verified: Read canonical protocol/recovery, recent owner feedback, r23/r24/r25/r26 checkpoints and current implementation boundaries.
decisions: Continue solo locally. No console send, Git index/history operation, remote publication or delegation. Preserve existing GLM merge state and read-only retail files.
remaining: Verify each owner request against production code and original consumers; correct uncovered implementation gaps; run affected mandatory gates on the final coherent source; hardware and independent review remain unperformed.
next: Finish original camera surge input/publication checks, then continue original reward/lock and audio producer boundaries.
blockers: None for local investigation. Hardware acceptance requires a later owner-authorized run, and cannot be inferred from PC/native tests.
ownership: root active

The audit ledger is docs/OWNER_REQUEST_AUDIT.md. Historical green tests and old
checkpoint statements are navigation only. Each conclusion requires a current
production call path, an original/data witness where fidelity is claimed, and
a distinction between local verification and hardware acceptance. Explicitly
parked reports stay visible; they are not silently marked resolved.

## First confirmed runtime omission: knockout retirement

The current timing/minimap handled elimination, but render/contact/AI-neighbor
consumers never received original actor884/888/8A8. Original435344 sets a
permanent ghost plus a temporary blink interval for non-humans.570B90/4280A0
use the world timer for expiry and7364DC parity for blinking;466890 supplies
the dynamic body ghost.468175 suppresses a response when either body is ghost
and both are dynamic, retaining static track contacts.57A300 rejects permanent
ghosts as ordinary AI neighbor candidates. The stop actor is not deallocated.

The portable transition, current scene body/shadow/glow hiding, car response
gate and live neighbor admission now receive that state. Restart creates a
fresh timing owner. tools/race_actor_retirement_native.py executes the actual
transition and visibility/body consumers without external hooks; its fixture
covers timer sentinels, expiry equality, unscaled/slow/zero dt and both phases.
The original per-view masks and replay/debug presentation overrides are
explicitly supplied ordinary values, not claimed complete multiplayer parity.

Targeted timing/session tests passed in root-retirement-tests-r4.log. The
first compile used a wrong helper name. The first run omitted required rival
identity and entered an unbounded new test loop; it was stopped with verified
PID/start identity. The test now checks setup and bounds its countdown. The
next fixture polluted the human lap state and incorrectly tried to restart an
already started session. Correcting those diagnostic inputs produced the r4
pass. These failed/cancelled runs are retained, never counted as acceptance.
The added retail knockout scene gate passed: both blink phases and final
body/shadow hide, unchanged scene resource ownership, and subsequent real
planner steps with the retained actor. The transition is supplied to the
actual event5 scene, not a naturally completed knockout race. Full required
gates and hardware acceptance have not run on this new source.

## Additional original-code findings

4AA200 initializes profile+8D;4B47A0 copies profile758951 to78A3FA.
This resolves r26's previously untraced default intensity. The actual native
initializer-to-race publication passes tools/camera_presentation_native.py;
tests/camera_presentation_native.inc owns the value. No retuning was needed.

447600 calls51D240 with unscaled73AD38, and51D240 forwards it through the
actor+30 update to524620. The engine audio envelope's real-time cadence should
not be changed to the camera's scaled physics dt. Category/mastering and
prerace scripted sound remain separate from this clock finding.

426450/426300 do not filter ghosts before summing drafting. Do not introduce
an intuitive retired-car drafting filter absent from the original consumers.
New ghost code does not change this native oddity.

## Camera motion and acceleration response

Original47D070 publishes the final camera eye derivative with unscaled73AD38.
The old NOS adapter used vehicle velocity instead. camera_motion.h now owns
that derivative, zeroes it on view cuts and retains it for non-positive dt.
Both Switch and PC preview consume RaceSession's actual final presented view.
The native view Y axis is negative portable up; the original5AC449 scalar
matrix operation order is now shared by both effect consumers. The supplied
matrix/eye native witness passes without external hooks. Targeted blur,
crash-camera and race-session tests passed in root-motion-tests-r1.log.
Logical display time still uses the session's fixed tick; actual presentation
interval aggregation and hardware appearance are not certified by this test.

Original489855..489E20 has an omitted longitudinal response: acceleration
history, delayed threshold detection, slip attenuation, target clamps,
asymmetric approach/return, speed fade and braking height. The old chase arm
was fixed. camera_surge.h now implements ordinary mode32C=0. A consecutive
original-executable witness (camera-surge-native-r2) covers accelerating,
coasting, braking, slip, variable/zero dt and thresholds without hooks.
489EB3..489F38 confirms the authored arm sign and braking height with an
explicit supplied ordinary forward blend on the x87 stack. No original
executable bytes are modified. Fixtures own results rather than this document.
ChaseCamera receives the completed outer-frame velocity derivative from
RaceSession, clears history on reset and retains the authored FOV. Its live
production-path targeted check passed in root-surge-tests-r3.log (drive,
blur, crash-camera and session). root-surge-tests-r2.log selected no tests
because the filter omitted the _tests suffix; it is not a validation pass.
The corrected command uses --no-tests=error. This is not complete
original camera collision/rear-view/terrain/DOF or console blur acceptance.

One rejected native view-transform probe stopped inside the final return
instruction because its declared byte range was too short. Extending through
that instruction produced camera-motion-native-r3, which passed. It is not
counted as a successful earlier run. One exploratory Image constructor call
passed a Path instead of bytes; corrected before any evidence was generated.

## Current validation boundary

The coherent retirement/camera correction source is frozen for mandatory
Default/Release validation in root-source-freeze-r1.zip and its adjacent
receipt. Further original audio/unique-policy investigation remains read-only
until those gates finish. This boundary is not a console delivery or a claim
that all owner criteria are closed. No hardware send or Git operation occurs.

## Camera integration: required gate failure and isolated comparison

The first Default mandatory CTest run failed camera-dependent replay/frame
pins. It is not a pass. Release configured/built, then its repeated expensive
CTest was cancelled after that known failure; it is not a pass either.
root-required-host-r1.json and root-required-host-r1-cancel.json preserve the
results and verified process identity. No failing pin was silently removed.

The sealed r26 and frozen r27 sources were built in a separate build-prefixed
reference directory. Identical diagnostic-only instrumentation masks precisely
the camera bytes in each encoded record; production code is not instrumented.
Both full retail replays produced real new outputs, no error text, and equal
retail manifests. Every noncamera record hash and every substep trajectory
sample matched. The actual camera/kernel change is independently covered by
camera_surge_native.py and production chase tests. This is the evidence for
updating camera-containing portable pins, not a change to original fixtures
or a fitted finish time. root-camera-isolation-r1.json records the commands.
Fresh image captures in pin-harvest-r1 preserve that same physical trajectory
and state for each sampler/effect combination. Pin replacement is still being
reviewed locally and is not a completed final gate.

## Unique reward sequence correction

Original4AB670 selects MU_UG_JunkmanGiveth.fng or
MU_UG_SamanthaGiveth.fng for the respective successful special event. The old
port reused normal garage art. Their authenticated FrontB offsets and the
GLOBALB GenericDialog package are recorded in the native witness research.
Current source uses those actual layouts, portraits and localized headings.
4C6C00/4C76D0 select a separate reward voice; Switch now requests it on unique
page entry without replacing the normal briefing voice or stopping music.

4C6CB0 filters installed unique flags.4C7360 maps the selected category via
6FAD60 and sets exactly one separate flag.4C8720/5A2620 on actual Civic unique
hoods charges no money and grants the original ownership key. The portable
transaction/eligibility now receives the corresponding guard. The bounded
native witness tools/career_unique_reward_native.py executes those consumers
and actual reward voice lookup without external hooks.

Correction to the first research note: a two-accept confirmation already
existed in CareerGarage. What was missing was its original dialog art, the
initial explanation state and the separate reward voice. Do not describe the
existing confirmation transaction as newly recovered. An accidental duplicate
condition during editing was removed before compiling that revision.

The native full accept handlers4C74D0/4C8900 first dismiss the explanation;
4C7200/4C85B0 block scrolling until then. The next accept requests the generic
confirmation. A supplied absent FEng context executes the handlers, stops at
the dialog-creation boundary and records its actual descriptor, without a UI
call hook. Current source now models that initial state and hides the actual
blue_menu_backing/BorderFade/button subtrees after dismissal. Visual inspection
of the first renderer output exposed the missing dismissal before acceptance;
the subsequent source correction is awaiting its targeted recheck.

The first unique UI gate failed because its new test mode constructed an
absolute non-ASCII output argument as a narrow filesystem path. It now uses
argument_path and links the existing path adapter. The corrected root-unique-
tests-r2.log passed both career and new UI gates, with the read-only fixture.
That pass predates the just-added initial-explanation correction, so it does
not validate the newest state by itself. The render output and native logical
checks remain local evidence; no console or independent review is claimed.

## Reward UI recheck and camera pin correction

The r3 unique UI and career checks passed with the retail fixture intact.
Newly captured choose/confirm pages were inspected: the explanation backing
is gone after dismissal, and confirmation uses the original GenericDialog.
The images intentionally capture the UI alone, not the garage world.

The full upgraded old/new replay also preserves every noncamera record and
240Hz physical trajectory (root-camera-performance-isolation-r1.json). The
fresh pin-harvest-r1 matrix supplied only camera-dependent portable pins.
Car-specific drive and INTEGRA camera sequence pins were refreshed; static
car images, input/trajectory pins and original operation fixtures were not.
The first targeted pin run reached the later INTEGRA pin and failed there;
that isolated camera-containing pin was then corrected. The coherent host
mandatory run r2 is active; no pass is claimed until both configurations end.

## Original mix investigation

539210 initializes the actual music descriptor gain to32767 before its
allocation helper.53CA50 multiplies it by the settings music gain; ordinary
profile3 settles to full output, whereas the port uses a fixed half gain.
536C20 separately scales the owner category10 engine bus; profile7 mutes it
and clearing the crash profile restores it through the native transition.
The first scratch consecutive probe mistakenly used listener+8 as a clock;
that field is the listener index. It failed at5369CC, not a passing result.
The corrected root-audio-output-oracle-r3 preserves index0 and executes real
profile requests/curve updates/consumers. Production mix correction remains
unimplemented at this checkpoint. These probes perform no audio IO.

## Mix and traffic-neighbor production correction

The corrected pure native mix witness is tools/race_audio_mix_native.py.
Its r6 run verifies the original descriptor initializer, request/clear,
curves and final category/music consumers without external hooks or IO.
The first reusable tool run differed only because the preexisting fixture
uses CRLF and the generator used LF; normalized text comparison fixed the
format check, without changing any numerical row. RaceAudioMix and both
actual audio gates passed in root-audio-mix-tests-r1.log. The runtime passes
crash-camera state through the snapshot, applies category10 to bank engine
controls and consumes the restored music gain in the device callback.
Other effect categories and spatial/adaptive producers remain unclaimed.

57A300 execution now also verifies ordinary-neighbor admission explicitly.
Permanent ghosts reach the traffic obstacle adapter without changing spawn
or retention. The retail pool test uses the same published actor as visible
then ghost, demonstrating different actual braking. root-traffic-ghost-
tests-r1.log and retirement-neighbor-native-r2 are passing local evidence.
The session propagation checks only allocated racer pointers and permanent
flags; transient body ghosts do not silently replace the original filter.

The old r2 source is sealed in root-source-freeze-r2.zip. Its mandatory run
was stopped by verified process identity before mixing further corrections;
247 completed Default tests and no completed full verdict are recorded.
Release did not run. root-required-host-r2-cancel.json preserves the identity.
The next mandatory run is a fresh coherent r3 boundary; no console/Git/remote
action has been taken.

## Mandatory r3 result and subsequent corrections

The complete Default r3 run finished with247 passes and2 failures, not a
passing gate. Release r3 did not run. The new actor_ghost helper incorrectly
indexed an absent timing roster in the isolated contact diagnostic; a bounds
check now preserves the original unregistered-body behavior. Its contact
selection passed root-contact-guard-tests-r1.log.

The second failure was the extra desktop CPU/GPU diagnostic camera pin set,
not original executable disagreement. root-diagnostic-camera-r1.py executes
fresh old/new outputs for all12 diagnostic cases with identical instrumentation
that hashes every noncamera record. All physical records agree; only portable
camera-containing pose and image pins were migrated. Production guards remain
intact. nfsu_texture_filter_retail then passed in root-audio-device-tests-r1.log.

The deeper sound audit found the original fresh master/bus and intrinsic
positional gain stage before526AA0 and53CA50. tools/audio_position_native.py
executes those consumers with actual contexts and explicit single-listener,
unity-song inputs; it excludes device IO and child object refresh. The human
engine and both race/FE music paths now apply the witnessed absolute gain.
The first synthetic FE test reused already-scaled PCM; it was corrected to
supply a fresh decoded block before the next application. The corrected
nfsu_audio_playback_tests passes. The other six selected checks in root-audio-
device-tests-r1.log pass, including actual retail engine/audio and FE transition.

These are targeted results after the frozen r3 source, not a replacement for
both mandatory full configurations. The earlier SDK r1 passed10/10 but also
predates these changes. Final source, SDK, Default and Release gates remain due.

## Confirmed remaining audio actors

425350 dispatches distinct human, remote racer and traffic classes. The port
currently plays the human actor only. Traffic original5251E0 drives ENV_STATIC
horn/pass-by controls, not a substitute player engine. The bank is the original
ENV_COMMON module; its sample table is being traced before binding output.
Remote prerace RPM scripts524240 belong to the AI subclass523420, not the
smaller human allocation525C10. The earlier tentative player-script omission
is withdrawn; remote engine/script playback is genuinely absent. This work
continues locally, without console transmission, Git mutation or remote writes.

## Traffic audio controller, bank and positional integration

The traffic controller is now implemented in traffic_audio.h and verified
against tools/traffic_audio_native.py's consecutive original5251E0 states.
The witness executes real geometric gates, shared sound RNG, horn family,
pass-by and fade lifecycle, with explicit positional/allocator/backend inputs.
A separate hook-free traffic_position_native.py executes the actual common
position interface with traffic context and supplied single listener registry.
The production distance gain, camera-angle publication and Doppler match it.
The first distance-curve attempt used rounding instead of the original CRT
truncation; corrected source passes. Paused category mute is also preserved.

RaceTrafficSound now predecodes the original ENV_COMMON samples and owns fixed
horn/pass-by slots for the traffic pool. Snapshot publication carries actual
positions, velocities, CarTypeTable indices and activation epochs. Despawn,
replacement and finish retire their voices. race_effect_audio_native_check.py
also compares the six selected ENV_STATIC event variants through the actual
bank wiring; all27 cases pass in traffic-effect-native-r1/report.json.

Adding the actual category9 channel exposed the minimum10ms curve interval
when an original profile says zero attack. RaceAudioMix now preserves it.
The expanded native consecutive mix fixture passes, including this transition.
root-traffic-live-tests-r4.log passes the audio units, actual retail PCM horn/
pass-by playback, retained storage and lifetime checks with the retail fixture.
The initial r1 compile failed before a stale old unit binary was inadvertently
invoked; that invocation is NOT evidence. Subsequent build/test runs guard the
build result before invoking tests. Final mandatory matrix still remains due.

Output stereo panning/resampling/limiter remain the declared portable backend,
not retail device equivalence. Remote racer engines and their prerace scripts
remain absent. The next action is their original actor/control-path audit.
No console send, Git mutation or remote publication has occurred.

## Human engine audit correction: prior envelope proof was wrong actor

The actual human vtable is6BEEB0; the remote subclass is6BF2C0. Its human
update52C620 dispatches load527080, RPM527180, shift callbacks5292B0/5293E0,
state529530, input52C3F0, camera balance52C140 and gain529CD0. The remote
update524620 uses523B30/523990/523A70/523E30/523C30/5235A0 instead.
The old engine_envelope_native.py executes the latter with the player's load
divisor override. Its mathematical agreement is real but does NOT establish
actual human engine scheduler equivalence. Earlier player-envelope completion
claims are withdrawn; the current production human still uses that path.

Human differences already confirmed: immediate throttle-conditioned input,
throttle-average load rather than the AI on/off average, authored initial
RPM drop on shifts, low-RPM upshift refusal, state-before-curve scheduling,
angle-dependent engine/exhaust balance, and category1 rather than AI10.
5293E0's1D18 field is CURRENT GEAR, not gearbox mode: the native notification
526AF0 publishes actor3D8 there. Keep that distinction when interpreting the
neutral and low-gear downshift guards and shorter durations. The native human
witness and production correction are the next required work, before any
final validation or console candidate. No physical acceleration retuning is
justified by this sound-class mismatch.

525BC0 at human vtable+9C is the NEXT table's destructor, not a human input
method. The real human table ends earlier and52C620 never dispatches that
slot. It was inspected only; no retail executable bytes were changed.

## Actual human control correction implemented

human_engine_native.py now executes actual52C620 human control order with
explicit physical/gear/camera inputs and bounded stops before unrelated shift
FX. HumanEngineEnvelope matches its consecutive RPM/load/shift/accent and
final gain rows. human_camera_balance_native.py executes52EA00,52C3A0 and
52C140; the corresponding live angle/camera balance now matches it too.
The actual category1 profile curve is separately natively checked. Runtime
human gain now reaches the bank BEFORE its controls, with the native
balance-before-accent order. The former remote brake latch no longer controls
human PCM. Retail playback checks throttle and shift behavior accordingly.

The new full-domain inverse-sine wrapper initially reused an insufficient
angle table. Its native camera witness caught the out-of-bounds tail. The
read-only generator now captures the complete native table. This is NOT a
confirmed preexisting physical trajectory bug: prior physical callers only
use the min-axis normalized domain and cannot reach that tail. The owner was
explicitly told this correction to the earlier broad diagnosis. Frozen r4
preserves the exact source before extending the table; no old physical state
or frame pins were refreshed for this change.

root-human-angle-tail-tests-r1.log passes the corrected units and actual retail
human/traffic PCM checks with the retail manifest intact. This predates the
latest sparse active-voice mixer loop, whose existing gates still need rerun.
No final full Default/Release/SDK result is claimed. Remote playback/prerace,
adaptive/limiter audio, natural world collision parity, console presentation
and independent acceptance remain open. Continue the audit, not a console send.

## Coherent human/traffic boundary before full validation r4

The corrected actual human and traffic retail checks pass. The mixer now builds
one active-slot list per output block rather than scanning unused traffic slots
for every PCM frame; the existing output tests and retail PCM checks pass.
Native original witnesses are registered under the retail read-only fixture
rather than left as manual claims. The first fresh-output wrapper mistakenly
passed its already-created temporary parent to tools requiring a nonexistent
output; those five checks correctly failed. The wrapper now supplies a new
artifacts child, and all five rerun successfully in root-native-wrapper-tests-
r2.log. The earlier six direct witnesses and audio tests pass in root-native-
gates-tests-r1.log; the overall first selection remains a failed run.

The next frozen r5 source is the Default/Release mandatory r4 and SDK r2 input.
No source mutation is permitted during those runs. Further investigation may
write only uniquely named scratch evidence, not the frozen production tree.
The new tests distinguish pure original instruction slices and explicitly
supplied boundaries; none is a complete hardware or natural trajectory verdict.
Remote sound, adaptive/limiter controls and the broader owner ledger remain
open, so this boundary is not an audit-complete release or console send.

## Frozen r5 full validation completed

Default configure/build/ctest passed260/260; Release configure/build/ctest
passed265/265, including retail-full and no skipped retail check. SDK r2
passed10/10. All1180 source manifest hashes were unchanged after the runs.
Exact commands and outputs are retained in root-required-host-r4.json and
its named logs; the sealed r27 BUILD_RECEIPT now includes the results.
The source freeze is released for further authorized corrections.
This is not owner acceptance: remote audio, adaptive profiles, collision
natural-world evidence, console presentation and independent review remain.
No Git or console action occurred. Continue the prepared r28 integration.

## Adaptive human audio and debugger setup

Original536800 publishes before human requests, not after them. RaceAudioMix
now caches published gains and binds actual52C1D0 idle/high-speed predicates.
Human envelope retains native local time, throttle edges and accepted shift
timestamps. The new consecutive witness and profile-state tests pass locally
in root-human-profiles-tests-r4.log. The first compile had a test variable typo;
the first clock comparison exposed Python double-dt rather than stored native
float-dt. Both failed attempts are preserved; corrected witness uses the
actual stored dt. Full final gates remain due after this new source boundary.

The optional debugger was absent. An isolated official v6 debugger environment
and Microsoft DbgEng redistributable were installed under debugger-owner-r27;
static8091 was retained. Owned PE32 smoke attach/read/three entry traces and
detach work. Non-ASCII executable path exposed a backend decoder failure;
ASCII target works. go_nowait does not pump execution; bounded go_wait does.
Return capture is not verified. No original episode was launched or measured.
This setup is prerequisite evidence, not collision parity or performance.

## Second initialization correction: human versus remote divisor

The human table6BEEB0+2C is526420; remote6BF2C0+2C is5234C0. Verified
526624 loads actor+40 RideInfo+1F0, calls4647C0 over nine torque samples,
and stores their maximum at human1F80. Remote5234C0 then overrides1F80
with160. Earlier notes and the first human witness had this ownership
reversed. Those initialization claims are withdrawn. Human native fixture
now authenticates both vtable slots and executes actual526624..526638
before human controls. Production snapshot supplies the selected/tuned
handling maximum. root-human-divisor-tests-r1.log passes the corrected
consecutive states. This sound correction does not retune physical torque.

The optional196C RPM subtraction is a curve output, not a proven rev limiter.
The current code scan finds its constructor and updater, but no active trigger
is established. Keep it unclassified, rather than inventing limiter behavior.

Owner cannot launch original on this PC now. Natural contact/scheduler
capture remains awaiting that external runtime, not passed or complete.
Static code and other local fixes continue. No console send or Git action.

## Remote engine runtime and signed bank controls

Remote playback now binds selected grid car and resolved engine level, with
fixed voices and predecoded PCM. It consumes live rival RPM/gear/torque/
throttle/position, actual remote shift state and authored523560 balance.
RemoteRevScript preserves both original tables and consecutive524240 state.
Native remote position and script fixtures pass; actual three-car PCM/lifetime
checks pass in root-remote-runtime-tests-r9.log. Script restart consumes the
actual planner/drag world RNG through a nonretained main-thread borrow. Sound
RNG is shared with traffic. Other original RNG consumers remain unimplemented;
this is not a complete natural world/sound scheduler verdict.

Raw5235A0 accent products can overflow signed32 on retail records. The wider
port arithmetic and premature bank parameter clamp were wrong. Native wrap/
shift results now pass through526AA0/536C20 and into bank controls unchanged.
The final PCM backend still owns its declared device clamp. The first new
comparison caught negative native outputs and the test fixture row layout
was initially misread; failures are preserved. Corrected raw-gain checks match
all32 actual banks in root-remote-runtime-tests-r8.log. Human initialization
proof still uses declared stock RideInfo curves; actual selected/tuned source
uses the production handling and its independently checked setup pipeline.

Source moved authored tables into one compiled owner to avoid public-header
copies. Final mandatory Default/Release/SDK gates remain due for this boundary.
Natural original contacts await a runtime episode the owner cannot supply now.
No Git action, hardware result or console delivery is claimed.

## Frozen r6 validation and next traffic correction

Default264/264 and Release269/269 passed in mandatory host r5, including
retail-full with no skipped retail. SDK r3 passed10/10. All1188 frozen source
hashes remained unchanged. Exact commands/results are retained in
root-required-host-r5.json and the sealed r28 artifacts/BUILD_RECEIPT.json.
The r6 source freeze is released. This is not console acceptance or review.

Original57D000 and4B3AC0 duplicate/skin stages differ from the port bag
without replacement. Fresh native execution with authenticated retail data,
six supplied seeds and consecutive config IDs passed60 boundaries; a scratch
C++ chooser matches them. This does not execute actor construction between
selections or prove the complete frontend/global RNG lifecycle. Owner cannot
launch the original now; natural contact/scheduler capture stays open.
Continue local corrections. No Git or console action occurred.

## Traffic weighted chooser and strategy map, r29 preparation

Production load_race_grid now uses the actual class/enable/variant filters,
weighted draw, duplicate retry and skin-RNG boundary instead of a bag. The
initial frontend port seed and unexecuted actor constructors are explicit
limits. New traffic_model native fixture and compiled decoder/chooser checks
pass. Original583BB0 strict half-range predicate replaces the port <= test;
its native event/seed fixture and ai_road consumer pass. Required targeted
checks are root-traffic-tests-r2.log. The first build failed a test helper name
arg_path; corrected to the actual argument_path before testing any binary.

The finite traffic-impact viewer initially failed because synthetic proving
course.name did not identify a retail road file. The fixture now names the
real grid road image while retaining the synthetic physical world. Dynamic
result storage follows the actual pool size. root-traffic-production-r2.log
passes selected hull wall retention; it is not natural original impact parity.

Native traffic policy4 is44A200/vtable6C97D4, created by583230/44AA10.
44E020 selects obstacle18/4543D0 then avoid3/453920, default8/44E260.
583910 combines cap/road interpolation/drop producers. This is a confirmed
remaining adapter gap; do not claim the current corridor reproduces it.
Ghidra query records are root-ghidra-owner-traffic-* under engine-audio-r24.
Next: mandatory final validation of this coherent chooser boundary, while
continuing scratch-only strategy research. No source edits during its freeze.

## Required host r6 failure and exact-model fixture correction

Default r6 ended266/267: race_world_contact_retail failed. Release r6 did not
run. All1196 frozen r7 hashes remained unchanged. SDK r4 passed10/10, and
r29 remains sealed and unsent. The failed host receipt is retained with it.
The mandatory contact fixture indexed traffic in the former bag order while
its native generator names six explicit models. The weighted selector changed
that order. The test now finds those same named models; no native contact pin,
state, inertia, tolerance or production solver was changed. Targeted rebuilt
race_world_contact_retail passes in root-traffic-world-tests-r1.log. The full
Default and Release matrix must now rerun; the prior full failure is not green.

Further scratch traffic strategy evidence: action3/vtable6C939C update453C60
tries current/adjacent lanes through453BA0, retains target and gap/time state,
and publishes its own speed/path/signals. It is not the finite corridor now
used by RaceTraffic. Current44F1B0 scratch uses initialized racer objects with
exogenous public poses; physical frame/cache provenance is not validated for
that new consumer. Do not fit production behavior to its unexpected misses or
claim a coherent natural traffic scene from it. Early attempts fail undeclared
instruction guards; no semantic prediction result is hooked or substituted.
Continue original producer/lifecycle recovery separately from accepted math.

## Exact-model fixture validated; next human shift correction

Mandatory host r7 passed Default267/267 and Release272/272, no skipped retail.
All1196 frozen r8 hashes remained unchanged. SDK r4 remains applicable because
only the host contact test and checkpoint changed after its10/10 pass. Failed
r6 remains recorded. r29 stays unsent and not fully accepted.

A further confirmed audio omission:5292B0/5293E0 schedules a delayed shift
cue, dispatched by the initial529530 stage only after its authored deadline.
Original529100 also scales event gain by audible RPM and tuning short8. The
port used immediate gear-edge playback with constant gain. Scratch C++ matches
native cue state and the native gain lookup; fixtures are prepared separately.
The native cue output actor is deliberately inactive, so this is not device
or live scheduler parity. Next integrate this confirmed producer/consumer gap.
Turbo bank scratch needs callback11 and wrapping inline IMUL; original timer
expires at equality. The first comparison failed on every first tick, and the
corrected scratch matches all tested banks. No turbo runtime is connected yet;
its selected setup, control producers and lifecycle still need restoration.
No physical, retail, Git or console mutation occurred during the frozen run.

## Human delayed shift integrated and targeted checks

HumanEngineShiftCue and actual529100 gain now connect through RaceEngineSound
return publication, the current category gain and RaceEffectsSound into PCM.
The former immediate positive-gear-edge trigger is removed. Authored delays
and gain are decoded from the selected sound row. Existing physical RPM,
shift curves and transmission are not retuned. Targeted Default audio/native/
retail fixture checks pass5/5 in root-human-shift-tests-r1.log. The compiled
engine-to-effects bridge observes one delayed sound and cancels at finish.

Scratch native turbo graphs initially rejected callback11 and inline IMUL.
After adding those only in scratch, all45 static-input bank cases pass in
root-turbo-native-bank-r2/report.json. The earlier45 first-tick mismatches were
real: callback11 expires on equality. Production graph/compiler and turbo
runtime are not changed by that scratch proof. Turbo control528B60/528830 can
execute on supplied parent/position/event state with no hooks, but constructor
and start values still need verification before binding. Normalized boost is
produced by52C3F0;1288 is its ring mean, not an invented timer. The earlier
five-entry interpretation is corrected below by actual constructor evidence.

The next frozen source is mandatory host r8 and SDK r5 input. No source edits
are allowed until those runs complete. Further turbo work stays in uniquely
named scratch files. Natural original collision capture is still unavailable;
owner cannot launch it now. No Git or console action occurred.

## r30 validated and human Turbo connected locally

Frozen r9 remained unchanged through mandatory host r8: Default268/268 and
Release273/273, no skipped retail. SDK r5 passed10/10. The corresponding r30
NRO and source/build receipts are sealed under build/owner-audit-r30/artifacts.
No console send, publication or Git/index mutation occurred. Those results
precede the Turbo integration and do not validate its new sources.

Actual525D45..525D8B and584310 show a three-entry pressure-difference ring,
not the five-entry scratch reading. The native selection witness executes the
RideInfo default, resource producer and filename resolver. Consecutive native
parent/spool/release checks use a supplied existing primary event and explicit
output allocator/device/completion boundaries. They establish scalar controls
within that boundary, not natural race/device lifetime. Scratch spool r1 first
failed paused gain; r2 corrected the real pause gate. Earlier timer equality
failures remain recorded. No solver or retail input was patched to match.

Production now connects human Turbo setup, normalized pressure, current human
RPM/load, category3 profile publication, shared sound RNG and selected bank to
two fixed voices sharing samples. The bank translator gained only the checked
timer and inline IMUL operations. Targeted checks pass7/7 in
root-turbo-tests-r1.log, including actual retail PCM/finish silence, native
controller and all-bank comparisons. Source: ENGINE_ENVELOPE.md and the tests.
Next freeze this coherent source and run mandatory Default/Release and SDK
validation. Continue scratch-only research while frozen. Immediate high-RPM
shift-disengage/backfire actors, native traffic strategy/path producers,
crash focus/DOF, console NOS appearance and independent acceptance remain open.
Natural original collision capture is unavailable because owner cannot launch
it now. It must not be marked passed. No repeat request or console send now.

## r31 validated, unsent; precise next traffic checkpoint

All1203 frozen r10 files remained unchanged through mandatory host r9:
Default269/269 and Release274/274, no skipped retail; SDK r6 passed10/10.
The coherent Turbo/shift source and unsent r31 NRO are sealed under
build/owner-audit-r31/artifacts with exact command/source/build receipts.
The later checkpoint and ledger append only documents; runtime inputs are
unchanged. Bash HEAD path/blob/mode ASCII checks pass in root-ascii-r10.log.
The current source byte/path and new automation scans are recorded at sealing.

Further traffic research stayed scratch-only. Actual44CF30/44F470 executes
on supplied initialized route poses in root-traffic-neighbor-query-r2.json.
The original query combines lane and physical forecasts. Scratch C++ matches
its aggregation with physical returns supplied from those same native calls;
this is not independently verified physical prediction or a traffic action.
The initial r1 range-sum assertion failed on a legacy declared block containing
undecoded padding. r2 retains the per-visited-instruction address/length guard;
there is no widened fallback execution or substituted semantic callback.

Actual458BD0 with vtable6B96B0 and549130 is checked in
root-traffic-cache-native-r1.json. Traffic does not share racer462840's
forced/disabled/marker policy. Type9 is not cached. Original458CC0 clears a
cached decision only after the signed, lap-aware window has passed. The
scratch explicit Traffic policy matches native decisions/replays and expiry
in root-traffic-policy-preview-r1/report-r2.json and expire-report-r1.json.
The first full-source scratch link failed unresolved unused functions and
produced no executable; the second compiles only the exact tested kernels.
No artificial successful query or stale binary result was accepted.

These cache/kernel candidates are NOT promoted into the live traffic owner.
Before that, recover its category/lane selection, bounded decision lifetime,
path/road context and583350 update order together. Current RaceTraffic's permanent
junction decisions, port admission/corridor and min-point speed remain open.
Do not replace the shared racer policy by assigning it a traffic role number.
The native callback pair, distinct event+0C and no-op accepted-event handler
must be explicit. Road progress/initialization and the eight-slot contract
need coherent live integration before removing the permanent cache.

Immediate high-RPM shift feedback528D70/528F80 remains a separate unconnected
actor. Decompilation/xrefs are preserved in root-ghidra-owner-human-disengage-r1.
It uses FX_SHIFTING_01, rather than silently reusing the delayed shift event.
Do not invent its timing, gain-curve initialization or bank group.
Natural original collision order/recovery, console NOS appearance and all
hardware/listening acceptance remain unavailable/unperformed. Owner cannot
launch the original now. No console send, Git mutation or independent review.
This is a validated local checkpoint, not completion of the full owner audit.

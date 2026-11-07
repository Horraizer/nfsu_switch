# Campaign feedback implementation, 2026-10-19

Owner assignment: fix the recorded remaining items. Solo implementation;
no new reviewers, chats or uploads. Preserve accepted drag-launch-r14 and
its immutable source/NRO/hardware evidence. Runtime-owned integration tree:
build/day20261008-root-combined, branch codex/draw-distance-original,
HEAD ea4d472c9c399265370fa10be94736b1121771ad; existing staged merge and
all prior dirty work retained. No commit/push/ref movement authorized here.

Local edits: original event portrait hashes and sender names; original
ordered 45-row speech request table at 0x6FA5D0; bounded original English
speech bank decode/mix; stop speech on leaving the message; original static
map icon strip spacing, clipping, size and alpha falloff. Added focused
career feedback render mode. These edits are NOT built or accepted yet.

Original evidence in ignored build/campaign-fixes-r15: sender-native-r1.json,
speech-event-table-r1.json, briefing-controller.asm, scroller-layout.asm,
scroller-item.asm, scroller-selected.asm. Real portrait function 0x4C55F0
executed for every distinct event portrait. Map runtime list uses original
master_thumb width 128, center clipping [-200,200], size factor
1-abs(x)/320 and alpha factor cubed. Earlier 0x4C3E car-list spacing is
unrelated and was rejected. Native speech requests: event1=14,3=6,4=33;
events2/5 absent. Speech request to bank ordinal mapping is still under
investigation; current direct indexing must not be called proven. Original
selected-icon pulse and movement are not yet implemented.

Failed build: requested nonexistent nfsu_frontend_sfx_tests target; Ninja
stopped without compiling. Log build-menu-r1.log retained. Correct focused
audio target is nfsu_frontend_music_transition_tests. No Switch build yet.

Remaining: verify speech ordinal, audio/render checks; world animations;
NOS blur in freeride; post-finish sound and original stop/results flow;
repeat impact audio; traffic penalty/near miss and drafting scoring/aero.
Detailed original audit remains root-campaign-feedback-20261019.md.
Next: verify speech dispatch ordinal, compile actual focused targets and
inspect rendered map/briefing frames before continuing implementation.

Speech ordinal issue FOUND and corrected before hardware: original manager
0x537A60 constructs speech child at +24 using vtable0x6BDE10 (not preceding
destructor slot0x6BDE0C). Request virtual90=0x53DC30 publishes group index;
0x51DCE0 supplies 95 group sizes at0x6F6BC8, whose sum is277 bank entries.
0x539B85 sums preceding group sizes. Thus requests6/14/33 select entries
24/52/119, each a single variant; direct bank indexing was wrong. Corrected
FrontendSpeech. Later multi-variant messages currently select first genuine
variant; random variant parity is NOT claimed. Bank SHA checked in loader.
First focused compile and six render checks passed before that correction;
finite PCM and original select/back transition checks also passed. These
checks did not establish speech identity; run again with corrected groups.
Map and briefing contact sheet inspected: neighbors visible, sender portraits
and names correct; selected pulse/scroll interpolation remain unfinished.

Road feedback implementation, local uncommitted candidate:
- Repeat impact scheduler now tracks last nonzero impact independently of
zero-impulse overlap. A 300ms quiet touching pair can admit another sound;
continuous impacts do not restart every tick. Existing audio graph and new
concrete repeated-impact regression PASS (impact-check-r1.log).
- HIT TRAFFIC connected at actual successful solver callback: original
creation-age100ms gate, magnitude>=5, factor=min(accumulated/40,1).
Original signed-tick/double age comparison retained, including rejected400.
- NEAR MISS uses real convex hull gap and original1.5/1.875m acquire/release,
player20.116228 and relative40.232456m/s gates; contact cancels. Bounded
nine candidates, cheap speed/distance rejection precedes hull queries.
- Original426300 drafting velocity cone/reach and426450 sum cap.9 connected
to human/rival inputs, shared479160 aero drag multiplier1-drafting and
style26. Actual native426300 plus original helper execution, no hooks,
30 supplied contexts match production within2e-6 (draft-native-r5.json/log).
Earlier r1-r4 harnesses refused undeclared helper instructions; retained,
not passes. The final range includes authenticated clamp/ftol helpers.
- Focused retail style/audio/speech transition checks PASS; corrected speech
group mapping rebuilt and decoded. No Switch/hardware acceptance yet.

Postfinish implementation: original controller3/action17 desired-speed0
now uses ordinary AI pedals/steering on human physics. Physical world runs
through finish presentation, ignores human gas/NOS/steer, preserves finish
place/timestamp. Actual loaded drag finish probe passed: car moved then
braked to rest, despite full gas/NOS input (finish-drive-r1.log). Race stats
stop sampling at finish. Hardware result transition waits for the stop after
existing minimum presentation dwell. Drift/no-rival finish flow and complete
mode/place-specific result sequence remain OPEN, not claimed restored.
Removed provisional unrelated FE sample30 from finish playback, the concrete
candidate NOS-like tail. No invented replacement cue; original audible
finish mapping still OPEN. Genuine engine/effects already stop at finish.

World animation and freeride NOS blur remain under investigation. Discovered
ANIMATEDTEXTURE is an effect-name table reference, not animation timing proof.
World chunk inventory is navigation evidence only; no fake UV animation added.
Next: inspect actual blur graphics state/corner submission; then authored
world animation records, final consolidated targeted PC/SDK validation/seal.

World animation source FOUND: STREAML1RA B0300100/version1, 30300102
52-byte definitions, 30300103 16-byte frame refs. Original loader5456C7,
constructor545C60, binder545D50 and update545EA0 retained in evidence.
Update uses integer milliseconds*.001 stored binary32, fps multiplication,
truncation/modulo; every frame texture rotates through(i+phase)%count.
Actual authored examples: SGN_ARROW3frames/2fps, WATERA14frames/10fps.
Added bounded parser, metadata retention before stream-byte release, explicit
trailing animation textures in the prepared region, real per-frame resource
aliases and fenced per-slot GPU image descriptors. No per-frame texture
pixel upload, geometry rebuild or whole immutable command cache rewrite.
Generic texture/malformed-loop tests PASS. First actual scene failed on a
missing WATERA001 resource: decode had loaded it, but scene preparation
retained only drawn base frames. Added explicit trailing-frame residency and
validation instead of fake fallback. Updated actual event1 scene PASS with
37 aliases and all full command bindings valid (scene-animated-r2.log).
Live phase-advance check added next; not yet compiled. Static reference
recordings now apply the same alias selection. Other modes still require
current scene smoke because the shared loader changed.

Blur: code now derives normalized top-left UV directly from fragment pixel
coordinates and each pass output dimensions, matching the working FXAA and
streak paths. Removes fullscreen varying/viewport Y-direction ambiguity.
This is a candidate coordinate correction, not hardware pixel acceptance.
The four original passes/offsets/weights/alpha remain. Native corner data
40D090 confirmed original four0..3 tap offsets. One-frame GPU sparse capture
will be enabled in consolidated NRO to verify real output in freeride.

Final build/ASCII/seal/hardware checks still pending. Do NOT upload old or
unvalidated candidate. All existing failed logs and accepted r14 preserved.

Correction before delivery: native animation run exposed the time unit. The
constant6CC7B0 is1/60, not.001;6F08A0/6F0888 are animation frame ticks, NOT
milliseconds. Corrected production phase input to world60Hz step, tests to
29/30/60/90 tick boundaries. Actual native545EA0 now passes35 supplied
contexts, including rotation of every image pointer, no hooks
(texture-animation-native-r3.json/log). Initial r1/r2 failed assertions
retained. No hardware build with the wrong phase was sent.

Final PC pass r3: circuit and drag full scenes with live alias changes PASS;
finish braking/result preservation PASS; accepted idle launch PASS; style,
audio, speech, texture and blur checks PASS; six menu renders PASS. One
attempted drift smoke via career_races fixture refused original setup identity
before loading. That fixture does not support this drift setup. Use the
existing nfsu_drift_track_check production drift fixture; do not weaken its
identity guard. Current drift runtime acceptance remains pending this rerun.

SDK first attempt failed on const MemBlock.getGpuAddr() (SDK method is not
const). Corrected helper qualifier; second real SDK build and selected7/7
package/shader/contract checks PASS, switch-build-r2.log. This binary predates
the corrected animation clock, so it is NOT the delivery candidate. Rebuild
now with final source, validate drift with its existing fixture, seal only
that final source/NRO. No upload; no Netloader readiness presumed.

Final corrected source checks:
- build-final-pc-r2.log: actual desktop build PASS. final-pc-r4.py:11 focused
commands PASS, including production drift fixture, both fully dressed race
scenes with live frame alias change, finish braking, idle launch, style,
audio, speech transition, texture parser, NOS arithmetic and six menu frames.
The unsupported event3 generic-grid attempt remains failed, not relabeled.
- switch-build-r3.log: corrected60Hz source SDK build and7/7 selected NRO,
embedded-shader, derivative and sample-contract tests PASS.
- ascii-final-r1.log: Bash tracked HEAD/worktree contents/names/modes and
whitespace PASS. Existing line-ending warnings retained.
- Descriptor writes happen after the acquired slot fence; original immutable
image descriptors are never modified. Existing frame descriptor invalidation
precedes draw submission. Only37 first-circuit aliases/52 drag aliases are
patched; source image bytes and static command lists remain immutable.

Ready for source/NRO seal, NOT sent. Hardware appearance, voice listening,
new texture residency and performance acceptance remain unverified. Sparse
blur capture perturbs one NOS frame; exclude it from performance comparisons.
Accepted r14, failed iterations, source seals and capture cursors preserved.
Remaining fidelity limits: later speech random variants currently first variant;
scroller selected-icon pulse/scroll interpolation not restored; exact original
mode/place-specific statistics/result page sequencing remains beyond the
new physical finish stop and existing career win/loss progression. These are
not claimed complete. Original finish cue identity remains unverified; wrong
provisional sample30 removed, not replaced arbitrarily. No independent review,
full Default/Release matrix, commit, push, merge, publication or upload here.

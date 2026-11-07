# Original minimap, race audio and career presentation audit

This is the local owner audit of the PC1.4 USA inputs. The executable identity
is documented in ENGINE_ENVELOPE.md. Retail files remain read-only. Static
Ghidra observations, bounded native execution, portable implementation and
hardware acceptance are different evidence boundaries.

## Minimap

Ghidra5912B0 agrees with the existing style1 tilt, distance, offset, extent,
UV and opacity fields and human/rival/start colors.58EFB0 and5910F0 agree with
the current bounded camera/filter kernel.58E810 submits the start/finish,
then eligible remote racers, then the human marker. The portable HUD now uses
that order, so a rival at the same map position cannot obscure the human.
The final original traffic loop has a separate driver734 gate in addition to
actor visibility. It does not justify adding every ordinary traffic vehicle.

Tests: nfsu_race_minimap_hud_tests and the existing minimap native/kernel tests.
This is not a fresh original full-race or portable screen comparison.

## In-race audio

| Area | Original evidence and current boundary | Open criterion |
| --- | --- | --- |
| Human engine/exhaust | Selected CarTypeInfo sound rows, audible RPM/load envelope, camera/position and mix category consumers; human_engine, human_profiles, human_camera_balance, human_shift_feedback and race_audio_mix native checks | Transient/fade producers, natural original race and device listening |
| Remote engines | Separate remote envelope, countdown rev script and position; remote_script and remote_position native checks | Natural rival timing and final output mix |
| Turbo |51DEC0/521C80 resource selection,528B40/528E50 spool/release controller; turbo_audio_native and effect-bank native checks | Portable listening and nondefault signed sound variant |
| Delayed shift |5292B0/5293E0 notification and529530/529100 pending cue; selected bank and category1 | Natural scheduling and listening remain unaccepted |
| Immediate high-RPM shift |528D70/528F80 conditions and selected bank actors are connected; remote523990/523E30 has its distinct immediate main and later end publication, checked by remote_shift_native | Local bounded output is checked; natural module order, other remote effects and listening remain open |
| Tyres/NOS | Authored effect graphs and sample decoding, finish silence; race_effect_audio_native and nfsu_audio_retail | Physical slip input and remaining output adapter are not original device parity; listening required |
| Traffic | ENV_STATIC selected models, horn/pass-by and position consumers; traffic_audio and traffic_position native checks | Natural driver event timing and listening |
| Impacts/props | Bounded contact episodes, original bank selection and prop events | Owner-reported intermittent missing impact sound remains open; natural collision sequence unavailable |
| Backfire | Human527480 controller, original shared sound RNG and selected CAR_1 SparkChatter bank are connected; backfire_audio_native | Remote output and natural/device acceptance remain open |
| Race music/cues | Original played-entry playlist, countdown/go; unrelated FE finish/lap cues removed | Final relative engine/music level needs listening |

The required plain CTest runs regenerate the original audio comparisons and
exercise the compiled retail PCM path. A passing bank/controller comparison
does not establish all event producers, output lifetimes or perceived volume.
No original PC live capture is currently available from the owner.

## Career speech and movies

The6FA5D0 English speech mapping agrees with the existing catalogue, including
first-match duplicate event handling and entry0. A voiced event is not inferred
from the sender name. The selected original speech-stream entry is used.
4C53C0/4C5200/4C52D0 distinguish briefing selection and its audio overrides.

The connected campaign extends through final event113; see CAREER_CAMPAIGN_FULL.md.
Original before movies at7
(05_ENG) and15(06_SAM) now run before their briefing, then the correct speech
starts after frontend audio is active. The separate unique-reward speech also
starts after the audio device, rather than failing before music.start.
The16..30 block adds no before movie. Event16 requests07_GOD after victory;
event26 uses the distinct TJ reward entry after its prerace briefing.

Event after-movie requests are durable and play on victory; the generic
catalogue is not limited to the first hardcoded intro.4AB670 orders unique
selection before extro movie before magazine reward before Underground menu.
The dispatcher preserves that priority and does not replay an acknowledged
movie after a save/reload. UG0 retains its separate short/long movie flow.

4E8180 directly dispatches04_GUM_s for first customization.04_GUM_e exists in
the files and as an executable string, but no executable reference/dispatch
has been established. It is not inserted at a guessed transition. Before
movies use their original ordinary/special handlers:4C4EE0 routes7/15/92
before difficulty, while4C5DF0 routes the other authored intros after race
confirmation. Both phases are now connected through the finale.

Tests: nfsu_career_retail covers durable before requests and continuations,
unique priority, migration and campaign result transitions. Actual rendered
movie playback and speech continuity after this change need a console run.

## Magazine car snapshots

The30230 catalogue contains original name, front, caption, optional model,
background, camera, option-group and template pools.51C180 fixes the lists;
51A8D0/51C480 select records and consume the original random state.5A0BD0 copies
the selected background/front/model, camera and caption into the photo record.
The optional model's negative index means absent; the other negative index
traverses no list nodes and uses the first record. Native output comparisons
check texture binding as well as selected indices, after a first rendered
capture exposed the earlier mistaken front/background interpretation.

career_magazine_native.py regenerates tests/career_magazine_native.inc.
frontend_magazine_camera_native.py regenerates the native487B20/487680 camera
fixture, including nonzero roll. The portable scene uses those authored
camera inputs and real installed car geometry, not a stock promotional image.

Each earned cover persists its car, installed part hashes, performance/unique
flags and recipe seed in the port-owned v8 save. Future customization does
not change an earned photograph. Older save versions remain readable.
51B060 tuning/reputation predicates and51AED0 complete package predicates are
connected to sticky pending tasks; eligible photographs are materialized at a
career victory. Event kind1 magazine rewards are parsed separately from style
unlock rows. Events19/25/27 now grant these race-specific covers, retaining the actual
car and installed setup at the victory boundary. Their pending pages and
movie priority survive save/reload; they are not generic style rewards.

Original FrontB DummyMagazineBack and DummyMagazine objects supply image
geometry, caption strings/position/scale/color and paper masks. The reserved
caption word is not a missing font selector. A temporary private opaque white
carrier prevents the portable overlay from leaking the car through black
paper-border masks; the shared garage/UI textures remain unchanged. The
background is submitted before3D, and front/model/caption/masks after3D.
Cover and campaign-map art need not occupy the atlas together: a selected map
is restored when leaving the album. Palette/geometry snapshots are resolved
through the normal garage binding. Movie and menu audio are not embedded in
these images.

A real stock and tuned car can be captured with nfsu_career_tests RETAIL
--magazine OUTPUT.tga or --magazine-tuned OUTPUT.tga. Output must be fresh.
The native fixtures and nfsu_career_retail own the selection/task/camera pins.
The photo clipping counterexample is also checked by the capture mode.

Remaining original frontend criteria: ordinary MagazineScreen/Giveth browser
layout/animation, its song override, car normalization for special magazine
cases and non-tuning race-record/network task producers are not all connected.
The current album opens earned covers and cycles them; this is not a claim
that every magazine presentation screen matches the original. Background
mode is recovered and checked but its complete widget-state consumer remains
open. Native recipe equality alone cannot certify the final screen.

## Acceptance

The owner asked for this work in one local session. No delegate authored or
reviewed it. No Git publication or console transfer is authorized in this
pass. The current recovery checkpoint and sealed build receipts record actual
commands/results. Mandatory builds/tests, a real-car CPU capture and SDK
packaging are separate from unperformed Switch visual/audio acceptance.

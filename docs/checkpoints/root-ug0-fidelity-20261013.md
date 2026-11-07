# UG0 owner fidelity audit, 2026-10-13

Assignment: inspect the latest owner UG startup reference and original PC1.4USA
for welcome, traffic penalty/dynamics/routes/audio/lights, world animation,
finish message, minimap colors and movable roadside objects. Solo read-only
investigation. No delegated agent, source publication or deployment.
Source inspected: f0d720b517d039614a2d19d829b5fcfc42c4562f.
Reference: video_2026-10-12_16-46-38.mp4, SHA256
D61D80B7531D24A7EB9FD9F80FDBFBE4F8BEBF011D983D544FFF34F9FCD4AFDD.
The recording is55.523seconds: welcome/movie/loading/start and opening race.
It is not a complete finish or a controlled traffic-impact experiment. Viewed
existing overview and freshly extracted34..49second contact sheet. No new
isolated listening comparison; sound omissions below are source findings.
Original speed.exe SHA256:
C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7.

## Confirmed presentation gaps

1. Welcome: FrontB decoded offset0xBC528C is MU_UG_Explained.fng.
TheText resolves English hash64711625, with authored line breaks, and the
package has Accept_button/Backbutton (Accept/Exit), original background,
logo and selection objects. Current switch/race_main.cpp jumps directly
from Underground selection to play_underground_prologue. It does not show
this package or allow its Accept/Exit decision. Implement the actual FNG
before the movie; do not substitute technical text on the loading screen.

2. Race Over: original HUD_SingleRace contains RaceOverMessage and
RaceOverBacking. Original4A7F24 loads localization1F98F615 (Race Over)
through4F66F0.4A7E1F..4A7E35 starts the named presentation state for message
and backing. The runtime branch also handles other modes/messages, so the
text alone is not a recovered full finish transition. Current Switch loop
breaks immediately on finished then opens loss results or victory movie.
No Race Over presentation is submitted. Implement its native appearance
and transition timing before results/movie; timing is still unmeasured.

3. Minimap: current src/race_minimap_hud.cpp supplies127/127/127 and inherits
player kind1 for rivals. Original58EA88 supplies palette7313DD and kind5.
Fresh actual execution of original5912B0 initializes that RGB to0/66/255;
player7313D4 is172/53/0. Native palette receipt preserved. These are consumer
input bytes, not final framebuffer bytes: preserve existing modulation.

## Traffic and scoring

4. Negative style event: GLOBALB34A07 record15 resolves90DFD9D2 HIT TRAFFIC.
Original tuning has rate-50, minimum-15, multiplier1, non-continuous mode.
These are raw rule fields, not a claim that every overlap costs50. Current
RaceStyle loads the rule but RaceSession::update_style never triggers15:
player pair impulses can only spoil clean-section/lap status. Recover original
contact admission/edge/factor and connect once per qualifying collision;
do not deduct every simulation slice or penalize racer/wall contacts as traffic.

5. Traffic dynamics: src/race_contacts.cpp computes an oriented-box SAT
witness and uses the recovered car impulse kernel, with positional separation.
It explicitly lacks original hard-impact state callback; SAT is not original
GJK/contact-cache equivalence. RaceSession sets impacted on any overlap.
RaceTraffic then permanently changes that actor to VehicleModel physics with
brake=1 until retirement. This is a port adapter, not recovered collision
wake/recovery behavior. Original lifecycle and pair evidence already exist in
COLLISION_PAIR_TRACE and EVENT0_ACTORS_258; no need to rediscover asset formats.
Next comparison must cover frontal/side/glancing encounters, separation and
subsequent traffic motion; this video cannot validate a synchronized impulse.

6. Routes: RaceTraffic reads TRACKB1003 authored route points, enabled lanes,
lane offsets, per-category speeds and34122 junction records.583BB0's traffic
policy uses event+0C, distinct from racer+0B; existing native evidence covers
that distinction. Paths are not simply the race lap spline. Nevertheless the
port chooses spawn route/point itself, uses its own obstacle corridor and
retirement scheduling, and retains junction decisions per activation. Entire
lane progression/junction/height behavior is not certified by primitive tests.
Need a PC road overlay and actor trace across both lane directions/junctions
before claiming routes exactly match original. No confirmed wrong route-ID
join from this audit; do not silently change lane sign or routing policy.

7. Traffic audio: race_audio_snapshot reads player engine/tyres plus contacts
involving player; remote impacts explicitly await spatial admission. There is
no traffic engine/horn/pass-by voice manager. Player-vs-traffic collision
sound admission already exists, which is different from a traffic engine.
Reuse recovered audio bank/graph machinery with original traffic selection,
attenuation/panning and bounded voice lifetime; do not allocate full player
sound stacks for every distant actor without measuring cost.

8. Traffic lights: actor mesh lamp_mode is updated, but original car glow
placement uses only player glow_anchors_/car_. RivalVisual has no glow-anchor
or glow-draw state. The road headlight projection is also a single player
projection. Thus missing traffic glow/beam is confirmed; do not call existing
lamp-material updates a complete headlight implementation. Add authored
markers and original class-specific activation/gains; share immutable textures.

## Environment

9. Track render templates retain static object/material data. Current race
particles are vehicle smoke/sparks; no fountain emitter or general animated
world-material/texture sequence is driven by the race frame. Existing crowd,
vehicle wheels and lights do animate; not all environment is frozen. Animated
arrows/fountains need a separate original emitter/material/clock trace, not
an arbitrary global UV scroll or animation of every chevron mesh.

10. Signs/cones: no roadside movable-body registry is connected to RaceSession
contacts. Static track polygons remain collision geometry, so this is not a
claim that all roadside geometry has no collision. Existing original research
TRACK_PROP_LIFECYCLE identifies34026 definitions/34027 placements and names
ZCS_PylonA, CS_STREETSIGN_B, ZCS_Chevrons, with proxy activation, shape/mass,
break and retirement paths. Implement actual placed-object ownership/hulls,
activation, motion, render transforms and unload/reset together. Do not turn
all visible signs into immovable walls or erase a mesh on proximity.

## Practical sequence / evidence boundary

A. Welcome, native minimap palette/kind and Race Over presentation.
B. One traffic contact event path shared by penalty, dynamics and impact audio;
   actual road overlay and route progression; then traffic engine/lights.
C. Roadside dynamic objects, then recovered world animation/emitter classes.
Keep the owner workflow sequential, with required focused tests and PC frames
before a combined console candidate. No new runtime fix or NRO is claimed by
this audit. Existing +10MiB candidate reached gameplay according to its log;
that does not accept the fidelity gaps above. No broad test matrix run.

Evidence: owned build/ug0-fidelity-r2 contains original FNG metadata,
localization references, finite native palette, style rule and fresh reference
frames. manifest.json seals these local artifacts. Initial unmapped read of
BSS palette failed and was replaced by executing the original initializer;
no raw static palette bytes were fabricated. Two arbitrarily aligned scratch
disassemblies are not proof; source conclusions use named established ranges
and valid original object/consumer evidence stated above.

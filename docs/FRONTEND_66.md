# Frontend integration boundary (issue66)

The default `nfsu_switch_race.nro` entry now plays the locally installed NA
startup movies through `nfsu_intro_movie`, then opens the two-choice frontend.
Explicit `--live` and `--replay` retain the existing event1 diagnostic entry.
A/B skips startup playback and Plus exits. In the menu, directional buttons
or one left-stick deflection change selection; A enters and B/Plus exits.
Free driving returns directly to the menu on Plus; Y retains pause and the
established vehicle controls/steering remain in use. No upload was performed.

Free driving uses the shared track1003 world, vehicle, camera and residency
systems with a zero-lap route. Event0's original first race also names forward
track1003; see EVENT0_SETUP_HUD.md and nfsu_event0_setup_retail. This is ordinary
continuous driving on that track, not an expanded open world. It does not emit
race lap or finish audio. `nfsu_frontend_retail` authenticates the texture inputs
and drives the real track past the former race finish while checking that the
session remains running. Regression values belong to that test.

## Original presentation

The old grit/bitmap layout was rejected by the owner at 42f96af6. Its artifact
is preserved in ignored worker evidence. The revised frontend reads the original
MU_Main_PC.fng from the decoded FrontB JDLZ package at 0xBEC98C. Its 161 objects
supply names, hierarchy, affine positions, dimensions, colors, UVs, proportional
ConduitMdITC_TT21i/TT14i letterforms and the original NFSU logo/angular backing.
The main panel uses the original Init channel's settled origin rather than the
serialized editor snapshot of its offscreen entry position. Selection uses the
original Underground/QuickRace highlight groups. QUICK RACE becomes FREE DRIVING;
Statistics, Customize Ride, Options and Play Online remain visible and locked.
The original Chyron EA logo, banner dimensions and proportional text are adapted
to the bottom safe area. Controller hints describe the actual Switch controls.

The prior static stock RSX background was rejected by the owner. The current
work in progress resolves the native FE_SKYLINE preset, its installed parts,
base paint and EDDIE vinyl. The reference CPU renderer still caches a scene;
it is not the accepted moving menu implementation. The GPU scene and original
material/effect consumers remain under recovery.

FEng image 0x4F2000 and font 0x4F48B0 halve channels before packed
vertex color submission. The port's decoded world material uses Modulate2x
and preserves half-pack then material multiplication for RGB and alpha.
Original type9 renderer 0x4F2280 supplies distinct TL/TR/BR/BL corner colors,
packed /2 and drawn as a two-triangle fan. The port interpolates those native
colors. `tools/frontend66_color_native.py <retail-root>` executes the original
font packing instructions with explicit supplied color/stack preconditions.
The active native FEng effect/sRGB context has not been executed; the selected
world material remains a port composition boundary.

Original source assets remain read-only, memory-only and unbundled. Private
owner photos and local render previews are not published or tracked.

## Authenticated original music

The prior document's shuffled-order assertion was unsupported and is removed.
`tools/frontend66_music_native.py <retail-root>` executes authenticated speed.exe
slices with Unicorn: fresh profile initializer 0x4AA881..0x4AA959, playlist mask
builder 0x520E90 and FE selector 0x53C4DD..0x53C671, stopping before external
voice IO. The fake caller objects and incoming registers are explicit bounded
preconditions; this is not a claim to execute the entire original process.
The executable SHA-256 is checked by that script.

Metadata table 0x6F4D08 has 26 rows; currentSongIndex consumer 0x51C7B0 binds the
matching title/artist/album. Fresh profile marks indices 0..18 IG and 19..25 FE.
The mask builder adds one for ABK raw song IDs. The first FE selection is bank20,
metadata19: Get Low / Lil Jon & the Eastside Boyz / Kings of Crunk. The owner's
original menu photo independently identifies the same song. Native 0x53CBD0
clears the played bit; the mask refills when exhausted. Persisted playlist order
and context cursor, not arbitrary AST order, choose subsequent eligible songs.

The port streams original FE bank IDs20..26 and IG IDs1..19 in their native
fresh-profile order. Entry0 is excluded. Returning from driving selects the next
FE song; the seven-song frontend playlist loops. Track title changes follow the
audio device's played-frame cursor. The installed ABK is authenticated by size
and SHA-256 before use; AST boundaries come from its entries, not guessed time
segments. Decoding uses the existing bounded stereo ring and native EA codec.
Intro output is stopped before menu output opens; menu output fades and stops
before driving output opens, then a fresh ring/device starts on return. Error
paths stop both device and decoder before displaying a menu notice.

ABK identity: Sound/STRMBANK/STRM_Music_UG0_MB.abk, 968 bytes, SHA-256
0D7239D566A1B7EAF8A86C20494CF7B085E5285AA616A1508BF6EEAC32060B41.
Frontend decoded image identity: SHA-256
94A714D1E8E3BEF7F77AEDCEE4FDC6350E285B06059B92D392687710E64E5D77.
Global font image identity: SHA-256
1502D9305F37E51503D4C4E57CAE32890059497AC450FEEE6D72B50323568953.

## Remaining owner acceptance boundary

The Underground choice reports that the introductory race is unavailable.
It never launches event1 under the event0 name. In the assigned base,
Event0SetupDescriptor::runtime_ready() is false and RaceRun creates only the
legacy player session. A complete original event0 launcher requires actual
actor factory/controllers, preset composition and mesh actuation, adaptive
rival tuning/RNG and final ground poses, traffic, opponent contacts, full HUD
and minimap, race rules/results/retry. Its descriptor is not a playable race.
Pending PR316/317/318/319 are not assumed merged or complete. These missing
backend consumers cannot be supplied by substituting the existing event1 run.

Original music and the original-layout renderer are implemented. Complete owner
acceptance still requires event0 runtime and original presentation fidelity,
including camera/profile/lighting above. The corrected original movie display
aspect is supplied by the independently authored intro patch; hardware listening,
proportions and transition acceptance must be checked on the exact new artifact.
No source, synthetic test or cross-build is a substitute for that evidence.

## Presentation recovery in progress

Authenticated speed.exe SHA-256:
C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7.
The decoded FrontB FE_SKYLINE row at offset 11949952 is consumed by menu
constructor 0x4B2C30 through preset application 0x4B26F0. Its native selected
parts include Skyline MANTIS widebody, SUPERNOVA wing and BBS RX2 20 wheels.
The body skin uses METAL_L3_TEST04 paint attributes and EDDIE vinyl, rather
than a tuning choice inferred from the owner's photograph. `frontend_preset`
resolves these records from the authenticated images. The native install
consumer 0x57EA80 disables the ordinary bumpers, body, skirts and decal
meshes when the widebody is present, and disables the engine under the hood.
`tools/frontend66_preset_native.py <retail-root>` and the actual-source tests
cover assigned part indices, enable bytes and sponsor substitutions; the witness supplies stock initialization as an
explicit precondition and does not establish full game startup.

Road constructor 0x4ADA60 initializes a repeating environment. Update
0x4AE1F0 changes world translation using the native 4000-tick clock, a speed
state, acceleration and fixed-angle sinusoidal modulation. It is not merely
texture scrolling or a camera orbit. `FrontendRoad` reproduces this consumer;
`nfsu_frontend_tests` checks its native velocity samples, strict wrap boundary,
segment spacing, parity and view-family selection. The fixed-angle
polynomial comes from genuine function 0x564B10 and its executable constants.

Placement 0x4AE530 computes trunc(2*offset/wrapLength), alternating parity,
and translated copies. Draw consumer 0x4AE650 assigns identity transforms
with the negative longitudinal segment offset, lateral global 0x735E90 and
vertical global 0x6FB044, then submits ten model slots per segment. Initialization
names these families _A, _Z and _Y. Views 1..3 use _A; 4..5 use _Z; 10..15 use
_Y. These are view identifiers, not camera-controller type identifiers. The
primary context 0x7404C0 is view1: CRT constructor 0x6922A0 builds view records
at 0x740460 with stride 0x60 using 0x5450F0, which computes the index and stores
it at +4. The original road update explicitly passes this primary context.
Its _A family and five translated copies are the verified main-view binding;
_Z/_Y belong to their separate view passes.

Camera constructor 0x486C80 and matrix builder 0x487680 establish the initial
pose. Idle orbit in 0x4B0340 is gated by Main +0x124 == 4 at 0x4B0ABD.
The actual FE_SKYLINE install entry 0x4B2C30 zeroes EDI at 0x4B2EF7 before
calling 0x4B2480; 0x4B14E8 stores that zero to Main +0x124. Constructor
state 4 alone therefore does not establish a main-menu orbit. The current
main-menu camera preserves the original initial pose. State-4 car-selection
callers are separate consumers; no arbitrary orbit is forced on this entry.

The genuine Main constructor enables rolling at 0x4AF71D (Main +0x10C = 1)
and initializes the phase to zero at 0x4AF76B. Wheel builder 0x4B1760 updates
Main +0x108 at 0x4B18F7..0x4B192A from road travel 0x746DD8 plus car-switch
travel Main +0x104, divided by wheel-0 rolling radius and the executable's
circumference constant. It truncates before adding to the 16-bit accumulator.
The original 0x40BA40 spin matrix is consumed by all four wheel node matrices;
brake matrices do not receive that spin. The menu reuses the existing prepared
wheel placement and applies the recovered phase around its recorded pivot.
`tools/frontend66_motion_native.py <retail-root>` executes the original phase
slice and spin matrices with explicit travel/radius/zero-switch boundary
inputs; `nfsu_frontend_tests` pins those native phase samples and checks resident
wheel matrix updates. This is not a whole-game render capture.

Live Switch presentation now uploads the resident scene and one padded atlas
of original frontend textures/fonts, then submits FEng and font quads every
frame. The CPU pixel background copy is absent from this path. Selection
changes log pad-poll-to-selection, scene/UI CPU time, acquire/fence/record and
present/total time. Hardware timing remains unmeasured. Original FEng temporal
animation and scene-specific reflection, lighting, shadow and glow consumers
remain incomplete; general race shading is not original menu effect parity.

PR319 dependency 9eb9a2dfcd43354253ee7288e36324186cb5d90f is integrated locally
with its source history preserved. GLOBALB supplies the otherwise missing
HEADLIGHTGLOW texture. Tunnel hash 0xC2456B43 has the original absent-texture policy: solid
initializer 0x53F9B0 calls lookup 0x5461C0 with missing fallback enabled.
Texture initializer 0x545998 selects DEFAULTALPHA for global 0x73551C;
static initializer 0x689AD0 hashes that same name for the draw skip comparison.
The actual tunnel caller 0x4AE650 submits through 0x40C910, which skips that
texture at 0x40CA0C. The A/Z/Y scene-model replacement tables are zero in
0x4ADA60. The reflection target 0x71AA28 is separately bound as ReflectedTex
at 0x40E7B0; it does not replace this solid diffuse entry. Frontend commands
therefore omit only the authenticated absent hash on detailed04/07_A while
keeping authored geometry resident; other unresolved drawable bindings fail.
`tools/frontend66_material_native.py <retail-root>` executes the original
missing lookup and draw branch with an explicit no-match bucket. This is a
bounded policy witness, not original whole-game startup or a GPU capture.
Sponsor alias consumer 0x56C3D0 selects NAME plus panel SHAPE suffixes
(_SQUARE/_RECT/_WIDE), not a guessed sponsor TEXTURE attribute. Native witness
results identify SPARCO_WHITE_WIDE on the rear window and KENWOOD/NX/SPARCO/HKS
_WHITE_RECT on the doors. Genuine CarRenderInfo constructor 0x56A130 binds
these replacement arrays at 0x56AA9A..0x56AAC9: installed slots19..29 use
panel=slot-19, with four subtracted for wide slots26..29. The latter therefore
use panels3..6, rather than an invented widebody mapping. The frontend now
substitutes selected sponsor hashes and original texture pixels on those
prepared draws. DEFAULTALPHA entries retain the native empty-decal behavior.
`nfsu_frontend_retail` checks that both actual widebody door meshes resolve
all four native sponsor textures after skin preparation. Generic stock scene
and GLM helper semantics are unchanged.

The resident FrontendGpuScene scaffold records road/car resources once and
updates the five native road placements through command transforms. It is
not connected to live presentation yet and refuses unexpected unresolved drawable
textures as a diagnostic. That refusal is not final user experience. Camera
orbit, wheel motion, required effects and measured input timings still need
their original consumers and live integration.

Original FE_COMMON logical up/down/select/back dispatch is implemented with
native entries 0/1/4/5. `tools/frontend66_sound_native.py <retail-root>` executes the original sound
settings constructor 0x5364E0, unattenuated gain initializer 0x52E3F0 and
arithmetic slice of cue method 0x523120. Descriptor 0x6F43B0 type4 bypasses
distance attenuation. The cue master is settings second-array record5 field+4;
constructor 0x5361D0 initializes that field. The port now applies the native
Q15 gain and unity pitch. Event registry PlayCommonSample (0x262943B9) voice
allocation/overlap policy remains under investigation. Select/back
transition lifetime is now handled by begin_stop/stop_ready: the caller keeps
servicing input, applet and menu presentation, the music alone fades, and the
device is retired only after its played-frame cursor acknowledges the last
cue/fade block. New cues are suppressed after the transition is committed.
`nfsu_frontend_music_transition_retail` runs the actual music/SFX source and AST
decoder with a controlled device callback and independent played-frame cursor.
It compares complete original select/back PCM after the music fade, then checks
that submission alone cannot retire the device. This is a port lifecycle
contract, not a recovered original scene-transition timing claim.
The full combined default/Release/retail/Switch gates and independent review
remain pending. No current work-in-progress preview is a hardware candidate.

Original GLOBAL_SKIN1 is an authored runtime alias, not an empty material.
Genuine 0x580B00 hashes its source at 0x580BA6; 0x580C37..0x580C73 gives
body and global slots the same installed RideInfo target. The global
substitution entry is updated at 0x56C375..0x56C389. Frontend preparation
reuses the existing composed RuntimeSkin through bind_runtime_skin and
re-prepares the same installed assembly. The material witness executes these
original source/target slices with the profile target as an explicit boundary.
The frontend retail regression checks the GLOBAL draw uses the generated
surface and that resident GPU road commands move without rebuilding definitions.

### FE root contact-plane placement and current-state diagnostic

The genuine FE car update at `0x4B1760` reads RideInfo wheel zero:
`0x4B17F5..0x4B1825` computes radius (`+0x80`) minus vertical
(`+0x78`) plus the executable float at `0x6F1AD4`, then writes the
normal scene position at view-scene `+0x748`. Its reflected branch negates
that result. This root placement follows each car-local part transform;
rolling wheels retain their original local pivot before the root is added.
The independently executed normal/reflected slices are reproduced by
`tools/frontend66_motion_native.py`. `nfsu_frontend_tests` checks the
native normal root on every car draw and checks transformed vertices for
all four rolling wheels with that root, while brakes remain unspun.

`FrontendGpuScene::render_reference` is a diagnostic CPU rendering of the
currently submitted road and car placements. It copies resident source
scenes, applies the current command matrices, and excludes only the already
proved native-empty C245 material groups. The live resident geometry is
unchanged. This diagnostic still uses the existing analytical material
implementation and does not certify FE lighting, reflection capture or GPU
appearance. The original static `load_frontend_scene` API remains separate.

### FE shadow and material retention

The resident menu adds `SKYLINE_SHADOWFE`, selected by original constructor
`0x56ABEF..0x56AC7E` when game mode `0x77A920` is 3. Its footprint uses
the original slots 0..11 LOD-A model-header reducer, including disabled
models and zero bounds for absent models. Original `0x567890` reduces
bounds and `0x56AB99..0x56ABEC` expands each bound by 0.1. The FE grid
consumer `0x56EB33..0x56EFD9` uses distinct FE axis scales, sixteen samples
and authored UV ordering, with fixed ground height and selected lifted
samples. It overrides the root height for shadow ground samples; the car
retains its independently recovered contact-plane root placement.

`tools/frontend66_shadow_native.py <retail-root>` executes the genuine
preset lookup, bounds reducer, inflation, grid and alpha slices. It supplies
the solid loader boundary from authenticated SKYLINE headers and the
previously recovered A/B fallback contract; it does not execute an asset
queue or a live whole-game startup. The SKYLINE geometry provenance is
SHA-256 `64DA8A1F29AC7C94832675C1FD164B2C0236104AD6B9E97010AFD873C12F1F12`.
The native FE alpha stays 92 across tested fade inputs; the original
`world.efx` Modulate2x doubles alpha. `nfsu_frontend_tests` pins all sixteen
native vertices/UVs, original texture identity, alpha and depth-write state.
The old race shadow placement and its terrain approximation are not used.

A texture-only `GLOBAL_SKIN1` reprepare previously discarded the material
vectors that `prepare_stock_frame` applied after its scene preparation.
The rebind now checks stable material-group identity and preserves those
already-decoded vectors. The frontend test verifies every vector and gate
survives and requires nonzero diffuse treatment. This restores the existing
material approximation; original harmonic lighting and live cube capture
remain separate unfinished FE work.

`FrontendArt::draw_overlay` draws the existing authored UI on an existing
CPU scene image, for current-scene diagnostics. The live Switch menu still
records those objects into its resident GPU atlas/overlay path. This CPU
diagnostic does not certify hardware opacity, frame time or temporal effects.


## Main-entry camera and authored UI viewport

The constructor camera is not the main-menu camera. Main entry calls
0x487BA0 with mode zero, publishing camera state one and saved mode zero.
The next genuine 0x4870C0 update selects the saved table at 0x7788E0 before
0x487680. The latter builds a VIEW matrix through inverse and look-at
consumers 0x40BC10 and 0x40B780. Its translation is not a world-space eye.
The frontend scene uses the inverted view and raw-to-world coordinate mapping,
including the authored roll. Shared camera roll and focal overrides are opt-in;
existing race and viewer inputs retain their prior zero defaults.

The original update publishes a fixed-angle FOV word, consumed by primary-view
projection 0x40DD00. For view one, 0x40DDD2 through 0x40DEF4 applies the
viewport ratio and globals 0x6B7C6C/0x6B7C70/0x6B7C74, truncates the two
half-angles, and evaluates their original fixed-angle cotangents independently.
The menu does not substitute a direct vertical FOV or an aspect-derived
horizontal focal scale for that consumer. tools/frontend66_camera_native.py
executes authenticated constructor-tail, entry, view and projection slices.
Explicit preconditions are zero camera storage, original scalar CPU dispatch,
a supplied viewport and clipping inputs. Only the recovered focal scales are
claimed from the projection slice; it stops before scene submission.

The authored DisplayMenuBar, quit_tag and back_tag are image objects outside
the original UI canvas. Their OP flags are copied unchanged by 0x5017D0;
this is not evidence for a hidden flag. Genuine image geometry and submission
place each quad entirely outside the horizontal clip range. Uniform
letterboxing in the port exposed those quads inside the wider screen. The
frontend CPU and GPU paths now map the original canvas independently on X
and Y, preserving those objects and allowing normal viewport clipping.
This mapping is separate from intro-media aspect handling.

The actual-source frontend retail test compares the native entry eye and full
basis, including roll, the original projection scales, all offscreen control
corners, and onscreen logo/font corners. tools/frontend66_ui_native.py repeats
the native UI witnesses with authentic FNG and font records. Its documented
boundaries supply allocation, texture metadata, COM capture and the authored
identity-rotation/unit-scale LogoCover parent translation. No visibility mask
or arbitrary object-name suppression replaces the native clipping result.

These camera/UI corrections do not complete FE harmonic lighting, live cube
capture or the remaining original temporal UI effects. Current source frames
remain diagnostics until combined validation, independent review and owner
hardware acceptance are complete.

## Bounded FE harmonic shader integration

The menu now has a separate frontend_car shader and ten-vector harmonic
uniform. Existing car_shaded race/viewer shaders keep their analytic default.
The CPU diagnostic evaluates the same original car.efx polynomial, specular
luminance exponent and pixel product. The original shader uses local raw
normals without an eye-facing flip or upper vdotn clamp, ignores vertex RGB,
and multiplies independently interpolated diffuse alpha and specular RGB.
It does not substitute the analytic broad light or artificial sky band.

Original 0x56DEF0 reads primary CView +0x40 for 0x544390. Its 0x542C20
consumer subtracts placement translation, applies the transposed placement
3x3 and clamps LocalEyePos length with 0x6B6348/0x6B634C. The CPU and GPU
receive that per-draw vector, including whole-car root and wheel placement.
The actual-source frontend test names the independent native vector witness
and representative shader contractions, including back-facing and nonunit
normals. The Switch compiled-interface gate checks the dedicated shader.

The current harmonic startup record is a bounded diagnostic state. It comes
from genuine 0x542E40 and 0x544390 with the recovered main-entry primary
CView, original 0x487590 reflection getter and 0x5707B0/0x40AD60 basis.
Unresolved zero-BSS flags 0x735548/0x73554C, reflection origin 0x7365A0,
and empty light-list sentinels are supplied boundary assumptions. They are
not proven retail profile defaults. tools/frontend66_lighting_native.py
repeats this witness and labels those limits explicitly. Actual 0x4AE1F0
road light-pool population and authored flare markers have been recovered
separately; this startup witness does not certify that populated state.

Live EnvMap cube capture, populated road-light harmonic generation, reflected
view consumers and remaining temporal UI still need connection. The FE shader
currently leaves the missing cube term absent, making that gap visible in the
source diagnostic instead of filling it with an invented environment. This
is an implementation checkpoint, not original-effect parity or acceptance.

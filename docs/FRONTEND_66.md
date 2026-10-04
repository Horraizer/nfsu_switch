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

The background is rendered from the nine original TRFE_ROAD02 tunnel solids
in FrontB, Road_Segment textures and the RSX geometry/textures. Default stock RSX
paint is resolved through CarTypeInfo +0xC5C and the actual BASE_PAINT attributes,
not an arbitrary requested color. Room modulation follows original world.efx.
FEng image 0x4F2000 and font 0x4F48B0 halve channels before packed
vertex color submission. The port's decoded world material uses Modulate2x
and preserves half-pack then material multiplication for RGB and
alpha, avoiding clipped white logo/highlight colors. Original type9 renderer
0x4F2280 supplies distinct TL/TR/BR/BL corner colors, packed /2 and drawn as
a two-triangle fan. The port interpolates those native colors instead of
flattening the main backing to one color or multiplying them by base SA color.
`tools/frontend66_color_native.py <retail-root>` executes the original font
packing instructions with explicit supplied color/stack preconditions.
The car uses existing decoded retail material coefficients and runtime skin.
The active native FEng effect/sRGB context has not been executed; the selected
world material is a port composition boundary. The current camera framing is
a port choice; the original animated camera,
saved-profile tuned car shown in the owner's photo and full original lighting
have not been recovered. The background is cached for menu responsiveness.
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

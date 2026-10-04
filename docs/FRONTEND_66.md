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

## Original presentation evidence and remaining acceptance

FrontB's decoded identity is authenticated before original PC_GRIT_BACK_01
and MENUSELECT pixels are decoded. GLOBALB's identity is authenticated before
its NFSU_LOGO is decoded. The retained images are memory-only and never bundled
or exported. The menu layout and built-in text are port UI. They do not claim
to reproduce the original FNG hierarchy, animations, font or RSX background.
The original compressed MU_Main_PC.fng references NFSU_logo and ConduitMdITC
fonts; merely rendering original texture pixels does not implement that screen.

The Underground choice reports that the introductory race is unavailable.
It never launches event1 under the event0 name. In the assigned base,
Event0SetupDescriptor::runtime_ready() is false and RaceRun creates only the
legacy player session. A complete original event0 launcher requires the actual
actor factory/controllers, preset composition and mesh actuation, adaptive
rival tuning/RNG and final ground poses, traffic, opponent contacts, full HUD
and minimap, race rules/results/retry. Its descriptor is not a playable race.
Pending PR316/317/318/319 are not assumed merged or complete. These missing
backend consumers cannot be supplied by substituting the existing event1 run.

The requested original menu music remains unauthenticated and silent here.
STRM_Music_UG0_MB segment0's duration does not prove its menu role. Native
EAXS_MusicStream's vtable at speed.exe 0x006BE080 has selector slot+0x98 at
0x0053BEE0; it distinguishes explicit indices from negative playlist commands.
The frontend transitions at 0x004D4A20 and 0x004CF110 send -2. The producer at
0x0053C3A0 uses profile playlist masks and shuffled ordering for ordinary music,
and conditionally substitutes index1 for zero in its explicit state. A native
startup/profile trace or an owner original recording that identifies the actual
asset is still needed before claiming the requested music. No approximate song,
inferred title or arbitrary segment was installed.

The complete owner milestone remains blocked by these original presentation,
music and event0 runtime requirements. The source/artifact here supplies a real
intro/frontend/free-driving integration for continued implementation; it is not
FINISHED menu or end-to-end Underground acceptance. Independent exact-head
review, merged-result gates and owner hardware evidence remain required.

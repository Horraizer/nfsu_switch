# Night lights and glows (issue #185)

## Current race-visuals comparison

The direct-owner Light Glow change replaces the procedural car overlay when
race visuals are enabled. It reuses the recovered frontend 0x5443F0 primitive:
authored inner/outer headlamp and tail textures from GLOBALB.BUN, descriptor
sizes/colors, powered marker-normal facing and original byte truncation.
Seven reserved slots cover two layers per headlamp and three brake markers;
missing or culled markers remain invisible. Ordinary-owner head/tail gains
and the service-brake response follow 0x56D8D0. Handbrake alone is not the
service-brake input. The shared frontend primitive's arithmetic is unchanged.

The race sprite shader uses the world texture/color modulation branch.
Original whole-pipeline pixel equivalence, occlusion-query behavior, damage
and special-owner activation, and reflected copies are not established by
this change. Existing world lights keep their existing rendering. This is
not a full-scene bloom or motion-blur implementation.

The original consumer witness is tools/frontend66_car_glow_native.py;
nfsu_freeride_skyline_tests checks its corners/color and full race resource
loading/frame generation/return to menu on both available events. Original
texture payload belongs to persistent car residency; borrowing command
descriptors remain inside the command-cache ceiling. Actual Switch visual
acceptance remains required. The legacy diagnostic path below is retained.

## Retail evidence

The retail `speed.exe` collector at `0x0056BDB0..0x0056C0DC` records
headlight and brakelight marker names. The consumer at
`0x0056DAEA..0x0056DB47` selects per-car emitters while walking the light
list at object offset `+0x500`; `0x0056DC07` calls the shared light draw at
`0x005447C0` after the car's opaque mesh pass. Its early check at
`0x005447FF..0x00544804` reads an emitter enable field at `+0x0C`.
`0x00544B66..0x00544BE5` contains distance fade gates with the constants
3900/4000 for one mode and 32000/40000 for other modes. The exact
screen-facing billboard size, fade curve, image, occlusion and road beam
contract have not been recovered. This is evidence for a dynamic light
consumer, not evidence that this implementation duplicates its pixels.

The event-1 `STREAML1RA.BUN` record M3 at file offset 44283904 contains
`XO_LENSEFLAREA_1B_00_DEINST`, `XO_CHEVRONA/B` and `XO_TUNNELENDB_1B_00`
meshes. No `XO_LAMPBULBA_*` mesh was found in that record. The scene
places the flare mesh at instance 5; its eight-triangle mesh binds
`HEADLIGHTGLOW` (`0x3394FE62`) from `Global/GLOBALB.BUN`. The existing
track material
path resolves that texture and draws its native additive material. The
existing supplemental texture path also binds the additive
`SGN_LI_B03_NEO` material; see [TRACK_MATERIALS.md](TRACK_MATERIALS.md).
These placed lights are track geometry and remain in both modes. The car's
own pack supplies lamp geometry textures, but no matching retail car-sprite
image was established from that pack. No new street-light mesh or
illumination field is synthesized.

## Reproduction

`--glows` enables four bounded car sprites on desktop and Switch. Retail's
consumer also recognizes `CENTRE_BRAKELIGHT` (`0xA2A2FC7C`), but this
approximation intentionally leaves that distinct center brake marker
out to retain its four-sprite bound and focus on the headlamps and corner
brakelights. The sprites' centres come from the stock 240SX's retail
left/right headlight and brakelight markers, transformed with the placed
part. The
existing animation state sets the tail brightness when braking; the
headlights are always on, as in [CAR_ANIMATION.md](CAR_ANIMATION.md).
Missing markers produce no sprite. A rear-facing sprite fades out for a
front-facing view and vice versa. The camera distance cap, angular fade,
sprite radii, alpha, warm colours and procedural 64x64 soft texture are
explicit approximations. The two textures are generated at load time; no
derived game image is checked in. At most four quads, eight triangles and
four draws are added to a frame. The GPU uses additive blending, depth
testing and no depth writes. CPU composition keeps the combined track/car
depth and draws glows after the opaque car pass, matching the Switch draw
order. The Switch reserves all four glow slots every frame; faded sprites
use zero-radius draws, so camera changes do not alter the package's draw
count. Static track lights retain their existing draw path.

This branch adds only the marker-driven car overlay: up to four additive
quads at the stock 240SX's head and brake lamp markers. In the event-1
chase replay, only the two tail sprites are ever live; the head sprites
never appear. The tail sprites brighten while braking. No street-lamp,
neon or flare drawing was added;
those remain existing track materials.

The effect is off by default until a paired handheld console measurement
establishes its cost inside the 60 fps budget. Use the same build with and
without `--glows` on the same uninterrupted event-1 replay and capture the
paired GPU SCENE pages. A rear chase view during braking is the useful photo;
the replay cannot turn the car head-on. The Switch diagnostic line reports
four reserved glow draws. The console result is recorded on the pull request;
desktop timing does not establish handheld GPU cost.

`tools/race_replay_check.py --glows` pins the desktop track-1003 replay
frames with the effect enabled, while its default mode pins the unchanged
baseline. `nfsu_race_run_tests` with `--sample-draws --glows` independently
builds the cached and reference glow slots, then compares every draw,
placement, upload resource and upload total. The glow-on command-cache size
is pinned by that test. Both modes leave race state, input and stream hashes
unchanged.

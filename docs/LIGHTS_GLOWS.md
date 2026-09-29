# Night lights and glows (issue #185)

## Retail evidence

The retail `speed.exe` collector at `0x0056BDB0..0x0056C0DC` records the
left and right headlight and brakelight marker names. The consumer at
`0x0056DAEA..0x0056DB47` selects the corresponding per-side markers and
`0x0056DC07` calls the shared light draw at `0x005447C0`. Its early check at
`0x005447FF..0x00544804` reads an emitter enable field at `+0x0C`.
`0x00544B66..0x00544BE5` contains distance fade gates with the constants
3900/4000 for one mode and 32000/40000 for other modes. The exact
screen-facing billboard size, fade curve, image, occlusion and road beam
contract have not been recovered. This is evidence for a dynamic light
consumer, not evidence that this implementation duplicates its pixels.

The event-1 `STREAML1RA.BUN` record M3 at file offset 44283904 contains
`XO_LENSEFLAREA_1B_00_DEINST` and `XO_LAMPBULBA_*` meshes. The scene
places the former at instance 5; its eight-triangle mesh binds
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

`--glows` enables four bounded car sprites on desktop and Switch. Their
centres come from the stock 240SX's retail left/right headlight and
brakelight markers, transformed with the corresponding placed part. The
existing animation state sets the tail brightness when braking; the
headlights are always on, as in [CAR_ANIMATION.md](CAR_ANIMATION.md).
Missing markers produce no sprite. A rear-facing sprite fades out for a
front-facing view and vice versa. The camera distance cap, angular fade,
sprite radii, alpha, warm colours and procedural 16x16 soft texture are
explicit approximations. The two textures are generated at load time; no
derived game image is checked in. At most four quads, eight triangles and
four draws are added to a frame. The GPU uses additive blending, depth
testing and no depth writes. Static track lights retain their existing
draw path.

The effect is off by default until a paired handheld console measurement
establishes its cost inside the 60 fps budget. Use the same build with and
without `--glows`, after sixty seconds of event-1 replay, and capture the
GPU SCENE page and REPLAY page plus a night-frame photograph. The Switch
diagnostic line reports the selected glow draw count. The console result is
recorded on the pull request; desktop timing does not establish handheld
GPU cost.

`tools/race_replay_check.py --glows` pins the desktop track-1003 replay
frames with the effect enabled, while its default mode pins the unchanged
baseline. `nfsu_race_run_tests` with `--sample-draws --glows` compares the
cached and reference GPU command streams, resource tables and per-frame
placements. Both modes leave race state, input and stream hashes unchanged.

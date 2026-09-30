# In-race HUD, group A

Issue #64, first implementation group. The default direct Switch race and
desktop reference draw speed, tachometer, needle, red zone and gear. The
diagnostic panel remains behind `--panel`; `--no-hud` disables this final
pass for a paired console measurement. Nothing in the HUD writes to race
state. The replay state and step pins are unchanged.

## Retail trace and assets

Observed in `speed.exe`, using Capstone 5.0.7:

- The HUD procedure starts at `0x004A0F50`. At `0x004A0FBC` it reads a
  floating speed from car offset `+0x3D4`, then takes its absolute value at
  `0x004A0FCD`. At `0x004A0FD9` the units byte at `0x0078A3F5` selects a
  multiplier. Metric multiplies by `3.6` at `0x004A0FE2`; imperial multiplies
  by `2.236999988555908` at `0x004A0FEF`. Text binding is at
  `0x004A1011..0x004A1030`. The integer helper at `0x00674898` uses `fistp`
  with a correction. The game screenshots show MPH.
- `0x004A1049` calls the piecewise needle mapping at `0x004A0EA0`; sprite
  placement follows at `0x004A1057`. Another needle path runs through
  `0x004A1064..0x004A1115`. Gear at `0x004A1124..0x004A1178` reads car
  offset `+0x3D8` and selects a glyph through table `0x006FB7B0`.
- `0x004A0E25` calls the gauge overlay selector at `0x00432A30`. It hashes
  the `3RDPERSON_` prefix and chooses the numbered `LINES` face from max
  RPM: below 7500 `7500LINES`, 7500..8499 `8500LINES`, 8500..9499
  `9500LINES`, and from 9500 `10500LINES`. It separately chooses `RED` from
  redline RPM. The stock 240SX (max 8000, redline 7500) gets `8500LINES`
  at `0x00432BA6` and `7000RED` at `0x00432BD2`. The LINES and RED hashes
  reach the HUD objects at `0x004A0E42..0x004A0E78`.
- The HUD constructor loads `3rdPersonTach` at `0x004A0429`, resolves it
  at `0x004A0431..0x004A0438`, and stores it at `[ebx+0x68]` at
  `0x004A0442`. It resolves the separate `rpm_fill` object into
  `[ebx+0x6C]` at `0x004A0445..0x004A0456`, and `Redline` into
  `[ebx+0x94]` at `0x004A04FF..0x004A0511`. The selector writes the
  LINES hash to the Tach object's `+0x24` at `0x004A0E42..0x004A0E56`,
  and the RED hash to Redline at `0x004A0E62..0x004A0E78`.
- Their rectangles come from `HUD_SingleRace.fng`, the JDLZ FEng package
  in `Global/INGAMEB.BUN` chunk `0x00030210` at file offset `0x16F400`.
  In the decompressed package, `3rdPersonTach` begins at `0x445C` and its
  SAT at `0x44AC` puts its centre at `(141.3611,-34.56946)` and size at
  `256x128` (`SAT+0x20` and `SAT+0x3C`). Its stored BGRA colour is
  `(192,128,0,255)`, or blue RGB `(0,128,192)` at full alpha. The
  separate `rpm_fill` begins at `0x4008`; its SAT at `0x4054` has the
  same size and nearly the same centre, with white RGB and alpha 150.
  The port draws the LINES texture with the Tach object's colour; it does
  not substitute the `rpm_fill` colour. `Redline` begins at `0x40D4`;
  its SAT at `0x411C` gives centre `(124.3611,-76.56946)` and size
  `64x32`. `3rdPersonNeedle` at `0x452C` has SAT at `0x457C`, with centre
  `(82.36,-27.57)` and size `16x128`. The images use their full UV rectangles: LINES
  `(0,0)..(256,128)` and RED `(0,0)..(64,32)`. Their screen rectangles
  are centred on those SAT positions with the listed native sizes, under
  the HUD group's screen transform. The centre difference and sizes give
  RED a top-left offset of `(79,6)` LINES texels. The decoded LINES hub
  at `(69,70)` maps to `(82.36,-28.57)` in the same local coordinates,
  one native pixel above the needle centre. Relative to that needle centre,
  the native LINES screen rectangle is `(-69,-71)..(187,57)` and RED is
  `(10,-65)..(74,-33)`, before the common HUD screen transform. This
  establishes both sprite registrations independently of the screenshot fit.
- `0x004A0EA0` clamps RPM divided by max RPM to 0..1. Its sprite rotation
  is 44..215 degrees for max below 9000, 50..210 for 9000..9999, and
  45..225 from 10000. The stock needle therefore rotates 44..215 degrees.
  The HUD path calls this mapping at `0x004A1049`, loads the needle object
  from `[ebx+0x30]` at `0x004A104E`, and passes its rotation to
  `0x004F6A70` at `0x004A1057`. The rotation path updates the object at
  `0x004F6B5A`. In the decoded 16x128 needle image the ring is
  at rows 59..67, around the sprite centre, and the shaft tip is near row
  125. The FEng centre places that ring on the face hub; the tip points
  outward toward the tick marks.
- `Global/INGAMEB.BUN` holds `3RDPERSON_RPMDIAL`
  (`0x40FAB387`, 16x128), `CENTER_DIAL`
  (`0x56447D19`, 16x16), and `SPEED_COUNTER_SPEEDBOX` (`0x6D079188`,
  128x32). `Global/DYNTEX.BIN` holds the selected LINES and RED overlays:
  `8500LINES` is `0xC41B7346` (256x128), `9500LINES` is `0x3897E447`,
  and `7000RED` is `0xD5D99FA0` (64x32). LINES contains the tick marks,
  numbers and hub ring visible in retail screenshots. The HUDTEX
  `3RDPERSON_8000` face is not referenced by `speed.exe`.
  `Languages/LanguageTextures.bin` holds
  `LCD_LET48` (`0xBBBA71C2`, 128x128), the digital glyph atlas. The
  `FrontEnd/FrontB.lzc` pack was inspected for the event definition; its
  font/layout data is not yet decoded for the HUD.

The selected stock face is labeled 0..8, with the red overlay between 7 and
8. Engine B upgrades add 500 or 1000 max RPM to the 240SX; a tuned 8500 or
9000 RPM car selects the 0..9 `9500LINES` face. The port uses the recovered
retail needle rotation, the FEng relative placement of LINES and RED, and
the decoded face hub for needle registration. The needle uses the same
logical scale per texture texel as LINES, with source row 64 at the hub.
The 265-degree sprite-frame offset aligns its outward shaft with the 0
and max label rays approximately; the absolute screen anchor and font
spacing are still screenshot fits.
For the 8500LINES band below a 6500 RPM redline, retail leaves the RED
output unwritten; the port deliberately omits RED. Drag mode selects a
separate `3RDPERSONDRAG_` texture family and is outside this event-1 HUD.

The port's car state has no recovered `+0x3D4` scalar. Its displayed MPH is
an **approximation** from horizontal velocity magnitude times 2.237,
truncated to an integer. Absolute placement, the other tints, backdrop opacity and safe-area fit
are **approximations** from the owner's screenshots. `SPEED_COUNTER` belongs
to the lap panel and is not drawn behind the gauge. The disc and speed box
use translucent vertex alpha over the road. The small MPH label is generated with the project
font because the original label layout is still unknown. No retail or
derived texture is committed; the selected textures are decoded from
the user's installation into a transient 512x512 atlas at startup.

The #47 first-lap asset check includes `Global/DYNTEX.BIN` and
`Languages/LanguageTextures.bin` under `sdmc:/nfsu/`.
The source installation's 476-file manifest was identical before and after
each trace and replay run.

## Rendering and validation

`build_race_hud_frame` fills fixed-capacity vertices; the maximal current
frame uses 11 quads and 66 indices. The final deko3d pass makes one indexed
draw with source-alpha blending and depth disabled. The atlas uploads once.
There is no per-frame allocation or CPU readback in direct mode. The optional
`nfsu_switch_compare.nro --hud` mode sends a synthetic 7500 rpm, first-gear
HUD through offscreen deko3d capture and compares it with CPU compositing.
It needs the HUD files in the retail root; the result awaits a console run.
Placement uses a 1280x720 logical canvas, 5% scale reduction and a right safe-area
anchor for handheld and docked sizes. The desktop CPU reference composites
the same quads and atlas with nearest texel sampling.

`tools/race_replay_check.py` pins 15 CPU output frames, including the early
straight at step 600, the first gear-slot 2-to-3 change (HUD 1 to 2) at
steps 5449/5450, and
the first lap crossing at 24287/24288. The frame hashes live in that test.
State and step hashes and the cached-versus-reference draw comparison remain
in their existing tests. A console run must still verify the GPU output.

At 3f53122, the handheld race replay GPU SCENE mean was 20.59 ms
([board comment 5627](https://git.accad.pro/murzintsevav/nfsu-switch/issues/203#issuecomment-5627)),
already above the 16.67 ms budget. The extra cost and 60 fps outcome are
**unknown until a paired console run**. Request both `--replay
--profile-frame` and `--replay --profile-frame --no-hud`, each after 60
seconds, with GPU SCENE page 7 and REPLAY page 2, plus a live-drive HUD
photo. Keep the same console mode and build for both measurements.

The remaining issue #64 groups are lap/current/best time, countdown lights,
WRONG WAY and minimap, in that order. Their timing and route inputs already
exist, but this round stops after group A as allowed by the assignment.

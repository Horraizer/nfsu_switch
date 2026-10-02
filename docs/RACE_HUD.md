# In-race HUD, group A

Issue #64, first implementation group. The default direct Switch race and
desktop reference draw speed, tachometer, needle, red zone and gear. The
diagnostic panel remains behind `--panel`; `--no-hud` disables this final
pass for a paired console measurement. Nothing in the HUD writes to race
state. The replay state and step pins are unchanged.

Issue #253 refines the gauge art, glyphs and placement and adds a completed
state audit. [HUD_253.md](HUD_253.md) distinguishes recovered geometry from
screenshot fits, links durable comparisons, traces the RPM producer and
consumer, and lists the unresolved original/console evidence.

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
  `(82.36,-27.57)` and size `16x128`. Redline's SAT stores BGRA
  `(0,0,150,150)` and Needle's stores `(242,233,207,255)`; the port uses
  RGB `(150,0,0)` at alpha 150 and RGB `(207,233,242)` at alpha 255.
  The images use their full UV rectangles: LINES
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
- Within the `GaugeCluster` object at package offset `0x3F58`, the gauge
  sprites are `rpm_fill` (`0x4008`, SAT `0x4054`, depth 110), Redline
  (`0x40D4`, SAT `0x411C`, depth 8.33), Tach (`0x445C`, SAT `0x44AC`,
  depth -1.67), and Needle (`0x452C`, SAT `0x457C`, depth 5). Those stored
  values alone do not establish runtime painter order. The earlier claim
  that smaller depth draws in front was an unsupported inference and caused
  the port to cover the needle with scale numerals. Needle follows Tach in
  the package's child records, consistent with the owner's original-HUD
  observation. The port now draws fill, Redline, Tach, then Needle, followed
  by text. Needle crosses above the scale; the fill/RED ordering is retained.
  The text records `3rdPersonSpeed` (`0x4268`, SAD `0x42F0`) and
  `3rdPersonGear` (`0x45FC`, SAD `0x4680`) store BGRA
  `(192,128,0,255)`. `3rdPersonSpeedUnits` (`0x4360`, SAD `0x43EC`)
  stores `(255,190,150,255)`.
- `Global/INGAMEB.BUN` holds `3RDPERSON_RPMDIAL`
  (`0x40FAB387`, 16x128), `CENTER_DIAL`
  (`0x56447D19`, 16x16), and `SPEED_COUNTER_SPEEDBOX` (`0x6D079188`,
  128x32). The gauge uses screenshot-matched `3RDPERSON_7000FILL`
  (`0x9107116C`, 256x128), rather than a generated disc and separate box.
  Its later runtime binding is an inference documented in HUD_253.md.
  `Global/DYNTEX.BIN` holds the selected LINES and RED overlays:
  `8500LINES` is `0xC41B7346` (256x128), `9500LINES` is `0x3897E447`,
  and `7000RED` is `0xD5D99FA0` (64x32). LINES contains the tick marks,
  numbers and hub ring visible in retail screenshots. The HUDTEX
  `3RDPERSON_8000` face is not referenced by `speed.exe`.
  `Languages/LanguageTextures.bin` holds
  `LCD_LET48` (`0xBBBA71C2`, 128x128), the digital glyph atlas. The
  `FrontEnd/FrontB.lzc` pack was inspected for the event definition; its
  font/layout data is not fully decoded for the HUD. MPH uses the thin
  `CONDUITMDITC_TT14I` atlas, supported by the original stock letterforms.
  `Languages/English.bin` FNTF records provide glyph dimensions, signed
  bearings and advances for that font and `LCD_LET48`; the bounded font
  renderer trace establishes their one-texel UV border. Runtime units-font
  binding remains an inference. See HUD_253.md for provenance/reproduction.

The selected stock face is labeled 0..8, with the red overlay between 7 and
8. Engine B upgrades add 500 or 1000 max RPM to the 240SX; a tuned 8500 or
9000 RPM car selects the 0..9 `9500LINES` face. The port uses the recovered
retail needle rotation, the FEng relative placement of LINES and RED, and
the decoded face hub for needle registration. The needle uses the same
logical scale per texture texel as LINES, with source row 64 at the needle
centre. The decoded LINES hub is one native pixel above that centre.
The `0x004F6A70` rotation matrix sends the sprite's +v axis to screen
angle `270 - rotation` in the y-down frame. It puts stock zero RPM at
226 degrees. The earlier 265-degree offset was a five-degree label fit;
the port now uses the traced frame. The absolute screen anchor remains a
screenshot fit. Font spacing/bearings now use the recovered FNTF metrics.
For the 8500LINES band below a 6500 RPM redline, retail leaves the RED
output unwritten; the port deliberately omits RED. Drag mode selects a
separate `3RDPERSONDRAG_` texture family and is outside this event-1 HUD.

The port's car state has no recovered `+0x3D4` scalar. Its displayed MPH is
an **approximation** from horizontal velocity magnitude times 2.237,
truncated to an integer. The Tach, Redline, Needle, speed and speed units
use their FEng colours and alpha. Gear retains blue: its orange/blue runtime
flag producer remains unrecovered. Absolute placement and units-font choice
remain **approximations** supported by the original screenshots. Text uses
the SAD scales, native FNTF bearings/advances and reference-supported right
alignment at the SAD position.
`SPEED_COUNTER` belongs to the lap panel and is not drawn behind the gauge;
neither the generated disc nor supplemental hub/speed-box art is used. No retail or
derived texture is committed; the selected textures are decoded from
the user's installation into a transient 512x512 atlas at startup.

The #47 first-lap asset check includes `Global/DYNTEX.BIN` and
`Languages/LanguageTextures.bin` under `sdmc:/nfsu/`.
Before/after retail manifests bracket trace and replay runs; their exact
results are recorded in the corresponding board evidence and handoff.

## Rendering and validation

`build_race_hud_frame` fills fixed-capacity vertices; quad counts and layout
are checked in `nfsu_race_hud_tests`. The final deko3d pass makes one indexed
draw with source-alpha blending and depth disabled. The atlas uploads once.
There is no per-frame allocation or CPU readback in direct mode. The optional
`nfsu_switch_compare.nro --hud` mode sends a synthetic 7500 rpm, first-gear
HUD through offscreen deko3d capture and compares it with CPU compositing.
It needs the HUD files in the retail root; the result awaits a console run.
Placement uses a 1280x720 logical canvas and the screenshot-fitted lower-right
anchor documented in HUD_253.md, for handheld and docked sizes. A handheld 960x540 scene is linearly
upscaled first; the HUD then draws into the 1280x720 presentation image. The
forced 720p path draws the HUD there without a blit. The desktop CPU reference composites
the same quads and atlas with nearest texel sampling.

`tools/race_replay_check.py` pins CPU output frames, including the early
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

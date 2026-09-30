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
  the `3RDPERSON_` prefix and chooses numbered `LINES` and `RED` suffixes
  from RPM thresholds. For the 240SX's 8000 maximum, the branch at
  `0x00432B96` selects `8500LINES`; with its 7500 redline, the branch at
  `0x00432BD2` selects `7000RED`. This trace establishes the retail gauge's
  per-car RPM selection, but does not identify the numbered base dial draw.
- `Global/INGAMEB.BUN` holds `3RDPERSON_RPMDIAL`
  (`0x40FAB387`, 16x128), `CENTER_DIAL`
  (`0x56447D19`, 16x16), and `SPEED_COUNTER_SPEEDBOX` (`0x6D079188`,
  128x32). `Global/HUDTEX.BIN` holds the numbered `3RDPERSON_8000`
  dial (`0xC2A1A026`, 128x128). `Languages/LanguageTextures.bin` holds
  `LCD_LET48` (`0xBBBA71C2`, 128x128), the digital glyph atlas. The
  `FrontEnd/FrontB.lzc` pack was inspected for the event definition; its
  font/layout data is not yet decoded for the HUD.

Inferred from the retail screenshots and the 240SX handling: the 8000 dial
matches this car's 8000 rpm limit and its red zone begins at the 7500 rpm
redline. Its decoded labels measure 231.4 degrees at 0 and 50.4 degrees at
8 around the pivot. The needle interpolates between those measured endpoints.
The retail dial selection address was not recovered. Exact original transforms,
sprite colors and font spacing remain unknown.

The port's car state has no recovered `+0x3D4` scalar. Its displayed MPH is
an **approximation** from horizontal velocity magnitude times 2.237,
truncated to an integer. Placement, tint, backdrop opacity and safe-area fit
are **approximations** from the owner's screenshots. `SPEED_COUNTER` belongs
to the lap panel and is not drawn behind the gauge. The dial and speed box
use translucent vertex alpha over the road. The small MPH label is generated with the project
font because the original label layout is still unknown. No retail or
derived texture is committed; the five selected textures are decoded from
the user's installation into a transient 512x512 atlas at startup.

The #47 first-lap asset check includes `Global/HUDTEX.BIN` and
`Languages/LanguageTextures.bin` under `sdmc:/nfsu/`.
The source installation's 476-file manifest was identical before and after
each trace and replay run.

## Rendering and validation

`build_race_hud_frame` fills fixed-capacity vertices; the maximal current
frame uses 15 quads and 90 indices. The final deko3d pass makes one indexed
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

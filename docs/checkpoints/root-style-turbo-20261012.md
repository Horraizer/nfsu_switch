# Turbo label correction and style WIP checkpoint

Owner requested correction of Turbo placement and comparison with the original.
Solo owner workflow, targeted checks only; no new review sessions.

## Completed Turbo correction

At source base 564f3fc, append_race_boost_hud subtracted the full text advance
from TurboPSI's SAT anchor. HUD_SingleRace.fng has Sj=0, meaning left/top,
not the right alignment used by the port. Removed only that subtraction.
Original parent offsets, font, scale, bearings, color and gauge are retained.

Authenticated PC 1.4 USA speed.exe SHA256:
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
Original object TurboPSI: package offset 19200, Sj at19288, SA at19332.
Executed original font loader and text submission via the existing
hud254_presentation_native.Renderer. The supplied string is Turbo; parent
anchor remains the previously measured port anchor, not a new full-game proof.
All 20 emitted glyph corners matched the port at the common 720p canvas.

Validation:
- cmake --build build --target nfsu_race_hud_tests -j 4: PASS.
- build/nfsu_race_hud_tests.exe: race HUD: PASS.
- build/root-turbo-native.py: original glyph submission completed.
- build/root-turbo-frame-run.py: PASS, actual retail atlas / port HUD raster.
- build/root-turbo-compare.py: PASS, 20 corners; final comparison uses fresh
  outputs after removing the two exact prior TGA paths. Earlier probe retries
  and overwrite refusal were not counted as successful renders.

Evidence is preserved relative to repository root under
build-artifacts/draw-distance-20261008-root/turbo-label-r1/receipt.json.
The comparison contains the owner's Deck capture and PC HUD before/after on
an isolated background. Reference top80px is cropped; there is no stretch.
Needle position, gear color and speed units are different supplied states;
this correction concerns the Turbo text anchor only.
No NRO rebuild, upload, Switch verification, push or merge in this correction.
The accepted audio-all-r1 build remains the current hardware-tested artifact.

## Existing style task remains unfinished

Owned tree: build/day20261008-root-combined, codex/draw-distance-original.
Local uncommitted initial style files: include/nfsu/race_style.h,
src/race_style.cpp, cmake/race_session_libraries.cmake. They compile but are
NOT integrated into RaceSession or the HUD and are NOT a finished feature.
No tracked physics, audio or session behavior was changed by this draft.
Research/scripts and 223 native item-state comparisons are in build/style-r1
and build/root-style-*.py. The bounded kernel sample matched current draft;
this is not complete producer, notification or in-game acceptance.

Next: finish style publication/reset ordering and notice behavior, connect
original-backed player/race producers and HUD, then targeted native/PC checks
and a coherent Switch package including this Turbo correction. Traffic near
misses, live drafting and pre-start NOS purge need their actual producers;
do not invent bonuses from the nitrous button or rival distance.
Keep retail read-only, retain audio-all-r1 acceptance, and leave tachometer
and acceleration fidelity in the owner's backlog.

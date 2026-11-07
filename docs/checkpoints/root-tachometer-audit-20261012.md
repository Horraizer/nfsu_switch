# Tachometer audit, UG0 combined pass

Owner-directed solo comparison, 2026-10-12. Base 0b5b918, local combined
successor. PC executable is USA 1.4, SHA-256
C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7.
Retail inputs were only read. Evidence directory, relative to main checkout:
`build-artifacts/draw-distance-20261008-root/ug0-polish-r3`.

## Compared chain

| Component | Original evidence | Current result |
| --- | --- | --- |
| Neutral throttle rise/release | 461C57..461D25 | Previous r2 correction retained: +1/-1.5 per world second, low-RPM opening floor. No artificial needle lag. |
| Physical RPM publication | 478FBB..47904B | Existing engine publication and inertia-dependent fall limiter retained. Needle consumes engine RPM, not wheel-derived actor speed. |
| Needle angle | 4A0EA0 | Existing clamped RPM/max mapping and three maximum-RPM angle bands agree with the inspected original. |
| Scale and red zone | 432A90..432C43 | Existing maximum/redline-dependent texture selection retained. |
| Limiter | Original maximum-minus-50 trigger and per-gear duration table at 700004 | Existing engine logic retained; no new smoothing/filter. |
| Gear glyph | HUD update and transmission publication | N/R/forward gears retained. Fixed blue engaged/orange clutch-transition color, 478F92 and 4A115A. |
| Shift timing | Current physical clutch modes | Test now observes actual transition completion and RPM every tick, instead of requiring an instantaneous two-tick gear change. |
| Turbo | 4A1460 and original HUD SAT geometry | Existing separate gauge and pressure mapping retained. No turbo physics changes in this pass. |
| NOS | Original NOS_2/NOS_6 artwork and SAT geometry | Existing installed-level segments and charge display retained. No NOS physics changes. |
| Drawing | Original textures, FNTF and relative SAT/SAD locations | Existing third-person layout retained, with presentation limitations below. |

The separate wheel-derived 427A30 path is not authority for a 50 RPM
needle follower. No such filter was introduced. The existing targeted HUD
test covers scale bands, needle mapping, units glyphs, gear color and
telemetry through manual up/down transitions.

## Remaining differences and limits

- Historical speed-source gap: this initial build used horizontal velocity.
  Superseded by root-powertrain-20261012.md: the HUD now consumes the existing
  original owner-speed publication with original binary32 MPH conversion.
- MPH is fixed; the original units preference / KM/H selection is not exposed.
- Presentation uses the third-person gauge for the current camera modes.
  Original first-person gauge variants and full FEng visibility/theme options
  are not implemented by this renderer.
- Absolute gauge placement/scale includes the existing measured 720p fit.
  Relative artwork placement is recovered; pixel equality for every camera
  and resolution is not claimed.
- Source comparison and PC tests do not prove every driving trajectory or
  the owner's perceived release timing on Switch. The combined NRO still
  needs the owner's start/rev/release/shift visual check.

## Other changes in the same build

Minimap now submits the original START_LINE sprite: one shared circuit
start/finish or separate point-to-point endpoints. Sources 58E8C8..58E9D2,
5912B0, INGAMEB texture 72974A14. Existing rotating perspective map remains.

UG0 crowd uses DDay scene 2338, its authored actor placement, retail meshes,
textures, skeletons and animation clips. It draws during countdown and stops
at GO. Poses are baked at the source's integer 30 Hz keys on load, packed
into immutable u16-indexed batches. There is no per-frame resource upload.
This is the starting crowd, not a port of the complete NIS cinematic or flag
starter. Fractional-key interpolation, original character lighting and
artist inverse-bind differences at male feet remain visual fidelity limits.
The selected original animation channel outputs were checked at three
integer frames; that is a bounded decoder check, not full skinning parity.

## Validation

- Native CMake affected-target build passed from the actual workspace.
- `ctest --test-dir build --output-on-failure -R
  "^(nfsu_race_hud_tests|nfsu_race_minimap_hud_tests)$" passed.
- `nfsu_start_crowd_check <retail-root>` passed finite vertices across decoded
  poses, animation changes, stable buffers, unchanged-time sampling, GO
  disappearance and new-session restart.
- `nfsu_intro_grid_viewer <retail-root> <fresh-prefix> 90 grid-revs` and the
  600-tick run passed four-car simulation, CPU captures and prepared GPU
  command validation; crowd is absent after GO. Images inspected on PC.
- Real Switch SDK NRO build passed. Focused Switch FMA census, race package
  and frontend profile contract passed. No full matrix per owner instruction.
- Hardware rendering and frame time remain unverified for this NRO.

The first unbatched animation resource implementation exceeded the existing
wet command-cache ceiling and was rejected before delivery. Packing poses
removed that failure without increasing the cache ceiling. The first map
assertion used a synthetic course without a start-line marker; the fixture
now explicitly supplies one. All earlier logs remain local evidence.

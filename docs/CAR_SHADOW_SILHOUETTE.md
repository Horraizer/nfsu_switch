# In-race car silhouette shadows

The Switch race scene enables the SHADOWRAMP layer for the player, rivals
and traffic. It can draw independently of a car's SHADOWIG texture. The
existing SHADOWIG and neon layers retain their own texture and ground taps.
Traffic SHADOWIG textures are resolved from the model pack and the shared
`Cars/TEXTURES.BIN`. In PC USA 1.4 the ambulance supplies its own SHADOWIG;
the other traffic models use their own named textures from the shared pack.
The original texture registry lookup at `5461C0` skips a missing texture
without using a substitute.

Retail PC USA 1.4 evidence:

- `56DC30` reads the active collision shape at actor `+3C`, body `+10`,
  then reads the shape vertices through `+0C`. The port uses the same named
  collision hull already decoded for the actor's physics. `56E591` adds
  `0.275` to the vertex height before projecting into the render-origin plane.
- `546598` passes the track's `+70` key to `546310`. The latter selects the
  last matching record of `34202` and admits type 2. `56D700` reads that
  record's `+2C/+30/+34` light position, or uses `(-16384,0,7000)`.
  The prepared scene resolves it once from the same GLOBALB input as the car.
  `56D799` sends `(horizontal_length,-z)` to `564DB0`. A ray below angle
  `1A90` uses the `564BD0` cosine in XY and sine in Z. The r106 preview
  exposed the reversed elevation test. `nfsu_car_shadow_projection_tests`
  compares the corrected ray with the isolated original routine.
- `56CF10/56CEB0` query the road and reject points outside the reference
  height/plane limits. `56D0C0` smooths each corner with thirds of its edges.
  `56E74C` computes the height-dependent 55-to-0 vertex alpha. `56D870`
  expands the rim in XY with the height-dependent 1.4-to-1.1 scale.
- `56A130` resolves GLOBALB texture `BADB4475` (SHADOWRAMP). All actors
  share its one upload. The central and edge coordinates come from the
  immediate vertices in `56DC30`. The unlit world-effect material doubles
  the sampled vertex RGBA and uses source-over with depth writes off.

The port uses a convex contour, a triangle fan and a rim strip in one
triangle-list draw per visible actor. It uses the existing fenced streamed
vertex buffer and fixed index ranges. No new shader or render target is
required. The view fade uses the original car pixel-size and LOD gates,
not a world-distance cutoff. `car_shadow_pixel_size` reproduces PC
`545180`: reject a radius-three sphere wholly behind the view, otherwise
truncate `scale * radius / (distance - radius)`, capped at `2 * scale`
when the denominator is at most half the radius. The position is the
body render origin, with the centre of mass removed.

PC `40DF15` writes the view descriptor's `+0C` from `40DCB0`: half the
MAIN display width (`701034`) divided by the scalar tangent of half the
unadjusted horizontal FOV word. GameCube `80005734/80005738` reads the
same camera FOV field `+C4` and calls `CalculateH` at `80005654`.
That console routine uses the fixed half-width constant at `802665CC`,
equivalent to a logical display width of 512, and returns 10 for a zero
tangent. `car_shadow_view_pixel_scale` uses this console source in the
Switch scene, independently of output resolution, widened projection and
split viewport dimensions. `drive_view_pixel_scale` also exposes the PC
width-dependent form for the original executable witnesses.

The two variants share a formula but have different rounding. GameCube
`80005668` shifts the angle word before calling `bSin` at `80167A28`;
`bCos` at `80167B20` adds a quarter-turn and wraps to the angle word.
The sine polynomial uses binary32 multiplication/subtraction and two
explicit fused multiply-adds. `80005680` rounds the sine/cosine division
to binary32 before `800056A8` divides the fixed half-width by that tangent.
Reusing the PC helper's wider intermediates can cross an integer LOD
boundary. The shadow-only implementation reproduces those operations;
the display projection and skin geometry do not use this function.

Chase and authored start/finish cameras carry their original angle through
`OrbitCamera`. The active basis and this angle are resolved after camera
replacement. Both split-player views use their own camera and the same
console scale source. `56DC6C` then scales the truncated size by the original two-view factor
and truncates again, starting LOD choice at B. The scene adapter applies
the same view to the player, rivals and traffic during that frame.

`car_shadow_silhouette_alpha` uses `car_lod_for_pixel_size` and the
thresholds in `kCarLodPixelSizes`. At A/B there is no view fade. At C,
`56E77A` interpolates from the lower to the upper C-band endpoint in
pixel units. At D, `56E46B` omits the silhouette. The height alpha remains
independent. `nfsu_car_shadow_projection_tests` pins original executable
size, scale, LOD and alpha witnesses, including the rear-sphere boundary,
near-camera cap, lateral displacement, lens changes and two-player scale.
Reproduce its fixture into fresh output paths with
`tools/car_shadow_view_native.py --root <retail> --output <new.inc> --evidence <new.json>`.
This uses the authenticated PC executable in bounded synthetic memory;
it does not launch the game. The PC scale helper remains independently
covered by those fixtures. `tests/car_shadow_gc_native.inc` separately
pins original GameCube scale bits and the reviewer's native GetPixelSize
LOD boundary. The console checks compare bits, without a scale tolerance.
Reproduce that fixture with
`tools/car_shadow_gc_native.py --disc <GNDE69-disc> --output <new.inc> --evidence <new.json>`.
`--full` emits every uint16 FOV word for an exhaustive comparison. The
tool authenticates Speed.elf and its boot-DOL sections, executes the
original scalar instructions and emulates only Unicorn's unsupported
`psq_l` ushort-angle load. It does not launch Dolphin. On the host compiler,
the float FMA library call itself rounds the product separately, so the
explicit fused steps use the double FMA overload followed by binary32
rounding. That bounded angle-domain implementation is checked against
the original GameCube results, including odd angle words and zero tangent.

The constant-height shortcut and strip ordering are not reproduced bit
for bit. Split-screen's `LanScene` currently leaves the silhouette option
disabled; the corrected `RaceGpuScene::frame_split_view` path applies when
the layer is enabled. Reflection captures omit the layer, and the front end
keeps its existing shadow path.

`nfsu_car_shadow_silhouette_retail` checks both projected layers on a traffic
actor with a shared SHADOWIG, its inactive slot and the shared ramp. It
also changes the real frame camera across native lens/distance cases and
checks player/traffic streamed alpha in the cached scene. A separate scene
and session with only the player check the supported reference recorder;
that recorder deliberately refuses multi-car scenes.
`nfsu_car_shadow_silhouette_sprint_retail` checks the expanded sprint backing
with the layer enabled. `nfsu_career_full_retail` enables the layer for the
production scene check of each authored campaign stage. Switch pixel
comparison and portable frame timing require a hardware run.

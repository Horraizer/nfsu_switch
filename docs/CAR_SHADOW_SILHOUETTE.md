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
required. The distance fade uses the original LOD2 endpoints as a port
distance gate. The original view descriptor, LOD selection, constant-height
shortcut and strip ordering are not reproduced bit for bit. Reflection
captures omit the layer, and the front end keeps its existing shadow path.

`nfsu_car_shadow_silhouette_retail` checks both projected layers on a traffic
actor with a shared SHADOWIG, its inactive slot and the shared ramp.
`nfsu_car_shadow_silhouette_sprint_retail` checks the expanded sprint backing
with the layer enabled. `nfsu_career_full_retail` enables the layer for the
production scene check of each authored campaign stage. Switch pixel
comparison and portable frame timing require a hardware run.

# Roadmap

## M0: Installation probe

- Verify the expected PC directory layout.
- Identify the executable architecture and build timestamp.
- Walk bounded NFS chunk trees.
- Test the parser with generated fixtures.

## M1: Car viewer

- Decode `GEOMETRY.BIN` object tables.
- Decode vertex and index buffers.
- Decode `TEXTURES.BIN` texture packs.
- Decode the embedded texture packs of `GLOBAL/GLOBALB.BUN` and resolve
  shared car textures from explicit, ordered sources. Runtime body-skin
  composition into `DUMMY_SKIN1` is in place and the generated texture is
  bound under the `%s_SKIN1` hash the retail render path overrides
  ([`RUNTIME_SKIN.md`](RUNTIME_SKIN.md)). P8 vinyl index chains and
  palettes are structurally extracted without guessing the blend.
- Export diagnostic meshes and images.
- Render one complete car on desktop: deterministic CPU reference renderer
  and TGA screenshot (`nfsu_viewer`, `docs/RENDERER.md`), and an interactive
  desktop window over the same prepared scene with camera orbit, pan and
  zoom, mesh and material inspection and diagnostic views
  (`nfsu_viewer --interactive`, [`INSPECTOR.md`](INSPECTOR.md)). The window
  is Win32 and GDI over the CPU renderer; a GPU one is still to do.

## M1.5: Complete car asset scene

- Decode embedded and raw shared texture packs with explicit source priority.
- Resolve shared car materials while preserving source provenance.
- Build a backend-neutral prepared draw list for the CPU and deko3d renderers.
  `PreparedCarScene` (`include/nfsu/scene.h`) is in place with an all-meshes
  profile, explicit mesh subsets and the stock car profile, and the CPU
  renderer consumes the first two; the deko3d backend is still to do.
- Installed-part and LOD selection are recovered: the car part database of
  `GLOBALB.BUN`, the stock part set per slot, the per-LOD mesh hash of a
  part and the fallback the game uses for a missing mesh
  (`include/nfsu/car_parts.h`, `nfsu_probe stock-parts`).
- Part placement is recovered: the car type table of `GLOBALB.BUN` carries
  the four wheel positions per car, geometry chunk `0x0013401A` carries the
  named attachment markers that place the exhaust and the licence plate, and
  every other installed part is drawn at the car's own matrix
  (`include/nfsu/part_placement.h`, `nfsu_probe car-placement`,
  `nfsu_probe part-markers`, [`PART_PLACEMENT.md`](PART_PLACEMENT.md)). The
  drawn wheel matrix's lateral term is traced but not reproduced.
- Selection and placement are assembled into a render profile:
  `nfsu::prepare_stock_car_scene` (`include/nfsu/stock_scene.h`,
  `nfsu_probe stock-scene`) builds the `stock car` profile of
  `PreparedCarScene` from the stock part set, the per-LOD meshes and the
  placement, with one instance per drawn copy. The wheel and the brake are
  opt-in and marked inexact because their drawn matrices are not
  reproduced.
- The CPU reference renderer draws that profile:
  `nfsu_viewer --stock-car NAME` places every copy with its instance
  transform, one copy per instance inside the retail draw order of the
  material groups, and reports drawn copies apart from meshes and
  stand-in placements apart from exact ones
  ([`RENDERER.md`](RENDERER.md), "Instances").
- The body-skin pixel compositor is recovered for both target types, with a
  bounded CPU reference (`include/nfsu/body_skin.h`,
  [`BODY_SKIN.md`](BODY_SKIN.md)). The RED/GREEN/BLUE/GLOSS lane mapping is
  proven.
- Real vinyl, vinyl-mask and doorline surfaces now reach that compositor
  through an owned input-assembly layer (`include/nfsu/skin_inputs.h`,
  `nfsu_probe body-skin-inputs`, [`SKIN_INPUTS.md`](SKIN_INPUTS.md)). It
  takes an explicit customization selection, resolves the pairs out of
  read-only packs with full provenance and owns every buffer the request
  points at.
- The composed skin now reaches the screen
  (`include/nfsu/runtime_skin.h`, `nfsu_viewer --skin-car`,
  [`RUNTIME_SKIN.md`](RUNTIME_SKIN.md)). The compositor path is taken from
  the real target record, the result is bound into `RenderTextureSet` at a
  documented priority over every pack source, and `prepare_car_scene`
  resolves the body material groups to it. Without the `--skin-*` options
  a render is byte-identical to before on its TGA, stdout, stderr and exit
  code; the usage text is the one thing that changed for every invocation.
  Which customization a car actually has is still the caller's to state: no
  part database is read here.
- The prepared scene can now be inspected by hand
  (`nfsu_viewer --interactive`, [`INSPECTOR.md`](INSPECTOR.md)). A click
  names the material group that drew a pixel, with its mesh, record fields,
  bound texture and the retail render state that texture selects, and the
  draws, meshes and depth views show how the car is cut up. The rule that
  makes a pixel locatable - the submission order and the triangle
  numbering - moved into the scene layer as `prepared_draw_order` and
  `prepared_triangle_ranges`, so the deko3d backend inherits picking rather
  than reimplementing it. No frame changed.
- Track retail evidence and remaining unknowns in
  [`CAR_SCENE.md`](CAR_SCENE.md).

## M2: Track slice

- Decode one `STREAM*.BUN` region. The 24-byte track vertex, the
  per-mesh vertex-format field that selects it, and one explicitly
  bounded region of `Tracks/STREAML1RB.BUN` are decoded end to end
  (`include/nfsu/track.h`, `nfsu_probe track-region`,
  [`TRACK.md`](TRACK.md)). The streaming tables are still to do.
- Place that geometry. The `0x80034100` scenery section beside each
  geometry root is decoded end to end: the object directory and its six
  mesh-hash slots, the instance transforms and their coordinate
  convention, and the bounding tree over the instances
  (`include/nfsu/track_objects.h`, `nfsu_probe track-objects`,
  [`TRACK_OBJECTS.md`](TRACK_OBJECTS.md)). All 593 retail sections decode
  with no issue. Which sections are resident together is still to do.
- Resolve track textures and materials. The bounded region resolves
  against explicit ordered track texture sources through the same
  `RenderTextureSet` the car scene uses; per-material effect state is
  still to do.
- Add visibility, camera, and static collision. Scenery visibility is
  recovered from the executable and implemented for one bounded region:
  the six-plane box test, the bounding-tree prune it drives and the
  per-instance gates (`include/nfsu/track_visibility.h`, `nfsu_probe
  track-visibility`, [`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md)). The
  zone layer that picks which sections are drawn is recovered as a rule
  and its data is decoded: the four children of the `0x80034150` chunk
  every `Tracks/*.lzc` carries, and the crossing test that picks one zone
  from a camera position (`include/nfsu/track_zones.h`, `nfsu_probe
  track-zones`, [`TRACK_ZONES.md`](TRACK_ZONES.md)). Residency still
  defaults to the explicit draw-every-section fallback, because a caller
  that has read no zone file has no zone to consult. Static collision is
  still to do.
- Measure CPU, memory, and draw-call costs.

## M3: Switch bring-up

- Add devkitA64 and libnx build support. An opt-in Switch configure
  (`cmake/switch-toolchain.cmake`) locates `DEVKITPRO` and `DEVKITA64`,
  names any missing SDK piece and the variable that fixes it, and builds a
  minimal libnx application into an NRO; the desktop build is unchanged
  ([`SWITCH_BUILD.md`](SWITCH_BUILD.md)). Nothing has run on hardware yet.
- Add controller, filesystem, timing, and logging services.
- Add a deko3d renderer with offline-compiled shaders. The shader pipeline is
  in place: `uam` compiles the car and track material variants at build time,
  and every program's manifest is checked against its GLSL, its compiled
  shader and the C++ vertex structs
  ([`SHADER_PIPELINE.md`](SHADER_PIPELINE.md)). The renderer that draws with
  them is still to do.
- Run the car viewer and track slice on hardware.

## M4: Playable race

- Recover vehicle data and handling behavior.
- Add checkpoints, laps, opponents, and race state.
- Add audio, HUD, save data, and frontend systems incrementally.


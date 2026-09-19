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
  shared car textures from explicit, ordered sources. The body skin
  (`240SX_SKIN1`), vinyls and P8 palette textures are still to do.
- Export diagnostic meshes and images.
- Render one complete car on desktop: deterministic CPU reference renderer
  and TGA screenshot (`nfsu_viewer`, `docs/RENDERER.md`). An interactive
  GPU window is still to do.

## M1.5: Complete car asset scene

- Decode embedded and raw shared texture packs with explicit source priority.
- Resolve shared car materials while preserving source provenance.
- Build a backend-neutral prepared draw list for the CPU and deko3d renderers.
- Recover installed-part and LOD selection before calling a profile stock.
- Track retail evidence and remaining unknowns in
  [`CAR_SCENE.md`](CAR_SCENE.md).

## M2: Track slice

- Decode one `STREAM*.BUN` region.
- Resolve track textures and materials.
- Add visibility, camera, and static collision.
- Measure CPU, memory, and draw-call costs.

## M3: Switch bring-up

- Add devkitA64 and libnx build support.
- Add controller, filesystem, timing, and logging services.
- Add a deko3d renderer with offline-compiled shaders.
- Run the car viewer and track slice on hardware.

## M4: Playable race

- Recover vehicle data and handling behavior.
- Add checkpoints, laps, opponents, and race state.
- Add audio, HUD, save data, and frontend systems incrementally.


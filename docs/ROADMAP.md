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
- Export diagnostic meshes and images.
- Render one complete car on desktop.

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


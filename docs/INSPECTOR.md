# Interactive car inspection window (`nfsu_viewer --interactive`)

`nfsu_viewer --interactive` opens a desktop window on the same
`PreparedCarScene` a deterministic screenshot is rendered from, so a car can
be turned, zoomed and taken apart by hand instead of one still frame per
process. It is a diagnostic tool over the existing renderer: it decodes
nothing of its own, reads no file the headless run does not read, and writes
nothing at all.

```text
nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> <out.tga> [every other option] --interactive
```

The flag is the last thing the run does. The screenshot is rendered and
written, and the whole report is printed, exactly as without it; then the
window opens. One line, `interactive: backend win32`, is appended to the
report after `wrote:`. Without the flag not one line of this code runs, so
the TGA, stdout, stderr and the exit code of every other invocation are byte
for byte what they were before it existed. The usage text is the one thing
that changed for every invocation, as it did when `--skin-car` was added.

`--interactive` on a build with no window backend is a usage error (exit 1),
refused before any file is opened, because an option that silently did
nothing would be worse. Building the window itself, or the first frame,
failing exits 2.

## Controls

| Input | Action |
| --- | --- |
| left drag | orbit |
| right or middle drag | pan |
| wheel | zoom, one step per notch |
| left click | select the material group under the pointer |
| `1` `2` `3` `4` `5` | front, right, rear, left and top views |
| `R` | reset the camera to the one the command line asked for |
| `F` | frame the selected group; `G` frames the whole scene |
| `Esc` | clear the selection |
| `N` / `P` | next or previous material group |
| `M` / `B` | next or previous mesh |
| `V` | cycle the view: shaded, draws, meshes, depth |
| `O` | cycle the overlay: summary, detail, help, off |
| `H` | highlight of the selected group on or off |
| `C` | cull: none, back, front |
| `S` | flat shading on or off |
| `T` | retail material state on or off |
| `X` | vertex color on or off; `U` flips V |
| `Q` or the window close button | close |

The pointer follows the grab convention: the car moves with it. Dragging
right lowers yaw, dragging down raises pitch, and a pan of one pixel moves
the target by exactly one pixel's worth of world at the target's depth, so
a point at that depth stays under the pointer.

`T` changes the submission order of the whole scene, which renumbers every
triangle, so it drops the selection rather than keeping one that was made
against ids that no longer mean the same thing.

## Views

Every view is derived from the buffers of the frame that was drawn, so each
one describes that frame and is never a second render under other rules.

- **shaded** is the frame itself, the same pixels `write_frame_tga` would
  put in a screenshot.
- **draws** colors each pixel by the prepared position of the material group
  that drew it, so one group is one flat color. It is the fastest way to see
  how a car is cut into 1242 groups and which of them are actually on screen.
- **meshes** does the same keyed on the prepared mesh.
- **depth** is a gray ramp between the nearest and the furthest drawn depth
  in the frame, the nearest white.

The background is kept wherever nothing was drawn, so the silhouette stays
readable in every view. The selected group is tinted amber on top of
whichever view is showing, unless `H` turns that off.

## Overlay

The overlay text is built by the inspector, not by the window, and is
printable ASCII. `summary` reports the view and frame size, every camera
value and the eye, every render option, the scene totals, the binding and
triangle statistics of the frame, and one line naming the selection.
`detail` adds, for the selected group: the mesh source position, directory
hash, offset and size; the group's `first_index`, `index_count`, material
key slot, flags and record offset; its record bounds; the bound texture's
size, bytes and source, pack and record provenance; the two raw texture-info
bytes with the alpha test, blend mode and draw queue row they decode to; the
material coefficient gates the flags open; and the world bounds of the
vertices the group actually draws. A group with no resolved texture says so
and names the hash it draws the fallback pattern for.

`help` lists the bindings in the table above.

## Backend neutrality

The split is the point of the design, because a deko3d backend has to
arrive later without reopening any of this:

- `include/nfsu/inspect.h` and `src/inspect.cpp` hold every rule: what a
  drag does, what is selected, what a view shows, what the overlay says.
  They open no window, call no operating system, read no file and keep no
  clock. A backend hands them the frame's per-pixel buffers through
  `InspectionBuffers` and gets back pixels, a highlight mask and lines of
  text. The only mention of the CPU renderer is
  `inspection_buffers_of(const Frame&)`, a convenience for the backend that
  has one.
- `include/nfsu/inspect_window.h` and `src/inspect_window_win32.cpp` hold
  the window: user32 and gdi32 only, no external dependency, no graphics API
  and no shader. The frame is a device-independent bitmap and the overlay is
  GDI text. `src/inspect_window_none.cpp` takes its place elsewhere and
  fails closed.
- Picking needs a backend to number its triangles the way the CPU renderer
  does. That rule is now written down once, in the scene layer, as
  `prepared_draw_order` and `prepared_triangle_ranges`
  (`include/nfsu/scene.h`); `render_scene` calls the first of them instead of
  restating the retail draw queue, and `tests/inspect_tests.cpp` checks the
  numbering against frames the renderer actually produced. A deko3d backend
  that submits the groups in `prepared_draw_order` and numbers the triangles
  it submits from 0 gets every control, view and overlay line for free.

Nothing in the inspector is part of the deterministic reference. The
screenshot path does not call it, and no interactive state can reach a
written file.

## Bounds

The camera is clamped at every step to the ranges `validate_camera` accepts
(`docs/RENDERER.md`), so no sequence of input can produce a camera the
renderer would refuse: pitch stays in -89..89, distance in 0.01..10000, each
target component within 10000, and yaw is wrapped into -180..180 instead of
accumulating. Pointer deltas are clamped before they are applied and wheel
steps are counted, not exponentiated, so a single enormous event cannot move
the camera unboundedly far. A delta that is not a number leaves the camera
valid.

The window renders at the client size, except while a pointer button is held,
when it renders at half of it and stretches the result, so that a drag stays
responsive on a CPU rasterizer. A click is resolved against a frame at the
full client size, so the pixel clicked and the pixel picked are the same one.
A client size the render limits refuse keeps the previous frame size and
stretches it.

A render is asked for only when the frame itself is out of date. The
inspector reports that separately from needing a repaint: the camera, the
viewport and the render options stale the frame, while selecting a group,
switching the view, moving the overlay and toggling the highlight are
answered from the frame already on hand. Switching a view on a car that
takes a moment to rasterize is therefore immediate, and
`tests/inspect_tests.cpp` pins which transition does which.

## Build and tests

`nfsu_inspect` is the state, `nfsu_inspect_window` the backend; the viewer
links the second, which links the first. The window backend is chosen by
`if(WIN32)` in `CMakeLists.txt` and nothing else selects it.

`nfsu_inspect_tests` covers the layer headlessly. No test opens a window:
one that did would wait for a person. It checks that the prepared order is
the identity and the retail order is a permutation that walks the draw queue
rows in ascending order; that the triangle ranges are contiguous from zero
and cover every submitted triangle; that a malformed order is refused; that
in **both** material states every drawn pixel's id names the group whose
texture is at that pixel, on frames the CPU renderer produced, which is the
whole picking contract; that the camera survives 200 rounds of hostile input
and stays valid; that a drag moves the target by exactly the pixels dragged
and never along the view; that the selection walks visit every drawable
group and every mesh once and wrap; that a click selects the group under it
and a click on the background clears; that the highlight mask marks exactly
the selected group's pixels; that each view gives one color per group or per
mesh and keeps the background; that a view is refused rather than invented
when the backend gave it no depth or no ids; that the overlay is printable
ASCII and reports what it claims to; and that the window backend refuses an
inspector with no scene and an out-of-range drag divisor before creating
anything.

The refactor that moved the draw order into the scene layer was checked
against a `nfsu_viewer` built at the previous head: the TGA, stdout, stderr
and exit code are identical for the default options, for
`--material-state retail` with coefficients, for a runtime skin composite
and for a mesh subset with `--cull`, `--shading`, `--vertex-color` and
`--flip-v`.

## Still to do

- The window cannot save what it is showing. The screenshot is written
  before it opens, from the command line's camera, and a new one needs a new
  run. A save from inside the window would need its own never-replace naming
  rule, which is a decision, not an implementation detail.
- There is no isolate mode: the selection is highlighted, not drawn alone.
  Drawing one group alone means re-preparing the scene, which is
  `--mesh-index` territory.
- The overlay is GDI text at the top left and does not wrap; a `detail` line
  longer than the window is cut off at its edge.
- The window is Windows only. A deko3d backend on hardware, and an X11 or
  Wayland one on a workstation, both implement `inspect_window.h` without
  touching anything above.

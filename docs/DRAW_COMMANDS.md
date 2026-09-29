# Backend-neutral draw commands

Issue #145 adds `nfsu_draw_commands`, built on desktop and Switch from
`cmake/draw_libraries.cmake`. It records the calls a renderer will submit
without including deko3d, allocating GPU memory, or calling a platform API.
It does not execute the commands or change the Switch application's CPU
framebuffer path. The device backend and its presentation are later issues.

## Contract

`record_car_commands` takes a `PreparedCarScene`, render options/limits and
the generated texture source (or `kNoHandle`). It uses
`prepared_draw_order` and `prepared_mesh_instances`: retail layer and texture
group order, followed by ascending source instance order inside each group.
Without instances, each group draws once in its authored space. Invalid
(non-drawable) and zero-length ranges emit no command.

`record_track_commands` takes a `PreparedTrackScene` and its `TrackDrawList`.
It preserves the list's exact order. It never selects visibility or sorts the
entries. All three list orders work. In retail mode, resolved track textures
select alpha test and blend variants from texture-info +85/+86. The caller
selects `retail_bucket` for the recovered texture-row ordering approximation;
see [`TRACK_MATERIALS.md`](TRACK_MATERIALS.md).

Every `RecordedDraw` contains:

- A stable diagnostic name and source group, mesh and instance handles.
- Vertex, index and optional texture resource-table indices. No texture means
  the fallback program; the previous draw's binding must not carry across.
- The entire source vertex range, base vertex zero, and a triangle-list u16
  index range in the source mesh's packed `MeshTriangle` stream.
- A generated `shader_manifest::Program` and its named variant, including
  vertex layout, sampler state, depth test/write, RGB colour write, blend
  factors/operation and alpha test/reference. `RenderOptions::cull` explicitly
  overrides the variant's default cull field. It does not alter its name.
- The Instance and Material std140 payloads the manifests describe.

The recording also carries viewport dimensions, clear colour and the Frame
uniform payload. The depth attachment must be cleared to its far value before
submission; every draw tests less-equal. Blended track draws leave depth
unchanged, while car draws and opaque track draws write depth. The alpha
channel stays at its clear value under RGB-only writes.
The options payload preserves flip-V, vertex colour, flat shading and the
separate diagnostic alpha cutoff. Material coefficients apply only to
resolved car materials in retail mode, including the diffuse/environment
flag gates. Fallback colour is the reference's hash-derived checker.

A recording failure clears the output and reports why. The existing scene
and list validators run before any indexing. Dimensions and source/submitted
triangle counts obey `RenderLimits`; pixel/raster limits apply only when
rasterizing. A bad car vertex index is refused with the draw and triangle
name: the CPU can skip it, but submitting it to a GPU is not safe. Scene
validation already refuses that condition on the track path.

## Ownership and placement

`RecordedCommands::uploads` is exactly the existing car or track upload table,
not a second resource inventory. Resource data pointers borrow the unchanged
scene. Commands, manifests and uniform values are held by value or point to
static manifest data; the scene must outlive the upload bytes they reference.
Resource lookup tables are built once per recording, so binding does not scan
all resources for every placed copy.

Pass `uploads` directly to `UploadLedger::upload_table`. Its returned handle
vector has the same indices the commands use. The ledger and the caller's
buffer/image/staging `MemoryArena` objects retain the existing placement,
fence and release rules, including generated-skin lifetime.

`reserve_draw_uniforms` reserves one range in a caller-supplied arena with
`kDekoUniformBufferAlignment` (256). Relative to its allocation offset:

| Payload | Offset | Bytes read |
| --- | --- | --- |
| Frame | 0 | 112 |
| Draw i Instance | `(1 + 2*i) * 256` | 64 |
| Draw i Material | `(2 + 2*i) * 256` | 80 |

The backend copies those payloads into its backing memory and binds the slots
0, 1 and 2 specified in the manifest. The caller releases the single arena
allocation after its last-use fence. Reservation changes neither an existing
output allocation nor held bytes on failure. The size is bounded by the
existing maximum GPU resource size; all usage is counted by `MemoryArena`.
No additional ledger or memory counters are introduced.

## Evidence

The CPU render functions accept an optional `ReferenceDrawObserver`. The
actual material/instance loop emits one observation before submitting each
nonempty range, independently of the command recorder. Normal callers have
no observer. The test comparison in `tests/draw_command_checks.h` checks
identity/order, buffer/index/texture references, program/variant, layout,
sampler and every fixed-function state field. A mismatch names the draw and
field; deliberate depth-write and alpha-test changes verify those failures.

The synthetic checks extend `nfsu_gpu_upload_tests` using its parsed car and
track fixtures. They cover all six car variants, off/retail modes, every cull
mode, fallback and generated skin bindings, reordered track visibility,
both track list orders, repeated car/track placements, retail reordering,
material coefficient gates, zero ranges, malformed scenes/lists, invalid GPU
indices, render limits, ledger upload/release and arena exhaustion. Every
uniform member offset and block size is checked against all four generated
manifests. The existing shader manifest suite independently checks vertex
attributes against the actual C++ types.

The optional executable reads the six stock inputs without writing any file:

```text
build/nfsu_draw_commands_retail.exe <retail-root>
```

Take file manifests before and after running it. It records and compares the
painted and unpainted stock 240SX in both material modes and checks every
instance matrix. Its off mode checks the painted and unpainted frame sizes and
SHA-256 pins in `include/nfsu/stock_frame.h`. CTest runs it as
`nfsu_draw_commands_retail` (label `retail`) when the build is configured with
`-DNFSU_RETAIL_ROOT=<installation>`; a build configured without the root does
not run it. No retail or derived asset is tracked.

The library is classified neutral. The neutrality gate follows generated
project headers from the build's generated include directory and scans their
contents recursively, just as it does tracked headers. It does not exempt
them. The shared shader header is generated even with `NFSU_BUILD_TESTS=OFF`.

These checks establish command correspondence, not GPU pixel equivalence.
Issue #146 adds the Switch-only executor and its packaged offline shaders;
[DEKO3D_RENDERER.md](DEKO3D_RENDERER.md) describes it and its limits.
No commands have run on hardware. The known shader/raster differences in
`SHADER_PIPELINE.md` remain; the Switch CTest suite runs on the build host.

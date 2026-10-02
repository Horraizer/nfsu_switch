# WorldObjectCollisionBody: bounded research for issue #259

This stage reproduces the kind-4 body construction and its zero/nonzero
backing gate from the pinned PC executable. It does not identify a named
placed kind-4 object on the introductory Underground event0 route, create a
body in the port, or measure a retail contact. The required named-instance
and runtime prerequisites remain open. A visible scenery object and an ASCII
type tag are different identities.

## Inputs and reproduction

The investigation started at `f5b08a3a3f698be23ac6bc58a861e8c3527f329d`.
All reads ran in a detached worktree under the actual non-ASCII workspace.
No retail process was launched and no retail or derived asset is tracked.

| Read-only input | Bytes | SHA-256 |
| --- | ---: | --- |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |
| `Tracks/TRACKB1003.lzc` | 433352 | `ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA` |
| `Tracks/STREAML1RA.BUN` | 92394688 | `2385556EC31142F7390434CA70152A9F3D629155AF991BB6595422FCB26A752E` |

With Python 3.8+ and Capstone, run from the worktree root:

```powershell
python tools/world_body_trace.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --out build/259-body-evidence --census
```

Use a fresh ASCII output path. The tool checks the three input identities,
maps VAs through raw PE sections, and rejects incomplete disassembly of the
listed ranges. It does not sweep through padding or treat raw displacement
matches as proven callers. The vtable ends before the following ASCII class
name. Reports are `259-instructions.txt` and `259-source.json`; source range
hashes, input provenance, constants and explicit unavailable fields are
included. No executable bytes or decompressed asset bytes are exported.

Before/after manifests cover the root and every directory/file, sizes,
mtimes and file SHA-256s. A difference fails the run. Output must be inside
the current workspace, outside retail and `E:/Games`, and not an ancestor
of retail. Existing output is rejected before reading retail. The optional
census reads all nonempty immediate files in Global, Tracks and FrontEnd,
with JDLZ decoded in memory according to [FORMATS.md](FORMATS.md). Every
chunk tree must parse completely; a malformed file is a failure, not an
empty census. Each inspected file has its size/hash in the report.

The trace is a research reproduction. Its PASS means the bounded static
reads and unchanged-retail checks completed; it does not mean collision or
event0 gameplay passed. The three file pins also do not authenticate every
optional census file as an original release: their individual hashes are
reported separately, including modified installed bundles and backups.

## Type, shape and constructor state

The caller at `0x0057817B..0x00578183`, inside the WorldObject constructor
`0x00578060`, takes input-record `+0x08` as the collision type and calls
`0x00578590`. This is an input to a WorldObject constructor, not the scenery
instance's six mesh hashes. The factory hashes the literal `BB` at
`0x006BA6B4` with the case-sensitive multiply-by-33 loop, compares it with
that type, and selects the constructor `0x00592470` when WorldObject
`+0x30` is present. The other branch passes the type hash to `0x00592280`.
Both return a body stored at WorldObject `+0x34`. See also
[COLLISION_PAIR_TRACE.md](COLLISION_PAIR_TRACE.md).

Both constructors pass kind 4 to `0x00468340`; the base stores the kind
byte at `+0x15`. They install vtable `0x006B8D88`, clear body
`+0x60/+0x68/+0x6C`, set `+0x16` and `+0x1D` to 1, and store the owning
WorldObject at `+0x140`. These are initial field writes, not proof of their
values at the first actual contact.

The BB branch follows WorldObject `+0x30`, then its `+0x08`, into reader
`0x0053F5F0`. That reader dereferences the supplied pointer's `+0x0C` and
reads vectors at nested `+0x20/+0x30`. A null nested pointer produces two
zero vectors; it does not establish a usable hull. The constructor creates
a shape with type 3 and eight points. `0x00473CF0` forms its center as
`(a+b)/2` and its half extents as `(b-a)/2`. The source geometry and loader
must still be joined to a named instance before using this box in the port.

The generic branch walks the shape list at `0x00779BD8`, comparing the
requested hash with shape `+0x18`. A miss allocates a default box. The
presence of this fallback is not evidence that a particular event0 object
uses it. A model/shape/type hash must retain its reader and record provenance.

## Backing gate, mass, inertia and impulse

Body `+0x60` is a zero/nonzero gate. Calling it a pointer is not supported:
the separate Smokeable constructor writes integer 1 there at `0x00596887`,
while storing the allocated rigid-body pointer at `+0x6C` and another
interface address at `+0x68`. This verifies the distinction between the gate
and the pointer; it does not recover a kind-4 activation writer.

| Method | Zero gate | Nonzero gate | Source |
| --- | --- | --- | --- |
| Inverse mass, vtable `+0x58` | Returns the float zero at `0x006CC7A4`. | Returns `min(rigid+0x148, 1 / float[0x006B5AA8])`, reading the rigid pointer from body `+0x6C`. | `0x005927E0..0x0059281B` |
| Inverse inertia, vtable `+0x5C` | Returns shared matrix address `0x00779C30`. Its live contents are not measured here. | Returns rigid `+0x190` when rigid `+0x138` is nonzero, otherwise rigid `+0x150`. | `0x00592820..0x00592845` |
| Apply impulse, vtable `+0x4C` | Returns without calling the rigid impulse method. | Scales the incoming vector by 1, or by cap/raw inverse mass when raw inverse mass exceeds the cap; calls `0x00584E30` with the scaled vector and supplied contact arm. | `0x00592940..0x005929B6` |

The cap denominator is the retail float near 0.3, not a selected object's
mass. For the finite positive mass branch, scaling the actual rigid impulse
gives the capped translational response. The inverse-inertia getter itself
does not apply that scale. Preserve these separate operations rather than
silently rewriting the descriptor as a guessed 0.3 mass object.

Shared initialization `0x00584B10` reads the descriptor through rigid
`+0x14`, stores its mass at `+0x144` and reciprocal at `+0x148`, and inverts
the descriptor matrix into `+0x150`. It copies the matrix, scales one
element by descriptor `+0x50`, and computes the alternative inverse at
`+0x190`. This establishes the shared reader; no descriptor source or value
has been selected for a named kind-4 event0 body.

The getter at `0x00592850` chooses rigid `+0x20` or the WorldObject-owned
state's `+0x40` as position. Additional vtable getters `+0x0C/+0x10/+0x14/
+0x18` have their exact source branches in the report. This stage does not
assign an unmeasured world-axis convention to those returned records.
Pair eligibility at `0x005927D0` is `body+0x1B == 0`; the constructors also
write `+0x1D`. These gates must remain separate from `+0x60`. Slots
`+0x54/+0x68` both call the zero-return function `0x00575940`; their semantic
names are not established here, so neither is named a mobility switch.

Base serialization `0x004688E0` exposes `+0x60`, and the WorldObject body
has a pose-write method at `0x005929C0`. Those paths do not show which
event0 instance changes the gate or supplies a rigid descriptor. Shape-derived
face construction in the owning WorldObject path is likewise not evidence
of dynamic rigid-body construction.

## Bounded asset search and the missing instance

The separate loader `0x00592A50` recognizes `0x00034300/0x00034301`, stores
payload pointers at `0x007344C0/0x007344B8`, and derives counts with strides
68 and 180 respectively. Its existence is not proof that these arrays feed
the BB factory or that a current race loads them.

At `f5b08a3`, on 2026-10-02, `world_body_trace.py --census` over this
installation parsed 96 nonempty Global/Tracks/FrontEnd files completely
and found no such arrays. The command reports the current census and the
identity of every inspected file. This bounded absence excludes using one
of these arrays from that corpus as an identified source record. It does
not exclude runtime-generated objects, other folders or other asset sources,
and does not establish that event0 contains no kind-4 body.

The named pylon in [PR272](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/272)
belongs to the distinct kind-2 VirginSmokeable to kind-1 Smokeable path. It
cannot fill this missing kind-4 identity. PR272 at `b3474e7` also required
the loader correction in
[review 6814](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/272#issuecomment-6814):
definitions append at the tail; forward lookup selects its first match.
The shape lookup uses definition `+0x80` as hash and `+0xB0` as an additional
pivot/offset argument. Preserve all duplicate records. The actual selected
pointer after full runtime load/unload history remains unmeasured; do not
hard-code a duplicate's mass from its name or local array order.

## Proposed shared body/contact boundary

This is the coordination contract from
[#259 comment 6782](https://git.accad.pro/murzintsevav/nfsu-switch/issues/259#issuecomment-6782),
refined by the gate/pointer distinction above. It is not an enabled C++ API
or a recovered retail scheduler.

- Instance identity includes input file/region, source definition record,
  placement/scenery offset or index, and runtime body identity when captured.
  Mesh/type names are labels; duplicate hashes remain distinct records.
- A descriptor carries verified body kind, shape reader/data, source transform,
  backing gate and actual descriptor selection. Mass/inertia, units and each
  evidence status are explicit. Unavailable data cannot silently become valid
  zero inverse mass, an immovable body, a default hull or a guessed mass.
- A contact identifies both bodies, kinds/types, witnesses, normal/separation,
  current-hit impulse and observed body-state transition. Each field carries
  traced/measured/approximate/unavailable status. Accumulated impulse is a
  different field: the car state-1 gate in [COLLISION_CLASSES.md](COLLISION_CLASSES.md)
  consumes the current hit, not the sum of earlier hits.
- Clock, phase and pair order state their observation source. A port 120 Hz
  tick cannot substitute for #256's missing retail collision rate/order.
- #259 owns shared body/contact initialization and response. #270 owns prop
  activation/break/lifetime/streaming/reset transitions and their visible,
  effect and audio consequences. Kind 1/2 and kind 4 remain distinct; lifecycle
  code consumes shared contact evidence and does not create a second solver.
- #268 owns the exact event0 setup and route authority; #258 owns multiple
  actors. Generic event1 or tuned replay settings cannot establish event0.

## Remaining acceptance and next evidence

A named event0-route kind-4 body is still required. Capture its input/source
record, WorldObject and body pointers, type/hash, shape, `+0x60/+0x68/+0x6C`,
descriptor mass/inertia and gate writes. Include an identified immovable
case and, if present, a backed case. Record actual runtime load selection,
clock, dispatcher order, witnesses, contact arm and pre/post pose, velocities,
impulse and car hard-hit state. [#256](https://git.accad.pro/murzintsevav/nfsu-switch/issues/256)
owns the remaining runtime scheduling/contact prerequisite; static creation
or vtable evidence alone cannot fill it.

After the verified subset supports implementation, reproduce the same
controlled contact and no-contact regions on desktop, Switch and the original
game, preserving existing kind-3 static telemetry. Run default and Release
C++ retail CTest with zero skips and the applicable Switch gates. Those gates
are unrun here because this stage changes only docs and a Python research
tool. No production simulation, event0 completion or hardware pass is claimed.
The owner handheld baseline remains `--scene-height 540 --gpu-boost on`,
a 960x540 scene and requested GPU+20percent; measured clocks and fixed-size
diagnostics must be recorded separately. This partial stage leaves #259 open.

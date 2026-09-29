# Track frame budget research (#212)

Research baseline: `7dad0c4800d24adbcf440206aadf51369965bbb2` (origin/main
when this worktree was created), 2026-09-27. Worker: GPT-6 Astra, Codex
session 7. This document and its desktop probe change no race behavior,
shader, simulation pin, draw order, visibility policy or reference frame.
The desktop target is below the Switch early return in `CMakeLists.txt:79`
and is added at `CMakeLists.txt:738`.

**Decision:** retain baseline `7dad0c4`'s ordered visible geometry initially; move
immutable validation, resource tables, mesh adjacency, instance/material
constants and hardware command construction to load time. The evidence
supports that direction much more strongly than reducing visible scenery.
CPU savings below are measured on desktop or explicitly projected; the
16.67 ms console result and effects budget remain acceptance measurements,
not a result claimed by this research. See the operation comparison and
prioritized plan below.

## Evidence and reproduction

Evidence labels used throughout:

- **R:** Capstone 5.0.7 observations of the supplied `speed.exe`, 3178496
  bytes, SHA-256
  `c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7`.
  Addresses are image VAs. The script parses the PE sections instead of
  assuming a mapping. Executable bytes and decoded effects are never
  committed. The command below also reports effect technique/pass counts
  by lexical inspection after XOR 0xAA and comment removal; these are not
  runtime technique selections.
- **P:** this repository at the baseline above, cited as file:line. The
  new probe is cited at this PR's version. These references intentionally
  do not describe another worker's unmerged renderer.
- **M:** historical desktop runs `profile-final-{1,2,3}`, probe `32973d9`,
  race/library baseline `7dad0c4`; **M2:** review-fix runs `profile-r2-{1,2,3}`
  with this PR's revised probe and the same `7dad0c4` race/library code.
  Each run has one
  warmup and 120 identical-pose frames at each of five replay ticks. Wall
  time, Release GCC 13.2.0, Windows, Intel Core i7-12700K. CPU identification
  command: `Get-ItemProperty 'HKLM:/HARDWARE/DESCRIPTION/System/CentralProcessor/0' | Select-Object ProcessorNameString`.
  Other sessions were active; no affinity or frequency control was applied.
  M's frame stages remain historical context. Only M2's like-for-like
  isolated comparisons support the priority-1 and list-saving estimates.
- **C:** coordinator's console photographs, not new measurements by this
  worker: [PR #206 comment 4593](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/206#issuecomment-4593),
  NRO head `547fd89d2c29305d983e1d1b3e7cb1a8ac435e00`, launched with
  `nxlink ... -- sdmc:/nfsu --live`. This is a historical observation, not
  a reproducible camera pose; the complete original nxlink command and raw
  timing series were not in the comment.
- **Decision:** [#55 comment 4717](https://git.accad.pro/murzintsevav/nfsu-switch/issues/55#issuecomment-4717)
  requires every driving frame <=16.67 ms with demo effects and permits
  about 15 s loads. Comments
  [4725](https://git.accad.pro/murzintsevav/nfsu-switch/issues/55#issuecomment-4725)
  and [4727](https://git.accad.pro/murzintsevav/nfsu-switch/issues/55#issuecomment-4727)
  give the owner's PC/Xbox performance reference. This study does not
  independently measure a Pentium III or an original Xbox.

Run from the worktree root in PowerShell; all paths and command literals
below are ASCII. No download or installation was performed. Python and
Capstone were already installed. Outputs refuse an existing directory and
must be outside `E:/Games`. The wrapper hashes **all 476 retail files**,
including relative names, sizes and mtimes, before and in a `finally` block
after every invocation. It checks byte equality even after a failed run
(`tools/frame_budget_evidence.py:14`, `:109`). No retail executable was
launched; all retail accesses were read-only parsing/profiling.

```powershell
$retail = 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground'
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target nfsu_frame_budget_probe nfsu_race_run_tests -j 4
$env:NoDefaultCurrentDirectoryInExePath = '1'
python tools/frame_budget_evidence.py $retail out/profile-r2-1 run -- ./build-release/nfsu_frame_budget_probe.exe $retail tests/recordings/event1.txt
python tools/frame_budget_evidence.py $retail out/profile-r2-2 run -- ./build-release/nfsu_frame_budget_probe.exe $retail tests/recordings/event1.txt
```

The third run was also the smoke test from the actual shared workspace's
non-ASCII current directory. It wrote only to this worker's ASCII worktree:

```powershell
python C:/Users/horra/AppData/Local/Temp/nfsu-212-codex-s7-r2/tools/frame_budget_evidence.py 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' C:/Users/horra/AppData/Local/Temp/nfsu-212-codex-s7-r2/out/profile-r2-3 run -- C:/Users/horra/AppData/Local/Temp/nfsu-212-codex-s7-r2/build-release/nfsu_frame_budget_probe.exe 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' C:/Users/horra/AppData/Local/Temp/nfsu-212-codex-s7-r2/tests/recordings/event1.txt
```

R's consolidated command (earlier exploratory ranges are not needed to
reproduce the conclusions):

```powershell
python tools/frame_budget_evidence.py $retail out/disasm-6 disasm 0x408f00:0x409090 0x446620:0x446724 0x446730:0x446891 0x40c690:0x40c830 0x40c910:0x40ca92 0x40ed50:0x40f0d9 0x40f1e0:0x40f2dd 0x40f300:0x40f3c7 0x40f3d0:0x40f6dc 0x410400:0x41054d 0x4465b0:0x446612 0x446400:0x4464fa 0x446140:0x4462f0 0x445ec0:0x44613a 0x445e10:0x445eb4 0x40db20:0x40dc58 0x409580:0x409ab0 0x401050:0x401092 0x445580:0x445650
```

The script's `E8_CANDIDATES` are byte-search candidates, not automatically
proven call edges. The edges cited below were decoded in the containing
functions. Do not interpret padding or bytes after a function return as
another recovered function.

Run the following in Bash from the worktree, including every exploratory
run's manifests in the original evidence archive and M2 in the revision
worktree. All comparisons returned 0. `disasm-5` failed its first
ASCII decoding attempt on an effect's non-ASCII byte; its finally manifest
also matched. The final script decodes bytes losslessly as Latin-1 in memory
and emits only technique names, counts and hashes, not shader source.

```bash
for d in out/disasm-{1,2,3,4,5,6} out/profile-{1,2,3} out/profile-final-{1,2,3}; do
  cmp "$d/before.json" "$d/after.json" || exit 1
done
# Review-fix worktree; original runs are in the original evidence archive.
for d in out/profile-r2-{1,2,3}; do
  cmp "$d/before.json" "$d/after.json" || exit 1
done
```

## 1. The original track frame

### It is D3D9 in this supplied executable

R's PE import walk finds `d3d9.dll!Direct3DCreate9`. There is a `d3d8.dll`
string too, but no `Direct3DCreate8` import. The decoded device calls agree
with D3D9: `SetStreamSource` at `0x0040EEDC` uses vtable +0x190,
`SetIndices` at `0x0040EF08` +0x1A0, and `DrawIndexedPrimitive` at
`0x0040EF82` +0x148. Thus a D3D8-only tracer would not measure this path.
This also agrees with `docs/MATERIALS.md:325`; the historical hardware
reference is not evidence that this executable exclusively uses fixed
function shading.

### Recovered call chain and culling

| Per-frame work | R: decoded instructions and interpretation |
| --- | --- |
| Assemble render contexts | `0x004095A9` calls `0x00408F00`; that function adds active contexts and masks at `0x00408F50..0x00409083`, then calls `0x00446620` at `0x00409086`. This is inside the render function beginning at `0x00409580`. |
| Build visible scenery for each context | `0x004466E0..0x0044671F` iterates contexts with stride 0xD0; `0x00446703` calls `0x004465B0`. Context +0x88/+0x8C delimit its output list. |
| Select sections | `0x004465C1` passes capacity 128 to `0x00446400`; zone lookup at `0x0044641F` selects the zone list. `0x00446473..0x004464CD` appends resident 2600..2699 and zone section IDs. No-zone path `0x0044642C..0x00446469` walks loaded sections whose ID modulo 100 is <10. |
| Only resident trees | `0x004465E0..0x004465FD` checks the section table at 0x0072A008, checks section +0x28 for a tree, then calls `0x00446140`. |
| Hierarchical six-plane culling | `0x00446140` walks the tree with a local stack; `0x00446181` propagates fully-inside classification without another box test. `0x0040DB20` classifies the box; `0x0040DC3D..0x0040DC4E` returns inside/intersecting/outside. Child and instance traversal follow `0x00446200..0x004462A0`. No per-frame tree-integrity/containment audit appears in this walk. |
| Instance gates and LOD | `0x00445EDA..0x00445EEB`: `(flags ^ 0x60) & mask`; `0x00445EFE..0x00445F2E`: instance box unless parent inside; `0x00445F34..0x00445F55`: projected size from radius+4, reject level <=1. `0x00445FB7..0x00445FDC` chooses the first resolved slot 0..3 whose threshold <=level. Mask bits 0x0800/0x1000 select slots 4/5 at `0x00445F61..0x00445FA0`. |
| Temporary visible record and matrix | `0x004460A9` requests one 64-byte matrix from the bump allocator `0x00401050`; `0x004460BB` calls `0x00445580` to widen the int16 rotation. `0x004460C0..0x00446108` fills translation and applies pass-specific adjustments. `0x00446119..0x00446130` appends a 12-byte record (solid/code, matrix pointer, instance pointer). Flag 0x200 follows the no-matrix record path at `0x00446074..0x0044609D`. |
| Consume records into material queue | `0x00446730` finds the context, walks records with stride 12 (`0x0044687D`), and calls `0x0040C910` at `0x0044685E`. Thus the queue documented previously for cars is also reached by scenery. |
| Additional material-group cull | `0x0040C9D0..0x0040C9F6` walks 0x3C-byte material groups and optionally box-tests each group, bypassed by command flag 4 or material flag 1. An inside node sets flag 4 at `0x004467B8..0x004467C2`. Texture rejection at `0x0040CA03..0x0040CA0C` can skip another group. |
| Queue then flush | `0x0040CA54` appends via `0x0040F1E0`. The main-view path calls `0x00446730` at `0x0040997E`, then flushes at `0x00409983` via `0x0040ED50`. Other conditional scenery/flush pairs appear at `0x004095F5/0x00409623`, `0x00409714/0x00409724`, `0x00409792/0x004097A4`; one game's whole frame is not necessarily one scenery pass. |

The size function at `0x00445E10` uses distance minus radius and context
+0xC0's projection scale, not merely a constant far distance. The runtime
projection scale, active context masks, exact selected zone/camera and
effect configuration in a representative retail 1003 run remain **open**.
Static disassembly does not supply their frame-specific values. Existing
`docs/TRACK_VISIBILITY.md:410` and `:494` document those gates in detail;
R confirms their instructions.

### Grouping, buffer lifetime and state

R establishes **row, effect, texture-slot FIFO, then material command**
order, not a global solid order. `0x0040F225..0x0040F246` computes row
`8*(texture[+0x54]!=0) + signed(texture[+0x51])`, with seven effect columns.
`0x0040F1E9..0x0040F1F5` reuses a texture's open slot when its effect
matches, even across intervening submissions; `0x0040F24D..0x0040F258`
links new slots at the tail. `0x0040F0AC..0x0040F0CF` drains seven effects
inside sixteen rows. `0x0040F079` clears the open slot when drained. The
batching scope is a flush, not the whole displayed frame. This saves state
calls; it does **not** combine the groups into one indexed draw.

| Lifetime / frequency | R evidence |
| --- | --- |
| Load-time geometry | Chunk parser `0x0040C690`: ID 0x134B03 creates an index buffer (+0x6C) at `0x0040C751`, locks at `0x0040C764`, copies bytes at `0x0040C776/7D`, unlocks at `0x0040C80D`. ID 0x134B01 creates VB (+0x68) at `0x0040C7D9`, locks at `0x0040C7EC`, copies at `0x0040C7FE/805`. Both creation paths pass usage 0, pool 1 (managed); IB format 0x65 (INDEX16), VB FVF 0. |
| Per-frame metadata | The 12-byte visibility entries and 64-byte widened matrices above; append allocates a pool command at `0x0040F268`, fills pointers/flags `0x0040F271..0x0040F2A9`. Pool construction uses 0x2C-byte commands at `0x0040F36E` (see note below). The flush returns entries to the free list at `0x0040F040..0x0040F06A`. |
| Per effect change | `0x0040ED88..0x0040EDE3` compares the effect pointer, ends the previous effect, binds its vertex declaration (+0x15C) at `0x0040EDC5` and begins the next effect. |
| Per texture/effect change | `0x0040EE14` calls `0x0040F3D0`, whose texture/effect cache at `0x0040F3E9..0x0040F3F7` can skip the setup. Texture binding is via effect parameter (+0xD0) or device SetTexture (+0x104). `0x0040F44C` configures alpha test; `0x0040F451..0x0040F543` selects blend/depth behavior. Non-car blended groups turn depth writes off at `0x0040F543`. |
| Per queued group | `0x0040EE49` calls matrix setup `0x00410400`. Its matrix pointer/effect/view cache at `0x00410437..0x0041044A` skips unchanged transforms. View/projection and world uploads at `0x0041047E/492/50D` or effect matrices at `0x004104F0/541` are conditional. Optional lighting/material work is gated at `0x0040EE57..0x0040EE85`. |
| Per changed VB/IB | Compare against cached globals at `0x0040EEBC` and `0x0040EEF7`; only changes call `SetStreamSource` / `SetIndices`. Buffer addresses reference the load-time handles. No Lock/copy of track geometry occurs in this flush branch. This is not a claim about untraced driver internals or every dynamic effect. |
| Per group per effect pass | `0x0040EF34..0x0040EF91` loops the effect pass count, applies a pass at `0x0040EF47`, and calls DrawIndexedPrimitive at `0x0040EF82`: triangle list, base vertex 0, start index derived from the group's relocated index pointer, primitive count `group[+0x0C]/3`. |

The pool's 4096 initial commands and 1024 initial texture slots are not
per-frame measurements or hard draw limits (`0x0040F31D..0x0040F3C3`;
`docs/MATERIALS.md:139` explains growth). Likewise the internal increment
at `0x0040EF93` is **outside** the effect-pass loop: reading that counter
alone would undercount API calls for multipass effects. Its neighboring
triangle accumulator uses solid vertex count (`0x0040EF9E`), not the
primitive argument; count API primitives from `0x0040EF5E/65` instead.

The flush also has a `DrawPrimitiveUP` branch at
`0x0040EFB4..0x0040F03D` (device +0x14C) when command +0x0C is nonzero.
Scenery append clears that field at `0x0040F29C`; the indexed scenery
formula below applies to that path, not every possible flush command (R).

## 2. Original counts: what is known and what is not

No API trace was captured. `Get-Command apitrace,renderdoccmd -ErrorAction
SilentlyContinue` and the standard Program Files directory check found
neither tool; this is not an exhaustive inventory of every disk. No tool
was downloaded or installed and the retail executable was not launched.
A future trace requires an approved D3D9-capable tool and an external
working/output directory or disposable copy; do not drop a proxy DLL,
configuration, save, log or capture into the read-only install. Approval
for a download was not assumed. The assignment explicitly permits the
following static/data fallback.

For scenery context c, accepted instance i, surviving material group g,
and selected effect pass count p(c,i,g), R gives the exact counting rule:

```text
API draws = sum_c sum_i sum_g p(c,i,g)
submitted triangles = sum_c sum_i sum_g p(c,i,g) * index_count(i,g)/3
VB binds = transitions of VB handle in the actual flushed command order
IB binds = transitions of IB handle in the actual flushed command order
texture setup = texture/effect cache misses in slot order
effect setup = effect pointer transitions across the bucket walk
```

These are submitted triangles, not post-clip primitives or shaded pixels.
R's `world.efx` census finds one pass in `world` and `world_fixed`;
`worldreflect` has a two-pass alternative and the headlight effects have
one- and two-pass alternatives. `filter` has four lexical passes. These
counts do not establish which techniques/passes ran in a frame. The CPU
call paths also include multiple contexts. Therefore multiplying the
port's draws by one and calling that a **measured retail total** would be
false (R addresses and FX census above).

Historical measurement at `7dad0c4`, from track 1003 with the M probe at
`32973d9` (`tools/frame_budget_probe.cpp:125..149`), at the **port replay**
views below: every sampled row had 94 car draws / 6137 car triangles,
far=250 and 5098 uploaded resources. Those values matched in all three
runs at that revision. They are not current car counts: #223 suppresses
empty decal draws. `nfsu_draw_commands_retail` prints current car draw and
triangle counts; `nfsu_race_run_tests` checks the current race draw count.

| Replay tick | Track draws | Track triangles | Visible / unplaced instances | Drawn sections | Unique meshes | Texture identities including fallback | Texture runs | VB / IB runs |
| ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 1120 | 59348 | 382 / 2 | 16 | 90 | 176 | 1096 | 236 / 236 |
| 6000 | 966 | 50885 | 304 / 3 | 17 | 76 | 149 | 950 | 190 / 190 |
| 11344 | 1099 | 51499 | 403 / 4 | 15 | 96 | 188 | 1050 | 248 / 248 |
| 24300 | 1123 | 60160 | 381 / 2 | 17 | 89 | 162 | 1100 | 239 / 239 |
| 36510 | 1559 | 71008 | 639 / 2 | 17 | 126 | 229 | 1454 | 374 / 374 |

**Conditional reconstruction, not a retail measurement:** if retail draws
exactly tick 0's selected placed slot-0 geometry, once, in a one-pass world
technique, before any additional group rejection, the formula yields
1120 indexed draws / 59348 triangles. Keeping that geometry but admitting
only group rejection gives 0..1120 and 0..59348. A single effect with all
those texture identities resolved could group it into 176 texture buckets;
actual unresolved textures and multiple effects prevent treating 176 as a
retail state-change count. Tick 36510's corresponding conditional ceiling
is 1559 / 71008. Those ceilings do not bound the complete original frame,
other LODs, extra passes, cars, sky or particles.

**Is our draw count the same? Not established, and the selection rules
differ.** `src/race_gpu_scene.cpp:175` calls default culling, not the
view's recovered zone list. `include/nfsu/track_visibility.h:286..291` defaults
mask=0 and screen-size gating off; `include/nfsu/track_draw_list.h:259`
chooses slot 0. `src/track_draw_list.cpp:421` emits every drawable group of
the selected mesh, without retail's material box test. Unplaced counts in
M identify missing bindings too. Whole-frame retail draw/state/triangle
numbers remain **open**, specifically because runtime masks, zones,
projection scale, camera and effects are missing. A lower fixed distance
or indiscriminate section omission is not a substitute for recovering them.

## 3. Where the port spends CPU time

### Correct the denominator first

C photo B reports 23.65 ms CULL/RECORD / 1278 track draws = **18.505 us**,
or /1372 combined draws = **17.238 us**. Photo A is 21.76 /800 = **27.200
us per track draw**, with 94 car draws still included in the numerator.
Neither is an isolated per-draw measurement or a linear cost model. CULL/
RECORD includes culling, car work, allocations and full-scene resource
work; it excludes `DekoRenderer::record_frame`. That call is inside C's
11.64 ms DRAW interval together with submit/flush/fence wait
(`switch/race_main.cpp:164..181`, `switch/deko_renderer.cpp:420..447`).

### Desktop measured stages

Historical M at `7dad0c4`, run `profile-final-2`, mean ms. Parent columns are disjoint boundaries
already provided by `RaceFrameProfile` (`src/race_gpu_scene.cpp:147..196`).
Their sum excludes caller destruction/assignment overhead. The isolated
measurements below are additional experiments, not extra frame stages to
add to this table.

| Tick | Cull + camera/car placement | List | Car recording | Cached track metadata | Combine + checks | Sum |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 0.313175 | 0.413467 | 0.053858 | 0.327192 | 0.583292 | 1.690984 |
| 6000 | 0.303467 | 0.352025 | 0.056767 | 0.314792 | 0.571508 | 1.598559 |
| 11344 | 0.347117 | 0.447550 | 0.060150 | 0.346192 | 0.664108 | 1.865117 |
| 24300 | 0.290308 | 0.406392 | 0.051800 | 0.299167 | 0.565300 | 1.612967 |
| 36510 | 0.295625 | 0.563492 | 0.051442 | 0.356533 | 0.599917 | 1.867009 |

Host variance matters: tick 0 sums in final runs 1/2/3 are
3.148925 / 1.690984 / 1.634567 ms. Tick 36510 sums are
3.300700 / 1.867009 / 2.029374 ms. Do not use the quickest sample as a
console prediction. Earlier exploratory runs are retained but excluded
from these tables; final code filters drawable adjacency and has a compiler
memory barrier after uniform copies. M prints the sample count and requires
nonempty successful output, exact ordered selection pairs at all five
views, and the complete 48336-step replay result pins
(`tools/frame_budget_probe.cpp:91..104`, `:171..179` at probe `32973d9`).

| Historical M at `7dad0c4`: isolated operation, tick 0 | Run 1 ms | Run 2 ms | Run 3 ms | Meaning |
| --- | ---: | ---: | ---: | --- |
| Cull with containment check disabled | 0.106370 | 0.063021 | 0.069332 | Still runs tree preparation; ordered visible pairs equal checked cull |
| Prebuilt mesh adjacency -> ordered pairs | 0.005503 | 0.003504 | 0.003449 | Reuses vector capacity; no full cost/uniqueness audit; every pair compared afterward |
| Existing list validator | 0.357881 | 0.264164 | 0.274999 | Included in parent List |
| Command validator | 0.254200 | 0.155149 | 0.146520 | Full combined resource/draw/index check |
| Execution planner | 0.395857 | 0.242912 | 0.234418 | Includes command validator; not additive with it |
| Backend update validation | 0.438478 | 0.240386 | 0.230453 | Includes execution planner and resource comparison |
| Uniform memcpy only | 0.004686 | 0.003943 | 0.003908 | 112 + 1214*(64+80) = 174928 payload bytes into 256-byte slots in host RAM |
| Copy/destruct combined upload table | 0.300272 | 0.228273 | 0.201673 | Metadata + strings, not GPU texture/geometry payload |

The update-validation probe passes the same upload table on both sides;
the renderer compares distinct tables (`switch/deko_renderer.cpp:327`).
Its resource-comparison/cache cost can therefore be lower. It is not an
exact measurement of backend update. The isolated means can invert nested costs (run 2 planner vs update) due
to separate sampling/cache/noise; subtracting those two is not a reliable
substage timer. The uniform microbenchmark uses cached host RAM, not Switch
GPU-mapped memory, and cannot prove Switch stores cost 0.004 ms. Command
recording and queue submission are **not executable on this desktop target**;
no substitute API or empty mock is timed as deko3d. Their exact console
split, and therefore the operation-by-operation allocation of the claimed
17 us, remains open pending the request below. Sources:
`tools/frame_budget_probe.cpp:105..124` at `32973d9`,
`switch/deko_renderer.cpp:326..400` at `7dad0c4`.

### Operation-by-operation comparison

| Operation | Original, R | Port, P at `7dad0c4` | Implication supported by M at `7dad0c4` or explicitly M2 |
| --- | --- | --- | --- |
| Tree topology and containment | Walks resident tree at `0x00446140`; trusts topology/bounds in that loop | `src/track_visibility.cpp:793` audits containment, then `:815` calls `prepare_tree`; `:90` allocates parent/seen/order/stack and recomputes subtree counts | Use M2's isolated checked-cull minus isolated trusted-cull below; topology work still remains. Do not subtract trusted cull from the parent camera/placement stage |
| Mesh -> group adjacency | Material pointer/count/stride already in loaded solid (`0x0040C9A7`, `0x0040C9D0`, `0x0040CA6C`) | `src/track_draw_list.cpp:364` rebuilds counts/prefix sums/order for all meshes via `:93`; `:447..448` builds sets; `:465` invokes another set-based validator at `:949` | Use M2's isolated full-list vs isolated ordered-handle selection below; this prototype is not a production validation replacement |
| Command representation | Pool entries carry pointers and flags (`0x0040F271..0x0040F2A9`) | `src/race_gpu_scene.cpp:118` copies the upload table; `:132..139` copies a template, formats a name and copies its transform for every selected draw | Cached-track stage 0.327192 ms, even after #206. Combined table copy alone 0.228273 ms in an isolated test, overlapping this kind of work |
| Combine / resource planning | Flush follows cached handles; no all-resource table reconstruction or CPU index walk in the traced indexed branch | `src/race_gpu_scene.cpp:18` validates track and car, `:28..45` copies/rebases car resources/names, `:46` plans execution (validates combined again); `src/draw_execution.cpp:56..59` replans texture copies | Combine costs 0.583292 ms; validation accounts for a material fraction. This is not all memcpy |
| Index safety | D3D API/driver validation not traced; game-side loop uses index pointer/count without scanning indices | `src/draw_commands.cpp:231` scans every index on each validation. Track validated separately, combined, then again in renderer update (`src/draw_execution.cpp:71..88`, planner calls validation at `:45`) | 59348*3*3 = 534132 track index visits at tick 0 across these three validations, although geometry never changes. Each combined validation visits 196455 indices |
| Uniforms | Matrix conversion once per visible instance, cached matrix/effect/view setup during flush (`0x004460BB`, `0x00410437`) | Renderer update validates all 5098 resources and command payloads, then copies 64+80 bytes per draw (`switch/deko_renderer.cpp:327..339`) | C's 4.01 ms UPLOAD/CONSTANTS includes validation; it is not a measured 4 ms memory-copy cost |
| State / hardware commands | Effect, texture, matrix, VB and IB caches above | `switch/deko_renderer.cpp:358..400` reconstructs and binds shader, uniforms, raster, color, blend, depth, attributes, VB layout, VB and IB for every draw | At tick 0: 1120 draws, 1091 textured. 13*1120+1091 = 15651 explicit track state/binding calls, plus 1120 drawIndexed calls; library-internal GPU words not counted |
| Submit / wait | No hardware timing from retail | `switch/deko_renderer.cpp:124..138` finishes list, signals fence, submits, flushes and waits | C's DRAW=11.64 ms cannot be called GPU scene time or assumed wholly hideable |

There are only two **port at `7dad0c4`** track program/pipeline keys in M, with
59/59/29/67/23 consecutive runs at the five poses. These do not represent
the original's richer material states. Source: `tools/frame_budget_probe.cpp:29..32`
defines the key; P `src/draw_commands.cpp:304..305` refuses retail track material
mode. State memoization should compare actual complete backend state,
including texture, buffers and uniform identity separately, not collapse
the original effects into these two keys.

## 4. Prioritized design for 60 fps

This is a proposed architecture and falsifiable budget, not an implemented
performance result. Keep ordered draw identity, complete geometry, distance,
transparency/equal-depth behavior and deterministic simulation unchanged in
the first changes. No lower-frame-rate option is proposed. The 15 s load
allowance is total, not an additional 15 s on top of C's 6507 ms load.

### Ownership and frame structure

At load, produce a privately owned, immutable `TrackRenderPackage`:
validated tree traversal metadata, section/instance handles, mesh adjacency,
the final combined GPU resource namespace, prevalidated ranges and backend
state, static instance matrices, shared material records, and immutable
deko3d lists. Keep vertices/indices shared as they are now. Store diagnostic
names once; runtime selection uses integer handles. Validate raw data fully
before publishing the package; retain the existing checked reference path
for differential tests. Untrusted new streamed packages must pass the same
checks before publication. An epoch/generation and last-use fence prevent
stale handles or freeing data still referenced by command lists. This design
extends the existing private owner/cache (`src/race_gpu_scene.cpp:49..85`,
`:94..110`) rather than weakening its lifetime contract.

Per frame: publish camera + deterministic car snapshot, traverse trusted
trees, append visible instance/list handles, record a short root list with
frame-slot camera constants, execute the selected immutable lists in the
same order, append car/effects/HUD, submit, and present. Wait only before
reusing an in-flight slot. Keep per-frame command memory separate from the
backing memory of immutable lists; clearing the root must not invalidate
children. Child lists inherit only the explicitly specified frame constants
and render target, and establish all other state they need. Use static
instance/material GPU addresses so children are independent of camera slot.
This is a design requirement motivated by `7dad0c4`'s borrowed pointers and
single-slot overwrite (`switch/deko_renderer.cpp:331..342`).

Start with **one child list per visible instance**, containing its groups
in `7dad0c4` port order. A whole-section list would draw previously culled
instances and could change blended/equal-depth order; it is not a free
equivalent optimization. Coarse lists may be selected only where the exact
required sequence is identical, or after separately measuring and approving
the geometry/order change. At tick 0, 380 drawn instances replace 1120 draw
recordings; at tick 36510, 637 replace 1559 (M). This reduces CPU list
construction, not GPU draw calls. Do not remove visibility selection to
make a benchmark faster.

### Savings and ordering

M2 measures both sides with the same isolated `measure()` harness: one
warmup, 120 calls, same immutable region and precomputed frustum/visible
set (`tools/frame_budget_probe.cpp:23..27`, `:91..111`). Checked and trusted
cull both construct/destroy their result; only `verify_containment` differs.
The full list constructs/checks/destroys its output; adjacency selection
reuses a preallocated vector, as the proposed design would. Both outputs'
ordered pairs match the reference. This measures the proposed list scope,
not a claim that the two implementations perform identical validation.
Camera construction, car placement and in-frame cache/timer differences
are excluded from **both** cull operands. Never subtract an isolated timer
from a `RaceFrameProfile` parent to claim a saving.

| M2 at `7dad0c4`, tick 0, mean ms | Run 1 | Run 2 | Run 3 |
| --- | ---: | ---: | ---: |
| Isolated checked cull | 0.225142 | 0.226547 | 0.217406 |
| Isolated trusted cull | 0.063832 | 0.062533 | 0.062277 |
| Checked - trusted | 0.161310 | 0.164014 | 0.155129 |
| Isolated full list | 0.320480 | 0.341421 | 0.342166 |
| Isolated adjacency selection | 0.003338 | 0.003430 | 0.003301 |
| Full list - selection | 0.317142 | 0.337991 | 0.338865 |
| Same run's full preparation parent sum | 1.421350 | 1.563400 | 1.485067 |
| K = C photo B 23.65 / parent sum | 16.639111 | 15.127287 | 15.925207 |
| Projected containment saving, K * isolated difference | 2.684055 | 2.481087 | 2.470462 |
| Projected list saving, K * isolated difference | 5.276961 | 5.112887 | 5.396495 |

Exact reproduction commands are `profile-r2-{1,2,3}` above. Parent sums
come from each output's tick-0 `FRAME_MS` row; both operands come from that
same run's tick-0 `ISOLATED_MS` row. For example, run 2:
`(0.226547-0.062533)*(23.65/1.563400)=2.481087` ms.
These replace the invalid 3.50 ms containment and 5.73 ms list projections
in the first review. Use run 2 as a stated example, not a selected fastest
run; all three projections are shown. The views still differ (1120 desktop
vs 1278 photo-B track draws), CPUs differ and the host is shared. K is a
prioritization model, not a measured scaling law or confidence interval.
Every console estimate must be replaced by direct A/B measurements.
Topology caching has no extra measured credit. Do not add overlapping
elimination ceilings or infer a complete optimized frame from these pairs.

| Priority | Concrete change | Measurement-based expected saving / limit | Required console check |
| --- | --- | --- | --- |
| 0 | Take the new baseline after #211; keep direct GPU presentation and frame-slot overlap owned by that worker. Add the timers below before choosing further GPU work. | C photo B has 8.07+12.32=20.39 ms in transfer/copy and HUD/present. That is the old path's removable envelope, not zero-cost replacement. Even if the old 11.64 ms DRAW were entirely GPU service, overlap cannot save another 11.64 after taking max(CPU,GPU). | Report actual CPU record/submit/wait/acquire, GPU timestamps and full frame; zero whole-frame readback in timed normal play. No savings attributed to this PR. |
| 1 | Validate containment/topology once, cache TreePrep and use bounded reusable traversal storage. Keep the same six-plane instance culler. | M2 run 2: isolated 0.226547-0.062533=0.164014 ms saved, projection **2.48 ms**; all three projections 2.47..2.68 ms. Only containment is measured; topology caching may save more but receives no extra credit. | Ordered visible IDs and node/instance results match; report cull mean/p95/max, allocations, and one-time load cost. Never suppress a containment failure. |
| 2 | Load-time mesh adjacency and resource namespace; reusable dense visible handles, no per-frame sets/diagnostic names/table copies. Publish prevalidated draw/instance handles instead of reconstructing RecordedCommands. | M2 run 2 isolated list 0.341421-0.003430=0.337991 ms saved, projection **5.11 ms**; all three projections 5.11..5.40 ms. Same-run cached-track 0.288808 + combine 0.549567 is a separate **0.838375 ms elimination ceiling**, K projection 12.68 ms, before required checked-handle/car-append work. Do not add resource-copy or validator microbenchmarks again. | Compare ordered commands/ranges/transforms and resource references over full replay. Show list/select, combine, static index visits and allocations on results pages; static index visits should become zero after load. Handle, generation and capacity checks stay. |
| 3 | Bake static instance and material GPU constants; frame slot writes camera/car and genuinely animated material values only. Use package validation instead of full immutable update validation. | At tick 0, remove 1120*144=161280 of 174928 payload bytes (92.2%). M update validation is 0.240386 ms vs host copy 0.003943 ms. C's combined 4.01 ms is the **entire ceiling**; applying that host fraction projects about 3.95 ms for validation removal alone, but GPU-mapped store cost is unmeasured. Initial budget: save 3..4 ms, verify before credit. | Split validation and memcpy timers; measure actual bytes written/flushed, camera/car-only vs full writes, cache operations and slot fences; verify transforms/materials bit-for-bit. Keep animated effects out of immutable records. |
| 4 | Prerecord per-instance children, then memoize redundant state on paths still recorded dynamically. Keep lists alive until their last-use fence; bound total list memory before construction. | M at `7dad0c4` reduces CPU dispatch units 1120->380 (66.1%) or 1559->637 (59.1%). Same GPU draw count. Adjacent-only state caching at tick 0 has a conservative API-call model of <=4339 vs 15651 state calls: 2240 UBO +59 shader +1096 texture +6*59 fixed state +2*59 layout +236 VB +236 IB. About 72.3% fewer state calls, **not** 72.3% of the whole frame. Console ms estimate = measured record_ms*(0.59..0.72), pending priority 0; C at `547fd89` provides only a 0..11.64 ms envelope within DRAW. Do not add prerecord and memoization percentages. | Time only record_frame/finishList separately; count command bytes, list calls, state binds, and draws. Compare inline vs child lists at identical views. Measure GPU too: fewer CPU calls may leave the same GPU work. |
| 5 (conditional, immediately after 0) | If either mode's GPU base scene exceeds 9 ms, recover retail material-group box tests and projected-size/LOD selection alongside CPU work. Resolve the missing masks, projection scale and LOD bindings from section 1. Then batch only proven order-compatible groups; try shared geometry/per-instance data before load-baked merged visibility clusters. | M at `7dad0c4` offers 90 unique meshes vs 380 instances at tick 0, 126 vs 637 at tick 36510, and 176/229 texture identities. Reuse opportunities, not legal batch counts. GPU saving is **0 ms credited until measured**, bounded by GPU track_ms, not combined DRAW. Group/LOD fidelity may reduce submitted geometry; no numeric reduction is supported without the missing runtime inputs. Coarse culling may increase GPU work. | Measure GPU/draw/triangle/state deltas, visibility/order, bytes and load cost. CPU-only steps preserve baseline ordered geometry. Retail selection changes require a separate fidelity comparison/reference review, not silently changing pins in this PR; validate retail boundaries/LOD transitions. Never replace recovery with an arbitrary shorter far plane. |
| 6 | Only if CPU still exceeds its allocation: deterministic simulation/residency producer and renderer consumer on separate available cores, fixed-size immutable snapshots and explicit fences. | C's per-tick averages span 1.315 ms (B: 5.26/4) to **2.53 ms** (A: 12.65/5). Use 2.53 in the budget, not 1.315. Overlap can hide at most min(measured simulation+residency, render CPU), minus handoff; the historical simulation-only opportunity spans 1.315..2.53 ms at these samples, not a guaranteed saving or a division by core count. | Exact 48336-step hash stream and results; sequence/generation IDs, queue depth, handoff/wait time, latency, no render->simulation feedback, no dropped ticks. Measure tail frames, not just throughput. |

The list-selection prototype only emits pairs; it does not reproduce all
cost accounting, resource validation or a complete optimized frame. Thus
priorities 1..3's ceilings are not an end-to-end benchmark of this design.
Nevertheless their scope covers the actual repeated operations rather than
guessing that raw draw dispatch costs 17 us. Priorities 4..6 remain ordered
by the measured console bottleneck, not by assumed savings.

### Explicit acceptance budget and effects headroom

Proposed **engineering allocations**, not measured effect costs:

| Critical path component | Maximum allocation |
| --- | ---: |
| CPU simulation + residency + selection + record/submit + HUD | 8.0 ms |
| GPU base track + car + compact HUD | 9.0 ms |
| GPU effects #182..#188 aggregate | 4.0 ms |
| Remaining deadline margin against GPU 13.0 ms | 3.67 ms |

With a safe overlapped pipeline the working hypothesis is a critical path
near `max(CPU 8, GPU 9+4)` plus measured acquisition/scheduling costs, not
8+9+4; those costs must fit the remaining margin. Reserve **2.53 ms** for
one 60 Hz simulation tick (C photo A's 12.65/5, worse than B's 1.315).
Each tick already includes two 120 Hz collision steps
(`include/nfsu/race_session.h:19`); do not count 2.53 twice for a normal
60 fps frame. This leaves **5.47 ms** of the CPU allocation for residency,
selection, metadata, writes, recording, submit and HUD. A proposed 2..4 ms
preparation/dispatch allocation leaves just 1.47..3.47 ms for those other
CPU costs. These are targets, not measured optimized remainders. Neither
photo bounds the worst tick; phase A must measure per-tick maxima and
catch-up frames. The baseline GPU may exceed 9 ms: C's 11.64 ms combined
DRAW cannot settle it. If either docked or handheld GPU exceeds 9 ms,
promote priority 5's retail group/LOD recovery and GPU work immediately
after priority 0, before effects acceptance; no reduced-frame-rate exit.

Reserve the effects envelope for the actual requirements:
[#182 car shading](https://git.accad.pro/murzintsevav/nfsu-switch/issues/182),
[#183 road reflections](https://git.accad.pro/murzintsevav/nfsu-switch/issues/183),
[#184 sky/fog](https://git.accad.pro/murzintsevav/nfsu-switch/issues/184),
[#185 lights/glows](https://git.accad.pro/murzintsevav/nfsu-switch/issues/185),
[#186 smoke/sparks](https://git.accad.pro/murzintsevav/nfsu-switch/issues/186),
[#187 fullscreen effects](https://git.accad.pro/murzintsevav/nfsu-switch/issues/187),
[#188 shadow](https://git.accad.pro/murzintsevav/nfsu-switch/issues/188).
Their mechanisms are themselves research tasks. No per-effect millisecond
claim is supported yet; each implementation reports incremental CPU/GPU,
memory and worst combined-scene cost. Fullscreen passes and reflections
must share the 4 ms allowance rather than each claiming it. The final
combined-effects run, including collisions/particles and lights, is required
even if each isolated effect passes.

C reports 6507 ms load, 86659072 GPU-block bytes and a preexisting loader
heap overrun. The design gets approximately 8493 ms of extra load headroom,
not unlimited memory. Count static constants as aligned instance slots plus
deduplicated material slots (256-byte alignment), add actual prerecorded
command bytes, and multiply only genuinely per-frame allocations by the
number of in-flight slots. Reuse or retire C's `547fd89` 2220547-byte command
metadata cache only after its references are gone. No full vertex payload
duplication by default. Console reports must stay within `7dad0c4`'s explicit
stream/persistent/GPU/transient limits and address load peak separately;
this PR does not repair #204. Sources: C, `src/race_gpu_scene.cpp:86..92`,
`switch/deko_renderer.cpp:183..223`, `docs/SWITCH_PERFORMANCE.md:68`.

## 5. CONSOLE_REQUEST for the coordinator

The issue comment is the dispatch. The #55 worker adds this instrumentation
on its renderer branch; this research branch changes no NRO or race path.
The coordinator builds and runs it through the established console workflow.
Do not ask the owner to transfer or load an NRO. No measurement or replay
check may depend on nxlink stdout: it is unavailable to the coordinator
([review 4826](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/214#issuecomment-4826)).

### Phase A: first run, one replay and one scene GPU timestamp pair

Extend #211's existing persistent result screen, verified at
`a1c72dcc49d10c45298dcd25ab79e6ed2369f572`:
`switch/race_main.cpp:116` collects the display rows, `:145..185` shows the
result until +, and `:400` leaves the driving loop at FINISHED. That head
already shows replay EQUAL, result hashes and driving-frame statistics;
this request adds substage distributions and GPU timing to that screen.
It does not assume this research's older `7dad0c4` source is the renderer
under test ([PR #211](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/211)).

The worker must add the explicitly named **proposed** `--profile-frame`
switch. It is not present at either cited head. First launch, once that
worker supplies a tagged build:

```text
nxlink <tagged-nfsu_switch_race.nro> -- sdmc:/nfsu --replay --profile-frame
```

Use the bundled event1 recording, normal direct presentation, one complete
replay, initially in handheld mode. Do not enable snapshots, detailed GPU
pass pairs or A/B variants in this first run. The coordinator supplies the
NRO path; angle brackets are placeholders. Stdout may be supplementary but
is never an input to the verdict.

All requested results must remain on screen **after FINISHED**, frozen
until + exits. Extend #211 with numbered, fixed-layout pages; D-pad left/
right changes pages without restarting the run or advancing simulation.
Use at most eight data rows per page and 80 ASCII columns, split long
hashes into labeled 32-character halves, and repeat build/mode/page labels
on every page. No auto-dismiss, scrolling log, truncation or rapid cycling.
Photograph every page. Collect into bounded preallocated storage; format
only after the timed run. The required pages are:

| Page content (paginate CPU rows as needed) | Required on-screen fields |
| --- | --- |
| Identity | Full tested commit and NRO SHA-256; toolchain/deko3d version; HANDHELD/DOCKED; actual CPU/GPU/memory clocks; DIRECT; instrumentation/variant tag. Coordinator supplies build-time identity if the app cannot derive it. |
| Replay | FINISHED; `REPLAY EQUAL` (otherwise DIFFERENT/RUNNING); state-sha256 and steps-sha256 from `RaceRun`; final step 48336, lap/total ticks and times. Compare both photographed hashes with the pinned desktop result. EQUAL is the existing full-stream/result check, not equality inferred from a final car pose. |
| Active frames | Count and exclusions/reset count; frame mean/p50/p95/p99/max ms; count and fraction above the stated tolerance threshold in `RaceFrameStats`; worst frame's tick and track/car draws/triangles; maximum catch-up and tick debt. Retain exact max and worst-frame identity even if percentiles use bins; display bin width. |
| CPU stages | Per-stage mean/p95/max ms for all regions below; SIM also per 60 Hz tick; parent sums and unassigned residual. Each row explicitly names its unit and sample count. |
| GPU scene | Completed query count; mean/p95/max ms; worst GPU frame's tick and draws; dropped/invalid queries; query mode ONE SCENE PAIR. No result derived from CPU fence-wait duration. |
| Load/memory | Load ms, initial upload ms; stream/persistent/GPU/transient peaks and limits, loader heap high-water mark; query/stat storage bytes. Mark unavailable fields explicitly, never as zero. |

For this first run, split the existing CULL/RECORD parent into the five
`RaceFrameProfile` regions: cull+camera/car placement, list construction,
car recording, cached-track metadata, combine/checks
(P `src/race_gpu_scene.cpp:147..196`). Split UPDATE into validation/resource
comparison and uniform writes (P `switch/deko_renderer.cpp:327..342`).
Split DRAW/PRESENT into hardware recording, finishList, submit/flush, fence
wait, acquire and remaining present CPU work; preserve SIM and RES/HASH/LOG
parents. Instrument the equivalent boundaries in the tested #211 worker
head, since presentation changed after P. Use `armGetSystemTick` /
`armTicksToNs`, exclude nested child time from exclusive parent residuals,
and do not count waits as active CPU work. This first split is enough to
locate the dominant region; finer containment/topology/list-validation or
allocation/index/state counters belong to phase B.

GPU query ring: separate 16-byte-aligned reports per in-flight slot with
frame IDs and completion fences. Record exactly one begin/end pair per
scene: `reportCounter(DkCounter_TimestampPipelineTop, begin_addr)` before
scene work and `reportCounter(DkCounter_Timestamp, end_addr)` after it.
Read each report's second uint64 (timestamp) only after the slot fence,
following SDK CPU-visibility/cache requirements. Convert unsigned delta
with `dkTimestampToNs(delta)/1e6`. Drain final outstanding reports after
FINISHED before freezing results; exclude drain time from driving stats.
This measures elapsed pipeline span, not utilization. Installed SDK
`C:/msys64/opt/devkitpro/libnx/include/deko3d.h:224`, `:1320`, `:1358`
defines counter types, reporting and conversion `(ts*625)/384`. Layout
and the tiled-cache flush side effect are documented in the
[deko3d primer](https://github.com/devkitPro/deko3d/blob/master/Primer.md#counters)
and [counter implementation](https://github.com/devkitPro/deko3d/blob/master/source/dk_variable.cpp).
Do not add a blocking inter-pass wait or substitute fence wait for GPU time.

Driving aggregates exclude menu/load/countdown/pause, the first resumed
frame and FINISHED frames, following #211's statistics policy at the pinned
head. Keep reset/exclusion counts visible. A photographed FINISHED page
containing frozen **active-driving aggregates** is valid measurement
evidence; instantaneous timing of the stopped FINISHED scene is not.
The coordinator posts these photos and a transcription as CONSOLE_RESULT
on #212. This is the entire first-run request; choose the next experiment
from its result, not from a presumed 11.64 ms GPU cost.

### Phase B: controlled A/B and acceptance after phase A

The worker must add a second explicitly named **proposed** switch,
`--profile-snapshots`, requiring `--replay --profile-frame`. Launch:

```text
nxlink <tagged-nfsu_switch_race.nro> -- sdmc:/nfsu --replay --profile-frame --profile-snapshots
```

Freeze the renderer input at replay ticks 0, 6000, 11344, 24300, 36510;
one warmup plus 120 normal rendered frames per pose, no simulation/replay
advance during the samples. Resume at the next exact replay tick, reset
the wall-clock accumulator so sampling creates no catch-up debt, and keep
snapshot samples separate from active-driving aggregates. Display each
pose's stage means/p95/max, GPU span, ordered draw/triangle/state counts
and ordered-selection hash on additional persistent pages. End with the
same photographed EQUAL and two hashes. Fixed poses diagnose cost; they
do not prove live 60 fps.

A/B one priority at a time at identical snapshots, full replay, mode,
clocks and presentation: checked/trusted cull, full/adjacency list,
full/static constants, inline/prerecorded children, state elision, then
legal batching or threaded snapshots if measurements require them. Give
every build a visible variant ID and photograph both sets of results.
For the dominant CPU parent only, add exclusive inner timers matching
the operation table: containment/preparation/traversal; adjacency/emission/
validation; table/template copy; track/car/combined validation and rebasing.
Report allocations, index visits, resource comparisons, command/list/state
counts and uniform bytes on the same persistent page system. GPU payload
uploads and CPU metadata copies need separate counters. Do not double
credit fence-wait reduction or add overlapping validation microbenchmarks.

Measure instrumentation perturbation in subsequent uninstrumented, one-pair
and detailed-query runs. An empty-list pair estimates query overhead. Only
then add track/car/HUD/effect pass boundaries, with end-of-pipeline times
at boundaries and explicit overlap accounting. Every new reported value,
including overhead and memory/list bytes, must have a labeled persistent
screen row; stdout remains optional. Acceptance uses no detailed queries.

Run acceptance **separately in docked and handheld** at each mode's actual
reported clocks, with no unreported overclock. Both must meet every active
driving frame <=16.67 ms, no growing tick debt, EQUAL and pinned hashes,
base CPU <=8 ms / base GPU <=9 ms checkpoints, load <=about 15 s and the
memory limits. A docked pass does not qualify handheld. Repeat the full
replay and coordinator's established live-driving/photo evidence in both
modes; the owner is not asked to load NROs. Once #182..#188 land, test
individual and combined worst effect scenes (collisions/particles/lights),
GPU <=13 ms, and <=16.67 ms full-frame maximum in both modes. Keep the
remaining 3.67 ms for measured scheduling/presentation/jitter.

If either mode's base GPU exceeds 9 ms, promote retail material-group box
and projected-size/LOD culling plus pass-specific GPU work immediately
after phase A (priority 5), alongside CPU work. Do not wait for all CPU
steps or lower the frame-rate target. Original API totals, exact console
substage costs, command-list memory and combined effects cost remain open
until measured; this document alone cannot close #55.

## Validation and scope

- `cmake -S . -B build -G Ninja`: configured with GNU 13.2.0.
- `cmake --build build`: success, no warnings.
- Review-fix `ctest --test-dir build --output-on-failure`: 74/74 passed,
  36.07 s with the compiler runtime on PATH and outside the sandbox.
- `cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release` and
  `cmake --build build-release --target nfsu_frame_budget_probe nfsu_race_run_tests -j 4`:
  success, no warnings.
- M2 Release probe runs: all three print
  `PASS: selection pairs equal; exact replay pins; sink=5014482` and
  `manifest files 476 equal True`; Bash `cmp` on all three pairs returns 0.
  `NoDefaultCurrentDirectoryInExePath=1` was set on all three runs; the
  `./build-release/...` launch works and run 3 is the actual-workspace smoke.
  M's historical sink was 4049386; M2 adds the checked-cull/full-list counts
  to the observation sink without changing selection or replay.
- Raw output, invocation JSON and manifests are retained outside Git:
  `C:/Users/horra/AppData/Local/Temp/nfsu-212-codex-s7-evidence.zip` for R/M,
  `C:/Users/horra/AppData/Local/Temp/nfsu-212-codex-s7-r2-evidence.zip` for M2.
  M used probe `32973d9` and output directories `profile-final-{1,2,3}`;
  reproduce historical M with that probe version and the corrected explicit
  executable path above. M2 is the source of the revised savings.
- ASCII gates are run in Bash on the committed tree, without `-I`:
  `git -c core.quotepath=false ls-tree -r --name-only HEAD | LC_ALL=C grep -nP '[^\x20-\x7E]'`
  and `LC_ALL=C git grep -n -P '[^\x09\x0A\x0D\x20-\x7E]' HEAD -- .`.
  Both must print nothing and exit 1. Also reject modes 120000/160000,
  scan new artifact bytes/paths and commit/board text, and run `git diff --check`.
- Only this document, the two research tools and the desktop CMake target
  are in scope. No other worker's branch/worktree or game pins are changed.

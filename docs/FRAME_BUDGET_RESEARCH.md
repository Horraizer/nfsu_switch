# Track frame budget research (#212)

Research baseline: `7dad0c4800d24adbcf440206aadf51369965bbb2` (origin/main
when this worktree was created), 2026-09-27. Worker: GPT-6 Astra, Codex
session 7. This document and its desktop probe change no race behavior,
shader, simulation pin, draw order, visibility policy or reference frame.
The desktop target is below the Switch early return in `CMakeLists.txt:79`
and is added at `CMakeLists.txt:738`.

**Decision:** retain the current ordered visible geometry initially; move
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
- **M:** three final desktop probe runs described below, each with one
  warmup and 120 identical-pose frames at each of five replay ticks. Wall
  time, Release GCC 13.2.0, Windows, Intel Core i7-12700K. CPU identification
  command: `Get-ItemProperty 'HKLM:/HARDWARE/DESCRIPTION/System/CentralProcessor/0' | Select-Object ProcessorNameString`.
  Other sessions were active; no affinity or frequency control was applied.
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
python tools/frame_budget_evidence.py $retail out/profile-final-1 run -- build-release/nfsu_frame_budget_probe.exe $retail tests/recordings/event1.txt
python tools/frame_budget_evidence.py $retail out/profile-final-2 run -- build-release/nfsu_frame_budget_probe.exe $retail tests/recordings/event1.txt
```

The third run was also the smoke test from the actual shared workspace's
non-ASCII current directory. It wrote only to this worker's ASCII worktree:

```powershell
python C:/Users/horra/AppData/Local/Temp/nfsu-212-codex-s7/tools/frame_budget_evidence.py 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' C:/Users/horra/AppData/Local/Temp/nfsu-212-codex-s7/out/profile-final-3 run -- C:/Users/horra/AppData/Local/Temp/nfsu-212-codex-s7/build-release/nfsu_frame_budget_probe.exe 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' C:/Users/horra/AppData/Local/Temp/nfsu-212-codex-s7/tests/recordings/event1.txt
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
run's manifests. All comparisons returned 0. `disasm-5` failed its first
ASCII decoding attempt on an effect's non-ASCII byte; its finally manifest
also matched. The final script decodes bytes losslessly as Latin-1 in memory
and emits only technique names, counts and hashes, not shader source.

```bash
for d in out/disasm-{1,2,3,4,5,6} out/profile-{1,2,3} out/profile-final-{1,2,3}; do
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
`docs/TRACK_VISIBILITY.md:405` and `:494` document those gates in detail;
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
`docs/MATERIALS.md:130` explains growth). Likewise the internal increment
at `0x0040EF93` is **outside** the effect-pass loop: reading that counter
alone would undercount API calls for multipass effects. Its neighboring
triangle accumulator uses solid vertex count (`0x0040EF9E`), not the
primitive argument; count API primitives from `0x0040EF5E/65` instead.

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

M derives the following geometry counts from track 1003 at precisely
specified **port replay** views. Every row has 94 car draws / 6137 car
triangles, far=250, 5098 uploaded resources. Values matched in all three
final runs (`tools/frame_budget_probe.cpp:125..149`).

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
differ.** `src/race_gpu_scene.cpp:177` calls default culling, not the
view's recovered zone list. `include/nfsu/track_visibility.h:283` defaults
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

Run `profile-final-2`, mean ms. Parent columns are disjoint boundaries
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
(`tools/frame_budget_probe.cpp:91..104`, `:171..179`).

| Isolated operation, tick 0 | Run 1 ms | Run 2 ms | Run 3 ms | Meaning |
| --- | ---: | ---: | ---: | --- |
| Cull with containment check disabled | 0.106370 | 0.063021 | 0.069332 | Still runs tree preparation; ordered visible pairs equal checked cull |
| Prebuilt mesh adjacency -> ordered pairs | 0.005503 | 0.003504 | 0.003449 | Reuses vector capacity; no full cost/uniqueness audit; every pair compared afterward |
| Existing list validator | 0.357881 | 0.264164 | 0.274999 | Included in parent List |
| Command validator | 0.254200 | 0.155149 | 0.146520 | Full combined resource/draw/index check |
| Execution planner | 0.395857 | 0.242912 | 0.234418 | Includes command validator; not additive with it |
| Backend update validation | 0.438478 | 0.240386 | 0.230453 | Includes execution planner and resource comparison |
| Uniform memcpy only | 0.004686 | 0.003943 | 0.003908 | 112 + 1214*(64+80) = 174928 payload bytes into 256-byte slots in host RAM |
| Copy/destruct combined upload table | 0.300272 | 0.228273 | 0.201673 | Metadata + strings, not GPU texture/geometry payload |

The isolated means can invert nested costs (run 2 planner vs update) due
to separate sampling/cache/noise; subtracting those two is not a reliable
substage timer. The uniform microbenchmark uses cached host RAM, not Switch
GPU-mapped memory, and cannot prove Switch stores cost 0.004 ms. Command
recording and queue submission are **not executable on this desktop target**;
no substitute API or empty mock is timed as deko3d. Their exact console
split, and therefore the operation-by-operation allocation of the claimed
17 us, remains open pending the request below. Sources:
`tools/frame_budget_probe.cpp:105..124`, `switch/deko_renderer.cpp:326..400`.

### Operation-by-operation comparison

| Operation | Original, R | Port, P | Implication supported by M |
| --- | --- | --- | --- |
| Tree topology and containment | Walks resident tree at `0x00446140`; trusts topology/bounds in that loop | `src/track_visibility.cpp:793` audits containment, then `:814` calls `prepare_tree`; `:90` allocates parent/seen/order/stack and recomputes subtree counts | Disabling only containment saves 0.250154 ms at tick 0 in run 2; topology work still remains |
| Mesh -> group adjacency | Material pointer/count/stride already in loaded solid (`0x0040C9A7`, `0x0040C9D0`, `0x0040CA6C`) | `src/track_draw_list.cpp:364` rebuilds counts/prefix sums/order for all meshes via `:93`; `:438` builds sets; `:465` invokes another set-based validator at `:949` | Existing list 0.413467 vs ordered handle selection 0.003504 ms; this prototype is not a production validation replacement |
| Command representation | Pool entries carry pointers and flags (`0x0040F271..0x0040F2A9`) | `src/race_gpu_scene.cpp:118` copies the upload table; `:132..139` copies a template, formats a name and copies its transform for every selected draw | Cached-track stage 0.327192 ms, even after #206. Combined table copy alone 0.228273 ms in an isolated test, overlapping this kind of work |
| Combine / resource planning | Flush follows cached handles; no all-resource table reconstruction or CPU index walk in the traced indexed branch | `src/race_gpu_scene.cpp:18` validates track and car, `:28..45` copies/rebases car resources/names, `:46` plans execution (validates combined again); `src/draw_execution.cpp:56` replans texture copies | Combine costs 0.583292 ms; validation accounts for a material fraction. This is not all memcpy |
| Index safety | D3D API/driver validation not traced; game-side loop uses index pointer/count without scanning indices | `src/draw_commands.cpp:231` scans every index on each validation. Track validated separately, combined, then again in renderer update (`src/draw_execution.cpp:91`) | 59348*3*3 = 534132 track index visits at tick 0 across these three validations, although geometry never changes. Each combined validation visits 196455 indices |
| Uniforms | Matrix conversion once per visible instance, cached matrix/effect/view setup during flush (`0x004460BB`, `0x00410437`) | Renderer update validates all 5098 resources and command payloads, then copies 64+80 bytes per draw (`switch/deko_renderer.cpp:327..339`) | C's 4.01 ms UPLOAD/CONSTANTS includes validation; it is not a measured 4 ms memory-copy cost |
| State / hardware commands | Effect, texture, matrix, VB and IB caches above | `switch/deko_renderer.cpp:358..400` reconstructs and binds shader, uniforms, raster, color, blend, depth, attributes, VB layout, VB and IB for every draw | At tick 0: 1120 draws, 1091 textured. 13*1120+1091 = 15651 explicit track state/binding calls, plus 1120 drawIndexed calls; library-internal GPU words not counted |
| Submit / wait | No hardware timing from retail | `switch/deko_renderer.cpp:124..138` finishes list, signals fence, submits, flushes and waits | C's DRAW=11.64 ms cannot be called GPU scene time or assumed wholly hideable |

There are only two **current port** track program/pipeline keys in M, with
59/59/29/67/23 consecutive runs at the five poses. These do not represent
the original's richer material states. Source: `tools/frame_budget_probe.cpp:28`
defines the key; P `src/draw_commands.cpp:306` refuses retail track material
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
This is a design requirement motivated by current borrowed pointers and
single-slot overwrite (`switch/deko_renderer.cpp:331..342`).

Start with **one child list per visible instance**, containing its groups
in original port order. A whole-section list would draw currently culled
instances and could change blended/equal-depth order; it is not a free
equivalent optimization. Coarse lists may be selected only where the exact
required sequence is identical, or after separately measuring and approving
the geometry/order change. At tick 0, 380 drawn instances replace 1120 draw
recordings; at tick 36510, 637 replace 1559 (M). This reduces CPU list
construction, not GPU draw calls. Do not remove visibility selection to
make a benchmark faster.

### Savings and ordering

For a numerical **projection only**, calibrate run 2's tick-0 preparation
sum to photo B: `K = 23.65/1.690984 = 13.986` console ms per desktop ms.
The views differ (1120 vs 1278 track draws), CPUs differ, and the host is
shared. This is a prioritization model, not a measured scaling law or a
confidence interval. Desktop savings below are differences or elimination
ceilings; console estimates must be replaced by direct A/B measurements.
The deliberately wide K=7..15 sensitivity range covers the slow host run's
calibration too. Do not add ceilings that overlap.

| Priority | Concrete change | Measurement-based expected saving / limit | Required console check |
| --- | --- | --- | --- |
| 0 | Take the new baseline after #211; keep direct GPU presentation and frame-slot overlap owned by that worker. Add the timers below before choosing further GPU work. | C photo B has 8.07+12.32=20.39 ms in transfer/copy and HUD/present. That is the old path's removable envelope, not zero-cost replacement. If the CPU becomes 29.6 ms and the old 11.64 ms DRAW were entirely GPU service, overlap saves at most 11.64 ms, not another 11.64 to subtract after taking max(CPU,GPU). | Report actual CPU record/submit/wait/acquire, GPU timestamps and full frame; zero whole-frame readback in timed normal play. No savings attributed to this PR. |
| 1 | Validate containment/topology once, cache TreePrep and use bounded reusable traversal storage. Keep the same six-plane instance culler. | Turning off containment alone: 0.313175-0.063021=0.250154 ms desktop (includes small camera/placement difference), projected 3.50 ms; sensitivity 1.75..3.75 ms. Caching topology may save more, but no extra credit yet. | Ordered visible IDs and node/instance results match; report cull mean/p95/max, allocations, and one-time load cost. Never suppress a containment failure. |
| 2 | Load-time mesh adjacency and resource namespace; reusable dense visible handles, no per-frame sets/diagnostic names/table copies. Publish prevalidated draw/instance handles instead of reconstructing RecordedCommands. | List prototype: 0.409963 ms saved, projection 5.73 ms (2.87..6.15). Cached-track stage 0.327192 ms and combine 0.583292 ms are a further **0.910484 ms ceiling**, projection 12.73 ms (6.37..13.66), with small checked handle/car append costs still required. Do not add the 0.228273 table-copy or validator microbenchmarks again. | Compare ordered commands/ranges/transforms and resource references to reference over full replay. Log list/select, combine, static index visits and allocations; static index visits should become zero after load. Handle, generation and capacity checks stay. |
| 3 | Bake static instance and material GPU constants; frame slot writes camera/car and genuinely animated material values only. Use package validation instead of full immutable update validation. | At tick 0, remove 1120*144=161280 of 174928 payload bytes (92.2%). M update validation is 0.240386 ms vs host copy 0.003943 ms. C's combined 4.01 ms is the **entire ceiling**; applying that host fraction projects about 3.95 ms for validation removal alone, but GPU-mapped store cost is unmeasured. Initial budget: save 3..4 ms, verify before credit. | Split validation and memcpy timers; measure actual bytes written/flushed, camera/car-only vs full writes, cache operations and slot fences; verify transforms/materials bit-for-bit. Keep animated effects out of immutable records. |
| 4 | Prerecord per-instance children, then memoize redundant state on paths still recorded dynamically. Keep lists alive until their last-use fence; bound total list memory before construction. | M reduces CPU dispatch units 1120->380 (66.1%) or 1559->637 (59.1%). Same GPU draw count. Adjacent-only state caching at tick 0 has a conservative API-call model of <=4339 vs 15651 state calls: 2240 UBO +59 shader +1096 texture +6*59 fixed state +2*59 layout +236 VB +236 IB. About 72.3% fewer state calls, **not** 72.3% of the whole frame. Console ms estimate = measured record_ms*(0.59..0.72), pending priority 0; currently the only measured envelope is 0..11.64 ms within DRAW. Do not add prerecord and memoization percentages. | Time only record_frame/finishList separately; count command bytes, list calls, state binds, and draws. Compare inline vs child lists at identical views. Measure GPU too: fewer CPU calls may leave the same GPU work. |
| 5 | If GPU base scene misses its allocation, batch only proven order-compatible groups: shared geometry + per-instance data first; optional load-baked merged geometry within conservative visibility clusters next. Recover retail LOD/group culling separately. | M offers 90 unique meshes vs 380 instances at tick 0, 126 vs 637 at tick 36510, and 176/229 texture identities. These are reuse opportunities, not legal batch counts. Upper CPU opportunity is the remaining record time; GPU saving is **0 ms credited until measured**. Bound by measured GPU track_ms, not the old combined DRAW. More triangles from coarse culling can make savings negative. | Report new draw/triangle/state counts, GPU timestamp A/B, visual/order equivalence, extra static bytes and load duration. Do not derive a 90-draw claim by ignoring material groups or reorder transparent geometry. |
| 6 | Only if CPU still exceeds its allocation: deterministic simulation/residency producer and renderer consumer on separate available cores, fixed-size immutable snapshots and explicit fences. | C photo B's 5.26 ms /4 ticks =1.315 ms/tick (average, not worst-case); overlap can hide at most min(measured simulation+residency, render CPU). Initially credit at most about 1.3 ms, minus handoff, not a division of total CPU time by core count. | Exact 48336-step hash stream and results; sequence/generation IDs, queue depth, handoff/wait time, latency, no render->simulation feedback, no dropped ticks. Measure tail frames, not just throughput. |

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
8+9+4; those costs must fit the remaining margin. Priority 1..2 projection
leaves about 1.7 ms of the old 23.65 ms preparation when using their
elimination ceilings; retaining 2..4 ms of preparation/dispatch and the
1.3 ms per-tick reference leaves room within the 8 ms CPU allocation. This
is a feasibility calculation, not proof the unimplemented remainder costs
2..4 ms. The baseline GPU may exceed 9 ms: the old 11.64 ms combined DRAW
cannot settle it. If it does, priority 5 and pass-specific GPU optimization
are mandatory before effects can be accepted; no reduced frame-rate exit.

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
number of in-flight slots. Reuse or retire the current 2220547-byte command
metadata cache only after its references are gone. No full vertex payload
duplication by default. Console reports must stay within current explicit
stream/persistent/GPU/transient limits and address load peak separately;
this PR does not repair #204. Sources: C, `src/race_gpu_scene.cpp:85..91`,
`switch/deko_renderer.cpp:183..223`, `docs/SWITCH_PERFORMANCE.md:68`.

## 5. CONSOLE_REQUEST for the coordinator

The corresponding issue comment is the dispatch. The #55 worker should
add this instrumentation to its own renderer branch; this research branch
contains no NRO or race instrumentation. The coordinator builds and runs
it. Do not ask the owner to transfer or load an NRO.

1. Pin and report the tested full commit SHA, NRO SHA-256, Release toolchain,
   deko3d version, docked/handheld mode, actual CPU/GPU/memory clocks and
   presentation mode. Run the post-#211 normal direct-present path. Existing
   launch syntax is `nxlink -s <tagged-nfsu_switch_race.nro> -- sdmc:/nfsu --replay`
   (source `switch/race_main.cpp:234..240`); angle-bracket path is supplied
   by the coordinator, not a new runtime option. Use the built-in event1
   recording and save the exact replay stream and EQUAL verdict. No new
   profile flags are asserted to exist.
2. Profile the full active race, excluding menu/load/paused/finished frames
   from driving aggregates. Also add a diagnostic-only fixed-snapshot
   sampling mode at ticks 0, 6000, 11344, 24300 and 36510: one warmup, 120
   frames each, same camera/visibility and complete normal render. Mark
   these separately; fixed snapshots cannot prove live 60 fps. Capture
   all active-frame maxima, p50/p95/p99, over-budget count, tick/camera and
   draw counts for the worst ten, load duration and memory high-water marks.
3. CPU timers from `armGetSystemTick`, converted with `armTicksToNs`, around
   each *exclusive* region: simulation per tick; residency/hash; camera/car
   placement; containment audit; tree preparation; tree traversal; mesh
   adjacency build; list emission; list validation; car recording; track
   resource copy; template selection/name/transform copy; separate track/
   car command validation; append/rebase; combined execution plan; backend
   resource comparison; backend plan/validation; uniform memcpy; hardware
   record; finishList; submit; flush; fence wait; acquire; GPU HUD recording;
   present. Include parent timers and check exclusive sums against them.
   These boundaries correspond to the P comparison above. Collect into
   preallocated storage; do not printf per draw or time log formatting as
   rendering. Report per-frame allocation count/bytes, index visits,
   resource comparisons, list count, state-call count, command bytes and
   uniform bytes separately from GPU upload bytes.
4. GPU query ring: allocate separate 16-byte-aligned reports per in-flight
   slot, with frame IDs and a completion fence. Record
   `reportCounter(DkCounter_TimestampPipelineTop, begin_addr)` before the
   scene and `reportCounter(DkCounter_Timestamp, end_addr)` after it. Read
   the second uint64 (timestamp) of each report only after that slot's
   fence, using the SDK's CPU-visibility/cache requirements. Convert the
   unsigned delta with `dkTimestampToNs(delta)/1e6` to ms. This is
   elapsed pipeline span, not hardware utilization. Use a separate detailed
   run for track/car/HUD/effect pass boundaries with end-of-pipeline
   timestamps at boundaries; account for cross-pass overlap instead of
   blindly summing top-to-bottom intervals. The SDK defines the counter
   types at `deko3d.h:224`, reporting at `:1320`, conversion `(ts*625)/384`
   at `:1358` in the installed SDK under
   `C:/msys64/opt/devkitpro/libnx/include/`.
5. Counter reporting flushes tiled cache; measure its perturbation using
   an uninstrumented run, one begin/end pair, then detailed pairs. Run an
   empty-list query pair for query overhead. Do not insert a blocking fence
   wait between passes. Keep the release acceptance run free of detailed
   queries. The report layout and flush side effect are specified by the
   [deko3d primer](https://github.com/devkitPro/deko3d/blob/master/Primer.md#counters);
   alignment and top/CROP timestamp distinction are visible in
   [dk_variable.cpp](https://github.com/devkitPro/deko3d/blob/master/source/dk_variable.cpp).
6. A/B each priority at the same snapshots, full replay and clock/mode:
   old vs trusted cull; old vs adjacency selection; full vs static
   constants; inline vs prerecorded lists; state elision; then only if
   required legal batching and threaded snapshot pipeline. Report deltas
   with ordered draw/triangle/state identity and replay pins. Track GPU
   improvement independently of CPU improvement; don't credit a fence
   wait reduction twice.
7. Acceptance: every active driving frame <=16.67 ms, no growing tick debt,
   unchanged deterministic pins, base CPU <=8 ms and base GPU <=9 ms as
   design checkpoints, load <=about 15 s and within memory budgets. After
   effects land, measure them individually and together; GPU target <=13
   ms with the remaining margin covering observed jitter/presentation.
   Record live driving/photo evidence through the coordinator's established
   console workflow; a finished/paused replay screen is not acceptance.

If the console data contradicts the projections, update the estimate and
next priority. Do not close #55 or call this design measured 60 fps based
on this document. Open items are: actual original API totals, exact console
substage attribution, command-list memory cost, actual base GPU service
time and the combined effects cost. Their missing evidence is specified
above rather than hidden in an assumed draw-cost number.

## Validation and scope

- `cmake -S . -B build -G Ninja`: configured with GNU 13.2.0.
- `cmake --build build` (executed with `-j 4`): success, no warnings.
- `ctest --test-dir build --output-on-failure`: final 74/74 passed,
  40.71 s. Initial sandbox run: 73/74, race guard could not canonicalize
  its own fixture directory; the complete unsandboxed rerun passed, and
  the final complete suite above passed after the harness refinement.
- Release probe runs: each prints `PASS: selection pairs equal; exact replay pins; sink=4049386`;
  each wrapper prints `manifest files 476 equal True`. Raw numeric output,
  exact invocation JSON and all before/after manifests are retained as local
  worker evidence, not assets in Git.
- ASCII gates are run in Bash on the committed tree, without `-I`:
  `git -c core.quotepath=false ls-tree -r --name-only HEAD | LC_ALL=C grep -nP '[^\x20-\x7E]'`
  and `LC_ALL=C git grep -n -P '[^\x09\x0A\x0D\x20-\x7E]' HEAD -- .`.
  Both must print nothing and exit 1. Also reject modes 120000/160000,
  scan new artifact bytes/paths and commit/board text, and run `git diff --check`.
- Only this document, the two research tools and the desktop CMake target
  are in scope. No other worker's branch/worktree or game pins are changed.

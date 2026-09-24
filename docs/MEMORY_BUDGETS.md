# Memory arenas and budgets

Issue #52 asks for persistent, streaming, transient and GPU memory budgets for
car and track scenes, with a graceful refusal before any allocation overflows,
high-water marks and fragmentation diagnostics, and repeated scene load and
unload cycles validated on hardware.

This note covers the half of that which needs no Switch. It builds the
allocator, the budgets and the diagnostics, and it runs the load and unload
cycles against the allocator itself. Nothing here has run on hardware, and no
number in it was measured on one. The hardware bullet is still open; see
[What is still open](#what-is-still-open).

Headers: [`include/nfsu/memory_arena.h`](../include/nfsu/memory_arena.h) for
the arenas, [`include/nfsu/memory_budget.h`](../include/nfsu/memory_budget.h)
for the numbers.
Implementation: [`src/memory_arena.cpp`](../src/memory_arena.cpp) (library
`nfsu_memory`) and [`src/memory_budget.cpp`](../src/memory_budget.cpp)
(library `nfsu_gpu_upload`).
Tests: [`tests/memory_arena_tests.cpp`](../tests/memory_arena_tests.cpp) and
the budget tests in [`tests/gpu_upload_tests.cpp`](../tests/gpu_upload_tests.cpp).
The upload side of the same work, issue #50, is [`GPU_UPLOAD.md`](GPU_UPLOAD.md).

## The four categories

| Category | What lives there | Released | Allocation pattern |
| --- | --- | --- | --- |
| `persistent` | what a session loads once: the car scenes of a race | with the session | allocate only, so first fit behaves as a bump allocator |
| `streaming` | bounded track regions, loaded and dropped as the camera moves | region by region, in any order | allocate and release interleaved; this is where fragmentation lives |
| `transient` | what one frame builds: the per-frame draw list | all at once, by `reset()` at the end of the frame | allocate only, then reset; this arena is the frame budget |
| `gpu` | GPU-visible memory: vertex and index buffers, texture images and their staging copies | by the upload ledger once the GPU is done with them | allocate and release interleaved, gated by fences |

The first three are told apart by lifetime and the fourth by who reads it.
Staging memory is transient by lifetime but lives in the `gpu` category,
because the copy engine has to be able to read it.

## The arena

`MemoryArena` hands out byte ranges of one fixed range `[base, base +
capacity)`. It owns no memory and never touches the bytes it manages. What
backs the range is the platform layer's: one block of CPU memory reserved at
start-up for the three CPU categories, or a deko3d memory block for the GPU
one. That is what lets one implementation serve every category, and what lets
it be tested on a desktop with nothing behind it at all.

It keeps five rules. The tests check the first four against an independent
model, or against a count of heap allocations, rather than against the
arena's own figures, and the fifth by passing in a Release build.

1. **Every byte is either free or held by exactly one live allocation.** The
   gap an alignment leaves in front of a placement stays free and is handed
   out again when something fits in it. There is no hidden padding, so
   `used_bytes + free_bytes` is always the capacity.
2. **Placement is first fit in address order,** so an arena nobody releases
   from behaves exactly as a bump allocator does, and a released range is
   merged with both of its free neighbours before `release()` returns.
3. **All bookkeeping is reserved by `create()`.** `allocate()`, `release()`
   and `reset()` are `noexcept` and never allocate. Live allocations separate
   free blocks, so there are never more free blocks than live allocations plus
   one, and `create()` reserves room for `max_allocations + 1` free blocks,
   `max_allocations` slots and as many free-slot entries, which covers every
   state the arena can reach. A test replaces the global allocation function
   and counts its calls inside twenty thousand arena calls, in the free list's
   worst shape as well; the count is zero.
4. **A refusal changes nothing** but the refusal counters, and never returns a
   range that reaches past `base + capacity`. For the CPU categories that
   address is the first byte of the next arena.
5. **Every guard is ordinary code,** not an assertion. The suite passes in a
   Release build with `NDEBUG` defined, which is where an assertion would have
   vanished.

A handle names a slot and a 64-bit generation. A slot is reused under a new
generation, and generations carry on across `create()`, so a handle to an
allocation that was released, reset away or created away is refused forever
after, including when a newer allocation holds its slot; releasing through it
would otherwise free the newer allocation and the next request would be
placed on top of live data. Arenas are move-only, because two copies of one
would hand out the same ranges.

## Refusing before overflow

`allocate()` returns `ArenaRefusal::none` or the reason it refused. Each
reason tells the caller what it can do:

| Reason | Meaning | What the caller can do |
| --- | --- | --- |
| `zero_size` | a zero-byte request | nothing is ever uploaded from an empty buffer; do not ask |
| `bad_alignment` | zero, not a power of two, or above the arena's `max_alignment` | ask for an alignment the arena was built for |
| `over_budget` | more bytes than the arena has free in total | no layout can hold it: release something |
| `no_fit` | the free bytes suffice, but no single free block holds the request at its alignment | releasing a neighbour of the largest block, or reloading in another order, can cure it; asking again as is cannot |
| `allocation_limit` | as many live allocations as the arena tracks | release something, or build the arena with more |
| `unknown_handle` | a release through a handle this arena never issued | a caller bug |
| `stale_handle` | a release through a handle whose allocation is gone | a double release |

The checks run in that order, so a request that breaks several rules is
refused for the first. The free-byte check is written as `size > capacity -
used`, a difference that cannot wrap: `used + size > capacity` would let a
request near 2^64 wrap into range. Capacity is capped at 2^48 and alignment at
2^20, which keeps every sum in the arena, and the per-mille figure below,
inside 64 bits.

The boundary is tested exactly, not near it:

- an arena of 4096 bytes at base 8192 takes 4096 bytes at 8192, ending at
  12288, and refuses one more byte and a 4097-byte request as `over_budget`;
- after a 100-byte allocation in a 4096-byte arena, the largest 256-aligned
  request that fits is 3840 bytes at 256, the smallest that does not is 3841
  and is refused as `no_fit`, and the 156-byte gap in front is then handed
  out whole.

## Diagnostics

`ArenaStats` holds every figure as data; nothing is printed.

| Field | Meaning |
| --- | --- |
| `used_bytes`, `free_bytes` | live bytes and the rest; they sum to the capacity |
| `live_allocations` | live allocation count |
| `free_blocks`, `largest_free_block` | how the free bytes are split, and the biggest request with alignment 1 that fits now |
| `fragmentation_permille` | `1000 * (free_bytes - largest_free_block) / free_bytes`, rounded down; 0 when the free space is one block or nothing is free |
| `extent` | one past the highest live byte, relative to `base`: how far the live set reaches, as opposed to how much of it is used |
| `high_water_bytes`, `high_water_extent` | the most bytes live at once, and the furthest extent, since `create()` |
| `period_high_water_bytes`, `period_high_water_extent` | the same since the last `reset()`: for the transient arena, the frame's own peak |
| `allocations`, `releases`, `resets` | successful calls |
| `refusals`, `refused[reason]` | refused calls, in total and by reason |

`high_water_extent - high_water_bytes` is what alignment gaps and
fragmentation cost at the worst moment. `free_ranges()` returns the free list
itself; it is the only call apart from `create()` that allocates.

Worked example, from the tests: eight 100-byte blocks in a 1000-byte arena,
then blocks 1, 3 and 5 released, leaves 500 bytes free in four blocks, the
largest 200, fragmentation 600 per mille and extent 800; a 250-byte request is
`no_fit` and a 501-byte one `over_budget`. Releasing block 2 joins three
blocks into one 300-byte block: three free blocks, 500 per mille.

## Frames

The transient arena is the frame budget. A frame allocates, then calls
`reset()`, which frees every byte at once, starts a new high-water period and
makes every handle of the frame stale. A request past the budget is refused as
`over_budget` and the next frame is unaffected. `tests/memory_arena_tests.cpp`
runs a thousand frames and checks, after every one, that the period peak is
that frame's own and that the lifetime peak is the largest frame's.

A frame that is still being read by the GPU must not have its memory reset.
Nothing here enforces that for the transient arena, because nothing in it is
GPU-visible; per-frame GPU data goes through the upload ledger, whose fences
do enforce it ([`GPU_UPLOAD.md`](GPU_UPLOAD.md)).

## The plan

`MemoryPlan` builds the four arenas of one `MemoryBudget`. The three CPU arenas
share one address space, in category order: persistent from 0, then streaming,
then transient, each base rounded up to that arena's `max_alignment`.
`cpu_bytes()` is therefore the size of the one CPU block the platform layer
reserves, and each CPU arena ends at or before the base of the next. The GPU
arena starts at 0 of its own address space, because it is backed by device
memory that shares no address with the CPU block. A budget that is refused
leaves a working plan exactly as it was.

A deko3d backend needs the `gpu` category split over at least two memory
blocks, because a block's flags are fixed when it is made and images need the
`Image` flag ([`GPU_UPLOAD.md`](GPU_UPLOAD.md)). The upload ledger takes one
arena per role for that reason. How to divide the GPU budget between the
buffer block and the image block is a placeholder decision; the only
per-region split this repository records is 164790 buffer bytes to 3006464
texture bytes, [`TRACK.md`](TRACK.md).

## The numbers

Every number below is exactly one of three things, and says which.

- **Derived:** computed from a structure, a limit or an API this project
  already has, so it holds wherever the code builds.
- **Recorded:** measured on retail data by earlier work, on a desktop, and
  recorded in another document. Not re-measured here, and not a Switch number.
- **Placeholder:** no measurement stands behind it. Each names the measurement
  that would replace it, and none is a guess at that measurement's result.

### Derived

| Figure | Value under the default limits | From |
| --- | ---: | --- |
| `car_scene_upload_ceiling` | 310378496 bytes (296 MiB) | `SceneLimits`: 4194304 vertices of `sizeof(MeshVertex)` 36, 4194304 triangles of 6 bytes, 128 MiB of RGBA8 |
| `track_region_upload_ceiling` | 260046848 bytes (248 MiB) | `TrackSceneLimits`: the same with `sizeof(TrackVertex)` 24 |
| `track_draw_list_frame_ceiling` | 33554432 bytes (32 MiB) | `TrackDrawListLimits::max_entries` 4194304 entries of `sizeof(TrackDrawListEntry)` 8 |
| `track_scene_frame_ceiling` | per scene | `TrackScenePlacementCost::resident_entries`, the list of a frame that sees every placed instance, times 8 |
| `upload_table_requirement` | per scene | the scene's upload table placed into empty arenas: payload, and the extent alignment gaps add |
| CPU arena `max_alignment` | `alignof(std::max_align_t)` | what the platform allocator guarantees for any object |
| GPU arena `max_alignment` | 4096 | `DK_MEMBLOCK_ALIGNMENT`, the alignment deko3d requires of a memory block's size; a layout asking for more is refused rather than placed misaligned. That a block's GPU address is aligned to it too, which is what makes an offset aligned inside the block an aligned address, is unverified; see [Unknown](#unknown) |

Each ceiling is what a preparation layer can at most hand an upload, because
it refuses anything larger. They saturate at 2^64 - 1 instead of wrapping, so
a hostile set of limits cannot make an unbounded scene look affordable.

Two of them are larger than the budgets below, and that is a finding rather
than a mistake: **the preparation limits admit scenes the Switch budget cannot
hold.** A car at the default `SceneLimits` can need 296 MiB of GPU payload
against a 256 MiB GPU placeholder, and one frame's draw list can be 32 MiB
against a 1 MiB frame placeholder. The limits exist to stop hostile input, not
to fit a console, so a backend must check the actual scene against the actual
budget. `upload_table_requirement` does the first half of that, and the
ledger's refusal does the second: a scene that does not fit is refused with the
resource that did not fit and the arena's reason, and nothing it placed stays
placed.

`upload_table_requirement` takes the same `ImageLayoutQuery` the ledger does.
On a desktop it is given `linear_image_layout`, so its image figures are the
texels alone and a lower bound. Given the deko3d layout query on hardware, the
same function reports the real image figures with no other change.

### Recorded

From [`TRACK.md`](TRACK.md) and [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md), for
the first retail region those documents measure, `Tracks/STREAML1RB.BUN` at
23015540, 178268 bytes long:

| | Bytes |
| --- | ---: |
| Vertex buffers, 5935 vertices of 24 bytes | 142440 |
| Index buffers, 3725 triangles of 6 bytes | 22350 |
| Texture texels, 40 RGBA8 textures | 3006464 |
| Upload payload in total | 3171254 |
| The region as read from the file | 178268 |
| Draw list with every placed instance visible, 46 entries | 368 |

[`TRACK.md`](TRACK.md) records the buffers of a second region as well,
`Tracks/STREAML1RX.BUN` at 9728, 784032 bytes long: 650976 vertex bytes and
103596 index bytes, 754572 in all. It records no texture byte count for that
one.

These describe regions on a desktop. They say nothing about how many regions
are resident at once, which is what a streaming budget depends on. The
texture figure is texels: the GPU images are at least that big and may be
bigger, by however much the block-linear layout pads them.

### Placeholders

| Constant | Value | Replaced by |
| --- | ---: | --- |
| `kPersistentBudgetPlaceholder` | 64 MiB | the peak of every persistent allocation of a race, measured on hardware: the car scenes once uploaded, and whatever else a session loads once |
| `kStreamingRegionBudgetPlaceholder` | 16 MiB | the peak streaming allocation of one retail region measured on hardware, over every region a route loads |
| `kResidentRegionsPlaceholder` | 3 | the most regions the retail streaming tables keep resident at once; those tables are not decoded ([`TRACK_OBJECTS.md`](TRACK_OBJECTS.md)), so nothing in this project can say |
| `kTransientBudgetPlaceholder` | 1 MiB | the peak per-frame allocation measured on hardware |
| `kGpuBudgetPlaceholder` | 256 MiB | the peak GPU residency measured on hardware, with every image sized by `dkImageLayoutGetSize` rather than by its texels, inside the memory mode the application runs in |
| `kArenaAllocationsPlaceholder` | 4096 per arena | the peak live allocation count per arena measured on hardware; it bounds the bookkeeping each arena reserves, 52 bytes per allocation |

`switch_memory_budget()` assembles them: persistent 64 MiB, streaming 3 x 16
MiB = 48 MiB, transient 1 MiB and GPU 256 MiB, which lays out as a CPU block
of 113 MiB (118489088 bytes) and a 256 MiB GPU range. A test checks that the
budget is assembled from exactly these constants and builds a plan; the values
themselves are free to change, which is what makes them placeholders.

## Repeated load and unload cycles

Issue #52 asks for these on hardware. What can run without it runs against
the arena, and against the upload ledger over real prepared scenes in
[`GPU_UPLOAD.md`](GPU_UPLOAD.md).

- **2000 cycles, one scene.** Forty requests of random size and alignment up
  to 4096 are placed into an empty arena and released in a different order
  every cycle, so every merge direction runs thousands of times. After every
  cycle the arena must be exactly as created - one free block of the whole
  capacity - and every request of every cycle must land where the first cycle
  put it. After 2000 cycles the high-water marks must still be the first
  cycle's, and no handle may name a slot at or past the scene's 40.
- **1200 cycles, three interleaved regions.** Region B loads while A is
  resident, A unloads, C lands in the hole A left, and so on until the arena
  is empty again. The release order inside each region follows one of four
  fixed permutations by cycle number, so every cycle repeats a sequence an
  earlier one ran exactly from the same empty arena. Once all four have run,
  the high-water marks must never move again; any movement after that is creep
  by construction.
- **Twenty thousand random operations** are compared with an independent model
  after every one: the offset first fit must give, the reason for every
  refusal, the free ranges, and every figure above. The run is required to
  have placed more than 5000 requests and to have reached both `over_budget`
  and `no_fit` more than 100 times each, so it cannot pass by never refusing.

Creep across cycles is the defect these exist for, and it is invisible in one
cycle.

## Tests

`tests/memory_arena_tests.cpp` states in a comment above each test the defect
it would catch. The mutation sweep that showed each can fail is in
[Mutations](#mutations).

## Mutations

Each mutant is one edit to the library, rebuilt and run against both suites.
A run is classified by what it printed, as [`CLAUDE.md`](../CLAUDE.md) requires:
a kill needs a non-zero exit, at least one `FAIL:` line and the
`<n> check(s) failed` summary; a survival needs exit 0 and the pass line;
anything else - a crash, a timeout, a missing summary - is unknown and is not
counted as a kill. Each suite was also run one test at a time, since both test
programs take a test name, so every kill is attributed to the tests that made
it and every test is shown able to fail on its own.

The sweep ran on library sources identical to the ones committed here; the
tests have only gained checks since, which can only kill more. These are the
author's mutants, and an author's set measures the set as much as the suite:
[`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md) records a suite its author's own 50
mutants scored at 49 caught, and an independent pass that then found 56
survivors among 124. An independent pass over this change is recorded here
once it has reported, and the sweep is run again on the final head.

43 mutants, 1 build error, 42 killed.

| Mutant | Change | Result |
| --- | --- | --- |
| A01 | refusal name no fit spelled as over budget | killed |
| A02 | create accepts a misaligned base | killed |
| A03 | create accepts base + capacity past 2^64 | killed |
| A04 | reset of a never-created arena makes a zero-size free block | killed |
| A05 | budget check refuses the exact fit | killed |
| A06 | budget check admits one byte more | killed |
| A07 | budget check written as a sum that wraps | killed |
| A08 | alignment ignored when placing | killed |
| A09 | the gap in front of an aligned placement is lost | killed |
| A10 | alignment above max_alignment accepted | killed |
| A11 | budget checked before the allocation limit | killed |
| A12 | allocation limit off by one | killed |
| A13 | no fit reported as over budget | killed |
| A14 | fragmentation figure inverted | killed |
| A15 | largest free block taken from the last block | killed |
| A16 | merge with the next block keeps its old start | killed |
| A17 | merge with both neighbours keeps the absorbed block | killed |
| A18 | never merges with the previous block | killed |
| A19 | a released slot is never reused | killed |
| A20 | a stale handle is not refused | killed |
| A21 | reset does not invalidate handles | killed |
| A22 | reset keeps the period high water | killed |
| A23 | reset clears the lifetime high water | killed |
| A24 | create does not reserve the free list | killed |
| A25 | extent off by one | killed |
| A26 | plan does not advance past an arena | killed |
| A27 | plan cpu_bytes stops at the last base | killed |
| A28 | plan creates in place, so a refusal clobbers it | build error |
| A29 | plan places the GPU arena after the CPU ones | killed |
| A30 | refusal total is the last reason only | killed |
| A31 | high-water extent tracks bytes instead | killed |
| A32 | release does not advance the generation | killed |
| A33 | create does not raise the generation floor | killed |
| A34 | a new slot starts at generation 0 | killed |
| B01 | ceilings wrap instead of saturating | killed |
| B02 | car ceiling at the track stride | killed |
| B03 | frame ceiling from triangles | killed |
| B04 | GPU arena below the block alignment | killed |
| B05 | buffer extent ignores alignment gaps | killed |
| B06 | staging batch is the largest | killed |
| B07 | requirement admits an alignment no arena models | killed |
| B08 | requirement arena sized without gaps | killed |
| B09 | streaming budget for one region only | killed |

Each test run alone, against the same mutants:

| Test | Kills | Mutants |
| --- | ---: | --- |
| `names` | 1 | A01 |
| `create_refuses_bad_configurations` | 3 | A02 A03 A05 |
| `default_arena_refuses` | 2 | A04 A06 |
| `exact_fit_at_the_boundary` | 2 | A05 A06 |
| `exact_fit_with_alignment` | 5 | A05 A06 A08 A13 A14 |
| `hostile_requests_are_refused` | 6 | A06 A07 A10 A11 A12 A30 |
| `fragmentation_figures` | 7 | A06 A13 A14 A15 A17 A18 A25 |
| `release_merges_in_every_direction` | 7 | A05 A14 A15 A16 A17 A18 A25 |
| `random_operations_against_the_model` | 9 | A08 A09 A13 A14 A15 A16 A17 A18 A25 |
| `load_and_unload_cycles_return_to_the_start` | 7 | A08 A09 A16 A17 A18 A19 A31 |
| `interleaved_streaming_cycles_do_not_creep` | 6 | A08 A09 A16 A17 A18 A31 |
| `frames_reset_the_transient_arena` | 4 | A20 A21 A22 A23 |
| `handles_are_checked` | 6 | A14 A16 A19 A20 A25 A32 |
| `create_again_keeps_old_handles_stale` | 3 | A20 A33 A34 |
| `allocation_limit` | 2 | A08 A12 |
| `arena_calls_never_allocate` | 8 | A08 A09 A12 A16 A17 A18 A19 A24 |
| `memory_plan_layout` | 5 | A05 A06 A26 A27 A29 |
| `derived_ceilings` | 2 | B01 B02 |
| `switch_budget` | 4 | A26 A27 B04 B09 |
| `scene_requirement_places_the_table` | 7 | A08 A25 A31 B05 B06 B07 B08 |
| `frame_ceiling_is_the_full_draw_list` | 3 | A05 A06 B03 |

A28 was written against a copyable arena. Arenas became move-only after it
was written, so it no longer compiles; it is re-expressed as a move for the
next sweep, and `memory_plan_layout` now releases an allocation made before a
refused budget, which is the check a moved-from arena fails.

## What is still open

Of issue #52's acceptance:

| Bullet | State |
| --- | --- |
| Fail gracefully before allocation overflow | done for the allocator and the ledger, tested at the exact boundary, in Debug and Release |
| Report high-water marks and fragmentation diagnostics | done, as data, tested against an independent model |
| Validate repeated scene load and unload cycles on hardware | **open.** Run here against the arena and the ledger only; no Switch was available and none of these cycles has run on one |
| Release build and full CTest pass | the new suites pass in both configurations; see the handoff for the full-suite result on Linux, where a pre-existing error outside this change stops `src/render.cpp` compiling |
| No retail or derived game assets enter Git | none do: every fixture is built in memory |
| ASCII-only artifacts and Git metadata | scanned; see the handoff |

## Unknown

- Every placeholder above. The Switch memory an application gets depends on
  the memory mode it runs in, and no figure for it is taken on trust here.
- How many regions are resident at once, and so the streaming budget.
- The GPU size of an image. `dkImageLayoutGetSize` decides it on hardware; the
  texels are a lower bound only.
- Whether a deko3d memory block's GPU address is aligned to
  `DK_MEMBLOCK_ALIGNMENT`, or beyond it. The header requires only the block's
  size to be a multiple of it. The deko3d samples align offsets inside a block
  and nothing more (`SampleFramework/CMemPool.cpp` line 55, at the commit
  [`GPU_UPLOAD.md`](GPU_UPLOAD.md) names), which is the same assumption the GPU
  arena makes up to 4096; it refuses any alignment above that rather than
  assume more, and neither is verified on hardware.

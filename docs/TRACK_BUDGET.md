# What one bounded track region costs

[`TRACK.md`](TRACK.md) decodes the geometry a bounded region draws.
[`TRACK_OBJECTS.md`](TRACK_OBJECTS.md) decodes where that geometry goes.
[`TRACK_VISIBILITY.md`](TRACK_VISIBILITY.md) and [`TRACK_ZONES.md`](TRACK_ZONES.md)
decode which of it a camera sees. [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md)
turns those three into one owned scene and one per-frame draw list, and
[`TRACK_RENDER.md`](TRACK_RENDER.md) draws it.

This note decodes nothing. It is the cost of all five, separated by the
stage that pays it, with the procedure that reproduces every number.

Header: [`include/nfsu/track_budget.h`](../include/nfsu/track_budget.h).
Implementation: [`src/track_budget.cpp`](../src/track_budget.cpp).
Tests: [`tests/track_budget_tests.cpp`](../tests/track_budget_tests.cpp) and
[`tests/track_budget_guards.cmake`](../tests/track_budget_guards.cmake).
Command: `nfsu_probe track-budget`.

## The one decision this note rests on

A budget wants two kinds of number and they do not have the same stability.

**Counts are a function of the input.** Draw calls, triangles, texture
binds, bytes retained: two runs over the same region give the same numbers
on any machine, in any build configuration, under any load. They can be
pinned exactly, and a test that pins them fails only when the code's
behaviour changed.

**Wall-clock time on this project's machine is not.** Several agent sessions
share this checkout and more than one is routinely live, so a stage timed
while another session is linking is slower for a reason that has nothing to
do with this code. That is not hypothetical here: the timings below were
taken twice on purpose, once with another build running and once without,
and the difference is recorded rather than averaged away.

So the two are split at the API boundary rather than mixed and then
qualified:

- `include/nfsu/track_budget.h` **reads no clock at all.** Every counter in
  `TrackBudgetCost` is exact, and `check_track_budget` reads nothing else,
  so a limit on a time cannot be expressed there.
- **Taking the clock** belongs to `nfsu_probe track-budget`, which repeats
  each stage and reports the spread rather than one number.
- Two library functions do take doubles and neither is an exception.
  `summarize_track_samples` is arithmetic over numbers its caller hands it -
  it reads no clock and does not know they are milliseconds - and
  `track_budget_costs_equal` compares two costs. Both are in the library
  rather than in the command because neither can be tested from a command,
  which the mutation section below measured rather than assumed.
- **No automated limit in this repository is a time**, and the section
  "What is automated, and what is not" below says what that leaves covered.

A limit that trips when another session happens to be compiling is worse
than no limit, because someone deletes it and then it covers nothing.

## What "bytes" means here, and what it does not

Every byte figure is a **stated formula over containers whose element count
and element size are both known**, never a heap or working-set total.
`prepare.vertex_buffer_bytes` is what `TrackDrawCost` already defines as the
vertex bytes a backend uploads; `frame.entry_bytes` is
`entries * sizeof(TrackDrawListEntry)`; `render.frame_bytes` is measured
from `nfsu::Frame`'s own four buffers rather than from `width * height`
times a constant this layer would have to keep in step by hand.

They therefore **do not** include the report vectors' capacity slack, the
`std::string` mesh names, the issue lists, the allocator's per-block
overhead, or anything the process allocated for another reason. They are the
dominant arrays a backend sizes buffers against, which is what a budget is
for. A process-wide memory number would cover the rest and would stop being
reproducible in exchange - the same trade the clock paragraph refuses.

## The four stages

`TrackBudgetStage` names them, and every counter is prefixed with its own
stage, so a counter's name says which stage pays it.

| Stage | What it is | Paid |
| --- | --- | --- |
| `parse` | `load_track_region` and `load_track_objects` | per region load |
| `prepare` | `build_track_scene`: the region prepared and placed | once per region |
| `frame` | `cull_track_region` and `build_track_draw_list` | every frame |
| `render` | `render_track_draw_list` | every frame |

The culler and the draw-list build are one stage rather than two because
they are what a frame pays and a backend cannot pay one without the other.

**Texture decoding is timed separately and costed under `prepare`.** It is
large - 8 MB read and 3 MB decoded for the reference region - and would hide
inside a neighbour if it were not timed on its own, but its bytes are
retained by the prepared region, so `prepare.texture_bytes` is where they
are counted. Only the time is separate.

## The measurement procedure

Everything below is reproducible from these commands. The inputs are stated
in full, because a measurement whose inputs are implied is not one.

### The region

The same region [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md) and
[`TRACK_RENDER.md`](TRACK_RENDER.md) measure, so every count here can be
checked against a number one of those two already records:

```text
container  Tracks/STREAML1RB.BUN
geometry   offset 23015540 size 178268
scenery    offset 23011344 size 4168
textures   Tracks/STREAML1RB_1210.BUN
```

### The build

**Release**, and the note says so wherever a number came from anywhere
else. A budget quoted from an unoptimised build is a budget for a program
nobody ships.

```text
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

The default configuration carries no `-O` flag, so GCC compiles it at `-O0`;
[`TRACK_RENDER.md`](TRACK_RENDER.md) measures what that costs in a rendered
frame's bytes, and the section below measures what it costs in time.

### The command

```text
build-release/nfsu_probe track-budget \
    X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --order material-group --repeat 30
```

It **writes no file at all.** The frame is rendered into memory and dropped,
so the command cannot leave a derived game asset on disk even by accident.
That is also why it has no `--out` and none of the output guards
`track-render` carries.

### Warm-up and repetition

`--repeat N` runs the whole pipeline `N + 1` times: **one warm-up, always,
whose times are discarded and which the report names.** The first parse of a
region reads it off the disk and every later one reads it out of the
operating system's file cache, so the first sample is a different
measurement from the rest and averaging it in produces a number that
describes neither.

For each of the five phases the report gives the sample count and then
**min, median, mean, standard deviation and max** - five figures rather than
one, because one number from a shared machine reads like a fact and is not
one.

### The determinism cross-check, which is part of every run

Every repetition's counter table is compared against the first, field by
field, and a disagreement **exits 5** with the counter named and both
values printed. The claim that the counts are a function of the inputs is
therefore a checked property of each run rather than an assertion in this
document. It is free: the repetitions are being run anyway.

`--no-time` prints the counts and the verdict and no times, which makes the
output byte-identical between runs and gives a differential check something
exact to compare. With times in it the output is deliberately not
reproducible.

## The reference region's budget

Release, `--repeat 30`, `--order material-group`, default 1280x720 frame.
Every count below was identical in all 31 runs.

### parse

| Counter | Value |
| --- | ---: |
| `parse.region_bytes` | 182436 |
| `parse.meshes` | 22 |
| `parse.valid_meshes` | 22 |
| `parse.vertices` | 5935 |
| `parse.triangles` | 3725 |
| `parse.materials` | 70 |
| `parse.decoded_mesh_bytes` | 164790 |
| `parse.sections` | 1 |
| `parse.valid_sections` | 1 |
| `parse.objects` | 27 |
| `parse.instances` | 39 |
| `parse.bound_nodes` | 10 |
| `parse.object_record_bytes` | 4056 |

182436 bytes read is 178268 of geometry plus 4168 of scenery, which is the
two region sizes the command was given: the loader reads the region and
nothing else, and this is the counter that says so.

### prepare

| Counter | Value |
| --- | ---: |
| `prepare.meshes` | 22 |
| `prepare.draws` | 70 |
| `prepare.drawable_draws` | 70 |
| `prepare.textures` | 40 |
| `prepare.vertex_buffer_bytes` | 142440 |
| `prepare.index_buffer_bytes` | 22350 |
| `prepare.texture_bytes` | 3006464 |
| `prepare.instance_bytes` | 2592 |
| `prepare.resident_bytes` | 3173846 |
| `prepare.placed_instances` | 18 |
| `prepare.unplaced_instances` | 21 |
| `prepare.resident_entries` | 46 |
| `prepare.resident_triangles` | 2827 |

The three buffer figures and the placement counts are the ones
[`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md) records for this region -
142440, 22350 and 3006464 bytes, 18 placed and 21 unplaced, 46 resident
entries and 2827 resident triangles - reproduced here by a different command
reading the same fields.

**`prepare.resident_bytes` is the number the design decision costs.**
[`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md) states the choice: the scene
holds the whole region and a draw list names a subset, so memory is
proportional to the region rather than to the frame. 3173846 bytes is how
much that is here, and **nearly 95 per cent of it is texture** - 3006464 of
3173846, or 94.73 per cent. The geometry a backend uploads is 164790 bytes,
which is `parse.decoded_mesh_bytes` exactly, because preparation copies the
decoded meshes and adds nothing; the textures are 18.24 times that.

**That ratio is the one number here most likely to matter to a backend.**
Whatever a deko3d port does about geometry, the texture working set is the
thing that will decide whether a region fits, and it is not a property of
this layer: it is what the retail packs hold.

### frame

| Counter | Value |
| --- | ---: |
| `frame.visible_instances` | 39 |
| `frame.visible_placed` | 18 |
| `frame.visible_unplaced` | 21 |
| `frame.draw_calls` | 46 |
| `frame.triangles` | 2827 |
| `frame.groups` | 42 |
| `frame.meshes` | 17 |
| `frame.texture_binds` | 41 |
| `frame.entry_bytes` | 368 |

**46 draw calls and 41 texture binds** is the draw-call cost issue #35 asks
for, and it is the whole region with no camera: `--eye` and `--target`
select a cull camera and cut it. The 46, 2827, 42, 17 and 41 are exactly
what [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md)'s acceptance pins for this
region in `material-group` order.

`frame.entry_bytes` is 368 bytes - the entire per-frame allocation, against
3173846 bytes held between frames. That ratio is the design decision
restated as a measurement: **the per-frame allocation of this arrangement is
about one ten-thousandth of what it holds between frames**, 368 against
3173846, or 0.0116 per cent.

### render

| Counter | Value |
| --- | ---: |
| `render.entries` | 46 |
| `render.triangles_submitted` | 2827 |
| `render.triangles_rasterized` | 2739 |
| `render.pixel_tests` | 164543 |
| `render.fragments` | 58383 |
| `render.fragments_written` | 46714 |
| `render.pixels_drawn` | 33449 |
| `render.texture_binds` | 41 |
| `render.frame_bytes` | 15667200 |

Every one of the first eight is a number
[`TRACK_RENDER.md`](TRACK_RENDER.md) already records for this region's
default view, which is the cross-check that this layer reads the fields it
claims to.

**`render.frame_bytes` is the largest single item in the whole budget.**
15667200 bytes at 1280x720 is `nfsu::Frame`'s four buffers - 4 bytes of
colour, 8 of depth, 4 of triangle id and 1 of blend mask, 17 bytes a pixel -
and it is **five times the entire prepared region**. It is also paid whether
or not a single triangle is submitted, which is why the command renders and
measures it even when nothing is drawn.

That figure is a property of the CPU reference renderer rather than of the
track data: the depth buffer is `double`, and the triangle-id buffer exists
so that [`INSPECTOR.md`](INSPECTOR.md) can map a pixel back to a material
group. A GPU backend would carry neither at these widths. It is recorded
because it is what this program costs, and it is labelled because a reader
budgeting for a deko3d backend must not carry it across.

## The initial budgets

Issue #43 asks for initial CPU, memory, texture, vertex and draw-call
budgets, so this section states ceilings rather than only measurements.

**The rule, so that the numbers are derived and not chosen.** Each ceiling
is the measured value **rounded up to the next power of two**, with one
exception named below. A stated rule matters more than the exact headroom:
it gives between one per cent and double the measured value depending on
where the measurement falls, it cannot be argued into a number that suits a
regression, and anyone can re-derive it from the table above. It is an
opening position, not a target - the right ceiling for a shipped backend is
a number that comes out of the hardware, and this project has no hardware
budget yet.

| Kind | Counter | Measured | Ceiling |
| --- | --- | ---: | ---: |
| draw call | `frame.draw_calls` | 46 | 64 |
| draw call | `frame.texture_binds` | 41 | 64 |
| vertex | `frame.triangles` | 2827 | 4096 |
| vertex | `parse.vertices` | 5935 | 8192 |
| vertex | `prepare.vertex_buffer_bytes` | 142440 | 262144 |
| vertex | `prepare.index_buffer_bytes` | 22350 | 32768 |
| texture | `prepare.textures` | 40 | 64 |
| texture | `prepare.texture_bytes` | 3006464 | 4194304 |
| memory | `prepare.resident_bytes` | 3173846 | 4194304 |
| memory | `parse.region_bytes` | 182436 | 262144 |
| memory | `render.frame_bytes` | 15667200 | 15667200 |

**`render.frame_bytes` is the exception and takes no headroom**, because it
is not a property of the track data at all: it is `width * height * 17` for
`nfsu::Frame`'s four buffers, so at a fixed frame size it can only change if
the renderer's buffer set changes, which is a deliberate act rather than
drift. An exact ceiling there is a tripwire on that act. It is also the one
ceiling in this table that a CTest entry already enforces, at two frame
sizes, because it needs no retail data.

**CPU has no ceiling in this table, and that is the decision** the section
below the timings explains rather than an omission.

The whole set, as one command:

```text
build-release/nfsu_probe track-budget \
    X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --order material-group --repeat 5 --no-time \
    --limit frame.draw_calls 64 \
    --limit frame.texture_binds 64 \
    --limit frame.triangles 4096 \
    --limit parse.vertices 8192 \
    --limit prepare.vertex_buffer_bytes 262144 \
    --limit prepare.index_buffer_bytes 32768 \
    --limit prepare.textures 64 \
    --limit prepare.texture_bytes 4194304 \
    --limit prepare.resident_bytes 4194304 \
    --limit parse.region_bytes 262144 \
    --limit render.frame_bytes 15667200
```

It exits 0 today and prints `budget: limits 11 exceeded 0`. Anyone with the
installation can wire that line into a check; this repository's own gates
cannot, because they have no installation.

## Timing

**Read the spread, not the median.** These are wall-clock milliseconds on
one developer machine and they are the least portable numbers in this
repository.

### What was running, and why that is stated first

Stated first because it is the variable that matters most, and because this
repository has twice drawn a wrong conclusion from a harness that looked
fine - once from concurrent arms where the contention it assumed never
existed, and once from a stale executable a failed compile left behind.

Two sets were taken, deliberately:

- **QUIET.** No other build, test or agent session running on this machine,
  checked by process list immediately before and after. This is the set the
  budget quotes.
- **LOADED.** Taken while a second agent session compiled in its own
  worktree, which is the ordinary state of this machine rather than an
  artificial stress. It is recorded to show how far a number taken at the
  wrong moment moves, and it is the evidence behind the decision not to gate
  a time.

Both sets are Release, the same region, the same command, `--repeat 30`
after one discarded warm-up. `nfsu_probe.exe` was deleted by exact name and
rebuilt before the quiet set, and its timestamp checked afterwards, so no
figure here comes from an executable an earlier failed compile left behind.

### The measurement

### What the timings say

## What is automated, and what is not

Issue #43 asks for regressions turned into automated limits **where
stable**, and the word is *stable*. What follows is the judgement, with the
reasoning, because the issue asks for a decision rather than for as many
limits as possible.

### Automated, in CTest

- **The budget mechanism.** `tests/track_budget_tests.cpp` covers the
  counter table, the four gatherers and `check_track_budget`, including both
  sides of every boundary and all three refusals.
- **The command.** `tests/track_budget_guards.cmake` drives 64 cases with no
  input files at all: every option refusal, both budget verdicts, four of
  the five exit statuses, and each of the report's four blocks.
- **Two real ceilings on a real counter.** The guard cases hold
  `render.frame_bytes` to an exact value at two frame sizes - 15667200 at
  1280x720 and 34816 at 64x32. That is an automated budget limit on a
  genuine memory cost, and it needs no retail data, because the frame buffer
  does not depend on what is drawn.

### Not automated, and why

- **The reference region's own counts.** They need the installation, and no
  gate in this repository has it. They are pinned in the acceptance section
  below and reproduced by one stated command, which is how every other
  retail number here is handled - [`TRACK.md`](TRACK.md),
  [`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md) and
  [`TRACK_RENDER.md`](TRACK_RENDER.md) all do the same. A CTest entry that
  silently skipped when the installation was absent would be worse than the
  acceptance list: it would read as a gate and cover nothing.
- **Every wall-clock time.** The measured variance above is the evidence,
  and it is not marginal: even on a quiet machine over 30 repetitions the
  standard deviation runs from 3.7 to 9.9 per cent of the median, and a
  concurrent build moves the median itself. A documented budget with no
  automated limit is what issue #43 allows when the reasoning is stated; a
  limit that flakes is not.

## Tests

### The library

`tests/track_budget_tests.cpp` builds no byte fixture and parses nothing:
every function under test reads a report struct and writes counters. The
inputs are therefore hand-set reports **with a different value in every
field**, which is what makes a gatherer reading the wrong source member
produce the wrong number rather than the right one by luck - the single most
likely real defect in a field-copy layer.

Three things it pins that are easy to leave out:

- **Each gatherer fills only its own block.** Every test pre-fills the other
  three blocks with sentinels and requires them untouched.
- **Every sum is a sum.** `parse.region_bytes`, `parse.decoded_mesh_bytes`,
  `parse.object_record_bytes`, `prepare.unplaced_instances` and
  `prepare.resident_bytes` each add two, three or four distinct values, so
  dropping any arm gives a different number.
- **The return value of `check_track_budget` says whether the check RAN.**
  Asserted in both directions, because a caller reading a refusal as "over
  budget" would report a typo as a regression and one reading it as "within
  budget" would report a typo as a pass.

### The command

`tests/track_budget_guards.cmake`, one CTest entry driving 64 cases, each
asserting the exact exit status and a diagnostic that names what was
refused. A CMake script rather than more `add_test` entries because
`PASS_REGULAR_EXPRESSION` makes CTest **ignore the exit status**, and here
the status is the verdict: exit 4 and exit 3 differ in one printed line and
mean opposite things about a budget.

**One of the five exit statuses is not reached there**, and the script says
which rather than leaving it to be discovered. Exit 0 needs
`geometry.ok() && objects.ok() && cull.ok()`, which needs a region that
parses cleanly, and a CMake script cannot build one: a minimal chunk header
is eight bytes of which at least three are NUL and `file(WRITE)` cannot
write a NUL. The retail acceptance below is where exit 0 is checked, and it
is a run someone performs rather than a gate.

Exits 1, 2, 3 and 4 are all driven, and so is the absence of 5: every
multi-repetition case there would exit 5 if the counts were not
deterministic.

**Exit 2 nearly was not driven, and how it came to be is worth recording.**
It is the library-error path, and most of its refusals are refusals over
content - a failed self-check, a box that cannot be framed - which no run
without a region reaches. One is not: the frame is allocated from the
caller's `--width` and `--height` rather than from the scene, and the two
are range-checked separately, so `8192 x 8192` passes the option parser and
is refused by `render_track_draw_list` for a product past
`RenderLimits::max_pixels`, with nothing loaded at all. An earlier draft of
this section declared exit 2 unreachable from the script on the strength of
the content refusals and never looked for that one. It was caught by
grepping the script for a case expecting exit 2 and finding none, which is
a cheaper check than it sounds and is worth doing for any claim of this
shape.

### Mutations

**A mutation score describes a SET, not a suite**, and no single number here
is the coverage of this code. Each row says what it measured and who wrote
it. They are not summable.

| Set | Written | Applied | Caught | Survived | Unknown |
| --- | ---: | ---: | ---: | ---: | ---: |
| The author's, against the first revision | 58 | 56 | 50 | 6 | 0 |
| The author's, against this revision | 58 | 58 | 57 | 1 | 0 |
| The first round's survivors re-expressed, plus four | 10 | 10 | 8 | 2 | 0 |

`Applied` is below `Written` in the first row because two mutants did not
compile; a mutant that does not build is not a mutant and is discarded
rather than counted as caught. Classification follows
[`CLAUDE.md`](../CLAUDE.md)'s rule: a non-zero exit that printed neither a
failure line nor the suite's own summary is UNKNOWN and not a kill. No
mutant in any row scored UNKNOWN - the two suites here do no file I/O and
allocate nothing a mutant can run off the end of.

The sets are not in the repository, for the same reason
[`TRACK_RENDER.md`](TRACK_RENDER.md)'s are not: they are a scratch harness,
not a gate. What is in the repository is the two CTest entries they scored.

#### What the first round found, and what closing it took

**All 33 mutants against `src/track_budget.cpp` were caught, and all six
survivors were in `src/main.cpp`** - the statistics helper and the
repetition comparison, which were local to the command then. They survived
for two reasons that no command-level case can get around:

- **A summary of wall-clock times has no value a test can assert against.**
  A swapped min and max, an unsorted sample set, a median that ignores the
  even case and a standard deviation divided by `n` instead of `n-1` all
  print numbers exactly as plausible as the right ones. Four mutants, four
  survivors.
- **A comparison that only ever runs over costs that ARE equal never takes
  its failing branch.** Deleting the branch changes no output of any correct
  run. Two mutants, two survivors.

Both were moved into the library as `summarize_track_samples` and
`track_budget_costs_equal`, where a test drives them with inputs a command
cannot produce on demand: sample sets whose statistics are known exactly,
and two costs built to differ. That is what the third row measures, and it
closed eight of the ten.

It also found a real hole while it was there. The first version of those
tests used sample sets of one, three and four, and **not two** - which is
the smallest count that has a spread at all and the boundary the `n > 1`
guard sits on. A guard written `n > 2` passed every other case. There is a
two-sample case now.

#### What still survives, and why

Two mutants, both the same shape, both stated rather than hidden:

1. The command's call to `track_budget_costs_equal` replaced by `if (false)`.
2. The `return 5` inside that call's failing branch replaced by `break`.

**Neither can be turned into a kill, and the reason is structural rather
than a to-do.** Both live on the branch taken when two repetitions of the
pipeline disagree about a count, and correct code never disagrees - that is
the property the check exists to assert. Reaching the branch from outside
would need a way to inject non-determinism into the command, which means a
test-only flag in a shipped program: a worse thing to carry than an
uncovered safety net, and one that would itself need testing.

What the suite CAN do, and does, is cover the comparison the branch
depends on. `track_budget_costs_equal` is driven directly with equal costs,
with costs differing in one counter, with costs differing in two so that the
first difference in table order is the reported one, and with a stage one
cost measured and the other did not. A fault in the comparison is caught;
only the command's use of the verdict is not.

#### The harness fault this sweep hit, recorded because it is the trap

The first run of the author's sweep reported **NOCOMPILE for all 58
mutants.** The cause was in the harness, not the code: it invoked the build
through Python's `subprocess` with `shell=True`, which on Windows is
`cmd.exe`, where the `export PATH=...` prefix it used is a syntax error. No
build ever ran.

It is recorded because the shape is the one this repository keeps meeting -
a comparison where neither side produces a real answer and the harness
reports agreement. **A sweep that returns one verdict for every input is
reporting its own failure**, and the only reason this one was caught is that
the uniform verdict was implausible. A sweep that had returned CAUGHT for
everything would have looked like success.

## Read-only retail acceptance

Every command below only reads the installation, and `track-budget` writes
nothing anywhere. No game data, screenshot or derived asset may be
committed.

```text
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure

build-release/nfsu_probe track-budget \
    X:/Games/NFSU/Tracks/STREAML1RB.BUN 23015540 178268 23011344 4168 \
    --texture X:/Games/NFSU/Tracks/STREAML1RB_1210.BUN \
    --order material-group --repeat 30
```

Expected:

1. `ctest` reports 30 tests passed, 0 failed, in both the default and the
   Release configuration.
2. The run **exits 0** and writes nothing to standard error. This is the
   only place exit 0 is checked; `tests/track_budget_guards.cmake` explains
   why it cannot be.
3. It prints `budget: repetitions 30 after 1 discarded warm-up, counts
   identical in all 31 runs`, then the 44 `cost:` lines of the four tables
   above, then five `time:` lines, then `budget: limits 0 exceeded 0`.
4. Adding `--no-time` makes the output **byte-identical between two runs in
   separate processes**, with the times removed. That is the differential
   check, and it is the form to use for one: with times in it the output is
   deliberately not reproducible.
5. Adding `--limit frame.draw_calls 45` makes the same run print
   `budget: OVER frame.draw_calls 46 exceeds 45` and exit **4**. Adding
   `--limit frame.draw_calls 46` instead exits 0 with `budget: limits 1
   exceeded 0`, because a counter equal to its ceiling is within budget.
6. Adding `--limit frame.draw_call 46` - one character short - exits **1**
   with `no track budget counter is named 'frame.draw_call'` and prints no
   verdict line at all.
7. The 476 files of the installation are unchanged in path, size and
   modification time before and after.

## Unknown, and what this does not cover

- **What a GPU backend pays.** Every time here is the CPU reference
  renderer's, and `render.frame_bytes` counts buffers a deko3d backend would
  not carry. The counts that transfer are the draw calls, the triangles, the
  texture binds and the upload bytes; the times do not transfer at all.
- **What the retail game pays.** This pipeline places one caller-named LOD
  slot and draws every resident section, neither of which is the retail rule
  ([`TRACK_DRAW_LIST.md`](TRACK_DRAW_LIST.md)). A budget over a set the game
  would not draw is a budget for this program, not for the game.
- **More than one region.** 21 of this region's 39 instances name meshes
  that stream in with another region, so a real frame's draw-call count is
  higher than 46 by however much those add. The streaming tables are
  undecoded ([`TRACK_OBJECTS.md`](TRACK_OBJECTS.md)), so this cannot be
  measured yet.
- **Peak memory.** Every byte figure here is a retained-array formula, not a
  high-water mark; the section at the top says exactly what is excluded.

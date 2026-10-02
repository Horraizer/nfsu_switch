# Introductory Underground event0: setup and HUD contract

Bounded research for [#268](https://git.accad.pro/murzintsevav/nfsu-switch/issues/268),
under [#56](https://git.accad.pro/murzintsevav/nfsu-switch/issues/56).
The owner selected the introductory race before starter-car selection.
The implementation reference is main at
`6e65e30e43b1393ee5e32d5a075c86778e45603c`.
This note and its probe change no gameplay, actor, renderer or physics code.
They do not establish a playable event0 or close the complete acceptance target.

Evidence labels used below:

- **R**: recovered from pinned retail bytes and an identified consumer.
- **O**: observed in an identified reference image; event identity may be unknown.
- **P**: proposed port contract or implementation order, not a retail finding.
- **U**: untraced or missing original evidence. A plausible default is not proof.

## Reproduction and provenance

Run from the research checkout, including when its existing workspace path is
non-ASCII. Python 3.8+ and Capstone 5.0.7 are required. No retail executable is
launched. `--out` must be fresh and outside both input roots. The probe refuses
ancestors/descendants of either input, existing outputs and non-ASCII names
for any new output directory. Existing workspace ancestors are permitted.

```powershell
python -B tools/event0_trace_test.py
$reference = Join-Path (Get-Location).Path '../../../reference'
python tools/event0_trace.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --reference $reference --out build/268-research-final
```

The reference expression applies to the assigned detached worktree under
`build/268-gpt6-worker`. In another checkout supply the actual reference root.
Never derive that path from decoded native-process stdout.
Omit `--reference` when reproducing only the retail trace.

The report contains `data.json`, `disassembly.txt`, `operands.json` and
`before-N.json`/`after-N.json`. Each input manifest covers the root, every
directory and file, relative names, modification time, size and file SHA-256.
Symlinks are refused. Matching manifests are required even after a failed
extraction. A manifest PASS followed by an exception is a failed probe.
The final PASS is printed only after all reports were generated successfully.
The tool never overwrites reports or deletes inputs/old outputs.

| Input relative to retail root | Bytes | SHA-256 |
| --- | ---: | --- |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |
| `FrontEnd/FrontB.lzc` | 4182578 | `76F368D5E5E30F96A8182BF457DBA4491395E075C3F336D86A32012B56E16F90` |
| `Global/GLOBALB.BUN` | 2361176 | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` |
| `Tracks/TRACKB1003.lzc` | 433352 | `ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA` |

The uppercase `GLOBALB.BUN` is the pinned uncompressed database described in
[RACE_EVENTS.md](RACE_EVENTS.md). Do not substitute the installation's modified
`GlobalB.lzc`. FrontB decompression yields 12822772 bytes, SHA-256
`94A714D1E8E3BEF7F77AEDCEE4FDC6350E285B06059B92D392687710E64E5D77`;
TRACKB1003 yields 961328 bytes, SHA-256
`2B8A91ADD76C172A56A50AD0C10239F75E2102C8796A33F255F9E8C10CFFF6B9`.
These are input identities, not port replay/frame pins.

All addresses below are PE32 virtual addresses in that executable.
The probe maps through the section table and bounds each read; it does not
assume that a VA minus a constant is a file offset.
Chunk/record offsets in compressed packs refer to decompressed bytes.
`RANGES` retains complete functions including tail branches, with exclusive
ends and byte-range hashes in the report. `CONTEXT_RANGES` retains additional
surrounding code/data; its linear disassembly and optional `--refs` searches
are explicitly search context, not independently inferred function boundaries.
The code/data distinction matters at inline jump tables.

Synthetic guards exercise PE mapping/truncation, JDLZ literal/back-reference
bounds, nested chunk bounds/depth, preset/event identities and missing hashes,
event counts/direction, marker alignment/duplicates/non-finite positions,
track traffic indexing, catalog bounds and fresh/overlapping output locations.

## Entry, fixed setup and variable inputs

**R.** The confirmation branch in `0x004B37D0..0x004B388B` calls career reset
`0x005A0A00` at `0x004B3837`, then sets `EDI=0` and calls event selection
`0x005A0CE0` at `0x004B383E`. It subsequently opens
`MU_UG_NewCareerFMV.fng` (`0x006C2630`, used at `0x004B386C`).
The reset sets career `+0x348` to **1** at `0x005A0A65`; with the career at
`0x0075EEF8`, this is difficulty `0x0075F240`. It is not a guessed current
profile difficulty on this new-career path.

Selection indexes the 256-entry event-pointer table at `0x00733FF8`, copies
the chosen event through `0x005A2350`, and stores the setup at career `+0x88B0`.
That setup has the copied 332-byte event at `+0x58A0`, race entries of stride
`0xB0C`, a current-race pointer at `+0x5864`, and actor configuration list/count
at `+0x586C`/`+0x589C` (`0x005A2930` initializer).
The career race builder is `0x004B5DF0..0x004B62DA`; one continuation calls it
at `0x004E8635`. **U:** the complete movie/cancel/skip callback linkage and
its exact visible loading/flyby sequence have not been recovered. The entry
selection itself and builder consumers below are independently established.

| Field | Retail producer/consumer and value | Status |
| --- | --- | --- |
| Event identity | FrontB child `0x00034A02`, event0 record at 11744912; `+0` is 0 | R |
| Mode | Event `+8=0`; builder leaves ordinary runtime mode `0x0078A30C=1`; drag/drift flags `0x0078A344/345=0` | R |
| Race slot | Event `+0x78`: track 1003, two laps, direction 0; `+0x98=1` slot; `0x005A2460..0x005A2510` copies into setup | R |
| Current track/direction/laps | Builder calls the setup consumer `0x004ABC20`; targets `0x0078A2F0`, `0x0078A2FC`, `0x0078A31C` | R |
| Track provenance | GLOBALB `0x00034201`, track1003 record 2102144, stride252, `+0x69=0` lap-skip | R |
| Field size | One human at `0x0078A320`, event `+0x128=3` rivals at `0x0078A324` | R |
| Difficulty | New-career reset sets 1; builder copies `0x0075F240` to `0x0078A3FC` | R |
| AI scalar | Event `+0x13C/+0x140/+0x144`, all float1.0; builder difficulty-indexed read at `0x004B5F66` to `0x0078A380` | R; controller semantics incomplete |
| Traffic selection | Event `+0x134=1`, builder jump table `0x004B62DC`, result `0x0078A300=1` | R |
| Player grid | Event `+0x138=2`, consumed by `0x004B6300` after actor setup | R |
| Other scalars | Event `+0x12C=1.0` to `0x0078A404`; `+0x148=2` to `0x0078A40C`; `+0x130=0` retained in raw words | R bytes; U complete meaning |
| Profile/options | Builder reads current frontend/profile options; units/HUD gates and display name are separate from the fixed car preset | R reads; U complete option mapping |
| Random inputs | Settings reset advances seed `0x006F227C` and saves seeds at settings `+0x130/+0x134`; adaptive tuning and traffic also use it | R; U replay seed capture/ordering |

Event1 at the next record has the same track/laps/direction, but player preset
hash zero, a different rival roster (WART/PEUGOT, NESTO/NEON, JOSE/GOLF), and a
different requested player grid. The comparison is emitted by the probe.
Thus an event1 diagnostic cannot establish event0 setup.

## Player and actual instantiated field

**R.** Event `+0xD0=0xFFAAD92A` selects **DDAY_INTEGRA / INTEGRA**, preset
record 11918992 in FrontB `0x00030220`, stride `0x408`.
Preset lookup/application is `0x004B2640`/`0x004B26F0`; the builder calls the
latter at `0x004B60C8`. Nonzero hashes bypass the profile-car fallback.
At `0x004B60E8`, the event-number-zero branch writes eleven dwords of **1**
to RideInfo `+8..+0x30` (`0x004B60F2..0x004B6102`).
These are performance levels, not cosmetic upgrades or the AI-context byte.
In [VEHICLE_TUNING.md](VEHICLE_TUNING.md) order: weight, suspension, engine A,
induction, nitrous, engine B, gearbox, tyres, grip, aero A, aero B.
Ride initialization `0x00417600` clears the accompanying item fields.
Do not copy this order into a differently ordered CLI array without mapping
the names; [race_setup.h](../include/nfsu/race_setup.h) exposes the current
named performance builder from #250.

The visual definition is the preset's **120 ordered (slot,value) pairs** at
`+0x48`; the probe emits every pair, including zero/unused values, for the
player and all three rivals. It also emits executable slot names from the
120-entry table at `0x006F0970`, and all matching part-name hash catalog rows
from GLOBALB `0x00034604` (48-byte records) / `0x00034606` (strings), including
mesh hashes and shared-type candidates. This is a reproducible full preset
definition, not a replacement handwritten generic tuned appearance.

Selected player identities, for orientation:

| Slot | Value | Catalog identity |
| --- | --- | --- |
| 6 WIDE_BODY | `0xE24D93F6` | MANTIS |
| 7 ROOF | `0xE6A58592` | BLIZZARD |
| 8 HOOD | `0xE1F5B884` | BEAST |
| 11 SPOILER | `0xDFAB0978` | SWITCHBLADE |
| 13 HEADLIGHT / 14 BRAKELIGHT | `0x6C31F7C6` / `0xCF3E3079` | LUNAR / REAPER |
| 15 EXHAUST | `0x7A8337DF` | THUNDERBOLT, shared EXHAUST type |
| 17 WHEEL | `0x71B5F88E` | ENKEI RT6 18, shared WHEELS type |
| 30 BASE_PAINT | `0xB8BCBDE3` | GLOSS_L1_COLOR02 |
| 34 VINYL_LAYER3 | `0x00145B41` | DDAY; catalog record uses generic vinyl slot31 |
| 115 WINDOW_TINT | `0xC9D2B801` | DARK TINT YELLOW |

**U/P boundary:** catalog display names do not establish final RGB or active
geometry by themselves. `0x004B26F0` first initializes stock RideInfo, preserves
its seed fields, then applies special paint/vinyl/decal and shared-part rules.
`0x004B26B0` recognizes vinyl slots31..34; `0x004B26D0` recognizes decal texture
slots59..114, remapping their catalog lookup to slot58. A wide-body part can
disable other selected parts. The current `select_installed_parts` and
`derive_enabled` in [car_parts.cpp](../src/car_parts.cpp) provide useful
parts/disable infrastructure, but are not an event0 preset importer.
No claim is made that listing both the stock bumper and wide body draws both,
or that a hash with no ordinary part-name match is invalid. The full special
paint/vinyl composition and runtime texture resolution still need a bounded
importer trace and visual acceptance; do not discard those pairs silently.

The constructor `0x005809C0` produces configuration records of stride
`0x750`: stable signed-byte ID at `+1`, grid byte at `+3`, role at `+4`, copied
RideInfo at `+0x10`, context bytes at `+0x735/+0x736`, name at `+0x73C`.
Names are labels, not identities. The player name is supplied by the current
profile; rival labels come from event records at `+0xD4 + 0x1C*i`.

| ID | Role | Event label | Preset hash / name / car | Preset offset | Final grid |
| ---: | --- | --- | --- | ---: | ---: |
| 0 | 1 human | profile name | `0xFFAAD92A` / DDAY_INTEGRA / INTEGRA | 11918992 | 2 |
| 100 | 2 opponent | BRIAN | `0x155E2B37` / ECLIPSE3 / ECLIPSE | 11785864 | 1 |
| 101 | 2 opponent | BRETT | `0x7AD155FB` / Supra1 / SUPRA | 11786896 | 4 |
| 102 | 2 opponent | LOLA | `0xDFB87891` / Celica1 / CELICA | 11787928 | 3 |

**R.** Rival record offsets are 11745124, 11745152, 11745180.
Each has three s16 context values at `+0x10`, selected by difficulty:
BRIAN and BRETT `(0,0,0)`, LOLA `(1,1,1)`; s16 tuning selector `+0x16=-1`;
byte `+0x18=1`. `0x004B415C` applies the visual preset, `0x004B41EB` selects
the context value, and `0x004B4227` takes the -1 tuning path. That path computes
the player's performance rating via `0x0057F2B0`, then invokes
`0x0057F700..0x0057F96E` with the player RideInfo/rating and a 5.0 tolerance.
It copies the player's levels/items and adjusts levels with rating tests and
random choices. **Exact rival upgrade vectors are runtime outputs**, not fixed
vectors inferred from ECLIPSE/SUPRA/CELICA or from the small context values.
The semantics of byte `+0x18=1` and complete AI/catch-up behavior remain U.
The random-racer fallback `0x004B3C60` is not the event0 visual roster path.

**R.** Initial grid bytes are player4, BRIAN1, BRETT2, LOLA3.
`0x004B628B` supplies the event's desired player slot2 to `0x004B6300`, which
swaps player and the racer holding that slot. Grid order is therefore BRIAN,
player, LOLA, BRETT, not event-record order.
`0x00422580` hashes `ForwardStartingPosition` case-sensitively, loads markers
from TRACKB1003 child `0x00034146`, aligns its payload to16 and walks 48-byte
records. Marker `+0x0C` is zero-based grid index; `+0x10/+0x14/+0x18` are raw
world position; `+0x20` is the u16 angle. The first four grid markers are
at 264640, 264688, 264736, 264784. `data.json` gives all eight raw transforms.
Grid position numbers1..4 select marker indices0..3, respectively.
The loader ground-projects height through `0x00440A40`; raw marker height is
not the final sprung body's center-of-gravity height.
`0x00422A70` instantiates by grid byte and role: role1 selects factory
classification1, role2 classification3, both through `0x00422410`.
Factory/body class and race role are separate concepts.
**U:** final body orientation/CG offsets and controller-to-racing-line assignment
are not fully traced. Do not apply the reset helper's angle convention to an
initial grid transform without following its actual factory path.

## Traffic and routes

**R.** `0x004B3AC0..0x004B3C5D` reads the traffic selector, looks up track1003
with `0x00441FE0`, and reads the signed byte at track
`+0xC0 + 2*selector + direction`. Selector1/direction0 reads `+0xC2=6`.
The raw eight policy bytes are emitted, with the selected count.
Traffic setup chooses CarType class2 through `0x0057D000`, retries duplicate
choices (bounded to100 attempts), constructs RideInfo and role3 records,
IDs **50+i**, grid0. Its variation choices use random state.
`0x00422B29..0x00422B36` sends these role3 records to `0x00598BF0`, separate
from the four-racer grid factory.

This establishes six traffic configurations and a random model policy.
It does not establish six simultaneously visible cars, their live spawn count,
positions, retirement/reuse policy, or exact random seed sequence.
The `+0x148=2` consumer at `0x00422523` indexes `0x006B7C20` and writes the
selected coefficient to a controller field; do not relabel it a maximum speed
without its downstream consumer.
The progress course is track1003/forward. The existing racing-line research
in [RACING_LINE.md](RACING_LINE.md) and [RACE_ROUTE.md](RACE_ROUTE.md) is useful,
but the presence of several traffic paths does not identify each actor's path.
**U:** route assignment, lane selection, traffic activation and exact AI inputs
remain acceptance work for #258/#63. Follow the real actor controller and
authoritative course; never fabricate an opponent as a dot on a fixed spline.

## HUD inventory and visibility evidence

**R.** One human plus ordinary mode chooses `HUD_SingleRace.fng` in the package
selection at `0x0042B3DB..0x0042B47C`; the other branches name drag, drift and
two-player packages. `0x004A8E40` constructs components; construction alone
does not mean visibility. `0x0042E620..0x0042EB97` computes a 64-bit mask from
race stage, view state, view permission flags at `+0x30`, mode globals, profile
settings and other predicates. `0x004A9540..0x004A980C` applies changed bits.
`0x004A9440` dispatches component updates (runtime call `0x00430DE9`).
The table below distinguishes low/high mask words; similarly numbered bits
in different words are not interchangeable.

| Component (HUD object offset) | Visibility and concrete source | Evidence limit |
| --- | --- | --- |
| Speed/RPM/gear (+0x18) | Low `0x40`; `0x004A0F50` reads signed speed car `+0x3D4`, absolute display, units `0x0078A3F5` (m/s times3.6 or2.237); RPM `+0x3E0`; gear `+0x3D8`; max/redline select face via `0x00432A30` | R; MPH/gear/RPM visible in retail-01.webp and retail-05.jpg (O, event unidentified) |
| Turbo (+0x1C) | Shares low `0x40`; `0x004A13A0` calls `0x0042A6B0`, with value/availability checks | R consumer; U exact event0 visibility and boost units |
| Position/field size and RaceTime (+0x20) | Low `0x200`; constructor `0x004A00C0`, update `0x004A01A0`; lap record `+0x0A` running rank / `+0x0C` finish place; total uses configured racer counts | R; excludes role3 traffic from four-racer standings; U exact layouts/mask at each phase |
| Nitrous (+0x28) | High `0x40000`, permission bit `0x40` in view flags; `0x004A64B0` uses timing/resource fields `+0x3B0/+0x3B8/+0x3BC` and `0x00449F80`; positive installed capacity required | R consumer; player level1 exists; U runtime charge/refill and observed bar lifecycle |
| Finish message (+0x30) | Low `0x4`; `0x004A7D60` reads finish status/place/total, binds RaceOverMessage and vignette animation | R; U event0 outcome wording/transition to next frontend |
| Minimap backing/start/finish (+0x3C) | Low `0x1`; `0x004A8140` resolves TrackMapStart/Finish/backing. Separate map object is created by `0x0042EC10` -> `0x0058E540` when low bit1 changes | R package/controller split; O player triangle/track in the two unidentified race stills; U exact opponent filters/colors/map projection |
| Lap/current/best (+0x40) | Low `0x400`; `0x004A1820`: LapNumber is signed highest-lap byte `+9` plus1, NumberLaps status `+0x530`, per-car lap starts/best arrays; completed-lap pulse `+0x0D`, held lap time at `+0x38` | R; O lap/current/best in the two unidentified race stills; U appearance at event0 start/finish |
| Leaderboard/names/gaps (+0x44) | Low `0x800000`; `0x0042E945..0x0042E98C` additionally gates by `0x0078A3F9`, mode and view flags. `0x004A3930` reads sorted status pointers `+0x520`, real names and `0x004353E0` gaps | R conditional component, not an always-on four-row assumption; U event0 options/layout/audio |
| Wrong way (+0x48) | Low `0x4000`; `0x004A3C60` reads view wrong-way flag `+0x4AC`, applies FadeIn/Blink/FadeOut | R; U continuous original behavior/reset notification |
| Split/lap notice (+0x4C) | Low `0x80`, view permission bit4; `0x004A1BF0` consumes per-car lap/split pulse `+0x0D/+0x0E`; view `+0x26` selects helpers `0x004A1D00/1D90/1E20/1E80`; notice expires after three world seconds | R dispatch/units; U event0 selected comparison variant, so do not always label it a gap to leader or best lap |
| Style points/notices (+0x50) | Low `0x20000`, view flag `0x400` plus mode checks; `0x004A83C0` reads view car `+0x9DC` and notice queue | R conditional ordinary-race component; U scoring producer and exact event0 visibility |
| Countdown/GO (+0x58) | High `0x20000`, final mask condition on `0x00700E8C`; `0x004A8000` calls `0x00434D10`, formats 3/2/1 or localized GO hash `0x2B9ADC01`, animates with `0x00434DD0` phase | R; U complete option/global gate and original footage/audio |

Mode-only components are also allocated by the shared constructor: heat meter
(+0x24, low `0x20000000`, drag predicate `0x00421A50`), smokeshow (+0x2C,
low `0x80000000`, `0x0041F600`) and drift score (+0x34, low `0x10000`,
`0x0042AB30`). Event0 clears these mode flags. Their assets are not evidence
that event0 should show drag heat, smokeshow or drift standings.
Online data/countdowns, time-trial-specific clocks and two-player placeholders
are separate predicates, not default event0 requirements.

Current lap display and timing are different values. `0x004A1820` holds a
just-completed lap value for three world seconds; it does not stop the actual
lap clock. It selects the current lap start from lap-record `+0x24` onward,
best from `+0x60` onward, and uses a localized placeholder when no lap start
is valid. The formatter handles the `0x7FFFFFFF` no-time sentinel separately.
Do not replace missing best/current with a made-up numeric time or advance
lap state inside the drawing function.
`0x004A2B10` has timed/changed-rank leaderboard presentation; all rival labels
being available does not mean they are simultaneously visible at every frame.

## Progress, ranking, clocks and lifecycle

**R.** The shared race-status/timing consumers are usable for this ordinary
event0 mode; they do not make the existing event1 route driver an event0 AI.
World clock `0x0073AD34` is in 1/4000-second ticks. `0x00585E00` adds rounded
world-frame delta; zero paused delta adds nothing. Status `0x00435570` uses
that clock. Stage setter `0x00434B50` holds/releases drivers and initializes
race/lap start times on GO. `0x00434D10` counts down in intervals from status
`+0x0C`; that interval is chosen at `0x00434316` based on `0x0078A368`.
Career settings reset `0x00431DF0` clears that flag, and the event0 builder
does not set it: the recovered default countdown interval is 0.9 seconds,
not a guessed whole second. A later untraced override remains possible until
the complete entry runtime is observed. Do not derive countdown from frames
rendered or target FPS.

`0x004353E0` uses the stored total times when both racers are finished.
For running racers it maps unwrapped lap/progress coordinate `+0x20` through
`0x00433340`, using the race's progress-to-time table at status `+0x564`.
Thus a live time gap is not simply Euclidean distance / current speed, nor a
constant seconds-per-meter guess. **U:** population/calibration of that table
for the complete event0 field; represent an unavailable gap explicitly.

**R.** `0x00434ED0..0x00434F7F` sorts the status `+0x520` pointer array with
adjacent swaps. When both finish-place bytes `+0x0C` are nonzero, smaller
finish place sorts first. Otherwise greater progress `+0x20` sorts first.
Equal progress does not swap; equal finish place does not swap. Ties retain
the array's previous order, not a newly applied name or stable-ID tie break.
It copies old rank `+0x0A` to `+0x0B` and writes the new one-based rank to
`+0x0A`. Preserve that previous-order input in a compatible sorter.
Initial array order is U until registration is traced; physical grid order
alone does not establish it.

Finish check within `0x00435570..0x00435A64`, at `0x00435A17..0x00435A5D`,
finishes a running car whose highest lap reaches the configured lap count.
`0x004350D0` records finish state/total/place; `0x00435BD0` returns the first
unused finish place. This is not sorting all same-frame finishes by their
interpolated crossing timestamp. **U:** exact same-frame actor update order;
do not call an arbitrary port actor-ID loop retail parity.
The crossing/backdating/rounding algorithms are documented in
[RACE_TIMING.md](RACE_TIMING.md) and implemented by `step_race_timing`;
current regression values belong in its tests, not this document.

| Phase/action | Recovered source behavior | Missing event0 observation/trace |
| --- | --- | --- |
| Loading/new-career movie | Event0 selected before NewCareerFMV; ordinary HUD package chosen when race view is created | Full movie-skip, camera/flyby/grid ordering and visibility are U |
| Prerace/flyby/grid | Stage logic and HUD mask have dedicated branches; cars instantiated by grid byte | Exact default entry path/camera durations and mask values are U |
| Countdown/GO | Stage3 -> stage4, 3/2/1/GO consumer, driver release, authoritative clock | Visual cue placement/animations, audibility and first controllable frame are U |
| Racing | Gauge/lap/rank/conditional HUD consumers read current car/status; traffic is separate | Actual visible default option combination, map rivals and cadence on hardware are U |
| Lap/split crossing | Per-car progress updates timing and pulses; HUD holds completed lap/split transient while world clock continues | Event0 selected split comparison and visible timing transitions are U |
| Wrong-way/car reset | Wrong-way flag controls notice; existing timing path keeps clock running for a car reset | Exact reset notice/audio and all actor/controller reset effects are U |
| Pause | Zero world delta stops timing; lifecycle must freeze every actor and AI, not only player | Complete event0 pause-menu bindings/options and animation clock choice are U |
| Retry | Existing restart handlers tear down/rebuild world and race status; do not reuse old timing arrays | Whether random setup is reused/reseeded, all traffic/AI/resource resets and intro retry transition are U |
| Finish/results | Finish place/total latch; RaceOver component animates from actual outcome | Introductory win/loss/scripted outcome, PostRaceResults rows, continuation to starter choice and audio are U |
| Quit | Timing teardown does not create a normal finish result | Exact intro exit destination/profile effects are U |

No continuous clip was present in the discovered reference directory (30
stills). `retail-01.webp` and `retail-05.jpg` show ordinary race gauges, player
map marker, lap/current/best timing and no visible field panel in those frames.
They do not show entry or identify event0. `retail-03.webp` shows an Underground
menu with an RSX background; it does not prove the introductory car or outcome.
Absent elements in two stills do not prove absence in event0's full lifecycle.

## Proposed integration contract (P)

These names are a bounded interface design; they are not new retail symbols or
production types added by this PR. Resolve U fields with explicit validity,
never silent placeholders that look like measured retail state.

| Field/group | Meaning and units | Producer, cadence and lifetime | Consumers/reset/finish semantics |
| --- | --- | --- | --- |
| `session_generation`, `config_id` | Monotonic generation and pinned event/source identity | Setup owner once per start/retry, immutable for generation | Reject stale snapshots; new retry generation clears all notices/results |
| `event_number`, `track_number`, `direction`, `lap_target`, `mode` | 0,1003,forward,2,ordinary circuit | Validated event0 setup once | Course/HUD/audio; no event1 substitution |
| `difficulty`, `seed_snapshot`, `profile_options` | New-career1; recorded seeds; explicit units/map/leaderboard preferences | Setup controller once; user option change as explicit event | Distinguish fixed defaults, profile inputs and random outputs |
| `actor_configs[]` | ID, role, preset hash, complete ordered slot pairs, eleven named levels/items, final grid index, AI context | Setup importer builds player/rivals and traffic records; resolved adaptive levels saved per actor | Physics/rendering share the same resolved config; traffic excluded from standings; importer must fail unsupported semantics |
| `grid_marker`, `initial_pose` | Raw marker identity and final world transform, position in world meters, orientation convention explicit | Grid/factory adapter once; ground and CG conversion owned by physics setup | Four racers use their own slots; never initialize every racer at the first marker |
| `stage`, `paused`, `world_ticks`, `race_start_ticks` | None/prerace/flyby/countdown/racing; authoritative 1/4000 s ticks | Session/lifecycle owner, once per committed simulation tick | Countdown/timing/audio read; pause advances no actor/race time; drawing advances none |
| `actors[]` telemetry | Stable ID/role, world pose, m/s signed speed, RPM, gear, max/redline RPM, resource validity/values | Each independent actor after the approved solver step; immutable published snapshot | Gauge/map/contact audio; no shared mutable VehicleState; live traffic uses actual active actors |
| `progress[]` | Track projection, unwrapped lap/progress coordinate, highest/current checkpoint/lap, wrong-way/reset flags | Course/progress producer after physics, once per actor tick | Timing/rank/map share projection; validity required for off-route state |
| `timing[]` | Start/crossing/lap/best/total in ticks plus valid flags; finish state/place | Per-actor timing from crossing events, rounded/backdated by existing rules | Latch finished total/place; car reset preserves timing; session retry clears it |
| `standings[]` | Stable actor IDs, rank, previous rank/order, finish place, optional signed gap ticks and reference ID | Race-wide status pass after all committed actor progress/timing | Preserve equal-progress previous order; same-frame finish policy must be resolved; traffic never receives race places |
| `hud_permissions`, `visibility_mask`, `view_actor_id` | Options, computed 64-bit component mask, real view actor identity | Controller/lifecycle pass after status; recompute on option/view/stage change | UI applies mask; component existence alone is insufficient |
| `hud_events[]` | Sequenced lap/split/rank/wrong-way/countdown/finish/style events with actor ID, world tick, reference/value validity | Event producers once per transition; retain until acknowledged/expired | Renderer/audio deduplicate by generation+sequence, not render count; separate presentation time only where approved |
| `results` | Actual outcome, ranked racer IDs, final totals/best/lap data, unresolved intro continuation flag | Race-result owner at authoritative finish/exit policy | #269 view consumes latched result; no synthetic winner, race completion, or traffic rows |

Publish one coherent snapshot **after** physics, progress, timing, standings
and lifecycle decisions. Rendering may interpolate poses between snapshots;
it does not extrapolate lap/rank/time or manufacture rival state.
Current `RaceSession` is explicitly one event1 player session, and
`build_race_hud_frame` reads that one session. It cannot expose an event0 field
by merely adding labels. The existing const HUD boundary is useful and must
survive a race-wide snapshot adapter.

The intended ordering is a port contract, not a recovered complete retail
frame schedule. #256/#257 must settle solver/pad/physics sequencing before
#258 depends on it. Same-frame finish ordering, RNG consumption, restart seed
policy and exact map/controller/resource producers remain marked U.

## Bounded implementation sequence and ownership gaps

| Order / owner issue | Concrete next scope / acceptance |
| --- | --- |
| Coordinator decision, no assigned implementation owner | Choose a bounded event0 setup/preset importer task: explicit event selector; validated four-racer configs, all preset pair semantics, named performance1, per-grid transforms, recorded options/RNG. #250 is a performance builder, not this importer. Keep entry/save/shop systems outside it. Resolve unsupported paint/vinyl consumers and initial rank registration. |
| #256 then #257 | Approved physics/handling pipeline first. This research does not waive steering/performance owner acceptance or choose a new solver. Baseline steering remains default. |
| [#258](https://git.accad.pro/murzintsevav/nfsu-switch/issues/258), with #63 | Real independently simulated roster/grid, adaptive rival setup, AI input controllers and real traffic population. Trace route assignment, catch-up, seeds and active traffic lifecycle; publish stable actor snapshots. |
| [#254](https://git.accad.pro/murzintsevav/nfsu-switch/issues/254) | Per-actor laps/current/best/total from authoritative crossing/timing; valid sentinels and lap transient; selected split variant verified instead of guessed. |
| [#266](https://git.accad.pro/murzintsevav/nfsu-switch/issues/266) | Map route and real player/opponents from shared course/actor snapshots; trace map projection, marker filters and colors; no dummy/racing-line-only markers. |
| #253 | Reuse recovered gauge/font/material assets with INTEGRA's actual resolved max/redline, units, resource availability. Keep event0 nitrous/turbo/style conditional rather than copying drift/drag UI. |
| [#269](https://git.accad.pro/murzintsevav/nfsu-switch/issues/269) | Authoritative standings/gaps/results and conditional leaderboard. Preserve previous order for ties; resolve initial/same-frame order and gap table. Obtain intro-specific result/continuation evidence before declaring parity. |
| [#67](https://git.accad.pro/murzintsevav/nfsu-switch/issues/67) | Race-wide pause/retry/quit, generations and notice invalidation; reset player/rivals/traffic/timers/AI/resources together. Trace intro retry/quit policy. |
| [#202](https://git.accad.pro/murzintsevav/nfsu-switch/issues/202) | Event0 mode/interface cues from actual transitions; determine whether countdown/GO, wrong-way, lap and finish cues are audible in the requested clip. A cross-build is not listening acceptance. |
| [#222](https://git.accad.pro/murzintsevav/nfsu-switch/issues/222) | Contact events from real physics participants, sample/gain/clipping acceptance. HUD never generates impacts from a position change alone. |
| #259 / #270 | Preserve static/moving object and prop lifecycle boundaries; this branch neither adds props to the roster nor replaces their frame phases. |

Additional ownership decisions: style scoring/notice producer and nitrous/turbo
resource simulation are not owned merely because a HUD issue can draw them;
intro camera/entry/results continuation and gap-table calibration need a
bounded owner if they fall outside the listed issues. The coordinator should
assign that scope explicitly rather than expand a HUD worker's physics task.

## Missing original evidence and acceptance still open

[CONSOLE_REQUEST, #268 comment7020](https://git.accad.pro/murzintsevav/nfsu-switch/issues/268#issuecomment-7020)
requests the exact new-career entry/settings and continuous event0 start,
lap/split, wrong-way/reset, pause/retry and finish/results with audio. It is a
coordinator-scheduled request; this worker did not deploy or ask the owner to
relay messages. The stills do not satisfy it.

That clip can establish visible default HUD elements and transitions, menu
wording, intro outcome/continuation, names/grid appearance and audible cues.
It cannot by itself establish RNG state, eleven opponent upgrade levels,
exact actor update ordering, route assignment or a numeric gap-table producer.
Those require runtime/source evidence. Match any future executable/clip to
its own input identity rather than quietly treating a modified installation
as this pinned build.

For the later handheld gameplay measurement use `--scene-height 540
--gpu-boost on`: intended scene960x540 and requested GPU boost+20 percent.
Record measured clocks separately from the request. HUD/presentation size,
fixed-resolution parity captures and gameplay FPS are separate measurements.
This research run made no hardware performance, driving or listening claim.

The partial PR is ready for independent review of the source-addressed setup,
evidence distinctions and reproducible tool. Complete event0 acceptance stays
open until the U rows, original lifecycle observations and actual gameplay
implementation/owner acceptance are satisfied.

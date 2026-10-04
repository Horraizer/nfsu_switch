# Original-versus-port audio investigation (issue #312)

Research at `ec21749671603a1505a63148feb3806d0a1c5059`, following the
owner's engine/transmission AND collision/scrape report on Switch candidate
`b4fe1e03de6998a8ab8cced3d9a4b30120560d94`. This is one bounded investigation,
not a production fix or a listening verdict. Assignment:
[issue #312, comment 8387](https://git.accad.pro/murzintsevav/nfsu-switch/issues/312#issuecomment-8387).

The strongest discrepancies are bank selection and the engine control graph,
followed by collision episode lifetime. Changing only loudness or the sample
decoder would leave those differences in place. Exhaust and shift paths are
also absent from the port; the original instructions establish their presence
but do not yet provide a complete live control trajectory for the intro car.

## Reproduction and evidence boundary

Run from this checkout with a standalone Python 3.8+ interpreter. The worker
used standalone Python 3.12, Capstone 5.0.7 and Unicorn 2.1.4. Dependency files
and reports stay in ignored directories; choose a fresh report directory on
each run. The code derives the checkout through `Path.cwd()` and uses ASCII
relative paths, including when the checkout's absolute path is not ASCII.

```powershell
& 'C:/Users/horra/AppData/Local/Programs/Python/Python312/python.exe' -m pip install --disable-pip-version-check --target build/audio312-deps capstone==5.0.7 unicorn==2.1.4
$env:PYTHONPATH=(Join-Path (Get-Location) 'build/audio312-deps')
& 'C:/Users/horra/AppData/Local/Programs/Python/Python312/python.exe' -B tools/audio312_trace.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --out build/audio312-trace
```

[audio312_trace.py](../tools/audio312_trace.py) checks every input SHA-256
and the LF-normalized hashes of five production source files before comparing
anything. These checks bind its small analytical port models to this source
revision. It does NOT execute a compiled port, compare rendered PCM, or launch
the original game. It executes original x86 instructions in Unicorn through
[audio312_native.py](../tools/audio312_native.py), with bounded addresses,
an instruction cap and explicit observation boundaries. The x87 control word
is explicitly set to normal round-to-nearest `0x037F`; a live game's per-thread
floating-point environment is not captured. The entire retail
tree's file hashes, sizes, mtimes and directory mtimes are compared before
and after, including on a failed trace.

The fresh output contains:

- `report.json`: input identities and sizes, source hashes, original callback
  and group tables, bank exports/PT metadata, stock bank selection, native
  engine controls, contact expiry/fade/intensity, spatial gain and a synthetic
  original RNG sequence. Numerical witness acceptance pins live in the tool.
- `instructions.txt`: bounded PE disassembly and ABK initializers with callback
  ordinals. Native addresses below are PE virtual addresses; ABK addresses are
  offsets in the named file. All range ends in the tool are exclusive.
- `before.json` and `after.json`: full input-tree manifests. No sample bytes,
  PCM files, executable copies, recordings or screenshots are emitted.

The runner deliberately rejects a changed input, stale output, an output in
the retail tree or a tracked directory, non-ASCII new output components, an
instruction outside the allowlist, an explicit bank deletion, and an engine
event whose group disagrees with the bank. A deletion request must not be
ignored just to obtain a steady-state measurement.

The original image is `speed.exe`, 3178496 bytes, SHA-256
`c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7`.
`Global/GLOBALB.BUN` is 2361176 bytes, SHA-256
`39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9`.
The six banks' complete identities are in `INPUTS` and the generated report.
This executable's regional/build identity must not be inferred for a different
retail installation merely from its filename.

## Engine: stock bank selection is now connected

The tool walks the top-level GLOBALB chunks, aligns chunk `0x34600`'s record
array to 16 bytes, and finds `240SX` as record 18 at file offset `0x1A8030`.
This identity does not come from the sound filename.

The original descriptor path `0x51E030..0x51E0BA` reads car type at `+0x10`,
calls `0x520320`, and indexes `0x6F8100`. The executed stock witness maps
car type 18 through sound index 18 to group 2. The ordinary upgrade path
reads descriptor `+0x20`; a separate global override can select upgraded
banks, so the witness explicitly fixes the stock upgrade to zero.

The executed producer fragments `0x51E0F4..0x51E17C` write engine type 12
and exhaust type 13. Each event record stores **type, upgrade, group** at
`+0x1E8`, `+0x1EC`, `+0x1F0` for record zero. The old RACE_AUDIO paragraph
reversed the last two fields; this investigation corrects that description.
`0x522020` and `0x521F80` copy the arguments into the manager, and
`0x521C80..0x521E0E` chooses the directory and bank name:

| Stock event | Original lookup result | Port at the audited source |
| --- | --- | --- |
| Engine, type 12 | `Car_2\Eng_UG_0\C2U0_M0_ENG_MB` | `CAR_0/ENG_UG_0/C0U0_M0_ENG_MB.abk` |
| Exhaust, type 13 | `Car_2\Eng_UG_0\C2U0_M0_EXH_MB` | No exhaust bank or separate exhaust voices |

This is a concrete stock-240SX discrepancy. It does not establish the
intro event0's tuned upgrade or listener settings. Correct bank selection
needs the actual car/upgrade descriptor rather than another fixed filename.

## Engine: sample order is not four adjacent RPM bands

`0x5236D0..0x523888` consumes the audio object's RPM at `+0x1F1C` and load
at `+0x1F8C`. It scales them with floats at `0x6CCAA8` and `0x6CCAAC`,
converts through `0x674898`, clamps the event controls, and supplies gain,
crossfade and pan. The witness executes this producer with the event handle
zero to stop before event dispatch into `0x64BC30`.

The CAR2 engine ABK's first module has initializer `0xA8` and template
`0x64C`. The exhaust initializer also starts at `0xA8`, with template
`0x644`. Both export `PLAYER_CAR`. The runner relocates callback displacements
using original table `0x6E9AE8`, following `0x64DF82`, and relocates bank
pointers in its in-memory copy. It executes the bank program and original
integer lookup, product, oscillator, edge and ramp callbacks. It records the
four Player nodes immediately before `0x64D860`; it does not run the sample
backend or claim its resampler, voice state, phase or PCM. These engine graphs
do not consume the Player return values. The deletion callback is intercepted
only while its request is zero; any nonzero request fails the fixture.

The report sweeps audio RPM 800/2500/4000/7000 and audio load 0/50/100 for
CAR2 engine/exhaust, plus CAR0 engine as a counterfactual with group 0. Every
case starts from the bank template, uses full event gains, and runs
120 graph ticks. A second set holds RPM at 2500 and changes load 0 -> 100 -> 0
without resetting the graph. `713CA0` is supplied a synthetic 60 Hz interval.
The original live cadence, upstream smoothing and category gains are NOT
recovered by that choice. Port comparisons set throttle to load/100 only to
expose the different layer rules; that equivalence is not a live-game claim.

At `ec217496`, the command above, CAR2 engine at audio RPM 2500/load 50
after settling: all four original players have positive gain, while the
source model uses only sample 2. The original pitches form two pairs
(samples 1/3 and 2/4), rather than RPM divided by four layer centers. The
native gain/pitch values are pinned in `witnesses()` and printed in the report.
At fixed RPM, load changes the relative sample mixture; the port's throttle
only scales the existing RPM weights. That predicts a timbre and pitch
difference even after correcting CAR0 to CAR2. It does not establish which
sample a listener would label "on load" without listening.

The exhaust graph has additional state-dependent player release behavior in
the load transition witness. Replacing it with one always-looping exhaust
sample would discard measured behavior. Exact histories remain in the report;
they are synthetic graph controls, not measured console output.

## Throttle/coast and transmission: linked paths, incomplete live trajectory

Static original instruction evidence adds these constraints:

- `0x523C30..0x523D6A` reads actor throttle at `+0x1DC`, scales it into the
  audio object, and uses mode/timer branches. `0x523BFE` copies a smoothed
  audio value into `+0x1F8C`. Feeding raw physics throttle straight into the
  bank is therefore not yet a proved replacement.
- `0x523EC0..0x524003` consults shift state and curve objects at `+0x1B94`
  and `+0x1C10` while shaping audio RPM/load. Recover these state transitions
  before claiming gear changes match. A trace of steady RPM cannot do so.
- `0x526AF0..0x526B4B` compares new and previous gear on the relevant actor
  event and calls different vtable entries for the two directions.
- `0x529100..0x5292AD` is the shift consumer: it derives gain from audio RPM,
  applies a category/spatial hook, replaces an existing handle and calls
  `0x5257F0`. The latter packs six bounded controls and resolves descriptor
  `0x6F2240`. `SHIFTING_SML_MB.abk` exports `FX_SHIFTING_01`; its initializer
  `0x9C..0x148` selects through a table containing silence and samples 1/2.

The port audio snapshot has no gear/shift event and creates neither exhaust
nor shifting voices. This omission is concrete; the exact shift timing,
variant selector, tuned event0 bank and original mix level are still open.
The shifting graph consumes the Player return for lifetime logic, so the
engine witness's observational Player stub is intentionally NOT reused to
claim an executed shift graph or a complete transmission result.

## Collisions: preserve the proven parts, correct the episode boundary

The original world event/sample link remains consistent with
[COLLISION_AUDIO](COLLISION_AUDIO.md) and the bounded evidence behind
[PR #279](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/279):
world impacts resolve `IMPACT_WORLD`, sample families 30..42, with subtype
round robin and graph gain/pitch; world scrape resolves `SCRAPE_WORLD` with
its one carrier and graph modulation. The new runner reproduces the bounded
instruction listings and independently checks bank identities/exports.
It does not re-run PR279's C++ gates or convert its acceptance into live parity.

`0x465830` adds each current impulse magnitude to record `+0x60`, updates
last-touch clock at `+0x0C`, and supplies motion/direction fields. Constructor
`0x465640` clears the accumulator. Pair allocation at `0x467220` stores
the record in pair `+0x264`; `0x467300` reuses a nonzero record. Audio attach
`0x530840` selects the world curves at `0x7367A4` and `0x7367C0`.

The missing physics-side lifetime evidence is now bounded directly:
`0x420AB0..0x420AD7` compares record last touch with the same clock conversion
and expiry threshold used at `0x530A90`. The witness independently executes
both branches. At `ec217496`, the reproduction command's contact-age witness:
both keep a surviving record across a roughly 17 ms gap, and both expire at
1200 original clock units (about 300 ms). `0x5307E0` then builds a scrape
retirement curve through `0x532670`; the same run measures about 133 ms from
gain 1 to 0. The boundaries and fade are pinned in `witnesses()`.

In contrast, `CollisionScheduler::update` in the pinned `collision_audio.cpp` starts
a new episode when `step > last_step + 1` and retires unseen pairs on that
same short horizon. The source-model case with the same pair observed on
120 Hz steps 1 and 3 produces a new impact on step 3. The original age paths
still keep the record at the corresponding short gap. The port also drops
the scrape into the generic mixer's five-millisecond gain ramp.

Predicted consequence: brief gaps during a wall rub can restart impact
variants, reset accumulated intensity and scrape modulation, and cut tails
early. This is conditional on the original body pair surviving; another
destruction path can retire it earlier. Original body-pair identity has NOT
been mapped to the port's polygon IDs. Adjacent polygons must not be merged
just because a longer age threshold seems plausible.

## Collision precision, randomness, admission and spatial gain

The native intensity witness executes the original world curve initializer,
`0x530F80`/`0x531030`, interpolation `0x532840`, and conversion `0x674898`.
That last helper adjusts its FIST result to truncation toward zero. The port
uses nearest-even for intensity as well as for graph arithmetic. These are
different operations: the nearest rounding in graph lookup/product does not
justify nearest rounding at the producer.

At `ec217496`, the reproduction command's subtype-5, impulse/motion-0.5 case:
both native intensity indices are 1, both analytical port indices are 2.
The report includes all seven world subtypes across weak, middle and clamped
inputs. This is a small but concrete curve-addressing difference, especially
at weak-contact boundaries; it is lower priority than lifetime and engine
selection. Do not globally replace every graph conversion with truncation.

`0x6511B0..0x651255` uses six shared state words at `0x713FCC..0x713FE0`
with additions, carries and a counter. The runner executes it from an
explicitly synthetic six-word seed. The port uses a private xorshift stream.
Original random-range callback `0x64CA50` and scrape deck `0x64CA80` consume
the shared generator. Algorithm, initial state and consumption order are
three separate requirements; only the bounded algorithm is recovered here.
The original scheduler's clock fragment `0x651E84..0x651EFA` also does not
establish that every graph tick is exactly 1/60 second in a live race.

Original impact admission `0x531FF0..0x532270` checks prior-record flags,
motion/direction and competing actor records before starting a sound. The
port's two alternating impact slots and two scrape slots are a bounded voice
policy, not a reproduction of that admission. No fixed ten-tick cooldown
was found, and adding one would not implement those checks.

`0x530740` composes record attenuation and listener/category gain. At
`ec217496`, the reproduction command's isolated input-22000, half-attenuation,
half-category case yields 5500. The source path supplies subtype gain without
those stages. That is a proved missing conditional transformation, not proof
that the player's actual default gain should be quartered. Original listener
state, pan/distance handling, live pool order and car/prop contacts remain open.

## Decoder, rate, loops and output: alternative causes checked

The PT metadata report distinguishes what is present in the banks from the
port's interpretation of absent defaults. The inspected engine, exhaust,
shift and scrape samples declare PCM codec 8. Engine/exhaust are mono
22050 Hz; shift/scrape are mono 44100 Hz. World impacts declare codec 10
at 32000 or 44100 Hz. Consequently an EA-XA predictor error cannot explain
the engine-bank/layer discrepancy. It remains a possible independent impact
decoder issue until compared to an original decoder oracle.

`audio.cpp` reads sample rate and loop tags, treating tag `0x87` as inclusive
and adding one. The inspected engine loops start at zero and span their
samples under that interpretation. Some exhaust/scrape loop ends exclude a
final frame; the port preserves the explicit loop end rather than always
looping the whole buffer. The metadata gives no basis for changing every
sample to 44100 Hz or erasing loop tags. Inclusive-end semantics and output
phase still need an original backend witness for bit-exact parity.

`audio_playback.cpp` converts clips and AST output to 48 kHz using linear
interpolation, loops explicit regions and uses gain ramps. Race mixing adds
a stereo-linked limiter and final PCM16 clamp. These are port choices;
original interpolation, filtering, headroom, mix balance and backend pan have
not been measured. Existing `nfsu_audio_retail` pins prove decoded data stays
consistent with that decoder, not independently correct against original PCM.
Neither this research nor a zero clipping counter proves that a console has
no underruns or that its final mix sounds correct. No matched recordings or
original runtime capture were available for this assignment.

## Smallest correction sequence and owner A/B cases

No implementation task is opened or reassigned by this report. Closed #65
remains the delivered bounded playback task; collision implementation and
acceptance remain in #222, and race/UI/music ownership remains in #202.

| Order | Evidence-backed next change or prerequisite | Discriminating validation/listening case |
| --- | --- | --- |
| 1 | Resolve car group/upgrade and reproduce the selected engine graph's sample/gain/pitch controls. Preserve the native witness as an oracle. Resolve actual event0 tuning rather than calling stock evidence tuned parity. | Fixed gear and RPM sweep, then hold near the middle of the range and change throttle/coast. Compare bank-only correction with full graph correction at matched perceived loudness. |
| 2 | Recover upstream audio RPM/load and gear transitions, then add separate exhaust and shift event graphs. The present witness intentionally stops short of these live producers. | Steady throttle versus release at the same RPM, low- and high-RPM upshifts, downshifts, and neutral revs. Listen for load timbre, exhaust release and synchronized shift cues rather than only volume. |
| 3 | Map durable collision body/contact identity, then implement original record aging and the scrape retirement envelope. Keep accumulator/variant/RNG state across verified same-pair gaps. | One light wall tap; continuous wall rub with a brief separation and recontact; recontact after a longer separation. Distinguish premature retrigger/cutoff from wrong sample family. |
| 4 | Correct intensity truncation locally, reproduce admission and attenuation with explicit fixtures, then recover RNG seed/order and control scheduling. | Weak taps around intensity boundaries, alternating left/right impacts and sustained scrape modulation. Original-vs-port event logs must show which record was admitted before comparing sound density. |
| 5 | Once event/control paths agree, compare decoder/backend PCM and output configuration, and tune neither resampling nor limiting without that evidence. | Rate-specific isolated clips and loop seams, then the same race with music reduced and normal. Record output underruns/late cycles, raw overload and limiter counters alongside listening. |

Use the same car, upgrades, camera/listener, route, race phase and audio
settings on both systems, including the actual intro event0. Capture control
timelines and audible output only with owner authorization; retain retail
assets locally. The owner can then distinguish an implementation improvement
from a louder mix. Hardware listening remains an acceptance step after an
independently reviewed implementation, not an outcome of this research PR.

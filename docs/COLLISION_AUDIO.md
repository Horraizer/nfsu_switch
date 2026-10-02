# Collision audio trace (issue #222)

This is a bounded static trace and supported world-audio implementation.
Owner listening acceptance remains open.
The tool `tools/collision_audio_trace.py` reads the source identities below,
checks exact instruction boundaries, resolves native-bank module exports and
Player tables, and reports integer lookup curves and callback ordinals. It
hashes the entire retail tree, including directory metadata, before and after.
Run from the checkout with Python 3.8+ and Capstone:

```text
python -B tools/collision_audio_trace.py --root <ASCII-retail-root> --out build/222-trace
```

Use a fresh output directory. Reports contain ASCII metadata/disassembly;
no executable, compressed sample or PCM is copied. No `skipdata` decoding is
used. Named fragment ranges are not claims of whole-function coverage.

## Input identities

| Input | SHA-256 |
| --- | --- |
| speed.exe | C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7 |
| Sound/IG_GLOBAL/IMPACT_MB.abk | C1EE8237C292E2934E296CAF10E7A78B25BDAE8326C517D1AA034379362CC6D5 |
| Sound/IG_GLOBAL/SCRAPE_MB.abk | F8064C70C59BB0B6E61596DE7F1A6856EA4AC48ACE3A48D50849C2B4E9FECB50 |

## Producer, classification and dispatch

`0x00465830` consumes the current solver impulse. `0x00465864..0x0046588B`
computes its magnitude and adds that magnitude to collision record `+0x60`.
This is per record, not the magnitude of the sum of all impulses in a tick.
The constructor at `0x00465640` clears it. `+0x0C` holds last-touch clock;
`+0x30` the witness; `+0x40` a contact-motion parameter.
`0x0046C0A8..0x0046C0D8` computes the magnitude of the tangent vector,
and `0x0046C3DB..0x0046C3EE` passes it to the record update. The adapter
uses tangent point speed in m/s. Retail pair reset/destruction cadence
still needs verification.
`0x00465AAC` calls vector normalizer `0x00567370` with the current impulse
and writes its unit direction to record `+0x70`.

`0x0052F6F0..0x0052F748`: either body kind 1 selects smokeable class 2;
both kind 0 with vehicle actors select car class 1; otherwise world class 0.
Track kind 3 and placed-body kind 4 enter world audio. This does not establish
what future prop actors should do, or implement missing live car/prop physics.
`0x00530840` attaches the audio record and chooses its subclass/vtable.

World classification at `0x00531DD0..0x00531F4E` uses unit impulse,
vehicle up at `+0xB0`, and forward at `+0x90`. It compares absolute vertical
impulse with float `0x006CCC68`. In the vertical branch it compares negative
up.z with float `0x006CCC3C`, then up.z with `0x006CCC68`; results are
subtypes 2, 3, or 4. Otherwise absolute dot(forward, unit impulse) is compared
with double `0x006CCC70`: greater gives subtype 4, equal or less subtype 5.
Do not infer variant families from convenient labels such as front/side.
The tool dumps the exact float bits and double value for these boundaries.

`0x00531F60..0x00531FD7` advances separate global counters: subtypes 2/5/6
cycle a four-entry family with variant offset 5; subtype 3 cycles three
entries with offset 0; subtype 4 cycles three with offset 9. Each counter is
incremented before wrap and offset. Sample selection is round robin; random
pitch modulation is a different node. Other subtypes select variant 0.

`0x00530F80..0x0053102A` maps record `+0x60` through the clamped scalar
curve initialized at `0x00530340`. Its upper input is base 3 times the
subtype multiplier table at `0x006B6450`; output is multiplied by 127 and
converted to integer at `0x00674898`. Configured subtype gain is table
`0x006F4F08`. Spatial/category gain processing at `0x00530740` and global
mix settings remain incompletely traced.

`0x00531350..0x00531455` supplies gain, nominal Q12 pitch 4096, pan,
variant and intensity to the consumer. `0x005259E0..0x00525A9F` clamps them
and dispatches event descriptor `0x006F8170`: IMPACT_WORLD, packed event
ID `0x2AE903DC`. `0x0064C380` resolves event ID and exact export name.

## Native bank graph to sample

IMPACT_WORLD export at bank `0xCD988` selects second module `0x9C`, template
`0x3EC`, initializer `0x1CD`, Player `0x490` and table `0x66C`.
Table entries map variant directly to samples 30..42. IMPACT_SMOKABLE selects
first module with samples 1..29; the old port sample 1 is in that module.

The initializer is relocatable x86 code. Its E8 displacement initially holds
an imported callback ordinal, not a PC-relative function address. Retail
`0x0064DF30..0x0064DFA1` relocates it through `0x006E9AE8`. Instance node
addresses start at template plus `0x18`, as `0x0064DC20` establishes.

The world graph copies input variant to Player index. Intensity indexes
signed 16-bit gain curve `0xA3C` and pitch curve `0xB4C` (128 entries each).
Callback 15 at `0x0064CDD0` clamps the lookup and directly selects an entry
when scale is 1. Callback 35 at `0x0064D0B0` multiplies curve gain by input
gain and 1/32768, then rounds. Callback 7 at `0x0064CA50` generates integer
pitch jitter in -150..150; its seed/ordering is not established. Callback 22
at `0x0064D0D0` sums nominal pitch, pitch curve and jitter. Callback 27 at
`0x0064D860` selects the Player entry and invokes sample backend `0x0064D5B0`.
Thus the sample/table binding is evidence, not a replacement sample guess.

SCRAPE_WORLD (packed ID `0x728703DC`, descriptor `0x006F8180`) resolves the
SCRAPE bank's only module: initializer `0x9C`, template `0x1B0`, Player
`0x32C`, table `0x37C`, one entry selecting sample 1. `0x00531470` supplies
variant 0. `0x00531030` maps record `+0x40` through the 0..40 input curve.
`0x00531170` tests the contact-motion parameter for scrape admission.
The graph uses gain curve `0x38C`, pitch curve `0x49C`, square oscillators
(callback 28, `0x0064D960`) with 250/2000 ms periods, rising-edge nodes
(callback 10, `0x0064CB90`), and ten-entry decks without replacement
(callback 8, `0x0064CA80`). The first draw after wrap excludes the last draw
of the previous cycle. Deck indices select pitch targets at `0x5AC` and gain
targets at `0x5D0`. Ramps (callback 29, `0x0064DA70`) have durations 261/370 ms.
The initial deck index is zero before a rising edge. Gain first uses configured
20000 (`0x006F4F28`) and 1/32768, then modulation factor float bits `0x38000100`.
Pitch is the pitch-curve output plus its ramp; the nominal pitch parameter is
not copied to this path. `0x00651E84..0x00651EFA` stores the graph's control
delta with 10..100 bounds and a fallback; the tool reports its exact constants.
The port evaluates the graph at 60 Hz with a private RNG; original scheduling
and seed remain unknown. The smokeable
scrape export is absent from this bank; no world-scrape fallback is proved.

## Lifetime, scope and missing evidence

`0x00530A50` compares audio-record last touch using clock conversion at
`0x006CCA38` and expiry threshold at `0x007000D8`, then `0x005307E0` marks
expiry and starts scrape fade. This audio expiry is not proof that the
physics pair accumulator resets on the same interval. Impact admission at
`0x00531FF0` also suppresses competing records; its actor/pool ordering is
not yet reproduced. No fixed ten-tick impact cooldown was found.

Remaining evidence: pair creation/reset cadence;
car-class variants; smokeable impact parameter path; exact scrape sample
loop/restart/fade semantics; global voice admission; category/spatial settings; audio RNG seed;
matched original event0 recordings and Switch listening. Synthetic typed
car/prop events cannot prove live actors exist. Original evidence request:
https://git.accad.pro/murzintsevav/nfsu-switch/issues/222#issuecomment-7025

The new mixer limiter is an explicit port output policy, not a recovered
retail mastering curve. Stereo-linked instantaneous peak attenuation and a
linear release keep PCM16 bounded. Raw overloads, attenuated frames and final
clips have separate counters; zero final clips alone does not prove sensible
mix balance. `nfsu_audio_playback_tests` checks coherent and opposing full-scale
signals, stereo ratio, release, quiet unity and block-partition invariance.

## Port adapter and listening matrix

`VehicleTrackTelemetry::audio_contacts` captures current solver J before mass
scaling/clamps in t m/s, polygon identity, tangent point speed and the current
forward/up basis. It does not change solver arithmetic, contact order, RNG,
state or encoded telemetry. `race_audio_snapshot()` copies both 120 Hz
substeps. No audio object holds a mutable RaceSession reference.

The live backend supplies only car-vs-track contacts. It uses contiguous
polygon observations as contact episodes, with a new accumulator after a gap;
these are NOT recovered retail body-pair IDs or reset semantics. Typed kind 4
inputs use the proved world path; kind 0 and 1 are explicitly unsupported and
produce no guessed fallback. Live car/prop acceptance stays open until those
backends exist. Episode starts select a variant, subsequent hits update the
same voice's parameters from accumulated magnitudes; there is no invented
impulse threshold or ten-tick cooldown. Duplicate ticks are ignored; pause
holds the scheduler; reset discards pre-reset contacts and clears voices.

Two impact slots and two looping scrape slots are bounded port voice policy.
Scrape separation ramps the voice to zero using the mixer's existing gain
ramp, without claiming the original fader duration. Spatial/category settings,
retail record admission and exact retail random sequence are not reproduced.
These limitations prevent a claim of complete collision-audio parity.

| Scenario | Listen / record |
| --- | --- |
| Event0 original, low/medium/high wall hit | Same location, speed and angle; compare onset, family, pitch and loudness. |
| Shallow wall hit and sustained slide | Impact once per episode, continuous scrape, smooth speed response and separation. |
| Two hits within one second / simultaneous contacts | No invented shared cooldown; inspect voice replacement and loudness. |
| Upright landing / sideways or overturned landing | Compare vertical family with orientation and original capture. |
| Pause, resume, reset | No stale impact replay; scrape stops on reset and pauses with game. |
| Music + engine + tyres + impact | Record OVER, LIMIT and CLIP; listen for limiter pumping and mix balance. |
| Car / placed prop / smokeable prop | Typed mapping evidence only where proved; live actors are still missing. |

Coordinator schedules all console runs. Required target remains original
Underground intro event0, actual tuned setup, opponents and full HUD. Event1 or
cosmetic --tuned does not satisfy it. Console baseline is --scene-height 540
--gpu-boost on; requested settings and observed clocks are separate evidence.
`nfsu_race_audio_tests` checks selectors, boundaries, bank/curve identity,
independent contacts, accumulated updates, episode reset, scrape modulation,
duplicate ticks and output-only telemetry encoding. `nfsu_race_run_tests` and
the full replay gates check unchanged simulation pins. A build cannot replace
matched original recordings or owner listening.

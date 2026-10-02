# Collision audio trace (issue #222)

This is a bounded static trace of PC retail, not owner listening acceptance.
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
`+0x30` the witness; `+0x40` a contact-motion parameter. The exact caller
units and record reset/destruction cadence still need verification.
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
The graph has gain/pitch curves, oscillators and ramp nodes; single-carrier
binding does not establish their modulation or time units. The smokeable
scrape export is absent from this bank; no world-scrape fallback is proved.

## Lifetime, scope and missing evidence

`0x00530A50` compares audio-record last touch using clock conversion at
`0x006CCA38` and expiry threshold at `0x007000D8`, then `0x005307E0` marks
expiry and starts scrape fade. This audio expiry is not proof that the
physics pair accumulator resets on the same interval. Impact admission at
`0x00531FF0` also suppresses competing records; its actor/pool ordering is
not yet reproduced. No fixed ten-tick impact cooldown was found.

Remaining evidence: pair creation/reset cadence; `+0x40` caller units;
car-class variants; smokeable impact parameter path; scrape modulation and
fade; global voice admission; category/spatial settings; audio RNG seed;
matched original event0 recordings and Switch listening. Synthetic typed
car/prop events cannot prove live actors exist. Original evidence request:
https://git.accad.pro/murzintsevav/nfsu-switch/issues/222#issuecomment-7025

The new mixer limiter is an explicit port output policy, not a recovered
retail mastering curve. Stereo-linked instantaneous peak attenuation and a
linear release keep PCM16 bounded. Raw overloads, attenuated frames and final
clips have separate counters; zero final clips alone does not prove sensible
mix balance. `nfsu_audio_playback_tests` checks coherent and opposing full-scale
signals, stereo ratio, release, quiet unity and block-partition invariance.

# Original collision contact and audio contract (issue #312)

This bounded continuation of [AUDIO_FIDELITY_312](AUDIO_FIDELITY_312.md)
starts from `2667c3fac7f27893a788c6226ba49f748d770b18`. It supplies the
missing body/contact identity bridge, executable admission and retirement
witnesses, and original spatial/category producers. It changes no production
code and makes no listening or PCM parity claim. The engine investigation
and its established witnesses are unchanged.

## Reproduce the continuation

Use the standalone Python and dependencies described in AUDIO_FIDELITY_312.
Run from this worktree, including its actual non-ASCII absolute location:

```powershell
$env:PYTHONPATH=(Join-Path (Get-Location) 'build/audio312-deps')
& 'C:/Users/horra/AppData/Local/Programs/Python/Python312/python.exe' -B tools/audio312_contacts.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --out build/audio312-contacts
```

[audio312_contacts.py](../tools/audio312_contacts.py) requires a fresh ignored
output directory. It verifies the executable identity and LF-normalized
source identities from the original investigation for `collision_audio.cpp`,
`race_audio.cpp` and `vehicle_track.cpp`. Its before/after manifests cover
every file and directory under the read-only retail root, also on failure.

The output consists of `report.json`, `instructions.txt`, `before.json` and
`after.json`. Numerical assertions live in the runner, not this document.
The executable's provenance is the same `speed.exe`, 3178496 bytes, SHA-256
`c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7`.
No retail bytes, samples, PCM or private captures are exported. The instruction
listing contains bounded disassembly and a sorted union of executed spatial
instructions. That union is not an execution timeline; embedded jump tables
are not decoded as instructions.

All execution uses the prior allowlisted Unicorn `Machine`, its instruction
cap and explicit x87 control word. Objects, body IDs, actors, lists, listeners,
settings and clocks are synthetic inputs. Native code makes the decisions;
this is not a running game or a compiled-port comparison. The boundaries are:

| Witness | Original execution | Deliberate boundary |
| --- | --- | --- |
| `pair_witness` | Pair constructor, symmetric lookup, retained-record predicate and its caller branch, existing cache slot lookup/binding, candidate reuse and destructor clearing | Null geometry shapes; existing cache entry; stop before pair destructor or uncached candidate geometry |
| `retirement_witness` | Expired-record cleanup through record destructor, audio lookup/retirement and scrape envelope setup | Starts after the already proved age predicate; body kinds exclude steep-track bookkeeping; no mixer completion |
| `admission_witness` | World admission and impact submission through original virtual calls | Audio objects already attached; observe queue append at `0x52FB00` without allocating a queue node |
| `scrape_admission_witness` | Original curve initialization, motion predicate and scrape request | Impact rejection deliberately set; stop before subsequent controller update/start |
| `budget_witness` | Native pending-versus-active replacement decision | Preselected nodes; observe stop/start calls, not full queue sorting or event execution |
| `spatial_cases` | Complete impact start `0x531350`, spatial producer, original profile/math, variant selection and category gains | Synthetic single listener; supplied subtype gain/intensity; observe event allocation at `0x530B80`; no backend |

## Identity belongs to bodies, then records

`0x466CE0` constructs a pair with two body pointers at `+0x258/+0x25C`
and an initially null contact-record pointer at `+0x264`. Body IDs are signed
words at body `+0x18`. It sets duplicate-pair bits for both orders;
`0x468300` queries them. The witness executes the constructor and both lookup
orders. A hull point or triangle is not this identity.

For a track contact, `0x441B90` uses raw polygon `+6` to find a cache slot
through `0x43FE70`. `0x46A514` stores that cache on body `+0x60` and the body
back on cache `+0x10`. The candidate loop at `0x469850` skips a cache entry
with an existing body. The destructor fragment at `0x46A5D2` clears the back
pointer and releases the cache reference. Both candidate paths are executed
around that clearing in the witness. This establishes reuse of a live track
body for that polygon, not a new body for every observed point or substep.

`0x467300` returns immediately when pair `+0x264` is nonnull. Even disjoint
body bounds then take the retaining branch in caller `0x4697EA`. With no
record, the same disjoint bounds take the destroying branch. The record
therefore protects the pair across a temporary geometric gap. Pair and record
are different lifetimes; a polygon cooldown cannot represent both.

The prior age witness remains the oracle for expiration timing. The new
`retirement_witness` continues from the expired branch at `0x420AD7` through
`0x465700` and `0x5302F0`. It verifies that the pair loses its record, the
record returns to its pool, body record counts decrease, and the matching
audio object loses its record pointer. Native `0x5307E0` marks that audio
object retired and sets up the original scrape fade while retaining active
impact/scrape flags. Destroying the physics record is not an instruction to
cut both voices immediately. The earlier fade witness supplies its timing.

Static destruction paths remain relevant: pair destructor `0x466E20` can
remove a record through `0x4672A0`, and `0x4682C0` destroys all pairs on a
reset. The age threshold is not a guarantee that every pair lives that long.
Those complete reset/destructor paths are not executed by this continuation.

At the pinned port sources, `race_audio_snapshot` copies `c.polygon` into
`CollisionContact::pair`; `vehicle_track.cpp` already combines points by
polygon. For the present single-car/static-track scope, a polygon identity
can identify the track side of a pair. It must also be scoped to the loaded
track and vehicle lifetime. Different polygons remain different track bodies;
do not merge adjacent polygons to suppress noise. Suppression belongs to the
audio controller's actor/direction rules below.

## Impact admission and scrape admission are independent

The world controller vtable is `0x6BE520`. `0x530240` finds the audio object
by contact-record pointer, attaches it if necessary, skips an active impact
or already queued object, then calls world admission `0x531FF0`. A refusal
calls `0x531150`, setting object `+0x219`. That rejection remains latched even
when the competitor is later removed; the witness tests that transition.

`0x531FF0` first checks this latch. Actorless objects bypass the subsequent
actor competition. Otherwise it checks global last-start direction/point at
`0x736730/0x736740`, including direction agreement, a magnitude window,
nearby position and vertical motion. The native cases distinguish a nearby
vertical event, a distant vertical event and a nearby horizontal event.
There is no elapsed-time test here. Those globals change on actual impact
start at `0x531350`, not on every impulse observation or queue request.

It then walks attached controller records. Eligible competitors have subtype
5, a nonnull matching actor and attachment flag `+0x218`. A different eligible
record suppresses a sufficiently aligned OR opposite direction: the predicate
uses the absolute dot product. An orthogonal record does not. The direction
on a competitor is its attach-time snapshot at `+0x230`; the current direction
is record `+0x70`. The predicate does not require the competitor's impact-active
flag. For the current eligible record, the already-started flag `+0x34F`
prevents another start. The runner pins both sides of the dot threshold and
changes actor, subtype, attachment and already-started state independently.

Attach `0x530840` snapshots the initial point/direction, subtype, distance,
flags and actor. `0x52F650` finds actor pointers only on kind-0 bodies at
body `+0x140`; bit `0x10` records their presence. For car-versus-track with
one actor, attach selects that actor. Bit 0 comes from `0x4664B0`, which checks
actor `+0x18`. Calling it an arbitrary audio priority or substituting a new
player-ID heuristic would discard its provenance. The two-actor selection
branches are listed but not executed here.

Scrape submission `0x530090` does not consult the impact rejection latch.
It checks scrape-active state and virtual motion predicate `0x531170`, then
sets scrape-request flag `+0x21B`. `scrape_admission_witness` executes below,
at and above the original initialized curve threshold with impact already
rejected. Thus impact refusal must not reset or mute a valid scrape graph.

The global pool is another stage. Static `0x52FB60` compares active-plus-queued
count against configurable capacity at `0x6F7E94`. The executed replacement
fragment at `0x52FE41` prefers a pending priority-bit record to a nonpriority
active one; for two nonpriority records it replaces only with a nearer one.
Equal distances, a priority active record and two priority records take
different nonreplacement branches. `budget_witness` pins these decisions.
It does not establish full list ordering, tie selection, per-frame occupancy
or live configuration. The port's alternating slots are not this policy.

## Spatial and category inputs now reach the event boundary

`0x531350` calls `0x530F20`, which copies the current record witness at `+0x30`
and invokes `0x52EA00` with no velocity vector. The witness executes the
original world/car profile at `0x6F47A0`, rather than replacing attenuation
with a guessed inverse-distance formula.

The synthetic single-listener provider supplies camera position/forward at
camera `+0x40/+0x50` and actor position/forward at actor `+0x60/+0x90`.
`0x52E540` computes both distances. This profile selects actor distance and
camera-relative direction for attenuation; its angular sectors have different
distance curves. Moving only the camera nearer does not substitute camera
distance into the attenuation. Moving the actor nearer changes it, and
rotating the camera changes the angular sector. `spatial_cases` checks each
of these separately, plus near/far and opposite-axis positions.

`0x52F0C0` produces object pan `+0x4C`; this is a cyclic angle passed to the
event, not a linear left/right gain. The downstream pan-to-speaker law remains
unknown. Do not pass it directly into a port API expecting a normalized pan.

For impact, the complete producer applies these stages in order:

1. Original profile attenuation and object base attenuation produce `+0x50`.
2. `0x530740` multiplies subtype gain by that value with signed Q15 shift.
3. It selects a category from actor descriptor kind 1/2 and actor player
   index, clamped to the supported range; otherwise it uses global `0x7356B0`.
   Manager mute and game modes 1/7 silence supported categories, then setting
   `category * 0xD10 + 0x858` applies another Q15 stage.
4. `0x531350` applies the additional global impact setting at settings
   `+0x3E1C`, again with Q15 shift, then supplies event gain, pitch, pan,
   intensity and variant before dispatch at `0x530B80`.

The witness independently changes category/global gain, low/high player
indices, descriptor kind, fallback category, manager mute and game mode.
Keep each integer shift in its original position; multiplying all factors
in floating point and rounding once is a different transformation. This
extends the prior isolated `0x530740` gain witness: that witness did not claim
the extra caller-side impact setting was absent.

The profile, settings and listener are explicit fixtures, not a capture of
event0 defaults. Dual-listener selection, live profile changes, scrape's full
event/mixer path, Doppler/backend interpretation and speaker gains are not
claimed by executing the impact producer.

## Implementation contract for the next production change

These are required interfaces and invariants inferred from the evidence;
they are not claims that the current port already implements them.

| Layer | Required copied state / behavior | Discriminating check |
| --- | --- | --- |
| Body identity | Stable vehicle handle and track-resource/polygon handle; reset generation; canonical unordered pair key | Swap body order without duplicating a pair; keep adjacent polygons distinct; reject stale keys after reset/reload |
| Contact record | Separate record generation, first/current witness, last-touch original clock, accumulated impulse, normalized direction and motion | Same live pair with a brief missing observation keeps record, accumulator and graph state; expiry or explicit body destruction retires it |
| Audio attach | Actor identity, body kinds/flags, initial direction/position, subtype, category inputs and attachment distance | Later contact movement updates current spatial state without overwriting the competitor's attach snapshot |
| Admission | Separate attached, queued, impact-active, already-started, rejection and scrape-request flags; ordered record set; global last-start state | Run `admission_witness` and `scrape_admission_witness` cases against the implementation; refusal of impact still permits scrape |
| Voice allocation | Capacity and original priority/distance policy kept separate from admission; deterministic observed queue order | Reproduce `budget_witness` before extending to a complete original queue oracle; do not call alternating slots equivalent |
| Control graph | Existing world families/graphs; local intensity truncation; record-lifetime oscillator/deck state; explicit graph clock/RNG inputs | Keep prior intensity/fade/graph witnesses and retain scrape modulation across a verified same-record gap |
| Spatial mix | Current witness; camera and actor transforms as separate inputs; profile, category and global impact gain; explicit pan representation | Reproduce `spatial_cases` at the event boundary before mapping native pan to stereo |
| Retirement | Physics and audio records detach; active tails own state independently until completion; explicit reset path | Match `retirement_witness`; no stale record dereference, immediate cutoff or reset of a different polygon's record |

For the current track observer, add these as copied observations without
changing contact order or solver math. A new key can encode a generation to
avoid reuse collisions; that is a port safety representation, not a recovered
original field. If a body-destruction signal is not available, an audio-only
age heuristic must remain explicitly bounded and must not be called original
pair-lifetime parity.

The implementation can begin with the proven single-car/world scope. Keep
car/prop controllers and two-actor selection separate until their own witnesses
exist. Remaining parity dependencies are live control/listener/settings input,
complete pool order, RNG seed/consumption and graph scheduling, and backend
PCM/pan behavior. Hardware listening remains acceptance after independent
review. No arbitrary polygon cooldown is part of this contract.

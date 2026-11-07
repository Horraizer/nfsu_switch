# Track prop lifecycle: original-source prerequisites for issue #270

This follow-up preserves [PR272 at b3474e7](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/272)
and [finding 6814](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/272#issuecomment-6814).
It corrects that draft's list direction and shape arguments, then extends
maintenance, settling, deletion, reconstruction and section reload evidence.
The new assignment is [#270 comment 7277](https://git.accad.pro/murzintsevav/nfsu-switch/issues/270#issuecomment-7277).
The old branch/tree is not changed. The assigned base is
`10563e2555270cb564fac910f1f2e1ba9569d4e4`, an isolated, unaccepted integration
substrate. This docs/Python delta does not certify its inherited production
changes or make the full branch-to-main diff a validated merge candidate.

The selected scenario is introductory Underground **event0, track1003,
forward**. [EVENT0_SETUP_HUD.md](EVENT0_SETUP_HUD.md) establishes the setup
source; a generic event1 run cannot supply its missing original evidence.
Evidence levels in this document are:

- **Static:** pinned original-file bytes and identified instruction consumers.
- **Synthetic:** Python pointer writes and finite scalar predicates, with
  constructed inputs. They are not x86 emulation, native extracted execution
  or an original gameplay measurement. Converted ages are inputs; x87 clock
  conversion/rounding is not emulated.
- **Original runtime:** unavailable. No game was launched or attached. No
  synchronized save/input/contact trace was supplied; process discovery found
  no running `speed` process during this pass.
- **Proposed:** implementation/capture boundaries, not recovered port behavior.

## Read-only reproduction

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |
| `Tracks/TRACKB1003.lzc` | 433352 | `ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA` |
| `Tracks/STREAML1RA.BUN` | 92394688 | `2385556EC31142F7390434CA70152A9F3D629155AF991BB6595422FCB26A752E` |

From the actual worktree, with standalone Python 3.8+ and Capstone:

```powershell
python -B tools/track_prop_trace.py --root 'E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground' --out build/270-fresh
python -B tools/track_prop_trace_test.py --report build/270-fresh/270-props.json
```

Use a new output directory each time. Paths are anchored through `Path.cwd()`;
output must be an ASCII path under an ignored build/out directory in that
workspace. Existing output, retail overlap, ancestors of retail, protected
`E:/Games` and paths outside the workspace are refused before writing.
Full before/after manifests cover the root, directories, files, sizes, mtimes
and file hashes, including after extraction failure. A manifest PASS followed
by an exception is a failed run. Only the final PASS confirms extraction.

`270-instructions.txt` contains bounded disassembly and range hashes, with
independent instruction assertions. `270-props.json` contains identities,
candidate fields, constants, dispatch/vtable targets and evidence labels.
No executable, hull vertices, decompressed asset bytes, PCM or pixels are
exported. Reports/manifests remain ignored; the script, source VAs and input
identities provide durable reproduction. Tests pin the census and candidate
checks instead of duplicating their current numeric results here.

## Record identity, list correction and selected-track evidence

Definitions are aligned 0x100-byte records in `0x00034026`; placements are
aligned 0x70-byte records in `0x00034027` (`0x005952A0`). The report retains
each file/chunk/record offset, record hash, descriptor hash and every duplicate.
TRACKB offsets refer to decompressed bytes; STREAML1RA offsets are file bytes.
Names, definition hash `+0x80`, placement model hash `+8` and hull hash are
different roles, not interchangeable identities.

**F1 correction.** `0x005952E2` reads sentinel tail `[0x0073777C]`;
E7 writes old-tail.next, E9 writes sentinel.prev, F5 writes new.prev, and
F8 writes new.next to sentinel `0x00737778`. This appends. Lookup starts at
sentinel.next (`0x0059638C`) and follows next (`0x005963AD`), stopping on
the first `+0x80` match. Helper `0x0040A880` is RET. The new reproduction
checks those exact instructions and synthetically replays insertion, removal
and reload. `0x005954F0..0x00595500` unlinks each unloaded definition by
patching its neighbors, so reload order can change the first match.

For an initially empty list containing only the two `ZCS_PylonA` records in
this file's array order, the first match is definition 257504, then 257760.
That conditional result is not a live mass winner. A prior matching load
or unloading/reloading the first node changes it; the tests exercise both.

**N1 correction.** `0x005968F8` supplies definition `+0x80` as the hash to
`0x00472C60`; definition `+0xB0` supplies a separate pivot argument.
The lookup compares shape `+0x18`, walks forward, and may adjust the chosen
shape on first use. The probe preserves duplicate hull headers, point/index
range identities and hashes without exporting geometry or pairing duplicate
definitions with hulls by position. [Reviewed PR283](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/283)
at `13c1fc8e4ddb17999f68f0bf67462095c8168a75` traces shape loading/pivot
adjustment further. Its local file order is not an observed load sequence.

Named capture candidates, each independently joined to its scenery record:

| Definition label/hash | Definition offset(s) | Placement / section / instance | Scenery / object-directory offset |
| --- | --- | --- | --- |
| `ZCS_PylonA`, `5087DB01` | 257504, 257760 | 810656 / 1204 / 49 | 47241648 / 47252384 |
| `CS_STREETSIGN_B`, `629B3B7D` | 247776, 248032 | 793520 / 1001 / 2 | 56234240 / 56252080 |
| `ZCS_Chevrons`, `662259F6` | 253152 | 794192 / 1003 / 63 | 55168112 / 55189048 |

Translation equality and the model's membership in the scenery object's six
mesh slots are required checks. The sign and chevron have nonzero exported
activation fields; the pylon has zero. The report prints exact field values;
neither visual size nor the mesh selects the effective mass or threshold.

The new stream join uses `0x00034107`, stride 56, section `+8`, track byte
`+10`, file index `+16`, file offset `+20` and read size `+24`. It verifies
that each scenery instance lies in the range for its named section in file
index 1 (STREAML1RA). All joined placements' sections have a set track byte
in this input. `0x00444C94..0x00444CCA` requests such sections during the
initial-load branch. This is stronger than mesh-name matching, but establishes
start-request eligibility, not successful live residency, route visibility or
a contact encounter. [TRACK_WHOLE.md](TRACK_WHOLE.md) describes the surrounding
request path. The initializer's placement exclusion `0x00595B70` requires
track 1201 and reverse direction before its model/position filters; it does
not exclude these candidates for track1003 forward.

## State fields: writers and consumers

| Field | Verified writer | Verified consumer / limit |
| --- | --- | --- |
| Placement `+0x0C` | Virgin constructor `0x005963BB` stores the kind-2 proxy; its destructor `0x005964F0` clears it. | Scenery reload `0x00445B0D` reads it. This is a proxy backlink, not a general pointer to the live kind-1 body. |
| Virgin `+0x60/+0x64` | `0x00596389/0x005963B8` store placement/definition. | Conversion `0x00596570` supplies model, definition and original transform to the factory. |
| Kind-1 `+0x158/+0x170` | Constructor `0x005969A1` stores definition; conversion `0x005966B7` stores placement. | Lifecycle `0x005977F0` reads both; serialization reconstructs placement from its index. |
| Kind-1 `+0x15C/+0x160` | `0x005969BF/0x005969CD` copy activation/break fields; missing child resources clear break at `0x005969E2`. | Inverse properties and callback `0x00597980`; activation clears `+0x15C`, repeated impacts consume `+0x160`, crossing writes -1. |
| Kind-1 `+0x164` | `0x005968A9` copies clock `0x0073AD34` at construction. | Unactivated proxy-reversion age. |
| Kind-1 `+0x168` | Constructor clears; `0x00597D00` stamps activation if old value is 0 or `0x7FFFFFFF`. | Reversion/lifetime/reclamation distinguish those sentinels from recorded activation. Settling does not clear it. |
| Kind-1 `+0x16C` | Constructor clears; `0x005974F4` refreshes when `0x005980D0` finds a contact involving a kind-0 body with actor backing. | Settling and integration-state classifier read its age. It is not the most recent contact of every kind. |
| Kind-1 `+0x4C2`, `+0x1D` | `0x00597C90` sets both; `0x00597C70` clears both. | Update, impulse and pair eligibility paths read them separately. Deactivation does not destroy the body or reconstruct a proxy. |
| Contact record `+8` / `+0x0C` | Constructor `0x004656AF/B4` stamps both; current-hit update `0x00465894` refreshes only `+0x0C`. | Settling reads `+8` of the selected record; audio/contact expiry uses last-touch `+0x0C`. |

The base constructor stores kind at `+0x15`. Virgin passes 2 and installs
`0x006B8A90`; Smokeable passes 1 and installs `0x006B8B18`. Conversion
allocates a different body; it is not an in-place kind-byte edit.
`0x00469714..0x00469729` converts then destroys the proxy. Another path
uses `0x005962C0`. Pool exhaustion can make conversion return null
(`0x0059657C..0x00596592`); do not model allocation as unconditional.
Kind-3 track and the separate kind-4 BB path remain distinct.

The Virgin constructor clears shape `+0x10`. Kind-1 construction allocates
its rigid body from definition `+0x20` (`0x0059686F..7B`); shared
`0x00584B10` reads its mass and inverts the descriptor matrices. Smokeable
inverse-property readers `0x00595170/0x005951A0` return zero while activation
field `+0x15C` is nonzero. Clearing that field changes the constraint; it does
not establish which duplicate descriptor or hull a live contact selected.

## Activation, integration and settling

The caller of the activation/budget callback is now bounded: common solver
`0x0046BAE0` calls virtual `+0x48` at `0x0046BEED` for job body `+0x10`,
then at `0x0046BF46` for job body `+0x0C`, with a negated vector on the
second path and separately computed contact arms. The Smokeable slot resolves
to `0x00597980`. These arguments precede current-hit recording
`0x0046C3EE -> 0x00465830` and final applied impulses at virtual `+0x4C`
(`0x0046C5D2/E4`); intervening adjustments mean the channels must be
captured separately. The callback is not a speed-only threshold.

For an inactive body with nonzero `+0x15C`, the callback compares that field
against `max(length(vector), length(cross(arm + pivot, vector)))`.
Below the threshold it returns without activation; equality reaches the
activation path. It activates, clears the field, applies a compensating
scaled opposite vector at the negated pivot, and enables the threshold
effect list. Zero reaches activation directly. This describes finite inputs;
the code tests zero/nonzero, not a universal positive-value validation.
Units and actual runtime vector/descriptor selection remain unmeasured.

The live world path calls maintenance at `0x00420E05`, before either contact
scheduler branch. With selector `0x00736200` nonzero, maintenance invokes
`0x005977F0` on kind-1 bodies, snapshotting nonnegative IDs before resolving
them through `0x00736050`; negative IDs take the direct branch. With selector
zero, the kind-1 update invokes lifecycle at `0x0059760F`. Thus deletion can
occur at different enclosing phases. The selector's live value is unavailable.

Update `0x00597420` always calls base preparation `0x00468F70`. Its active
branch recomputes inverse properties, subdivides the supplied dt using
constant `0x006EF28C`, and integrates through `0x0046AA40`. This does not
establish a fixed collision rate. It writes the physical transform into the
owned visible object's `+0x50`, or the separate draw transform at body
`+0x460` (`0x00597586..0x005975BF`), then updates effect transforms.

Settling `0x00597B80` requires all of these finite comparisons:

- Age of body `+0x16C` strictly exceeds float `0x006B5A7C`.
- Contact count byte `+0x1C` is nonzero; `0x00468820(0)` selects the first
  matching live contact record in pair-list order. Its **creation** age at
  `+8` strictly exceeds float `0x006B5A78`.
- Linear and angular speed magnitudes are strictly below floats
  `0x006B5A70` and `0x006B5A74`. Equality does not settle.

Success tail-calls `0x00597C70`, clearing active/eligibility flags and
refreshing candidate storage. It is neither deletion nor original-pose reset.
A second mechanism matters: integration-interface vtable `0x006B8B08 +4`
is `0x00597FA0`. It calls classifier `0x00597EA0`, writes body `+0x4C4`,
and can damp or zero rigid fields `+0x70/+0x80` while changing
rigid `+0x140` and body `+0x1D`. That path does not clear `+0x4C2`.
The report includes both routines and their source constants; collapsing
them into one boolean sleep state would lose original behavior.

## Port settling implementation and bounded verification

`RaceProps` applies the recovered classifier, momentum writer and retirement
predicate through `prop_settling.h`. The car-contact clock is refreshed by the
existing SAT overlap adapter. Static support retains per-polygon creation and
last-touch clocks in first-admission order. Original scheduler branches
`0x00420720` and `0x00420AB0` compare last-touch age against `0x007000D8`;
equality expires the record. This is distinct from the creation age read by
retirement. `0x00585E6C` rounds each supplied update duration into clock ticks.

`tools/prop_settling_native.py` executes the original clock, predicates and
momentum writer against bounded synthetic inputs. It authenticates the retail
executable and explicitly stubs contact lookup, candidate-list maintenance and
retention branch endpoints. The refresh component list is empty. Compare its
vectors with `nfsu_prop_settling_tests`; this is not a live trajectory proof.
The desktop intro viewer's `props-check` additionally checks resting pose
persistence and reactivation through actual placed-prop collision calls.

The existing vertex support and SAT adapters remain: full original contact
manifold generation, pair scheduling and streaming lifecycle are not claimed.
No distance sleep, new physics tolerance or simulation cadence reduction is
introduced. Source settling constants are tested in `nfsu_prop_settling_tests`.

## Removal and section streaming are separate

`0x005977F0` has two deletion routes:

1. Placement exists, activation clock is 0 or `0x7FFFFFFF`, byte `+0x1A`
   is clear, and construction age strictly exceeds float `0x006CCC00`:
   attempt to allocate/construct a Virgin from the placement, then destroy
   the kind-1 body. A null allocation skips construction at `0x0059786F`
   but still reaches deletion. This is unactivated-body reversion; it is
   not settled-body respawn, and proxy restoration is not unconditional.
2. Activation is recorded, definition `+0xC0` is nonzero, and activation age
   reaches that value: call virtual `+0x7C`, then destroy. For the verified
   Smokeable vtable, `+0x7C` is the RET helper, not a demonstrated death effect.
   The old draft's "positive lifetime" wording was too strong: the finite
   comparison accepts any nonzero lifetime, including negative inputs.

Pool maintenance also calls `0x00595750` when free slots are scarce or on
its periodic phase. That routine has motion/position tests, a proximity
scalar from `0x00421400`, activation-age tests and definition `+0xE4`
exemptions before queuing kind-1 destructors. These are distinct from the
exported lifetime and from streaming. The tool exposes the complete routine
and source constants; the proximity scalar's full semantics and live pool
pressure are not established. Initial capacity is not an event0 debris cap.

The stream lifecycle is source-addressed independently:

| Operation | Source behavior | Consequence |
| --- | --- | --- |
| Scenery chunk unload | `0x00445B60` handles `0x80034100`, clears `0x0072A008[section]`, unlinks its header and releases model references. | This handler does not walk placed props or call their destructors. That bounded fact does not exclude another surrounding caller doing so. |
| Scenery reload | `0x004457C0` reconstructs section arrays; `0x00445B00..3E` joins placement section/index. Nonzero proxy backlink clears scenery bit `0x10`; zero sets it. | Reload reads proxy ownership. It neither reinstates a kind-1 transform nor constructs a new proxy in this loop. |
| Definition/placement unload | `0x005954C0` unlinks definition nodes for `0x34026`; `0x34027` clears placement base/count globals. | Different handler from section eviction. Body teardown must be correlated with it; no safe whole-load ordering is inferred. |
| Draw mask | `0x00445EE2..EB` tests `(instance_flags XOR 0x60)` against render-context byte `+0x84`. | Bit `0x10` suppresses that scenery instance when the corresponding mask bit is set. The live mask and resulting visible episode still need observation. |

On conversion, the proxy destructor sets that scenery bit if its section is
resident and clears the backlink even if the section is absent. Kind-1
destruction `0x00596CC0` releases effects, model/rigid resources and the base
body; it does not reconstruct a proxy or clear that scenery bit. The pool
wrapper `0x00596C90` returns the slot. Therefore section reload is not
evidence of physical respawn, and a vanished scenery mesh is not proof of
breakage. The runtime must correlate proxy, live body and separate draw owner.

## Reset and serialization prerequisites

The new trace establishes a complete local teardown/reconstruction chain:

```text
race message 8: 0x00422BD4 -> 0x00422130 (teardown)
                            0x004221FF -> 0x0041F9B0 (world destructor)
                            0x0041FA71 -> 0x00595EA0 (prop shutdown)
                            0x00595EA3 -> 0x00595530 (kind1/kind2 cleanup)
then           0x00422BDA -> 0x00421F00 (reconstruction)
                            0x00421FB9 -> 0x0041F6E0 (new world)
                            0x0041F93C -> 0x00595BF0 (prop initialization)
```

The message number is decoded from the tables at `0x00422C20/0x00422C34`,
not guessed from a label. Cleanup detaches kind-1/kind-2 registered bodies
into a temporary list, then invokes their virtual destructors. Shutdown
releases both pools and definition effect templates. Initialization rebuilds
templates, pools and proxies from placement records with matching definitions.
World construction/destruction also reset clock `0x0073AD34`. This supports
a proposed reset implementation boundary. It does **not** link a particular
event0 restart UI action to message 8 or prove no residual state in execution.

Serialization is a separate reconstruction path. `0x00595F70` calls cleanup
when serializer byte `+0x3D` selects reading, reconstructs kind-2 bodies from
placement indices and kind-1 bodies through the definition/factory path.
`0x00596F50` exposes activation flags, threshold, budget and clocks to
`0x00571C20`; it rebuilds `+0x170` from a signed placement index at
`0x00597044..68`. A negative index maps to null. These are additional possible
field writers, not live frame maintenance or an unconditional restart reset.
Full save-format fidelity and the loaded record choice remain unverified.

## Break, effects and #222 input boundaries

The definition's `+0x8C` child count, `+0x90..9C` resolved pointers and
`+0xA0..AC` child hashes feed replacement `0x00597D10`. Callback
`0x00597980` consumes nonzero break budget and writes -1 when crossed;
kind-1 gates `0x00420586..0x00420600` then call replacement. It constructs
children at the parent's pose, activates them and destroys the parent.
Resource check `0x00597E10` can disable the initial budget. None of these
branches proves a child-replacement class was encountered in event0.

In the joined track1003 placement definitions, the probe/tests find zero
initial child count, break budget and lifetime, and zero effect hashes at
`+0x84/+0x88`. Other *unplaced* definitions in the same file have effect
hashes; their presence is not an event0 spawn witness. No class-wide absence
or full-route exclusion follows from this bounded corpus. Record later
writers, other loaded sources and named live selection before implementing
breakage, debris, smoke or fire.

Effects have two independent lists: constructor branches `0x005969FC` and
`0x00596AE6` use definition `+0x84/+0x88`; activation enables body `+0x4B8`,
threshold crossing enables `+0x4B0`, and `0x00597730` supplies motion.
Destruction releases both lists. Category hash `+0xDC` and resolved ordinal
`+0xD8` do not by themselves identify a particle or an audio sample.

For [#222](https://git.accad.pro/murzintsevav/nfsu-switch/issues/222), capture:

- Clock `0x0073AD34`, supplied dt and phase. `0x00585E00` advances that clock
  from accumulated time with source conversion `0x006CC9FC`; lifecycle ages
  use reciprocal scale `0x006CCA38`. Neither establishes wall-clock cadence
  or pause behavior. Preserve clock resets and sentinels explicitly.
- Pair/record/body IDs and kinds, definition/placement/hull load generations,
  callback vectors/arms at `0x0046BEED/BF46`, current-hit vector at entry
  `0x00465830`, accumulated vector `+0x50`, sum of magnitudes `+0x60`,
  record creation `+8`, last-touch `+0x0C`, and tangent parameter `+0x40`.
  Keep final applied vectors and resulting physical/visible transitions too.
- Classifier `0x0052F6F0`: either body kind 1 selects audio class 2.
  The event selectors then use `SND_SmokeableImpact`/`SND_SmokeableScrape`.
  This closes the former upstream-selector unknown; it does not supply a
  Smokeable bank/sample, gain, cooldown or audible event0 acceptance.

## Proposed handoff and remaining acceptance

Use [#259's proposed contract](WORLD_OBJECT_BODIES.md), extended with source
record/load-generation identity, proxy backlink, kind-1 owner, both sleep
mechanisms, creation/activation/car-contact clocks and each deletion reason.
Unavailable fields must stay unavailable. #259 owns the shared contact
mechanism; #270 must not add a second solver. [#256/PR283](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/283)
still needs original selector, scheduling, witness and impulse captures.
No production prop/physics behavior is implemented here.

The smallest next evidence is an identified original event0 save/profile and
repeatable input sequence, with an authorized trace of the named pylon and
one named constrained candidate. Record loaded executable identity/base,
source records and actual selected pointers, conversion, both sleep paths,
deletion reason and visible/contact/audio state. Trace message 8 and the
actual restart UI action together; trace section unload/reload separately.
Include thread/event/frame order, supplied dt and clocks, plus original clips
that show what the state transitions look and sound like. A debugger's timing
overhead must be recorded; stopped snapshots alone do not measure cadence.

The no-contact, low/high-speed, glancing, repeated-hit, simultaneous-two-actor,
stream-out/in, pause and full-race-restart matrix remains unperformed. Prove
every relevant class or bounded exclusion using route evidence; verify
original placement restoration and no residual bodies/children/effects.
Later implementation needs default/Release retail CTest with zero skipped
tests, applicable Switch gates, original comparisons and full-scene memory,
CPU/GPU and owner footage. The handheld baseline remains
`--scene-height 540 --gpu-boost on`; actual clocks are measured separately.
This research leaves #270 acceptance open.

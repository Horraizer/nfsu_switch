# Handling251 wheel contact and load successor

CHECKPOINT
role: worker
agent: GPT-6 visible handling251 worker 01a1161e-2ee4-7d21-9d7d-91774b3af1be
updated_utc: 2026-10-08T00:27:19Z
assignment: issue251/item2 finite body-force/torque/update first divergence
phase: local successor; exact-head independent review pending
parent: 27b47f2519a91c737a346f7efa2b01afbacbbae4
base: 743ebc06b1859e7ef5bdb9ffd2a64789628b0c45
branch: detached WIP; eventual codex/251-handling-tyres-tach-followup; publication held
worktree: owned handling251-followup/nfsu-switch
ownership: same worker; root01a0f5d2 owns wake/correction/re-review/acceptance; reviewer01a11165

## Source and correction

The root closed the previous F2/F3 correction after the independent27b4
verdict. ClosedA1 and that numeric-domain correction remain separate from
the inherited handling acceptance failures. The new assignment retains the
supplied short-brake completed controls, imported initial public pose and
explicit publication context; it does not widen those fixture assumptions.

At27b47f, passive original/current observations locate the earliest actual
body-force mismatch in the initial wheel application, before the first
585410 derivative. The preceding aero applications match. Initial body
position, P/L, V/W, axes and derivative time match. Original and port wheel
axes, ground normals and nominal strut forces also match, including the
original polynomial's slight front cosine scale. This is not a steering
constructor or native-fixture axis defect.

586F10 computes wheel contact from the world hub minus radius times ground
normal. The port used the virtual cast intersection along body-up instead.
586F7D..58704F then adjusts positive nominal load using the wheel-across
axis and ground normal. The port passed nominal load directly. Both missing
producers change the prepared force/torque before body integration; the
later body and strut mismatches are consequences in this supplied context.

The successor adds explicit source helpers for the5AC69C world hub
translation,586F10 contact point and positive-load adjustment. It retains
the per-component binary32 matrix accumulation order, source cross-product
and dot-product grouping, original factor floor and nonpositive-load branch.
478480's body-position subtraction also occurs in binary32. The virtual
ground query remains the existing adapter. The mechanical wheel producer
consumes adjusted load in original units; existing telemetry retains the
nominal SI strut force and virtual cast intersection. A source contact-point
cache writes on contact and persists across a miss, independently of load.
It is used only for active contact forces and is omitted from canonical
bytes, since a later contact overwrites it before any force consumer.
Force/moment caps, body integration, steering, nominal strut force, partner
epoch, numeric-domain guards and integrated aero corrections are unchanged.

## Bounded evidence

Owned ignored evidence is under build/handling251-worker. The original
executable is authenticated and read-only; complete retail content/metadata
manifests before/after native execution match. Synthetic x87 control0x007F
remains a declared witness assumption. No instruction replacement is used.

- body-first-divergence-preservation-r1.json seals the unchanged27b4 trace
  and first-producer audit before correction.
- contact-kernel-compare-r4/report.json compares isolated original5AC69C
  and full586F10 with the final helpers: preserved coupled callback inputs
  plus declared tilted normals, scaled axes, signed nominal loads and
  complete contact/miss point retention branches.
- body-force-port-r5 and f1-strut-port-r6 are passive clones of final
  production code, run twice fresh. Their serialized rows match each other.
- contact-final-audit-r3/report.json checks every paired force/arm and
  accumulated F/T application, every derivative entry, every force-end
  P/L/V/W/F/T and snapshot axis, every nominal strut callback, and all declared
  native capture fields after outer normalization, including cached contact
  points and adjusted mechanical loads. All paired values match. Nominal
  strut-force telemetry is checked separately against nominal callbacks.

The finite source agreement does not close F1: the original and successor
both fail the unchanged strict straight-brake displacement threshold. The
measurement and its threshold are in contact-final-audit-r3/report.json.
No gameplay hash, schema, tuning value, tolerance or acceptance criterion
is changed or retired. The prior partner-epoch regression and its nominal
strut-force telemetry pins are unchanged. New source witnesses cover distinct
snapshot translation order, tilted contact normal, front-axis scaling,
alignment floor, rear unit axis, the nonpositive branch and miss retention.
A runtime contact-to-miss regression checks retained source cache with zero
airborne nominal force, without inferring airborne force from isolated
positive-nominal/miss inputs.

## Validation and recovery

The exact commands/results are preserved in contact-targeted-r3/receipt.json
and individual logs. Targeted player powertrain/aero, race session/particles
and bare race audio tests pass. Vehicle, vehicle track, drive and performance
levels still fail inherited behavior/hash criteria; consult the final audit
and logs instead of treating them as green. ClosedA1 --seams-only passes
twice, including valid raised/lowered seam negatives. Existing F2/F3 state,
telemetry and future-refusal regressions pass in the player suite.

Full Default/Release/retail/merged/non-ASCII build-topology and Switch/hardware
gates remain UNRUN for this successor. The Atlantic heavy slot was released;
the current dispatch remains finite correction and review, with no broad
handling matrix expansion. This is a local
WIP source successor, not a required-gate or whole-functionality handoff.
No publication, merge, deploy or retail write occurred. Root direct visible
chat delivery plus its existing heartbeat is the completion/wake mechanism;
the worker does not contact or dispatch the independent reviewer.

Next: root preserves the successor pathlist and assigns the same independent
reviewer to inspect the exact diff before this narrative, reproduce the
bounded source comparison, verify the correction and regression effects,
and return any findings through the root for the same-worker correction loop.
Remaining issue251 behavior/controller/live acceptance stays open.

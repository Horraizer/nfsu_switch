# Drift camera and scoring follow-up

## Original-code evidence

The authenticated PC USA1.4 executable is the same input used by
`tools/race_camera_native.py` and `tools/drift_state_native.py`.

- `4B47A0` selects the fourth configured camera slot when entering drift.
  `42E020` retains that slot in the drift cycle. The ordinary cycle skips it.
  The port now starts drift with its authored high anchor and passes the mode
  to the camera cycle. The generic camera geometry and steering are unchanged.
- `43E790` promotes a multiplier only after the pending score exceeds its
  target. Equality must keep the preceding multiplier. The port previously
  used an inclusive comparison. Native boundary vectors now cover every
  performance class and promotion, plus adjacent speed values.
- `43E310` also uses a strict threshold for the direction-change chain gate.
  Its comparator now follows the original expression.
- `43FBB0` admits a recent qualifying impact at the inclusive impulse
  boundary; `43E310` has a separate strict penalty threshold. The live drift
  adapter now preserves that distinction. `466500` confirms the absolute
  normal-height test; that test was already correct.

The score formula, class-dependent minimum speeds, progress high-water mark,
bonus-wheel factor, separate competitive/general totals, banking, impact
latch and finish publication were inspected against the recovered producer.
Checks are named below; this is not a claim of whole-race bit identity.

## Verification entry points

- `tools/drift_state_native.py`: executes original combo code and writes
  fresh native vectors. An inactive timer is represented with the same zero
  timestamp in both sides; the report now preserves that input exactly.
- `nfsu_drift_state_check`: compares those vectors and exercises real banking,
  cancellation, notification publication, wrong-way reset and finish states.
- `tools/race_camera_native.py`: native ordinary/drift cycle, initial camera
  dispatch and viewport projection, without camera/math callback hooks.
- `nfsu_drive_tests`: both camera cycles compared with the native fixture.
- `nfsu_drift_track_check`: a real Civic drift load selects the native initial
  high view; an input-only slide must accrue and bank competitive points.

## Limits

The owner's last event13 score establishes a completed playable drift, not a
replay of the same controls in the original. The existing log has no per-step
style state, so it cannot establish that every individual point matches.
Native scalar/state witnesses and the live integration check are separate
pieces of evidence. Track contacts and their history, camera collision
retraction and all visual HUD transitions retain their documented adapter
limits. Human Switch acceptance of the changed view remains separate from
packing the NRO. General release/merge checks and independent review remain
required before publication; this experimental pass uses the scoped checks
requested by the owner instead of another full-game test cycle.

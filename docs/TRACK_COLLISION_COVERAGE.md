# Track collision coverage and prop-pool maintenance

The owner reported pass-through cones, fences and bridge barriers on Terminal
in Quick Race with opponents and traffic enabled. The original language table
identifies Terminal as track1004. Inspection distinguished authored Smokeables
from static track polygons; visible scenery is not automatically a collision
shape and was not converted into guessed boxes.

## Confirmed failure and correction

The existing registry retained moved, sleeping, zero-lifetime Smokeables
indefinitely. Once its bounded live pool was occupied, later Virgin conversions
failed. The proxy was discarded but its unmoved scenery stayed visible. Thus a
visible cone could have no remaining collision body. The failure was reproduced
with a real Terminal cone and a supplied full-pool state before the correction.

Original595670 invokes595750 under pressure and on its periodic phase.
Original595750 protects awake bodies and definition+E4 exemptions, measures
proximity to human actors through421400, and queues eligible resting bodies
for destruction. It uses the initial free-slot snapshot, not a threshold changed
after each queued removal. Under the stock006EF284 configuration, its periodic
pass also retires a sphere wholly below its local road height.

RaceProps now runs that maintenance before body integration, records the first
activation clock, retains the descriptor exemption and COM-relative sphere
radius, and frees eligible bodies before admitting more proxies. The road-height
caches are bounded by live slots rather than allocated for every placement.
Rivals and traffic do not act as human proximity anchors.

A failed conversion also retires its scenery owner. The production renderer now
handles an expired placement even when it never received an impulse. This keeps
an exhausted conversion from leaving visible, noncolliding scenery.

A separate mode-selection omission was corrected: Quick Race with no opponents
and no traffic still owns authored props. Solver-only diagnostic replays retain
their explicit empty object world.

## Verification and boundaries

- nfsu_prop_settling_tests compares the pressure predicate with the authenticated
  original-code fixture in tests/prop_pool_native.inc and checks maintenance
  cadence. Reproduce the fixture using tools/prop_pool_native.py with the
  read-only PC1.4 USA retail root.
- The generator executes595750 and421400 on explicit one-body lists and pool
  snapshots. List-end, single-human mode and final destructor are declared
  boundaries. It is not a natural full-world contact recording.
- nfsu_race_session_tests exercises the actual contact queue with an occupied
  pool of distant, resting objects and a new roadside collision. This state
  failed before the fix and retains the new collision afterward.
- nfsu_freeride_skyline_retail checks the solo Quick Race branch and runs an
  expired, never-hit placement through the actual renderer to reject stale
  visible geometry.
- Read-only Terminal diagnostics checked authored placement/scenery bindings,
  approaches to prop types, and static polygon admission. They do not establish
  every accessible bridge impact or complete original-game fidelity.

The phase input is the owning simulation frame. Matching the original global
presentation counter after arbitrary menu/pause history is not established.
Current single-human ownership is intentional; this is not paired-LAN world
prop acceptance. Default/Release gates, Switch packaging and runtime acceptance
must be recorded separately at the tested source head. Until the console check,
Terminal behavior on hardware remains unverified.
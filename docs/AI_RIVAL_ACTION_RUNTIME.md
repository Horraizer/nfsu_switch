# Ordinary rival action integration

The shared PC/Switch adapter is `IntroAiPlanner` in `src/intro_ai.cpp`.
`AiRivalActionController` owns persistent action records and the maneuver
intention. It connects the recovered action owner and lazy strategy queries;
it does not replace the physical vehicle solver or the drag strategy.

## Category and path ownership

Original450210/44F070 reads the individual RideInfo+424 performance category.
The adapter derives it from each actual tuned rival setup, separately from
configuration735's difficulty context. It supplies this category to the road
context, authored offset/speed columns and driver.

The rebuild condition in45AE10 includes the selected strategy's virtual1C.
Policies1/2/4/5 return true (577FD0); policy3 returns false (5314D0).
Policy3 is the scripted-human strategy selected by4623D0 in a special context,
not the ordinary traffic policy4 selected by583230. Controller4BC can also
force rebuilding. Keeping an action alive
therefore does not imply retaining its old geometric path. Do not introduce
a blanket retain policy for ordinary rivals. Path cursor and extension
retention remain available where the selected strategy actually permits it.

## Action owner and selection

The connected owner follows44B94E,44F700 and44BC30: all completion predicates
run in their original order, explicit requests have their own replacement
path, and automatic candidates replace an owned action only at a strictly
higher priority. Equal-priority factory work does not reset the current
lifetime. Unready updates enter fallback4.

Normal strategy44D8B0 queries recovery10, obstacle19, contact12, traffic2,
passing1, rollover14, blocking5 and following0 in that order. Queries are lazy;
they are not sorted by priority. The controller supplies retained state for
the ordinary default, passing, following, blocking, traffic, contact,
recovery, rollover and stopped actions. Actual contact pair endpoints come
from RaceSession instead of a guessed nearest car. Later controllers see
paths published earlier in the actor update order.

Following uses45BAE0's single-target ordered search. It must not use45B150's
all-neighbor score selection. Both searches call the existing native-derived
path geometry, fitness and collision prediction. The maneuver horizon is
7006EC; projection blending uses the separate6B7538 horizon.

## Evidence and limits

`nfsu_ai_path_tests` pins original450210 outputs across supplied moving poses
and every ordinary performance category. It also compares45BAE0 selection
and candidate order against the authenticated executable with explicitly
supplied geometry/admission services. The latter is a control-flow oracle,
not independent evidence for geometry; the separate existing maneuver,
fitness and prediction fixtures cover those producers.

`nfsu_ai_strategy_tests` covers native strategy query order, priority gates,
blocking and connected owner admission. Connected contact-action outputs are
checked against native cycle fixtures by `nfsu_ai_path_tests`.

`nfsu_rival_action_ug0_retail` and `nfsu_rival_action_event8_retail` use real
retail grids, three physical rivals and configured traffic. They require all
rivals to finish and then stop. Human pilot input is synthetic. These checks
do not establish original lap times, identical whole-race trajectories or
console performance.

## Authored modes, reactions and actor phases

Configuration736 is propagated from the event setup to each RaceRivalSetup,
including the Switch producer. Original4B420B clamps authored values outside
0..2 to1; it does not subtract one. The original77A340 mode row supplies the
blocking priority and length policy. `nfsu_ai_strategy_tests` checks these rows
against `tools/ai_strategy_vectors.py` executing the authenticated original.

Original452B40 additionally checks the selected traffic owner's strategy
virtual0C. Ordinary traffic policy4 rejects this special action2 request;
policy5 admits it. Special global flags bypass this gate. A moving ordinary
traffic obstacle therefore continues into the passing query rather than
being forced into action2 merely because its actor role is3. The production
snapshot carries the actual traffic policy. Missing policy metadata refuses
instead of manufacturing a negative query. Native fixtures execute452B40 and
the actual strategy virtuals with a declared collision-query service; a
connected-owner regression checks both policy4 and policy5 selections.

Original44C2E0 consumes the shared WORLD+2D0 stream before passing-target
admission, and after admitted contact/traffic queries. The actor reaction
state follows429100's strict timer replacement and429190's asymmetric timing
comparisons. A busy timer still consumes the original random draws. These
are signaling reactions, not physical steering overrides. Rival remote-audio
vtable6BF2C0 has no-op48/4C methods and false50, so restoring this state does
not invent an audible horn for a normal rival. Headlamp state855 is consumed
by56D8D0; the ordinary owner's already-on headlamp gain is clamped there.

`tools/ai_reaction_vectors.py` executes the unmodified producer, timer setter
and post-physics consumer on declared actor/neighborhood/time/RNG inputs.
Its only external service replacements are recorded audio status/start/stop
services for the timer-output fixtures. `nfsu_ai_strategy_tests` pins the
resulting states, exact float bits, final RNG states and output boundaries.

Within the ordinary rival group, each controller now performs road context,
neighbor/path input, action selection and speed publication contiguously.
Later rivals observe earlier publications. The human context follows the
rivals, as in420C8A's role phases. There is no all-cars road-RNG prepass.
An actor whose driver has not published a path has empty prediction geometry;
that is not a missing-service failure. Reverse recovery retains its separate
motion-prediction branch.

The retail connection census in the local r62 evidence checks the complete
TRACKB set. Its sole type9 connection belongs to the train crossing in drag
TRACKB1201, hash24CEBC53. No ordinary campaign circuit/sprint uses it.
The live animated train producer is a separate drag boundary and remains
unsupported; an annotation alone must not fabricate obstacle action19.

Full original AI equivalence is not established by these component fixtures.
The human intent path remains a port adapter. Pre-world RNG construction,
traffic/controller/audio scheduling across the complete world and identical
whole-race physical trajectories remain outside this ordinary-controller
pass. The runtime retains its explicit deterministic port seed. These tests
use no new live owner recording and do not establish console performance.

## Ordinary controller cadence and driver publication

Original44B56D..44B70F selects one rival from the role2 world range with
the signed frame counter modulo8. A full update requires the selected actor
and at least0.2 seconds since controller4C0, or a missing initial action.
`ai_rival_full_update` preserves this caller gate; it is not a driver or
physics limiter. `nfsu_ai_strategy_tests` checks it against the authenticated
native branch fixture from `tools/ai_controller_cadence_vectors.py`.

The runtime keeps each driver's last path, speed and catchup publication on
ordinary intervening frames. It does not rerun the action factory, candidate
queries or speed jitter. Blocking action5 updates/publishes without running
the scheduler. A side descriptor2/3 targeting a human can refresh its path
when the target remains in the current neighborhood; that branch retains
the preceding speed publication. It does not resample a path when no new
publication occurred. One-shot reset/cleanup flags are not repeated while
retaining a plan. Physical drivers still run every world step.

Driver virtual3C tail-calls459980 after copying an action path. The shared
`ai_path_publish` redistributes its existing points uniformly by arc length,
using459870's original interpolation and583F20's distance-to-index curve.
It preserves route metadata and clears the original cursor/prediction epochs
only when the count exceeds two. The cumulative squared-length order is
original4599ED..4599FB's `(dz*dz+dx*dx)+dy*dy`; changing that order affects
real captured nonuniform paths. `nfsu_ai_driver_tests` pins native publication
bits, including a captured-polyline rounding witness. Both ordinary and drag
planner publications use this common boundary.

The real recording consumer in ORIGINAL_AI_RECORDER.md compares the gate,
publication, driver commands and retained regulators on actual captured
states. This establishes conditional component behavior, not matching
whole-race inputs. The port's world-frame origin, pre-world RNG seed, physical
LOD selection and complete traffic/human scheduling remain adapters or open
boundaries. In particular, ordinary rivals still use the port's explicitly
selected physical model; this change does not pretend to restore original
far-vehicle LOD by skipping physical simulation.

## Regenerating native fixtures

Use the authenticated PC1.4 USA retail installation read-only. The generators
require Python, Unicorn and Capstone. Supply an ASCII retail-root path.

- `tools/ai_path_context_vectors.py --rival-default --grade G` produces the
  per-category default-action witnesses. Run for each supported ordinary grade.
- `tools/ai_target_path_vectors.py` produces following-choice control witnesses.

Both require `--root`, `--output` and `--evidence`. Their evidence JSON records
the executable identity and supplied inputs. Generated fixture values belong
in the tests, not in this document.

The strategy and reaction generators also require `--root`, `--output` and
`--evidence`. Each records the authenticated executable and declared service
boundaries. Raw retail bytes are never written into these artifacts.

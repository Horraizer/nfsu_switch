# Career difficulty and catchup

## Original policy

The profile selects Easy/Medium/Hard. Each rival in each event has three
signed contexts in FrontB event records at rival+0x10. The context is not
simply the profile index, and can differ between the three rivals. Event0
uses the same authored contexts on all profile difficulties.

Rival performance is adapted to the actual owned human car by57F700.
Difficulty affects the driver's selected configuration, rather than changing
the human car's acceleration. Fixed setup support is bounded by the event
parser; an unsupported setup is refused.

Ordinary catchup is44BD10/44C010. The controller waits for its configured
update interval and startup grace. It compares total competitive progress,
including completed laps, and converts the difference to distance using the
course length. A positive distance means that the rival is ahead.

Two distance curves are interpolated using the rival context. Rank difference
contributes separately, weighted by event+0x12C. A bounded speed-difference
correction and the human's recent speed/road-speed history also contribute.
The history retains ten samples;430778/43080A/584270 define its timer and ring.
Target speed can be reduced below its ordinary road target when the rival is
ahead, or increased when behind. Actor+0x404 receives max(1, catchup), so a
speed penalty is not a negative physical boost. This is conditional assistance,
not a guaranteed overtake or a forced finish-time target.

Table selection depends on course length and traffic density. In4B5DF0,
setup+0x59D4 copies event+0x134 into78A300.44C0E0..44C101 chooses the alternate
curve bank when that traffic selector is2 or3. It is not the race-type field.
The parser separately reads track lap-skip/point-to-point state for length
selection. Multiplayer/progress wrapping is a separate original condition;
ordinary single-player career does not wrap away completed laps.

Drag sets78A344 and bypasses ordinary44BD10 catchup. Its signed launch lead
and shift errors come from the selected configuration. Negative launch leads
can open throttle before GO; positive values delay the rival after GO.
Career retains the native Underground gearbox defaults rather than applying
quick-race difficulty overrides. See root-drag-career-20261019.md in checkpoints.

## Audit and correction

The 2026-10-22 audit found that ai_catchup_tuning selected its curve bank from
the drag boolean. This kept dense-traffic ordinary races on the wrong bank.
RaceRun now reads the authored traffic selector and passes it through the
planner to the decoder. No arbitrary difficulty, boost cap or finish time
was introduced.

The original-code witness is tools/ai_catchup_vectors.py. Its fixture is
checked by nfsu_ai_catchup_tests, including the decoded retail table, startup
grace boundaries, both directions of distance/rank advantage, history,
traffic selectors, long races and point-to-point courses. It executes the
authenticated speed.exe instructions with supplied race snapshots and no
semantic replacements. nfsu_career_races_tests checks that the production
planner receives the event's traffic selector.

## Evidence limits

These checks establish the isolated arithmetic, table selection and bounded
production wiring. They do not establish identical complete race trajectories,
original scheduler ordering, traffic interactions, owner input or original
live frame pacing. A live original comparison and Switch play acceptance
remain separate. No independent reviewer has approved this local integration.

# Underground drift: scoring, HUD and handling

## Scope and evidence

This article describes the PC USA 1.4 implementation recovered from the
owner's copy of Need for Speed Underground. It is not a proposal for new
scoring rules. The first campaign drift is event 3: Drift Track 1 (1301),
four forward laps in stream region L1RB. Event 0 is its prerequisite.

Executable identity: `speed.exe`, SHA-256
`c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7`.
The score tuning records come from chunk `0x34A07` of `Global/GLOBALB.BUN`;
English captions come from `Languages/English.bin`. No retail files are
modified or redistributed by the recovery tools.

The original has three distinct layers:

1. A drift producer measures the car, progress, collision and wheel surfaces.
2. A shared style-event accumulator manages continuous scores, short gaps,
   minimum duration, banking, cancellation and notifications.
3. The drift HUD displays those states and maintains a score leaderboard.

The initial implementation of `drift_score_factor` and `drift_forces` is
checked against the bounded native calls in `tools/drift_native.py` and
`tests/recordings/drift-native.txt`. That scalar comparison does not establish
complete race, collision, steering, tuning or HUD parity. The live adapter,
state machine and campaign entry now exist; their individual checks and
remaining limits are described below.

## 1. What the game measures

The basic drift angle is the difference between the car's heading and the
horizontal direction of its velocity. It is not the steering-wheel angle.
A driver can countersteer while the car still has a large drift angle.

The game obtains a 16-bit direction from the velocity, subtracts it from the
16-bit heading, converts the result to radians and wraps it into a signed
interval. Scoring uses the absolute value; some handling and chain decisions
also need the sign. Helper `0x464EA0` suppresses this angle at low forward
speed; `0x464ED0` contains the actual angle calculation.

Speed in the following formulas is the magnitude supplied by the actor's
`+0x3D4` field, in metres per second. The scoring producer also reads:

- performance class, returned by `0x57F150`;
- yaw angular velocity;
- route progress and the highest progress already visited;
- whether a wrong-way/reset timer is active;
- the current drift style phase (qualified, unqualified, waiting or terminal);
- the surface category beneath each of four wheels;
- recent collision state.

Performance class is **not** the Easy/Medium/Hard selection. Difficulty has
separate effects, including handling and the simulated opponents' scores.

## 2. Admission: when a slide can earn points

The base producer is `0x43E540`. It first requires forward progress beyond
its previous high-water mark. It updates that high-water mark even when the
current sample does not earn points. Driving back over already covered
progress therefore does not simply farm the same section again.

Other gates are:

- no active wrong-way/reset timer;
- absolute slip angle greater than five degrees;
- speed strictly above a class-dependent threshold.

The speed threshold has two tables. The stricter table is selected for an
drift whose style phase is not qualified (phase 0), or when `abs(yaw_rate) > 0.1` and
`slip_angle * yaw_rate < 0`. The latter condition distinguishes the direction
of rotation relative to the slide; it is not a test of the player's stick.

| Performance class | Qualified threshold, m/s | Stricter threshold, m/s |
| --- | ---: | ---: |
| 0 | 12.5 | 15.277778625488281 |
| 1 | 11.111111640930176 | 13.94444465637207 |
| 2 | 9.722222328186035 | 12.777777671813965 |
| 3 | 8.333333969116211 | 10.083333015441895 |

These are the original binary32 table values at `0x6B7890` and `0x6B7880`.
They must not be replaced with one universal minimum speed. Boundary
comparisons and the original float stores matter near the threshold.

## 3. Base score formula

For an admitted sample, let `v` be speed in m/s and `a` the absolute slip
angle in radians. The producer first stores this quotient as binary32:

```text
r = float32(v / 10.083333015441895)
base_factor = float32(r * r * a)
```

The drift rule then supplies a rate of 120. The continuous accumulator uses
frame time, the current combo multiplier and any surface multiplier:

```text
new_pending_points = old_pending_points
                   + combo * 120 * base_factor * surface_multiplier * dt
```

This is a description of the normal accumulation path. Entry delay,
minimum points, timeout, cancellation and publication order still govern
whether these pending points become a completed drift.

Consequences:

- Doubling speed roughly quadruples the base contribution, if all gates and
  other factors stay equal.
- Doubling the angle doubles the contribution, subject to the car actually
  sustaining that angle and continuing to make progress.
- A larger stick deflection alone earns nothing.
- The same trajectory must not earn twice as much simply because rendering
  runs at twice the frame rate; elapsed simulation time is part of accrual.

For example, at 20 m/s and 0.5 radians, the recovered base factor is about
1.967. Before combo or zone bonuses, that corresponds to about 236 points
per second while the sample remains eligible. This is an explanation of the
formula, not a promised score for a particular driven corner.

## 4. Pending points versus banked points

The retail DRIFT record is continuous. Its minimum duration is approximately
0.1 seconds, its minimum raw score is 50, and its gap timeout is approximately
0.3 seconds. Those are separate conditions. A very brief slide can produce
internal accrual without becoming a completed scored drift.

The shared accumulator distinguishes waiting, active-but-not-qualified,
qualified, completed and cancelled states. A qualifying drift can survive a
short interruption. After its timeout it is banked; an unqualified attempt
can expire without an award. A collision can cancel the current event.

The implementation must preserve the order of publication, reset, timeout
and accrual. Rounding every frame to an integer would throw away fractional
points and alter the result. Integer text on the HUD does not mean that the
underlying accumulator is integer-valued.

Relevant original functions are `0x436230` (trigger/update), `0x4361B0`
(accrual), `0x436940` (event tick) and `0x43E310` (drift producer tick).

## 5. Combo multiplier

Function `0x43E790` controls the displayed multiplier, up to X5. It is not
just a counter incremented after each corner.

The multiplier progression uses performance-class score thresholds:

| Class | First threshold | Second | Third | Fourth |
| --- | ---: | ---: | ---: | ---: |
| 0 | 350 | 1400 | 4200 | 11200 |
| 1 | 300 | 1200 | 3600 | 9600 |
| 2 | 250 | 1000 | 3000 | 8000 |
| 3 | 200 | 800 | 2400 | 6400 |

These come from `0x6B78A0`. The code compares the pending drift score with
its current target, updates the multiplier and computes the next target.
The HUD obtains the remaining target from the producer rather than deriving
it from the total race score.

Speed and wrong-way state can reduce the multiplier. The speed thresholds
are 0, 30, 40, 50 and 60 km/h, with a 5 km/h margin. A 1.5-second timer
controls reduction/recovery states. The HUD has separate flags for the
warning and recovery animations. Replacing this with an immediate reset to
X1 whenever speed drops would change both scoring and presentation.

The original texts include `GAIN X%d WITH` and `MAXXXED`. The first displays
the next multiplier target; the latter is the maximum state.

## 6. Bonus zone

Function `0x43E6E0` counts wheels whose published surface category is 3.
This is a physical surface test, not a screen-space distance to the wall.

If more than one wheel has that category and speed exceeds one quarter of
the vehicle speed value returned by `0x580240`, the surface multiplier is
the number of qualifying wheels: two, three or four. Otherwise it is one.
The exact meaning and construction of that reference speed must remain tied
to its original producer when the gameplay adapter is connected.

The original also triggers a separate `BONUS ZONE` style event from this
state. Its tuning record is continuous, with a 100-point rate, minimum 10,
and a one-second timeout. That style notification is distinct from the
surface factor multiplying the drift score.

Do not implement the zone as an arbitrary additional percentage whenever
the car is visually close to a barrier. It would disagree with the authored
track surface and with partial wheel overlap.

## 7. Wall impacts and recovery

`0x43E9D0` reads the recent collision state. `0x43E310` treats its returned
impact value above 0.2 as a drift cancellation and forces the combo to one.
A larger value above 5 can also trigger the `WALL IMPACT` style penalty,
subject to the collision-type/latch test.

The penalty record has rate -10 and minimum -15. Its factor is supplied by
the collision producer; this is not a fixed subtraction of ten points for
every touch. The collision producer's units and complete latch lifecycle
must be preserved rather than interpreting these constants as km/h or
metres without evidence.

A cancelled pending drift and a negative style award are different events.
They must not accidentally deduct the same points twice, nor erase every
previously banked drift from the entire race.

## 8. Chaining and named drift achievements

The producer tracks the signed extreme of the previous slide. A direction
change, sufficient angle and sufficient speed can trigger `CHAINED DRIFT`.
The recovered thresholds include 0.4 radians and about 17.88109 m/s. The
code has separate tests for the old extreme, the new side and the current
speed, so a tiny left/right steering wiggle is not equivalent to chaining.

A separate progression checks the current drift score against the following
retail achievement records. The comparison is strict; these are not labels
chosen from the final race total.

| Caption | Drift threshold | Style award |
| --- | ---: | ---: |
| GOOD DRIFT | 1000 | 100 |
| GREAT DRIFT | 3000 | 200 |
| SUPERB! | 7000 | 500 |
| COLOSSAL! | 15000 | 1000 |
| OUTRAGEOUS! | 20000 | 2000 |
| INSANE DRIFT!! | 50000 | 5000 |
| DRIFT KING!! | 100000 | 10000 |

`0x43EF30` handles this progression and publishes the localized notification.
A full port needs the animation, lifetime and reset of the notification as
well as the English spelling.

## 9. The drift HUD

The actual mode package is `HUD_Drift.fng`, selected at `0x42B41E` from
runtime byte `0x78A345` or override `0x7361A4`. Its gauge constructor binds:

- `3rdperson_DriftAngle_Angle_degrees`;
- `3rdperson_DriftAngle_Meter`;
- `3rdperson_DriftAngle_Filler`;
- `3rdperson_driftometer`;
- `DriftSpeed_Backing`;
- `SP_Drift_Title` and `SP_Drift_Point`;
- `Style_TotalDrift_Title` and `SP_DRIFT_TOTAL`;
- `drift_multiplier_text`.

The signed angle and the speed/qualification presentation are different
inputs. `0x4A7B00` limits the displayed angle and transforms the meter; this
presentation limit must not silently cap the physical angle used for score.
The drift leaderboard orders scores rather than route time gaps. Lap count,
best drift, total drift, multiplier, pending score, style points and position
must remain separate fields.

The owner's reference videos show the gauge, X1/X2/X3, pending DRIFT text,
named achievements and score standings. They are the visual reference for
placement, transparency and animation; source object names alone do not
prove that a rendered frame matches.

## 10. What changes in handling

It is not correct to implement drift by switching off one generic
stabilizer. There is an explicit drift setup branch in `0x57F2B0` and a
per-update force function, `0x479350`.

The setup branch distinguishes drivetrain layout using rear drive fraction:
front-biased below 0.2, rear-biased above 0.8, and the middle category between
them. It adjusts how upgrades affect mass, alternate yaw inertia and tyre
friction, and supplies drift-specific downstream values. Those changes must
be applied to the selected car's real tuned record.

For the per-update force, let `a` be signed slip angle and `w` yaw angular
velocity. When `a*w > 0`, the game adds an up-axis torque:

```text
torque = drivetrain_and_difficulty_gain * angle_curve(abs(a)) * w
```

The gains are negative:

| Drivetrain category | Easy | Medium | Hard |
| --- | ---: | ---: | ---: |
| Front-biased | -14 | -11 | -7 |
| Middle | -9 | -7.5 | -5 |
| Rear-biased | -10 | -9 | -8 |

The angle curve is piecewise linear through approximately
(0 degrees,0), (45,0.7), (75,0.9), (89,1), (90,0), and stays zero beyond
its endpoint. This provides conditional damping as the vehicle rotates into
the slide. The opposite sign does not receive that torque from this branch.

The same function computes a tyre factor from:

```text
input = abs(a) * (1 - 0.55) + abs(w) * 0.55
```

A ten-sample curve over [0,1] maps that input to a factor ranging from 1.1
at zero to approximately 0.76 at the upper endpoint. The factor is published
to two wheel components and is consumed by the subsequent sliding/static
friction preparation. It must be connected with the original wheel mapping
and update order, not multiplied into every tyre force at an arbitrary point.

There is a similar adjacent function at `0x479510`, selected by byte
`0x78A346`. It is **not** the drift branch. An earlier exploratory local note
mislabelled it; the event setup and HUD dispatch corrected that attribution
before a gameplay implementation was enabled.

Other controls remain part of the task: drift-specific setup, steering
caller context, recovery/respawn, nitrous prohibition and their actual order
in the vehicle update. Passing a scalar torque comparison does not establish
that an entire drift car already drives like the original.

## 11. Opponents and event result

The first drift runs the player alone on the visible track; the other names
form a score competition. Original `0x43EB00` also updates simulated score
entries, with authored values and the event difficulty factor. Three ordinary
circuit AI cars must not be spawned merely to fill four leaderboard rows.

The campaign adapter compares the final banked score with those entries.
First place marks event 3 won and grants its authored difficulty reward; a
loss returns to the map without that reward. Restart replaces the session
before a result is committed. The profile stores the selected difficulty
and purchased starter car. The other initial campaign events remain separate
work; their map entries do not launch this drift by substitution.

## 12. Reproduction and current limits

From the owned workspace, with Python, Capstone and Unicorn available:

```text
python tools/drift_native.py <retail-root> build/drift-native-new.json
cmake --build build --target nfsu_drift_check
build/nfsu_drift_check tests/recordings/drift-native.txt
```

Use a fresh output filename: the oracle refuses overwrite. It authenticates
the executable, executes bounded original functions, and fails if native
execution does not return. It never runs or writes the retail game.

The base-score oracle supplies the car/style accessors explicitly. The force
oracle supplies an isolated body and wheel context. Neither substitutes for
checking the live caller, full state machine, track surfaces, original video
and the final Switch build. These remaining checks are recorded in
`docs/checkpoints/root-drift-first-20261015.md`.

### Authored simulated opponent scores (43EB00)

Event3 supplies CARL, CARLA and DIRT. Their Easy/Medium/Hard contexts are
(0,1,4), (0,1,5), and (1,2,5), respectively. The event's score scalar is
0.25 on Easy and1 on Medium/Hard. These are input provenance from the
original event descriptor, not a difficulty multiplier invented by the port.

The player crossing a type6 track zone advances that zone's lap visit counter.
The counter is compared with the highest lap reached (42AC60), rather than
the live lap, which can decrease when crossing the line backwards. Once the
counter is ahead of that high-water mark, staying in the zone gives no award.
The first containing zone in file order is considered. Each zone
contains four first-lap values at+0x94 and four later-lap values at+0xA4,
indexed by vehicle performance class. Every virtual rival receives:

```text
range_factor = minimum[context] + (maximum[context]-minimum[context])*random
increment = float32(zone_points * range_factor * event_difficulty_scalar)
score = float32(score + float32(increment * performance_scale))
```

One random value is retained per entry, rather than generated on each zone
visit. The port owns a deterministic seed; this does not reproduce the exact
PC front-end random-call history. The original also draws for the human entry.
The normalized acceleration rating is clamped to[0,10]; the final opponent
scale is clamp(rating*(1-0.65)+0.65,0.65,1). The original global normalizers
are1 in the supported executable. This is distinct from the player's combo.
The original's 24-bit floating-point control rounds each arithmetic operation
in the award and factor calculations. The port preserves that order.

The competition reads the accumulated total of style item0 (drift), not the
sum of all general style awards. Achievement notices and their separate style
awards therefore must not silently inflate the competitive drift score.
Best likewise means the best banked drift chain, not a best lap time.

The second campaign drift is event 13 on track 1302 (four laps forward), with
WART, DIRT and CARL. The native comparison in
`nfsu_original_feedback_retail` checks first and later awards on both tracks,
all three difficulties, and the four-lap zone budget of track 1302 at the
lowest performance class. This shares controlled input samples with the
original; it does not reproduce a player's original lap or RNG history.
For human scoring, 464EA0 reads the published slip and returns zero when
published forward speed is below 1 m/s.


## 13. Port integration and remaining differences

The implemented campaign path is Race Map -> event 3 -> original event
briefing/difficulty selection -> four-lap drift -> result/statistics -> map.
The briefing uses MU_UG_EventBriefing.fng, HEAD_DIRT, the authored event
message and the original three purses. The original implementation started
with event 3; the current campaign also connects subsequent drift events.
The first race still needs to have been won and a starter bought.

Competition points and general Style Points are distinct. At 43DE72 the
original clears item 0's inclusion flag (+15); 436A28 then excludes its
banked total from the general style sum. The drift item still determines
competition rank. Achievement and wall-impact events retain their separate
style effects. Counting the drift total twice would change the career score.

Wrong-way recovery follows 43EDE0: wrong direction or negative forward speed
starts a timer, a one-second grace precedes a three-second countdown, and
reset is requested strictly after four seconds. Recovery uses the existing
track reset service; it does not teleport every frame or award new points
for an already covered progress interval.

Post-race 49CD90 dispatches to the drift statistics path: BEST DRIFT SCORE,
LONGEST DRIFT (seconds), and ALL LAP SCORES. It does not show circuit lap
times under a renamed heading. The port stores completed-lap banked deltas
and score positions; original per-lap result-storage ordering still needs
an independent native trace before bit-identical reporting is claimed.

Validation entry points:

- nfsu_drift_check: score and force scalar vectors from the original binary.
- nfsu_drift_setup_check / drift_setup_native.py: complete 0x2B0 setup record
  comparison for three drivetrain layouts and four upgrade levels.
- nfsu_drift_state_check: native combo vectors, banking, impact latch,
  separate style totals, finish and wrong-way timer boundaries.
- nfsu_drift_track_check: real track load, HUD/map resources, and an
  input-only Civic slide that must actually bank points. The tool changes
  controller input only; it does not seed velocity, angle or score.
- nfsu_career_tests <retail-root> <new-output-directory> drift: briefing
  frames, difficulty rewards, defeat, duplicate reward refusal and resume.
- nfsu_race --event 3 --car CIVIC --generate <new-recording> --out <new-dir>:
  complete route traversal. This conservative driver normally drives below
  the drift admission threshold; a zero score there is not a scoring test.

## Start scene and animated HUD

The single-player selector at 47BC63..47BC82 chooses playlist 9 when there
is one local player and one physical race vehicle. INGAMEB's 0x34036 table
contains scenes 2331..2334 in that list. The port currently chooses 2333
("Fly Guy walking up to car") deterministically. It does not reproduce the
original race RNG history at 47BD07..47BD17. Scene 2337 belongs to a different
launch branch and is not the single-player drift scene used here.

The scene reads its two camera cuts, car motion, starter and spectators from
the user's STREAML1RB and TRACKB1301. The actor class named NISFlagGirl also
selects male starter meshes: its authored mesh field is used without guessing
from the class name. Car and starter compound clips use the recovered half
rate; camera cuts use the scene clock. Geometry is prepared once at loading;
frame updates select immutable animation buffers rather than uploading them
again. The crowd retires at the authored scene end, after GO.

During the cinematic the race HUD is hidden. The chase camera and tachometer
appear at the authored release point; the drift panels and map appear at GO.
The drift car starts physics at the final authored car pose, so switching
from the cinematic does not move it back to a different grid slot.

HUD_Drift.fng supplies the multiplier Highlight, banked-total Highlight,
TooSlowToGain/LosingMult colors and wrong-way text states. Highlight chains
back to Init; LosingMult repeats. Timing uses the simulation clock. Font
resources and Sj vertical alignment follow the original FNG bindings; in
particular TOO SLOW! uses the large Conduit font, not the small timing font.

Presentation checks extend nfsu_drift_track_check: original animation keys,
loop and return boundaries, cinematic/countdown/GO visibility, animated crowd,
unchanged upload pointers and scene-end retirement. The existing
nfsu_start_crowd_check covers the unchanged UG0 lifecycle and restart.

Limits of this candidate:

- This implements the start scene and the named HUD states, not the whole
  FEng renderer. Flying score/penalty text and every leaderboard transition
  are not yet reproduced. Authored 3D countdown objects are not connected.
- Camera placement uses the track start plane; the original dynamic terrain
  height filtering and all four random start-scene variants are not ported.
- The ordinary port does not yet implement the whole 4773F0 assist that the
  original omits in drift. This change cannot claim to disable a fully
  restored ordinary stabilizer. It implements the separately recovered
  drift setup, steering caller, rear-wheel scale and yaw torque.
- Native collision records accumulate solver impulse magnitudes and retain
  record lifetimes. The live adapter uses the port's current polygon/pair
  contacts. Scalar wall cancellation tests are not full collision parity.
- The reference-speed adapter, complete surface-force behavior, original
  front-end RNG history and complete live trajectories are not native parity
  results. These limits are separate from the successful bounded comparisons.
- Console loading, human driving feel, GPU presentation and performance
  require the owner run. No console run is implied by packaging success.

## Separate item0 score display

Original4A85E0/4A6EB0 publishes drift item0 to its own retained record;
it does not enter the ordinary style-notice queue. Failed publication keeps
the previously shown points even after scoring resets, suppresses the old
achievement label and uses the original red lifetime owned by4A83C0.
Successful publication is shown for its update, including a banked record,
then hidden when no new record arrives. The viewport owns presentation state;
repeated render passes do not accrue points or consume a publication twice.

4A87B0 formats the full item0 value without dividing by the multiplier or
adding a signed prefix. The authored SP_Drift_Point and SP_Drift_Title
positions/scales are retained. The bonus-zone packed color selected by4A7660
is decoded with the original4F75B0 channel order, rather than reversing its
red and blue components. A later achievement can replace a label hidden by
an earlier failed drift.

The native verifier is tools/drift_popup_native.py. It executes publication,
display lifetime and actual FEng color/visibility setters; only text output
is captured. It is not evidence of full original FEng rendering. The
registered nfsu_drift_hud_retail and nfsu_drift_state_retail checks exercise
the portable display model and its real scoring connection. General queue
continuous-item retirement, every leaderboard/group animation and final
portable appearance remain separate open criteria.

# Road rival AI parity

CHECKPOINT

role: worker

assignment: Owner request to restore gameplay-relevant opponent behavior on the hard-opponent-pace branch. Gitea API lookup returned TLS alert 112. The previous function inventory is local under build/gc-ai-telemetry/retail-ai-loc-01 in the outer workspace.

base: 81056f48cbcb542c69b946730a694414b1e346c0

## Restored behavior

PC 1.4 USA callback 453060 restores the forward maneuver on leaving the
cross-traffic avoidance action. The controller now does so before selecting
its replacement. The connected transition failed on the base commit.
nfsu_ai_strategy_tests exercises the action through the chooser and inspects
the descriptor from the subsequent factory. The captured Peugeot did not use
this action in the measured event 19 window.

In session world-20261114T092640Z-fd78a5b5 the four actor+1C racer records
have position bytes 1..4 at offset 0A in every available complete in-race
memory capture. 44BF69 loads this signed byte. 44BF82 and 44BF8C index the
history gains with it. Both the port's decoder and its old PC fixtures had
treated the PC byte as a zero-based slot. The arithmetic rank difference
elsewhere is unaffected. The generator now runs the PC code with real
positions and includes both history branches at each position. The old
decoder fails every case with the updated fixture. The arithmetic and the
decoder are checked separately by nfsu_ai_catchup_tests and
nfsu_ai_catchup_rank_retail.

## Connected measurement

At base 81056f4, using the owner's recorded event 19 Hard opening, the
unmodified PC Peugeot was at 127.783 km/h at 21.08975 s after GO. The
unmodified autonomous host replay was at 170.690 km/h. The cross-traffic
change alone reproduced every previous DETAIL record. With both fixes the
Peugeot reached 171.058 km/h. The other two rivals remained at 163.436 and
57.329 km/h at that instant (PC 163.242 and 134.161). The corrected rank
lookup does not resolve the Peugeot's missed park gate. No limit on throttle,
per-event adjustment or recorded state override is part of either change.

At the route decision at clock 100552, the corrected Peugeot pace scale was
0.868274 versus 0.862578 on PC. At the final shared refresh before contact,
clock 116795, it was 0.904158 versus 0.864515. The two runs refreshed on
matching clocks through this point; the original refreshed again immediately
after the gate. Local reproduction: run road-pace-replay.py in the outer
build/gc-ai-telemetry/retail-ai-loc-01 directory. It relinks the original
and modified C++ against the same libraries, compares all baseline output
records and reads actual PC speeds from snapshots.bin. The diagnostic does
not fix the opponents' path or RNG inputs.

## Remaining divergence

In the separate original-steering control at base 81056f4, native and
host world seeds matched on entry to Peugeot's controller from clock 76187
through 81627. During native frame 81627 the generator advanced six times
between the final rival controller return (sequence 186462) and the human
driver entry (186463). The probes do not distinguish the traffic and human
controller phases inside this gap. At 99643 an observed native call from
Peugeot's route planner draws from 462792, the rejected-target gate, while
the host has no corresponding draw. The seeds had reconverged at 99531.
Identify the producer of the six draws and compare both rejected-target
arguments before changing RNG ownership. Do not advance the seed by hand.

## First divergence before Golf meets the player

At 58a7da9, the connected Hard event 19 replay used the owner's
world-20261114T092640Z-fd78a5b5 capture. Times below are relative to GO.
The player's recorded motion and pad and the recorded physics intervals
drive the host. The rivals and collision responses evolve in the host.

- At 5.7455 s Nissan's contact with the first bus sign has a different
  outgoing impulse. At 7.89825 s Peugeot's road boundary clamp uses half
  the model width. The original instruction at 0045C653 loads the selected
  neighborhood entry's lateral half-extent. The two inputs differ even
  when the incoming coordinates are identical.
- At 8.2475 s Peugeot contacts another bus sign. Its incoming pose and
  momentum match the PC frame; its outgoing momentum differs. At 8.27625 s
  its position first differs. At 12.12525 s Golf publishes a different
  passing path. Substituting just Peugeot's recorded heading for that
  decision recovers all 13 points. Golf's position first differs at
  12.18825 s. The PC first admits Golf/Tiburon at 13.1285 s.
- The corrected planner uses the neighborhood half-extent. Comparing both
  point arrays around the roadside contact requires separating earlier
  collision errors. With the two recorded sign impulses in the diagnostic
  only, all raw and resampled coordinates at the five sampled publications
  match. The independent full rerun with autonomous contacts still admits
  Golf/Tiburon at 13.0375 s and misses the first park gate. Feeding the
  port the two native gate endpoints gives the exact PC time and position
  for Peugeot's continuation. Do not change the gate geometry.
- On the two port sign jobs the authenticated PC contact search, rewind
  and impulse routines return the same witnesses, sampled pose and
  outgoing momenta as the port. Those synthetic inputs do not establish
  the witnesses the PC used live. The owner's archive has the car state
  on each side of the contact. The live pair's two witness points inside
  the dispatch and the sign's full rigid state are the missing comparison.

Local reproducer in the outer workspace: build/gc-ai-telemetry/road19-first-touch.
Run full_tape.py race full-event-fixed for the complete connected replay,
early_path.py read for the five publications, combined_native.py for the
two native sign responses, and report.py for the recorded-versus-host plot.
The separate pc-sign capture matches the original executable and records
the pair before and after one actual roadside sign response. A new live
capture must be compared on its own inputs; a new run can have a different
grid or a different car hitting a sign.

## Contact before the first full Move

At d7e6424, in the control with the two roadside sign continuations
restored, Peugeot met the human at clock 76060 and switched from the
reduced to the full mover. Both car positions after the impulse matched
the PC. The driver's throttle and steering word already matched, but the
port used them during the two force refreshes before the first full Move.
The original engine field at +78 was zero on entry to Move and one on
return. 4775F4 sets the front steering matrices; 477607 writes the
throttle. The original constructor at 476ECE also clears the shared
component interval before refresh. Both operations belong to all such
contacts, independent of event and car model.

The contact refresh now retains the fresh mover's initial front wheel
axes and throttle until Move. The registered nfsu_ai_track_tests case
refreshes a replacement twice before its first Move. The constructor's
shared interval reset applies only when the rival actually changes to the
full mover.

Retest with firstmove_controls.py in the same local reproducer: the full,
free replay finished, still admitted Golf at 13.03750 s and missed the
gate. Restoring only the sign impulses or only the junction choice also
missed. Restoring both produced Peugeot's first gate contact at 19.393 s
against 19.414 s in the recording. None of these controls drive the game.

Independent review found the same boundary for the brake fields. The
wheel constructor starts them at zero. 4778B0..4779A0 writes service and
parking brake during Move. A contact refresh before Move used to apply a
held pedal, including locking the rear wheels. Both contact entry points
now use the same refresh. The paired pedal case in nfsu_ai_track_tests
checks throttle, service brake and parking brake individually on fresh and
initialized components. Run firstmove_controls.py pedals for this replay.

## Standing road start, 2026-11-23

At 59a13cf, event 92 stage 3 on Hard, the Dolphin reference Celica
reached 33.18 km/h at one second. The port reached 42.12. Changing only
the replay timestep in the GameCube world reproduced the effect inside
the original: 33.55 km/h at 30 Hz and 41.64 at 60 Hz. Throttle was
already fully open. The seed and upgrades were held fixed.

Two consumers explain the early advantage. The body rest decision
reads the published gear, and the launch traction decision reads the
published forward speed before sampling the previous load and slip of
each tyre. With intermediate 60 Hz publications the body releases sooner
and the traction decision can latch a different diagonal grip factor.
After a standing road grid release the port samples these two operands
together at 30 Hz until leaving the tyre model's standing range. Player
and full-physics rival cars share this rule; the driver, gearbox,
contacts and camera keep their normal update rates. Drag and drift use
their existing launch paths. The sampling and rewind regression is in
nfsu_player_powertrain_tests.

In the candidate based on 59a13cf, the Celica reached 33.21 km/h after
one second and 58.11 after 1.5 s, against 33.18 and 57.54 recorded.
The RSX reached 38.60 against 38.36. The original Eclipse reached
43.26 and the candidate 44.99. The local comparison is under
build/gc-ai-telemetry/career-bot/launch-fix in the outer workspace.
The launch-fix-control recordings contain the original 30/60 Hz runs.

Finish crossings are a separate comparison. The headless field runner
now ticks the real race audio service with the shared world random state,
using an in-memory output sink. In spectate-decision-92/neutral-0 the
original Celica finished in 266.01 s, the otherwise identical control
in 263.61 and the candidate in 275.28. The RSX won all three. In the
four event 19 driving cases, the same controller was run against each
simulation's live state. It can generate different player inputs after
the trajectories diverge. An opening speed match does not predict the
finish; compare the crossing of each named car on each individual run.

Recorded PC intervals are already the native publication clock. Applying
the 30 Hz adapter there moved the first Golf/Tiburon collision to 8.23 s
in the event 19 PC input replay. The fixed-step world now passes its clock
source to the other grid slots. With the native intervals restored, the
complete opening trace matches the control with sampling disabled, byte
for byte. The first contact is at 13.04 s, against 13.13 s on PC. Peugeot
still misses the park gate in this unassisted replay. The session regression
covers a fixed world and supplied 60 Hz and 144 Hz clocks for both cars.

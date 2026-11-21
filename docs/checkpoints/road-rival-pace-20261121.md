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

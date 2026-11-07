# Glow versus streaks diagnostic capture

role: coordinator implementing directly under owner override
agent: Codex root
phase: awaiting-hardware
scope: owner-approved detailed comparison of glow-r1 and streaks-r1
publication: local only; no push, merge, deployment or reviewer

Both variants add identical bounded input/frame recording and GPU pass timestamps.
Use --ab-record in glow-ab-r1, then --ab-replay in streaks-ab-r1.
The menu remains available. Select the same event/free-drive/car/setup.
Record at most 6000 rendered frames; Plus ends and saves complete frames.
Replay repeats input timestamps and simulation tick counts per rendered frame.
Every rendered frame checks canonical RaceRun state and camera float identity.
Pause, mode change, suspend, mismatch or cancellation invalidates the run.
The only route output is sdmc:/switch/nfsu-ab-route.txt, outside retail data.
CPU/GPU rows stay in bounded RAM and are printed only after queue drain.
Use tools/compare_ab_profile.py with the two captured logs to validate pairing.
It refuses invalid verdicts, unequal routes/clocks, or incomplete GPU rows.

Measured passes: cube, road reflections, main scene, streak/resolve,
FXAA, upscale, HUD. Additional fragment barriers are identical in A/B but
perturb scheduling. These are diagnostic timings, not ordinary release timings.
Presentation returns are not actual display scanout timestamps.
CPU record-versus-replay paths have different small capture overhead.
The graphics variants also differ in primary lamp sprites and the opaque glow
shader, so a whole-build delta is not exclusively the cost of history trails.

Checks: native route CTest 1/1; standalone C++17 warnings-as-errors roundtrip,
input clock/button/axis preservation and malformed/context/truncation refusal;
analyzer pairing plus route/clock/incomplete/invalid rejection controls.
Glow initial Switch 51/51. Streaks initial 52/53: profile source witness used
old slot32 literal. Updated to the actual slot160 query layout without removing
its HUD ordering assertion. Final race NRO, actual embedded shaders and profile
contract 3/3 for each variant. Broader native matrices deliberately not rerun.

Remaining: actual Switch record, replay identity, matched timing comparison,
owner visual choice. No hardware result claimed. Accepted glow-r1 is preserved.

# Career continuation implementation checkpoint

Owner: solo sequential continuation after UG0. No delegated sessions.
Base: f0d720b517d039614a2d19d829b5fcfc42c4562f.
Tree: build/day20261008-root-combined, codex/draw-distance-original.
Historical initial phase: implementing, then uncommitted; no publication/upload.

Implemented: retail catalog/rewards/starter purchase, port-owned dual-slot save,
movie return transitions, generic stock frontend scenes, draft career pages,
and initial Switch menu integration. Backend retail tests pass at dirty tree.
Evidence: build/career-implementation-r1/ui-build-r7.log and frames-r4.
Original dynamic career maps are distinct from race minimap assets.

Not complete: UI placement/states/performance displays, actual SDK integration,
movie playback and all starter scene checks. Map event launching and garage
parts editing are not implemented. Existing difficulty/AI, drift physics/score,
world/traffic and earlier UG0 fidelity backlog remain outside this increment.
No new NRO, full-career parity, or hardware acceptance claimed.
Next: correct original page visibility/states and validate combined PC flow;
then compile SDK and package only a coherent candidate.

## Verification and remaining work

PC: native Release build of career, frontend and movie targets passed.
Focused CTest: career retail, existing frontend retail, movie player plus retail
before/after manifests passed. pc-checks-final.log records that earlier delta.
Subsequent UI change initially failed PC capture because TrackInfo was read
from FrontB instead of GLOBALB. Failure retained in pc-scenes.log. Corrected
source reads authenticated GLOBALB; TrackInfo bit2 selects Sprint planes.
Final pc-scenes-r3.log: five original starter scenes and default Skyline reload,
all four career screens and five initial event map records passed. Frames-r8
contains the actual CPU reference renders, not Switch screenshots.
The career atlas compact packing changed allocation from32MiB to16MiB without
resampling. Existing main-menu packing remains unchanged.
MovieReader fully decoded video and audio from02_FAN_s,02_FAN_e,04_GUM_s;
logs movie-*.mad.log. Fake audio-device lifecycle tests cover completion,
active skip, quit and cleanup through the production career movie player.
This is not an actual Switch audio/visual acceptance run.

Actual SDK built and packaged tag career-r1-wip. Targeted NRO identity/RomFS
and embedded shader checks passed2/2 (switch-checks-r2.log). The Windows-host
CMake help command against the MSYS cache failed from path topology; rerun
inside the SDK Bash environment succeeded. No failed command counted as pass.
The copied WIP NRO and SHA256 are in build/career-implementation-r1/verification.json.
No console upload, publish, merge, full Default/Release matrix or hardware
acceptance performed. Full release gates remain unperformed; this is not a
worker handoff or release-ready result.

Current usable increment: initial win reward, movies, starter buying, save
resume, retail event map browsing, Underground menu and first garage entrance.
Current limitations: no launch from the new map, no garage parts editing,
no selected difficulty consumption, performance/reputation UI still unbound,
no complete frontend temporal animation/camera parity. The map and garage are
not a full career. These limitations prevent treating this NRO as ready for
owner acceptance or replacing ug0-memory-r3. Existing UG0/free driving remain
in source; successful compile does not prove their console regression safety.

Next: bind original stock performance/reputation and car-browser presentation,
then connect selected event setup and difficulty before owner build delivery.
Source inspection for original UI producer5801B0/5802C0/580400 and garage
consumer4AFF90 is preserved as exploratory source, not a completed port.
Scope stays solo; no reviewers, background agents or new research matrices.

## Stock frontend ratings continuation

Local WIP base2aa2c2b35e7e665fbca388378d9d7c93ab5e771a preserves the first
career continuation. Current follow-up binds stock performance to UI bars.
The shared581B30 acceleration descriptor was extracted without changing its
arithmetic; AI performance still consumes the same descriptor. Frontend bounds
follow57C8EB/581D30 over all eligible authenticated CarType records, including
upgrade ranges.5802C0/5801B0/580400 calculate the three stock UI ratings.
No simulation physics tuning or stock rating table was substituted.
Stock-only zero visual reputation uses4B0FD0 empty colors and star scale.

Verified from this follow-up tree: native Release career/AI target build;
original executable comparison via tools/career_performance_vectors.py;
all starter PC scene loads, scene reload, four page renders, five event maps;
focused CTest career retail, frontend retail, intro player, AI powertrain and
read-only fixtures. Evidence: pc-stats-frames-r2.log, pc-stats-checks-r2.log,
stats-original-r2/comparison.json and frames-r10 below the existing evidence
root. The original comparison has no hooked producer and uses007F with
original bound initialization and supplied authentic stock setups.

Failed earlier checks are retained: incomplete native declared call range,
probe indentation error, and zero rating fields when the current catalog
source lacked the new binding. The latter failure was reproduced by focused
CTest, not repinned. Restored binding yields the original exact values again.
The original probe also required distinct execution trampolines to avoid
reusing Unicorn's translated block after code replacement. Earlier exploratory
numbers from the reused trampoline were invalid and are not evidence.

Map launch inspection confirms a real remaining dependency: RaceRun and
load_supported_race only accept events0,1,8; full rival grid/planner integration
is conditional on event0. The career map is browsing, not a generic working
race launcher. Do not route other modes through event0 or claim full career.
Garage remains a screen and tutorial return, not a parts editor. Difficulty
is saved but not a completed selection/setup interface. The previous broader
UI, drift, progression, traffic/world and hardware limitations remain open.

Next: connect the generic event setup/rivals and completion return before
claiming a playable next career event. Current PC images may be shown as
progress; WIP NRO must not replace the accepted UG0 build. No publication,
merge, full matrix, console upload or hardware acceptance in this follow-up.

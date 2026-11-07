# Drag GPU startup correction, 2026-10-18

Owner reported drag failed again after difficulty-r4. Runtime log:
build/difficulty-original-r2/nxlink-difficulty-r4-run1.log, lines124..156.
Grid, scene, session, audio, HUD and first frame all completed. Renderer open
refused reflection group0 copy594: invalid vertex or index range. This was a
caught load error, not established OOM or an OS crash. Owner returned to menu.

Reproduced on PC with complete validate_recorded_commands enabled in the
production career scene test. drag-repro-r1.log records the exact failure:
track reflection needed80 vertices and42 indices, but ResourceMap overwrote
track source0 with race-nitrous/atlas-quads(16 vertices) and atlas-indices(24).
Source indices are local to each package; the combined table's effect and
rival resources reused them. Earlier tests checked only nonempty draw counts.

Correction: ResourceMap takes an explicit car/track resource namespace. All
three callers select their own namespace; reflection mapping ignores appended
car, rival, crowd, particle, glow and nitrous packages. Validation remains
strict. No effects removed, no memory limit increase, no physics changes.
Career scene check now includes particles and complete renderer-open command
validation, including reflections and combined resource bindings.

Focused verification under owner instruction to minimize tests:
- cmake --build build --target nfsu_career_races_tests -j6: PASS,
  build/drag-gpu-r5/build-fix-r1.log. Earlier compile typo failure preserved.
- NFSU_CAREER_SCENE_CHECK=1 nfsu_career_races_tests RETAIL4 1:
  PASS on CIVIC,GOLF,MIATA,NEON,PEUGOT; drag-*-r1.log. Actual production
  geometry, all rival resources, particles/glows, reflections and HUD loaded.
- Same complete scene check: UG0 with authored INTEGRA, events1,2,5 with
  stock CIVIC: PASS, scene-event*-r1/r2.log. First UG0 attempt accidentally
  used CIVIC and correctly refused authored-player mismatch; preserved.
- build/drag-gpu-r5/build-switch-drag-gpu-r5.sh: SDK build exit0 and7/7
  selected package/shader/GPU-contract checks PASS, switch-build-r1.log.
- Bash ASCII/entry-mode/whitespace scans PASS, ascii-r1.log. New artifact
  names, scripts and source snapshot bytes checked separately when sealed.

NRO/source identity: build/drag-gpu-r5/drag-gpu-r5-receipt.json.
Current tag drag-start-gpu-r5 retains the SD startup journal.
Difficulty, dirt and shadows remain included from difficulty-r4. Frozen source
diff versus r4 contains only draw_commands.cpp, career_races_tests.cpp and
root checkpoints. Earlier sealed builds and failed history preserved.
Hardware start/play acceptance remains pending; this desktop reproduction and
package validation are not evidence of a successful Switch drag race.
No Default/Release full-matrix rerun, independent review, source publication,
commit/merge of staged GLM integration, or release claim.
Next: confirm Netloader readiness, send sealed r5 once and inspect actual
GPU-ready/first-present/play/finish log from owner test.

Owner confirmed readiness and authorized r5 upload. nxlink reached server
active; no stderr or game stdout at this checkpoint. Runtime log and process
identity are in build/drag-gpu-r5/upload-drag-gpu-r5-run1.json.
PID72480 start2026-10-18T15:48:14.1746021Z. Leave this owned log server running.
Next: inspect the actual owner drag attempt, particularly drag.gpu.ready and
first-present stages. Successful hardware gameplay is still pending.
Frozen source ZIP precedes this upload-only checkpoint.
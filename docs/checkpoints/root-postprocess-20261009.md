# Original Light Glow comparison

role: worker
agent: Codex root, direct owner assignment
base: 5c235b577bc2bc0f6c245ed5ff61dd635d66066d
phase: awaiting hardware evidence
branch: codex/draw-distance-original
worktree: build/day20261008-root-combined
saved: local-only source and artifact package; no push or merge

Owner selected original Light Glow after root corrected the unsupported
full-scene bloom premise. Bounded retail inspection identifies LIGHTGLOW,
LIGHTSTREAKS and MOTIONBLUR separately; filter.efx alone does not prove bloom.
No retail files modified. Work is direct, without workers/reviewers, using
the owner's minimal-check comparison workflow.

Implementation: shared original 0x5443F0 primitive moved unchanged from
frontend_motion to light_glows; actual race car markers/normals, three
original GLOBALB images, two head layers and three tail slots, ordinary-owner
gain and service brake input. Original payload uses persistent car residency.
Missing markers remain absent; original full occlusion/special-owner/damage
activation and complete pixel parity remain unverified. No full-scene blur.

Verified from the actual workspace path:
- tools/frontend66_car_glow_native.py: genuine original consumer PASS.
- cmake --build build --target nfsu_freeride_skyline_tests -j6: PASS.
- nfsu_freeride_skyline_tests.exe with authenticated retail root: 0 failures;
  both events, complete effects, original corner/color witness, visible glow
  commands, frame/cube generation, frontend return/re-entry.
- root-glow-r1-build.sh: actual Switch SDK, shaders, package and CTest 51/51.

Logs and exact NRO identity are in runtime-relative
build-artifacts/draw-distance-20261008-root/glow-r1-*.
Initial compile and texture-filter failures are retained in earlier logs;
the final native and SDK logs supersede them. No broad native matrices rerun;
historical gameplay/image-pin failures are not claimed resolved.

Next: fresh owner Netloader readiness, send the sealed glow-r1 NRO and
compare lamp appearance/braking plus runtime timing against aniso4-r1.
Hardware acceptance and measured GPU cost remain open.

# Original Light Streaks comparison

role: worker
agent: Codex root, direct owner assignment
base: b89358efe690ead07a0f492b38dcdfd43e016a92
branch: codex/draw-distance-original
worktree: build/day20261008-root-combined
phase: awaiting hardware comparison
saved: local only; no push or merge; no delegated agents

Owner accepted glow-r1 appearance, then requested Light Streaks separately.
Original 409B70 uses a persistent full-view target. 409BCA sets blend factor
0x008296B4; first 40D6E0 submission uses ZERO/BLENDFACTOR, the four light
consumers add current emitters, and second submission uses ONE/ONE.
40CC55..40CCC5 allocates the view-size render target and clears it once.
The recovered cadence is one update per rendered main view, not delta time.

Implemented one persistent RGBA8 scene-size image in the Switch renderer:
RGB history fades by 130/255,150/255,180/255, current car and known authored
road sprites are depth-tested into it, then RGB is added before FXAA/upscale
and HUD. Same queue/barriers serialize it across swap slots. Renderer lifetime
clears history on race exit/re-entry. No full-scene threshold, spatial blur,
scene feedback, camera reprojection or arbitrary longer decay added.
The comparison uses FXAA with the accepted anisotropy and mip settings.
Streaks are disabled for the separate MSAA path whose depth is multisampled.

Primary type-9 street sprites reuse existing parsed authored lamp records,
original descriptor/texture and primary-view billboard equation. This retains
that loader's ground-hit subset; unknown other light classes/auxiliary lists,
original occlusion-query behavior and complete original pixel parity remain
unproved. Seven car slots and fixed per-road-marker slots keep the prepared
package stable. Invalid/invisible lights contribute zero. The opaque backend
sprite shader now implements the same original material as the other fragment
variant; it previously missed the new car-glow branch.

Validation from actual workspace path:
- tools/light_streaks_native.py --retail-root: PASS original pass scheduling,
  blend factor, producer order and non-main gate. GPU calls are captured
  boundaries, not original GPU pixels. Initial stub stack-cleanup failures
  retained in r1/r2 logs; corrected final witness returns normally.
- cmake --build build --target nfsu_freeride_skyline_tests -j6: PASS.
- nfsu_freeride_skyline_tests.exe with retail: 0 failures on both events,
  full-effects load, authored road + car draw namespace, frame/cube generation,
  frontend return/re-entry and cache ceiling. This is native scene validation,
  not a PC execution of the Switch postprocess shader.
- root-streaks-r1-build.sh: real SDK/shaders/package, 53/53 PASS.
- After final cache-accounting edit: race NRO rebuilt, package gate PASS;
  embedded shader test resolves and checks both new blobs in actual RomFS.
- No repeated full native matrices, per owner's reduced comparison workflow.

Evidence: runtime-relative build-artifacts/draw-distance-20261008-root/
streaks-r1-* and streaks-root-original*.txt. Final manifest seals NRO identity.
Next: fresh Netloader readiness, send sealed streaks-r1, owner visual check
while passing street lights/braking/turning, then inspect actual GPU timing.
Hardware first-frame safety, appearance, timing and acceptance remain pending.

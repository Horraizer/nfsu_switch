# Free-drive fidelity r41

CHECKPOINT
role: worker
agent: GPT-6 root combined owner session
updated_utc: 2026-10-23T11:56:20.797468+00:00
assignment: owner free-drive r40 feedback; props, jump camera, NOS blur
phase: validating
branch: codex/draw-distance-original; owner publication hold
head: ea4d472c9c399265370fa10be94736b1121771ad
base: none; mixed integration with MERGE_HEAD 7d18437db01bd799bb4ac20bd759055f18b02e47
worktree: build/day20261008-root-combined
saved: local only; source edits and baseline backups in build/free-drive-fidelity-r41
verified: r40 console run Tiburon/1006 completed and exited; sparse frame/GPU samples do not prove full-frame 60 FPS; targeted crash camera, ground/shadow and blur checks passed; original filter vertex witness passed; complete r41 gates starting
decisions: free drive skipped RaceProps setup because RaceRun gated it on rivals or time trial. Include free_drive_ in that gate. Original camera ground query 440A40 returns height zero on valid no-hit and predictor 429CF0 does not abort. Reuse the existing original-compatible cached polygon selector in shared track_ground layer. Owner declines subjective NOS video; continue read-only Ghidra code comparison.
remaining: tests for free-drive props and camera adapter; full required default/Release/SDK/ASCII gates; NOS entire render path comparison and confirmed correction; hardware acceptance
next: run required Default, Release, SDK and ASCII gates; seal NRO and record remaining hardware acceptance
blockers: Git publication explicitly held; Switch appearance/runtime acceptance requires later hardware run; no blocker to local investigation
ownership: root active solo; no reviewer approval claimed

## Local evidence

- build/free-drive-fidelity-r41/initial.json pins initial HEAD, index and source hashes.
- root-r40-console.log is a frozen completed r40 log.
- root-ghidra-*.json record read-only Ghidra 8091 queries of speed.exe.
- Retail speed.exe SHA-256: c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
- Original blur 40CD40 arithmetic and existing native oracle agree; this alone does not prove GPU image parity. Four-tap pass 40D090, reduction 40D370, composite 40D6E0 and camera presentation are under inspection.
- Source changes so far: free-drive RaceProps gate; shared TrackGroundCache/query_track_ground extracted from existing car shadow selector; RaceCameraWorld uses that selector and valid no-hit height zero. No NOS correction made yet.

## Implemented sampling correction

Original filter shader contract and native radial vertices confirm unadjusted clip-corner UVs. D3D9 integer pixel centers require a minus-half-pixel conversion from Maxwell gl_FragCoord. All three Switch filter shaders and CPU preview now use it. See docs/SPEED_BLUR_RENDER_RECOVERY.md and tools/speed_blur_render_native.py. This is not a claim of complete hardware image parity.

## Validation correction

The first Release full run failed nfsu_freeride_skyline_retail at tick944: newly enabled free-drive props interacted with the player, while the paired solo RaceRun and direct RaceSession fixtures omitted props. The universal fix initializes track props for every RaceRun, independently of rival/traffic counts. The direct-session comparison now loads the same props on each side and requires a real object impact. First-run gates remain failed, not accepted. A fresh source freeze and required rerun will follow targeted verification.

## Validation scope correction

The second Release run failed two loader ceilings and the existing solo replay pin because unconditional props also changed solver-only benchmark worlds. Live free-drive always loads props. Explicit set_intro_traffic, including an empty selection, now requests the authored prop world in other modes. Existing rival and time-trial paths remain enabled. The paired race test explicitly requests that same world; it still requires a real prop hit. No memory limit or existing replay pin is changed. Failed runs are retained under validation and validation2.

The validation2 Default full-draw subprocess PID23388, started2026-10-23T12:22:56Z and verified by path/parent command, was stopped after the version had already failed required gates and was superseded. Its run is aborted/failed, never accepted. Targeted corrected Release checks passed6/6 including read-only fixture; see root-r3-target-ctest.log. Full validation3 starts from a fresh immutable source snapshot.

## Sealed local r41 2026-10-23T12:51:37.728097+00:00

phase: awaiting-evidence
verified: required Default and Release configure/build/full CTest, retail read-only fixtures, SDK packaging/shader checks and ASCII gates PASS. Command results and hashes: build/free-drive-fidelity-r41/artifacts/BUILD_RECEIPT.json. The checkpoint extension is post-validation documentation only and has its own hash in the receipt.

Original filter vertex witness and camera adapter tests passed. All exposed free courses passed real loader/simulation/draw checks with physical props ownership. Jump camera activation through the real ground adapter is tested. This does not establish hardware appearance parity or original small-object collision solver parity. NOS pixel-origin mismatch is corrected; complete visual acceptance remains open. The existing one-frame pixel readback is enabled so the next hardware run can produce GPU evidence without requiring a subjective video. Its diagnostic frame is marked timing_perturbed=1. Free-drive always loads authored track props. Other live worlds retain existing rival/time-trial setup or request it explicitly through set_intro_traffic, including empty traffic. Solver-only fixtures retain their deliberately empty world. The paired fixture explicitly requests matching props and includes real impacts on both sides.

r40 final completed run statistics (software counters, not sampled frame estimates) are preserved in root-r40-console.log under this task directory. At integration HEAD ea4d472c9c399265370fa10be94736b1121771ad and prior r40 source receipt, 6550 frame-work samples had mean16.646429ms/max19.442135ms and no frames above25ms. GPU samples mean10.699287ms/max12.592552ms. Correction to initial checkpoint: full run statistics were available at the end, beyond the periodic lines first inspected.

No Netloader send, Git publication or index/ref mutation occurred. Independent review remains unperformed. Next: owner-authorized r41 console run for free-drive props, an eligible jump and NOS rendering, retaining the same profiled route.

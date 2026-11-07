# Introductory Underground race: authored visual grid

Assignment: owner selected event 0 and authorized sequential implementation
in this chat. No delegated worker or reviewer. Current slice is the authored
cars and static grid, not a complete opponent race.

Accepted base: 24665d0a03bbae52e5f16c5f1a1d38b62676a23a (boost-r6).
Owned tree: build/day20261008-root-combined.
Branch: codex/draw-distance-original. Local-only checkpoint; no push or merge.

## Implemented

- Generic ordered FrontB preset lookup and paint/vinyl composition retain
  the existing Skyline wrappers. Sponsors and runtime skin alias now follow
  the prepared car type.
- Authenticated event0 descriptor supplies Integra, Eclipse, Celica and Supra
  and their final original grid markers. Each scene owns its mesh and texture
  payload; no repeated 240SX or Skyline substitution.
- Installed lamp position aliases use the original BINNAME helper at
  0x4BBD70 and the constructor override at 0x580DB8..0x580E39 before the
  shared suffix builder. This fixes unresolved custom Eclipse lamp groups.
  Existing in-pack position textures remain selected.
- The player uses all eleven level-one upgrades from the recovered intro
  setup. Opponent adaptive performance is deliberately still unresolved;
  their static reset specs are stock placement inputs, not accepted race tuning.
- nfsu_intro_grid_viewer produces actual chase and elevated PC grid frames,
  refuses unresolved drawable textures and wrong preset/car identity.

## Verification of this slice

Native configure is the existing retail-enabled Release build at build/.
Commands and results:

- cmake --build build --target nfsu_intro_grid_viewer nfsu_freeride_skyline_tests -j 4
  PASS after fixing compiler/link and marker-index errors recorded in earlier logs.
- python -B tools/frontend66_preset_native.py <retail-root> --preset <name>
  PASS for DDAY_INTEGRA, ECLIPSE3, Supra1, Celica1. The port's actual
  resolved records match every ordered slot of each original execution.
  Original helper also supplies installed lamp BINNAME seeds.
- nfsu_intro_grid_viewer <retail-root> <output-prefix>
  PASS: four authored car scenes, final grid IDs and original marker fields,
  no unresolved drawable textures, wrong-car refusal, two fresh PC frames.
- ctest --test-dir build --output-on-failure -R '^nfsu_(freeride_skyline_tests|freeride_skyline_retail|event0_setup_tests|event0_setup_retail)$'
  PASS 6/6 including before/after retail read-only manifests; no skipped test.
- cmake --build build-switch -j 4 --target nfsu_frontend
  PASS with actual Switch SDK. This is library compilation, not a four-car
  Switch runtime check or a new NRO delivery.

Only targeted checks were run under the owner's minimum-tests instruction.
No complete native/Release/Switch matrix or independent review is claimed.

Evidence relative to repository runtime root:
build-artifacts/draw-distance-20261008-root/ug0-pc-grid.log,
ug0-native-<preset>.log, ug0-original-lamp-builder.log,
ug0-native-build-r4.log, ug0-targeted-regression.log, ug0-sdk-compile.log,
ug0-grid.png and ug0-chase.png. Original retail files remain read-only.
The native witness stubs stock initialization as documented in the tool;
its finite preset/disable/sponsor evidence is not original whole-game execution.
PC frames use the existing reference renderer and are not deko3d captures.

## Remaining and next action

The Underground menu entry still reports unavailable. No new NRO has been
sent. The visual grid does not yet act as opponents in RaceSession/RaceGpuScene.
Next connect four stable actor payloads and actual moving rival states into
one scene, then shared car contacts and standings/finish. Original adaptive
rival upgrades and controller/route applicability must be resolved explicitly;
the existing port test driver is not original AI. Preserve accepted free drive.

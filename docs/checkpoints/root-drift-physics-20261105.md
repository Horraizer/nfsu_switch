# Drift physics audit checkpoint

role: worker
agent: Codex root solo
branch: codex/draw-distance-original
head: ea4d472c9c399265370fa10be94736b1121771ad
scope: original PC USA 1.4 drift, with runtime fixes and PC verification
phase: PC validation complete
worktree: build/day20261008-root-combined

The retail speed.exe was authenticated before executing the isolated native
functions. Ghidra exports and executable witnesses live in
build/drift-physics-audit-root. Retail files were read only.

- 4B5DF0 selects the drift mode. 4607A0 chooses the steering assist curve in
  460A80 and the assisted gearbox path in 46DB60. Drift uses the existing
  recovered steering predicate and gearbox/handbrake dispatch.
- 57F2B0 calls 575960 first. It changes mass and alternate yaw by the authored
  weight tier, changes tyre curves and drive split by the drivetrain family,
  and applies a smaller mechanical aero increment. Both cosmetic aero levels
  are cleared. Inertia was built before the mass blend and remains in the
  body setup passed to 584800. The port now preserves this order, including
  the unique weight item.
- 477775 and 477A17 omit the ordinary 0.85 grip and rear stabilizer in drift.
  479C30 also bypasses the ordinary low-speed front-drive torque. 479640
  still consumes gear quality and previous wheel load/slip in drift. The
  port now runs the shared grip path with just the original mode gates.
- 4783C0 prepares the wheel forces, applies 4791F0, then 479350. The latter
  uses heading minus velocity angle, yaw, drivetrain and difficulty. It adds
  torque along the published up axis and stores both rear wheel scales for
  the next force callback. The scalar operations and torque addition now
  use the original runtime binary32 rounding.
- The player score uses the existing 43E540/43E790 state machine, course
  progression and impact adapter. Opponent standings use 43EB00 zone/lap
  awards, with no co-running physical rivals. The selected event is number
  13 on track 1302, matching the Dirt message and second drift course.

Evidence:
- Full 57F2B0 outputs match for the checked mass, inertia, yaw, split,
  aero and performance classification fields for Civic, Miata and Skyline,
  in stock, chassis-only and full-tune configurations with and without the
  unique weight item. The exact upgrade inputs are recorded by the native and
  port matrix probes.
- Authentic 479640 executes with the drift flag and publishes shift and
  transient diagonal grip. Runtime regression checks production wheels.
- Original 479350 matches the port in the expanded binary32 force corpus.
  The original vector tool and tests/recordings/drift-native.txt agree.
- Live PC simulation used the normal race entry, controls and commit path.
  Stock Civic completed all four Event 13 laps on Easy, Medium and Hard,
  with zero resets and banked player points. The run is a port runtime
  check, not a simultaneous original trajectory comparison.
- Required HEAD path/blob ASCII scans, changed-file byte scans, and
  git diff --check passed.
- cmake -S . -B build-release-crest -G Ninja -DNFSU_RETAIL_ROOT=<retail root> -DCMAKE_BUILD_TYPE=Release: passed.
- cmake --build build-release-crest --parallel 8: passed.
- CTEST_PARALLEL_LEVEL=8 ctest --test-dir build-release-crest --output-on-failure: passed; retail manifest unchanged.
- cmake -S . -B build-crest-followup -G Ninja -DNFSU_RETAIL_ROOT=<retail root>: passed.
- cmake --build build-crest-followup --parallel 8: passed.
- CTEST_PARALLEL_LEVEL=8 ctest --test-dir build-crest-followup --output-on-failure: passed; retail manifest unchanged. See default-full-r2.log.

Remaining evidence: a matched-input original drive would validate the whole
trajectory and shared contact/track adapters together. Switch was not used
for this PC audit. Independent review and any Git handoff remain separate
under the owner's existing Git/index/history/remote hold.

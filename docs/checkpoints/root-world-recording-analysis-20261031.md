# Existing event9 recording: analysis and recorder limit repair

## Assignment and holds

Owner instruction: analyze the recording already made; do not require another
drive. Solo work in the root combined worktree. Publication, board, independent
review and console sending remain on hold. No game C++, CMake, Switch source or
sealed r88 NRO is changed by this work. No retail game file is written.

## Preserved source

The source is Documents/NFSU-Recordings/world-20261031T161241Z-8209ab17
and its existing ZIP. The recorder reached its old tape-size ceiling. This is
a partial recording, not a complete race. The accepted prefix has contiguous
calls, balanced accepted enter/leave records and no native ring overflow.
All source manifest hashes were verified; neither source nor ZIP was rewritten.
See build/world-recorder-cap-r1/root-preserved-partial-r1.json and
root-drive-analysis-r1.json for measured counts and timing, instead of copying
pins into this checkpoint. Missing tail data is never reconstructed.

The separate profile3 ownership correction and original event9 entry are in
root-pc-profile3-ownership-20261031.md and its installation/live-read receipts.

## Comparisons actually run

All following paths are relative to build/world-recorder-cap-r1.

- root-target-replay-r1.py compiles root-target-probe-r1.cpp against existing
  production libraries. It compares production ai_driver_target with authenticated
  original45FB30/45E370, supplying recorded human published paths, pose, velocity,
  heading, speed and category. All tested target XY and heading/velocity errors
  agree bit for bit. This is a fresh target calculation, not a seeded driver
  history or a whole-car replay. root-target-replay-r1.json is the result.
- root-rival-goals-r1.py compares production target XY directly with goals stored
  in the recorded post-driver snapshots of all three original rivals. All
  supported cases agree bit for bit. The excluded cases have empty paths before
  the launch. Published point geometry does not change across a compared driver
  tick; path-cache metadata does. A half-forward-speed input negative control
  produces mismatches. root-rival-goals-r1.json records counts, exclusions,
  negative control, source/library/probe hashes, actual roster and upgrade arrays.
- root-motion-analysis-r1.py records native gearbox request-to-commit times,
  actual shift speeds/RPM and one right-button press/release. Lateral movement
  is projected against the last published path before that input. It is not
  a guessed lane width, input latency measurement or claimed end-of-manoeuvre
  timestamp. root-motion-analysis-r1.json preserves exact measurements.
- Original rival speed samples are retained at matched game times, with null
  samples when an actor does not yet exist. Recorded speed is not a Switch FPS
  benchmark. No completed-race result or final placing is invented.

Original short input continues to produce lateral movement after release.
The original request-to-gear-commit interval is distinct from physical-button
latency: the recording does not contain a complete shift-button callback chain.

## What is not proved

This result clears the numeric target calculation on these recorded inputs.
It does not establish full lane/action choice, complete steering history,
engine/gearbox/body evolution, collision parity or end-of-race AI balance.
Human driver snapshots stop before the extended error-history fields, and
drag controller snapshots do not contain the full retained-road state. No
missing state was filled with guessed zeroes to claim deterministic agreement.

The saved chronological input/path/speed data is usable for further bounded
comparisons without asking the owner to repeat this drive. A later complete
controller replay must first extend/authenticate capture coverage of the
missing retained-road and history blocks. No additional drive is requested now.

## Recorder repair and validation

The host recorder's configurable tape ceiling is raised, including GUI status
and --max-gib. ZIP verification now streams members instead of allocating an
entire large member in RAM. The raw tape schema and native agent are unchanged.
Installed recorder and WORLD.md hashes/backups are in root-installed-r1.json;
the already open recorder must be restarted to load the new Python source.

root-validate-r1.py ran the Python unit suite, CLI help and the authenticated
all-kind native/production-host smoke. Exact commands and exit results are
in root-validation-r1.json and its logs. Native host lifecycle, tape/archive
integrity and pairing passed. These are recorder gates, not game acceptance.

The final ASCII/HEAD/index check is root-ascii-final-r1.json. The additional
root-final-integrity-r1.json covers authored automation/path bytes and unchanged
r88 game sources/NRO. No Git/index/ref or publication operation is performed.

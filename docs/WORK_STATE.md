# Work state snapshot

Recorded 2026-10-07. This is a dated recovery index, not live authority.
Read [AGENTS.md](../AGENTS.md) and [WORK_RECOVERY.md](WORK_RECOVERY.md).
Reviewers inspect their pinned diff before narratives, including this file.
Verify remote heads, issue state and ownership before acting. This snapshot
never authorizes a replacement coordinator, additional workers or another batch.

## Product target

The owner requires original startup videos with sound, then original menu
presentation and music, with exactly two available gameplay modes:

- Underground: the complete introductory event0, with original cars,
  visual/performance tuning and decals, real opponents/traffic, shared
  contacts, nitrous on Switch L, original HUD/minimap/audio, start, finish,
  results and return/retry lifecycle.
- Continuous free driving on the first track, with return to the menu.
  This does not authorize an expanded open world.

Other gameplay modes remain unavailable for this milestone. The complete
original game and broader car/track coverage remain later work. See the
[owner two-mode decision](https://git.accad.pro/murzintsevav/nfsu-switch/issues/291#issuecomment-8658),
[roadmap #291](https://git.accad.pro/murzintsevav/nfsu-switch/issues/291),
[event0 integration #292](https://git.accad.pro/murzintsevav/nfsu-switch/issues/292)
and [full coverage #299](https://git.accad.pro/murzintsevav/nfsu-switch/issues/299).
An accepted playable baseline does not imply complete milestone acceptance.

## Accepted accumulated baseline

[Issue328](https://git.accad.pro/murzintsevav/nfsu-switch/issues/328) consolidates
accumulated work in [PR329](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/329).
Accepted source: `9f899db4f6065181e53ea9376dde6914052991c1`.
Previous main base: `83d63d9f1831b413f7020bc4cf799c40c58a87c8`.
Actual merged origin/main: `9f018ac2f449afaee2639fbe5cbd8fa5dd600b9b`.
Merged 2026-10-07 at08:51:14UTC;
[merge/evidence record8840](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/329#issuecomment-8840).
[Final exact-head REVIEW_PASSED8836](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/329#issuecomment-8836)
accepts source9f899db. The coordinator verified freshly fetched refs and
merged-result tree `478eb21cc35ae7845952168c8127623a7bd669f2`, identical
to reviewed source and actual merged main; no additional merge delta.

[Owner acceptance8834](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/329#issuecomment-8834)
records the 2026-10-07 accepted working baseline and explicit authorization to
merge the accumulated work. Ordinary startup reached the menu and live free
driving. Owner reports usable handling, good overall appearance and nearly
good frame rate. Controls/audio/tach improvements are accepted follow-up work,
not blockers for this baseline. This does not establish stable60FPS.

Accepted hardware artifact provenance: NRO source tag `9f899db4f606`,
SHA256 `adea3bab370018ff59de474c437f10adbe9bbe1ed4b0317a6c310c84b24d0782`.
This artifact used ordinary `sdmc:/nfsu` startup, not profile/replay bypass.
No claim of full-intro/re-entry coverage follows from menu/free-drive acceptance.

The independent review preserved the original complete frozen cycle and
reran every affected correction group in default and Release, plus the
current-head Switch and ASCII gates, under the owner's explicit one-cycle
exception. Retail fixtures passed without skips. Exact commands/results
and evidence reuse limits are recorded in verdict8836; the coordinator's
current-main freshness and identical merged-result confirmation are in8840.

Source inclusion/provenance is recorded in
[the integration inventory](checkpoints/integration328-sources.json) and
[the worker recovery checkpoint](checkpoints/integration328-worker.md).
They retain the17 inventoried PR sources, four continuation sources and20
local worker sources; two old PRs are explicitly superseded rather than merged.
The inventory's intermediate current_head and old progress text are historical;
PR329 exact-head handoffs and merge record8840 govern the integrated state.
PR272's append/lookup correction is incorporated through corrected PR286;
PR276's older navigation is superseded by newer recovery/two-mode decisions.
Old branches, measurements and review links stay preserved.

Normal live defaults select recovered1 steering, car shading, glows and wet
road effects. The accumulation includes original intro/menu/music work,
car/Skyline/material fixes, corrected tyre angle/domain handling, direct
full-physics engine-RPM/gear display, minimap/HUD/effects, and bounded
contact/nitrous/actor/physics/audio/prop recovery. Research/source inclusion
is not a claim that every event0 runtime adapter is complete.

## Current ownership and next action

One coordinator remains responsible:
`01a0f5d2-29c4-7fe2-86b4-e58d66adf088`.
Integration worker: `01a110e2-d690-79e2-b02c-5a88348e7319`;
`build/day20261007-integration-worker`, source9f899db preserved immutable.
The same worker prepares only this recovery follow-up in
`build/day20261007-integration-recovery` from merged9f018ac.
Independent integration reviewer: `01a11165-8dc1-78d3-9323-f76276e02036`;
final source-tagged Switch artifact and completed exact-head verdict are
recorded in8836. That reviewer remains assigned to the short recovery-doc check.
The coordinator owns review/merged-result confirmation, merge/issue state
and continuing follow-up. No additional integration rebuild/deploy is authorized here.

[Atlantic #327 / WIP PR330](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/330)
is the retained next assignment, child of #299: exactly one additional original
circuit, DT3 - Atlantic, track1005/event8, three forward laps. It uses explicit
`--event 8`; event1 remains default. No career expansion or unrelated physics.

- Worker: `01a11502-3110-70a0-a163-4da195d45fe6`;
  branch `codex/day20261007-next-track`;
  worktree `build/day20261007-track-worker`.
- Independent reviewer: `01a11522-cce4-7b53-8b6e-a02a171318cb`.
- Published source: `57048c64aa67671e73087a99b1a4c5e9bac7c677`;
  prior base `83d63d9f1831b413f7020bc4cf799c40c58a87c8`.
- Dependency supplied and worker explicitly resumed by the coordinator on
  exact accepted merged main `9f018ac2f449afaee2639fbe5cbd8fa5dd600b9b`;
  see merge record8840. Compatibility integration and gates are now pending.
  [Checkpoint8825](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/330#issuecomment-8825)
  records the earlier wait, preserved source and wake mechanism, not a
  current missing dependency. Do not adopt a moving integration branch.
- Next: integrate only that pinned dependency and necessary Atlantic
  compatibility; execute strengthened shared RaceRun three-lap/replay checks,
  required native/default+Release retail/Switch/ASCII gates, actual later
  raster/road evidence, independent review and owner Switch acceptance.
  Earlier source-head passes and static review do not establish full57048 gates.
  PR330 remains WIP; there is no REVIEW_REQUESTED or final acceptance yet.

Atlantic timing/accounting: first work2026-10-07T06:17:16Z; source commit
2026-10-07T07:13:56Z; passive queue wait began2026-10-07T07:29:07.9557431Z
and was ended by the coordinator's explicit accepted-main resume instruction.
The worker records the actual queue-end timestamp separately; older snapshots
with a null end are historical and do not imply the worker is still waiting.
Record in `build/day20261007-track-worker/build/worker-handoff/track327-worker-queue-timing.json`;
queue/dependency evidence also appears in
[checkpoint8820](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/330#issuecomment-8820)
and checkpoint8825. Queue time stays separate from implementation/tool/test
phase time. Aggregate overlapping active time and direct worker token metadata are
unavailable; do not estimate tokens from text or infer summed elapsed time.
Coordinator accounting record:
`build-artifacts/integration-20261007-root/track327-token-usage.json`.
It contains exposed cumulative token snapshots and separate queue timing;
its complete=false state is unfinished accounting, not a final effort/cost
claim. Do not sum repeated counters or add cached/reasoning subsets again.
Wake: root heartbeat/compact wait_threads plus explicit send_message_to_thread.
The owner is not a routine relay; reuse these same assigned chats.

## Remaining acceptance, separated by purpose

- Controls, sound fidelity and tachometer behavior: owner-accepted subsequent
  improvement/research, including [tach #324](https://git.accad.pro/murzintsevav/nfsu-switch/issues/324)
  and [audio #312](https://git.accad.pro/murzintsevav/nfsu-switch/issues/312).
  Direct engine-RPM/gear export is implemented; further original wheel/engine
  coupling and owner braking/needle behavior are separate fidelity questions.
- Shadows: the race ground shadow remains a planar approximation; see
  [CAR_SHADOW_FIDELITY](CAR_SHADOW_FIDELITY.md). Overall appearance acceptance
  does not prove original shadow projection/fidelity.
- Environment: accepted static race environment approximation is distinct
  from the original live race reflection-cube producer/consumer. Frontend
  cube evidence does not complete that race runtime path; see
  [SWITCH_RACE](SWITCH_RACE.md) and [CAR_SHADING](CAR_SHADING.md).
- Menu exhaust: the remaining frontend exhaust behavior is a distinct
  follow-up in merge record8840, not proof of complete original menu effects.
- Intro/lifetime: source/software playback coverage and ordinary menu entry
  are distinct from complete owner playback, skip/sound/pause behavior,
  menu-return/retry and repeated menu-to-race entry. Full-intro/re-entry/lifetime
  and combined menu-to-race memory acceptance remain unverified. See
  [startup #320](https://git.accad.pro/murzintsevav/nfsu-switch/issues/320)
  and [frontend #66](https://git.accad.pro/murzintsevav/nfsu-switch/issues/66).
- Event0: actor/preset/opponent/traffic/world scheduling, full shared contacts,
  nitrous actor-clock/override/input/engine lifecycle and complete race
  start/finish/results/return/retry remain distinct milestone acceptance.
  Bounded recovered numerical laws and research PRs do not close #292.
- Performance: owner acceptance is not an independently proven stable60FPS
  or repeated-entry memory/lifetime bound. Link exact artifact/scene evidence
  for future measurements; keep test-owned current pins in their tests.

Issue291/292/299/66/320/324/325/312/327/328 were open when this snapshot was
verified. Do not close parent scopes merely because PR329 is merged. The
coordinator records actual remaining acceptance and changes board state only
when each owner's criteria are met.

## Historical recovery links

The October4 snapshot is prior navigation, not today's active worker queue.
Old material/frontend rejections and provider-limit handoffs retain historical
value but do not supersede accepted9f899db or re-block it automatically:
[car review8678](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/319#issuecomment-8678),
[tooling8682](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/319#issuecomment-8682),
[menu8680](https://git.accad.pro/murzintsevav/nfsu-switch/issues/66#issuecomment-8680),
[startup8681](https://git.accad.pro/murzintsevav/nfsu-switch/issues/320#issuecomment-8681),
[scaling8683](https://git.accad.pro/murzintsevav/nfsu-switch/issues/320#issuecomment-8683).
Old standalone PR313/316/317/318/319 and the HUD/minimap/props/handling/tuning
research sources are reconciled by the integration inventory, not an
instruction to restart stale workers or independently merge included sources.

The closed overnight scope remains24 terminal assignments,15 worker
handoffs and9 reviews, not24 accepted gameplay features. Its quota is exhausted.
Keep the [completion checkpoint](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/276#issuecomment-8382)
and [final inventory](https://git.accad.pro/murzintsevav/nfsu-switch/issues/292#issuecomment-8373).
Do not rerun or poll that closed runner. PR276's superseded navigation and
old frontend/car artifact records remain historical evidence, not active blockers.

## Hardware and recovery discipline

Use exact source-tagged artifact and argument records; accepted ordinary
startup differs from profile/replay diagnostics. Any new upload requires the
owner's current netloader readiness. Keep retail/reference files read-only,
private owner recordings local, and artifacts ASCII. Preserve original
worktrees/branches/evidence; clean only owned exact names when authorized.
No secrets, private transcripts or retail assets in tracked artifacts.

The next delivery is Atlantic on the supplied exact accepted-main dependency
through the same resumed worker/reviewer loop.
Further baseline controls/audio/tach fidelity and milestone lifecycle criteria
remain separate follow-ups. This snapshot grants no unrelated merges or deploys.

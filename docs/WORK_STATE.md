# Work state snapshot

Recorded 2026-10-04. This is a dated recovery index, not live authority.
Read [AGENTS.md](../AGENTS.md) and [WORK_RECOVERY.md](WORK_RECOVERY.md).
Reviewers inspect their pinned diff before narratives, including this file.
Verify remote heads, issue state and ownership before acting. This snapshot
never authorizes another coordinator, new workers or another batch.

## Product target

The owner requires original startup videos with sound, then original menu
presentation and music, with exactly two available gameplay modes:

- Underground: the complete introductory event 0, with original cars,
  visual/performance tuning and decals, real opponents/traffic, shared
  contacts, nitrous on Switch L, original HUD/minimap/audio, start, finish,
  results and return/retry lifecycle.
- Continuous free driving on the first track, with return to the menu.
  This does not authorize an expanded open world.

Other gameplay modes remain unavailable for this milestone. The complete
original game remains future work. See the
[owner decision](https://git.accad.pro/murzintsevav/nfsu-switch/issues/292#issuecomment-8658).
The owner rejected partial menu delivery. An unfinished research handoff
is not FINISHED product acceptance.

## Merged baseline and completed overnight scope

Live origin/main was checked at
`ec21749671603a1505a63148feb3806d0a1c5059`, the merge of
[PR #311](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/311).
The runnable baseline is an event 1 player-only diagnostic on track 1003,
with controls, rendering, streaming, static track response, audio and a
speedometer. It is not a complete introductory event 0.

The overnight authorization ended with 24 terminal scopes: 15 worker
deliverables and 9 independent review assignments, not 24 gameplay features.
Its quota remains exhausted. Some research PRs remain open; scope completion
does not mean every PR was merged or every parent issue was completed.
See the [completion checkpoint](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/276#issuecomment-8382)
and [final inventory/test plan](https://git.accad.pro/murzintsevav/nfsu-switch/issues/292#issuecomment-8373).
These records carry artifact identities and historical gate results.
Do not rerun or poll the closed overnight runner.

## Active owner-authorized work

| Scope | Owner and branch | State and next action |
| --- | --- | --- |
| [Cars #315 / PR #319](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/319) | External GLM; `codex/day20261004-glm-cars315` | Owner drove five models at `bc473b9493f8f8cc05ec4ee1a4f50bc686931f1c`. Material defects independently reproduced; changes requested. Correct original runtime bindings, add regression coverage and re-review. |
| [Startup media #320](https://git.accad.pro/murzintsevav/nfsu-switch/issues/320) | Existing Sol worker `intro_media`; `codex/day20261004-intro-media` | Owner confirmed sound and rejected video proportions. Worker pushed scaling correction `ce86b62dd36655a0ca6c0c6b3218bb9d9c15f9a0`; full gates, integration, independent review and hardware retest remain pending. |
| [Menu #66](https://git.accad.pro/murzintsevav/nfsu-switch/issues/66) | Existing Sol worker `menu66`; `codex/day20261004-menu66` | Demo `42f96af6c9abeac09aa69421195e385b19c1893e` supplies intro/menu/free-drive/return but failed original appearance/music acceptance. Worker resumed original layout/fonts/garage/music implementation. No accepted final artifact yet. |

Car hardware findings and reproduction are in
[review 8678](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/319#issuecomment-8678),
which supersedes the earlier passing review's merge eligibility. Checker
materials are not accepted original rendering. Shadow and handling concerns
are recorded; root causes are not established by the video alone. Steering
remains baseline. Capstone was installed for the existing GLM investigation;
see [tooling checkpoint](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/319#issuecomment-8682).

Frontend rejection and resumed ownership are recorded on
[#66](https://git.accad.pro/murzintsevav/nfsu-switch/issues/66#issuecomment-8680)
and [#320](https://git.accad.pro/murzintsevav/nfsu-switch/issues/320#issuecomment-8681).
The movie scaling correction is a
[worker checkpoint](https://git.accad.pro/murzintsevav/nfsu-switch/issues/320#issuecomment-8683),
not an independent PASS. The owner's original Deck menu reference identifies
"Get Low" by Lil Jon & the East Side Boyz. The menu worker reports original
executable selection evidence; new implementation still needs independent
review and hardware acceptance.

These workers retain their own worktrees. Root coordinates and reviews only
changes it did not author. Recurring monitors remain paused; local workers
report directly and GLM uses its existing external handoff channel. This
snapshot does not restart the old batch or grant more workers.

## Other unmerged work and remaining acceptance

- [Audio #312 / PR #313](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/313):
  terminal bounded research at `2667c3fac7f27893a788c6226ba49f748d770b18`;
  independent review pending. Engine/shift/impact/scrape fidelity remains
  open. Research findings do not demonstrate original live mix parity.
- [Controller PR #316](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/316),
  [nitrous PR #317](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/317),
  and [contacts PR #318](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/318)
  remain open. Their earlier local workers stopped at a provider limit;
  worktrees/evidence are preserved. Verify current remote heads and verdicts.
  A gate or verdict for a different head is not current merge authorization.
- [Integration #292](https://git.accad.pro/murzintsevav/nfsu-switch/issues/292)
  remains open: event 0 actors/presets/opponents/traffic, shared contact
  integration and the complete race lifecycle are not accepted.
- [Tuning #250](https://git.accad.pro/murzintsevav/nfsu-switch/issues/250),
  [steering #251](https://git.accad.pro/murzintsevav/nfsu-switch/issues/251),
  [lap HUD #254](https://git.accad.pro/murzintsevav/nfsu-switch/issues/254),
  [traffic/opponents #258](https://git.accad.pro/murzintsevav/nfsu-switch/issues/258),
  [filtering #262](https://git.accad.pro/murzintsevav/nfsu-switch/issues/262),
  [minimap #266](https://git.accad.pro/murzintsevav/nfsu-switch/issues/266),
  and [props #270](https://git.accad.pro/murzintsevav/nfsu-switch/issues/270)
  were verified open. Conditional HUD presentation is not ready.
- The older [roadmap PR #276](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/276)
  remains open. Reconcile its documentation against this snapshot before
  integration; its older prose is not live implementation state.

## Hardware and recovery discipline

The five-car run used the PR #319 artifact. The frontend demo used the
separate menu branch artifact. Neither proves the other branch or is a
merged release. Use the board's exact artifact/argument records.
Requested `--scene-height 540 --gpu-boost on` is distinct from measured
clocks. Frame comparison, limited driving measurements and successful
loading do not establish original fidelity or complete-race acceptance.
Any new upload requires the owner's current netloader readiness.

Keep retail/reference data read-only. Owner photos and recordings stay
private and local; publish only authorized ASCII technical observations.
Preserve worktrees and evidence. No secrets, private transcripts or retail
assets in commits/attachments. Follow ASCII and standalone-interpreter gates.

Next product delivery is the corrected combined intro/menu build after
software checks and independent review, followed by owner hardware
acceptance. This does not close event 0 or authorize unrelated PR merges.

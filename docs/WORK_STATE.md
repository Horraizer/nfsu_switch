# Work state snapshot

Recorded 2026-10-01. This is a recovery index; verify live issue and PR state
before acting. Read [AGENTS.md](../AGENTS.md) and
[WORK_RECOVERY.md](WORK_RECOVERY.md) first. A reviewer reads the diff before
this file. Only the coordinator updates this index; workers checkpoint their
own issues. This snapshot does not authorize a new session to take ownership.

## Current direction and assignment

The owner assigned the current Codex session as coordinator, then prioritized
provider-independent roles and recovery before further game work. This
protocol change is the current assignment, on branch
`codex/portable-agent-protocol`, based on
`765aec8bbab32ca1dd3cf0165db4432201911e52`. It has no gameplay changes.
Validate and independently review the protocol before merging it. The PR and
its checkpoint will record the final commit; this initial snapshot cannot
claim that the new protocol is already reviewed or merged.

## Game work to resume

The owner launched [#252](https://git.accad.pro/murzintsevav/nfsu-switch/issues/252)
to investigate wall/track-object, traffic and opponent collisions separately.
The current port's static track response is insufficient; traffic and opponent
behavior must be measured rather than assigned speculative impulse rules.

Verified research already merged:

- [PR #255](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/255), merge
  `c752c80d34979ecb0888f46556d5e7c62e0c16f7`: body classes and current limits in
  [COLLISION_CLASSES.md](COLLISION_CLASSES.md).
- [PR #260](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/260), merge
  `765aec8bbab32ca1dd3cf0165db4432201911e52`: pair creation, witness updates and
  the BB object data path in [COLLISION_PAIR_TRACE.md](COLLISION_PAIR_TRACE.md).
  Both PRs received independent review. They changed documentation, not the
  running collision solver.

[#256](https://git.accad.pro/murzintsevav/nfsu-switch/issues/256) remains open.
Next technical action: trace the caller and frequency of dispatcher
`0x0041FD76` and its placement relative to vehicle integration, using the
pinned retail executable described in COLLISION_PAIR_TRACE.md. Global pair
order, a named placed object instance and pre/post contact measurements also
remain unresolved. Static tracing can continue while owner recordings are
pending; clips alone cannot establish internal solver order or impulse.

The owner agreed to record original-game examples on Deck: wall/object,
traffic and opponent impacts, with the HUD visible before and after. Those
clips have not yet been received in this snapshot. Their agreement is recorded
on [#252](https://git.accad.pro/murzintsevav/nfsu-switch/issues/252#issuecomment-6129).
See the latest [#252 phase report](https://git.accad.pro/murzintsevav/nfsu-switch/issues/252#issuecomment-6206)
and [#256 remaining criteria](https://git.accad.pro/murzintsevav/nfsu-switch/issues/256#issuecomment-6204).

Dependent implementation tasks are
[#257](https://git.accad.pro/murzintsevav/nfsu-switch/issues/257) (two-car
contacts), [#258](https://git.accad.pro/murzintsevav/nfsu-switch/issues/258)
(traffic/opponent integration), and
[#259](https://git.accad.pro/murzintsevav/nfsu-switch/issues/259) (placed objects).
Check their actual dependencies before dispatch. Parent #252 stays open until
its acceptance criteria are met.

## Other owner priorities

- [#253](https://git.accad.pro/murzintsevav/nfsu-switch/issues/253): finish the
  speedometer against original references, including needle behavior during
  gear changes. Then [#254](https://git.accad.pro/murzintsevav/nfsu-switch/issues/254):
  lap timing display like the original game.
- [#202](https://git.accad.pro/murzintsevav/nfsu-switch/issues/202) and
  [#222](https://git.accad.pro/murzintsevav/nfsu-switch/issues/222): owner reports
  wrong impact and finish sounds (finish resembles nitrous depletion).
  Whether simple races should have a countdown sound is unresolved; the owner
  recalled none on Deck. Treat that as an open question, not a confirmed rule.
- [#250](https://git.accad.pro/murzintsevav/nfsu-switch/issues/250) and
  [#251](https://git.accad.pro/murzintsevav/nfsu-switch/issues/251): tuning and
  steering work already on the board; inspect scope before assignment.

No active worker assignment for further gameplay implementation is asserted
by this snapshot. Read later checkpoints and confirm ownership before taking
over. Missing local screenshots, clips or scratch files must be reported as
missing evidence; prose about them is not a substitute for the originals.

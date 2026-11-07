# Saving and resuming work

This procedure supplements [AGENTS.md](../AGENTS.md). It applies to every
provider and role. It is a manual protocol; no automatic backup, monitoring,
checkpoint writer or takeover lock is installed by this document.

## Durable records

- Git stores source, decisions, reproduction instructions and committed work.
  Push coherent progress to the assigned branch before a long measurement,
  review dispatch, provider switch or end of a working session. An incomplete
  commit is allowed when explicitly marked WIP with its missing checks; it is
  never a release or merge candidate merely because it was pushed.
- The Gitea issue defines scope, acceptance and dependencies. Its checkpoint
  comments record current progress. The PR stores worker handoffs and reviewer
  verdicts tied to exact commits. Later checkpoints supersede earlier progress
  claims, not the underlying evidence or an owner's instruction.
- [WORK_STATE.md](WORK_STATE.md) is a dated, coordinator-maintained recovery
  index of priorities, active issues and evidence links. Refresh it when the
  queue, decisions or blockers materially change. It is not a second board.
- Store evidence needed to reproduce a claim in tracked ASCII documentation or
  in a durable issue/PR attachment, with commit and input identity. A local
  temporary filename alone is not durable evidence. Never commit retail files,
  credentials, private model transcripts or generated binary output.

## Checkpoint boundaries and format

Record a checkpoint on assignment or takeover, after a meaningful finding,
commit, test result, review round or merge, before a long external wait, and
before ending or transferring a session. Do not wait for a final handoff.
For an extended investigation without commits, checkpoint at least every
30 minutes while actively working. The interval bounds manual recordkeeping;
it cannot guarantee zero loss from an abrupt failure.

Use an issue comment, or the PR comment thread once there is a PR. A local
subagent follows the relay rule: it writes the checkpoint to its own scratch
file and signals its coordinator, who posts it verbatim. Reviewer checkpoints
record progress only; they do not replace or add a second verdict in a round.

```text
CHECKPOINT
role: coordinator | worker | reviewer
agent: <provider/model and session identity>
updated_utc: <ISO 8601 UTC>
assignment: <issue and PR links; owner/coordinator dispatch reference>
phase: <investigating | implementing | reviewing | awaiting-evidence | complete>
branch: <remote branch; or none with reason>
head: <full work commit SHA>
base: <full base commit SHA>
worktree: <machine label and ASCII relative locator; or none>
saved: <pushed commit; or local-only changes and exact recovery location>
verified: <commands, results, tested SHA and durable evidence references>
decisions: <confirmed decision and source; distinguish hypotheses>
remaining: <unmet acceptance criteria>
next: <one concrete next action, required inputs and expected result>
blockers: <missing evidence/tool/access; or none>
ownership: <current assignee; active, released or superseded>
```

Use `none` with a reason for fields that do not apply. Record the work SHA
before a checkpoint-only commit; the checkpoint does not need to contain its
own SHA. Failed, skipped and unrun checks stay distinguishable from passes.
Preserve the worker handoff and reviewer verdict formats in AGENTS.md.

If Git or Gitea is unavailable, save the same record in an ASCII Markdown
file under `docs/checkpoints/<task>-<session>.md` in your own worktree. Workers
and coordinators make a local WIP commit when possible; reviewers keep an
untracked report in their own scratch directory and never commit. Record
whether the work has been pushed. If the
only copy is local, say so explicitly; a fresh clone cannot recover it. Once
access returns, push the branch and relay the checkpoint to the board. Inspect
the remote before retrying a write whose outcome is unknown, so a lost reply
does not create duplicate PRs or repeat a merge.

## Resume without chat history

1. Read AGENTS.md and this procedure. Identify your assigned role and scope.
   If no work is assigned, inspect state but do not claim somebody else's task.
2. Inspect `git status --short --branch`, `git worktree list`, and remotes.
   Preserve unfamiliar dirty and untracked files. Fetch the relevant remote
   refs; if offline, record that freshness could not be verified.
3. Resolve the issue, PR, exact head and base. A reviewer reads their diff now,
   before worker checkpoints or WORK_STATE.md. Then read the latest checkpoint,
   handoff, verdict, issue acceptance and linked evidence. Other roles can use
   WORK_STATE.md first to find the active issue.
4. Reconcile records with actual branch heads, merged PR state and current
   ownership. A green result for another SHA does not validate the current
   tree. If records conflict, preserve both, inspect their evidence and record
   the resolution; ask the owner only for an unresolved intent/authority issue.
5. Use your own worktree at a pinned SHA. Recover a dirty predecessor tree only
   after its writer has stopped: preserve a patch and untracked files locally,
   inspect them for secrets/retail data, and explicitly mark them unverified.
   Do not reset, clean, stash, delete or seize that tree to make startup easy.
6. Post a takeover checkpoint before editing. State what is verified, the next
   action and the remaining gap. Continue applicable validation from the saved
   state; repeat only checks invalidated by changes or missing evidence.

## Ownership and interruption

The owner appoints the coordinator; the coordinator dispatches workers and
independent reviewers. Assignment includes role, issue/PR, full starting SHA,
worktree, expected deliverable and return channel. Model identity is metadata.
A role transfer preserves scope and never grants additional authority.

Before replacement, stop the old session where possible and record that it has
released the assignment. In an abrupt outage, the owner or current coordinator
explicitly supersedes the old assignment on the issue, with session identity
and timestamp. A silent process or stale checkpoint alone does not prove that
another writer is gone. If its status is unknown, work in a separate worktree
and branch; do not move its refs. A returning session checks ownership before
any write and hands its saved work to the current assignee.

The board is a record, not a distributed lock. Only one coordinator may manage
the queue at a time. Provider failure can trigger a replacement assignment;
it cannot bypass independent review or missing hardware evidence. Record a
blocked check explicitly even if another provider cannot run it either.

## Cold-start exercise

After changing this procedure, assign an independent session with no authoring
chat history a read-only recovery exercise. Give only its role, the checkout,
the pinned commit and this entry point. For review, give the base SHA too.
It must identify the active task, verified result, unmet criteria, correct next
action, ownership and required evidence from the repository/board alone.

Record its answer and any missing information in the PR. Check both normal
startup and the documented offline limitation: a local-only change cannot be
recovered on another machine until copied or pushed. A paper procedure and a
single exercise do not constitute tested cross-provider failover or backups.

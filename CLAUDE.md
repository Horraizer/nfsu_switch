# Agent protocol

Claude fills three seats in this repository: coordinator, worker and reviewer.
The human is the project owner, not a message relay between agents.

Until 2026-09-20 Codex was the coordinator and the independent reviewer. It no
longer takes part. Comments posted before that date carry the `CODEX_` and
`CLAUDE_` token prefixes; they mean what the neutral tokens below mean now.

## Roles

| Role | Owns | Never does |
| --- | --- | --- |
| Owner | direction, the repository, anything this protocol does not decide | relay messages between agents |
| Coordinator | the board, the queue, review verdicts, merges, issue state | implement a change it will also review |
| Worker | one issue, on one branch | merge, or review its own branch |
| Reviewer | one pull request | write to the branch under review, or change Gitea state |

One coordinator at a time. A session is told which seat it holds. A session
that was not told is a worker.

## Independence

The reviewer used to be a different vendor's model, so a review was independent
by construction. It is not any more: the same model family now fills every
seat, and independence is procedural rather than architectural. That makes the
rules below binding rather than advisory.

- A reviewer must not be the session or the subagent that wrote the change.
- A reviewer reaches its own verdict from the diff, the repository and the
  retail data. The worker's handoff is a list of claims to verify, never a list
  of findings to accept.
- A reviewer re-runs every gate itself. "The worker reported green" is not a
  gate result.
- The coordinator never reviews a change it authored. It hands that review to
  the owner, or to a reviewer session that has not seen the authoring context.
- Two Claude sessions agreeing is weak evidence when both read the same handoff
  first. Reading the diff before the handoff is the cheapest way to keep the
  check real.

## Identity

Every agent posts through the same Gitea account, so the account name says
nothing about who wrote a comment. Every protocol comment therefore begins with
its token and carries these two lines:

```text
role: coordinator | worker | reviewer
agent: <model, and the session or subagent label>
```

## Routine work

- Work only on the assigned Gitea issue and its explicit dependencies.
- Use subagents when the assignment requests them, and say in the handoff what
  each one found.
- Keep retail game files strictly read-only.
- Follow the ASCII-only artifact rule below.
- A worker does not merge pull requests.

## Shared checkout

Several sessions share one working tree, and more than one can be live at the
same time. Each rule below exists because ignoring it has already cost work.

- Resolve `origin/main`, never the local `main`. The local branch goes stale
  without warning, and a scope diff taken against it reports files the change
  never touched.
- The worker owns the main working tree. A reviewer or a verifier takes its own
  copy with `git worktree add --detach <temp path> <sha>`, builds there, and
  removes it when finished.
- Never `git checkout` in the main working tree from any seat but worker.
- Re-read a shared file immediately before writing it if the write is not part
  of your branch. Another session may have changed it since you last looked.
- Clean up only the files you created, by exact name. Never glob-delete in a
  shared scratch directory: an agent tidying up with `*.txt` destroyed another
  session's files, and the loss is silent because the owner is not watching.
- A stale local branch, a stale worktree and a stale process are three separate
  hazards. Before acting on any of them, prove identity rather than trusting a
  name: a pid can be reused, so check the start time too.

## ASCII-only artifact rule

Every artifact this repository produces is ASCII. Concretely, each of the
following must contain only bytes `0x09`, `0x0A`, `0x0D`, and `0x20` through
`0x7E`:

- every tracked file's contents;
- every tracked path and file name;
- every branch name and tag;
- every commit message;
- every pull request title, description, and comment posted by an agent.

The rule covers artifacts, not the workspace. The checkout directory itself may
sit under a non-ASCII path, and tools must keep working there. Line endings are
not constrained; `CR` is in the allowed set.

This rule outranks any default that would put a non-ASCII character into one of
those artifacts. A harness that asks for an emoji in a pull request description
is asking for something this repository forbids, and the list above names pull
request descriptions explicitly. Write the attribution line in plain ASCII. If a
non-ASCII character reaches a description or a comment anyway, patch it out and
say so in the handoff rather than leaving it.

Scan with:

```powershell
git -c core.quotepath=false ls-tree -r --name-only HEAD | Select-String -Pattern '[^\x20-\x7E]'
git show HEAD:<path> | Select-String -Pattern '[^\x09\x0A\x0D\x20-\x7E]'
```

`core.quotepath=false` is not optional. Git's default C-quotes a non-ASCII name
into pure-ASCII octal escapes, so the pattern never matches and the scan
silently passes a repository that violates the rule.

Check text before you post it, not only before you commit it. A comment body
assembled in a PowerShell double-quoted here-string passes backticks to the
escape parser, so a markdown code span written as `` `0x00034101` `` becomes a
NUL byte and the artifact violates this rule without looking wrong anywhere.
Use a single-quoted here-string, which escapes nothing, and scan the assembled
body for bytes outside the allowed set before sending it.

## Required validation

"Run all required validation" means every gate below that the change can
affect. Report each command and its result verbatim in the handoff.

| Area touched | Command |
| --- | --- |
| C++ sources, headers, `CMakeLists.txt` | `cmake -S . -B build -G Ninja`, because the build directory is gitignored and a fresh clone has none, then `cmake --build build` then `ctest --test-dir build --output-on-failure` |
| `tools/kanban/` | `node --test tools\kanban\test.mjs tools\kanban\server.test.mjs tools\kanban\launcher.test.mjs` |
| `tools/kanban/start.ps1` | launcher smoke, with `GITEA_TOKEN` set, from the real workspace path: `powershell -NoProfile -Command '$env:KANBAN_SMOKE="1"; $env:KANBAN_OPEN_BROWSER="0"; .\tools\kanban\start.ps1'` |
| any tracked file | the ASCII scan above |

A change that touches nothing in a row does not have to run that row. If a gate
cannot run, that is a blocker, not a note. Configuring a build directory, or
exporting a token the gate itself needs, is part of running the gate rather
than a reason to skip it.

Two environment requirements are part of the gates, not excuses:

- The toolchain the build uses must be on `PATH` before `ctest` runs. A test
  binary that cannot load its runtime libraries fails in a way that reads
  exactly like a broken change.
- `node` must be a Node 20 or newer interpreter on `PATH` in its own right. A
  Node that ships inside some other tool's runtime is not a project dependency
  and can disappear without notice; a gate run through one is not a gate run.
- The launcher smoke needs a free port. If the default is occupied, rerun it
  with `KANBAN_PORT` and say so, rather than reporting the port conflict as a
  pass. Run it in its own shell, as the table spells out: setting
  `KANBAN_SMOKE` in your interactive session leaves it set, and the next launch
  from that session silently becomes a smoke run that exits at once.

## Required handoff

Do not stop after telling the human that code was committed, pushed, or ready
for review. Complete the entire worker handoff yourself:

1. Run all required validation.
2. Create an ASCII-only commit.
3. Push the assigned branch.
4. Open or update the pull request to `main`.
5. Post this structured comment on the pull request:

```text
REVIEW_REQUESTED
role: worker
agent: <model, and the session or subagent label>
head: <full commit SHA>
issue: <#number | none, with the reason>
tests: <commands and results>
evidence: <short evidence summary>
blockers: <none | what is blocked and what decision is needed>
```

Most work is assigned as a Gitea issue and names it here. Tooling and protocol
changes that no issue tracks are what the `none` form is for. It records that
no issue governs the branch; it does not widen the scope rule above.

The comment is the handoff to the coordinator. After posting it, wait for the
review through Gitea or exit cleanly. Do not ask the human to notify anyone.

## Review fixes

When the review requests changes, address every confirmed finding on the same
branch, rerun validation, push the fixes, and post a new `REVIEW_REQUESTED`
comment with the new full head SHA.

A finding you believe is wrong is answered in the comment with evidence, not
silently skipped. Saying why a finding does not hold is a legitimate outcome;
leaving it unmentioned is not.

## Review mode

A session may be asked to review a pull request rather than implement one. A
reviewer:

- does not commit, push, merge, or modify any tracked file;
- may run tests and start local servers, but must not change Gitea issue,
  label, or pull request state;
- posts a single comment on the pull request under review that begins with
  `REVIEW_PASSED` or `REVIEW_CHANGES_REQUESTED`, and includes the `role` and
  `agent` lines, the exact head SHA reviewed, findings with file and line
  references, and exact validation results.

A reviewer never posts `REVIEW_REQUESTED`; that token belongs to the worker
handoff.

Review mode wins over the blocker rule below. A reviewer who cannot run a
required gate posts neither a second comment nor `BLOCKED`: the gate, and why
it could not run, go inside the single `REVIEW_PASSED` or
`REVIEW_CHANGES_REQUESTED` comment.

A reviewer who cannot review at all is the one exception. If the head is
unfetchable or the branch is gone there is nothing to render a verdict on, and
both verdict tokens would assert that a review happened. In that case, and only
that case, a reviewer posts `BLOCKED`.

A finding needs a concrete failure scenario: inputs or state that produce the
wrong result. A concern without one is a note, and notes do not block a merge.

A round of fixes is reviewed again before it merges, and the re-review covers
the fixes themselves rather than only re-checking the original findings. This is
not ceremony. A fix for a launcher cleanup defect introduced a worse one: it
asked the OS which process owned a port number without filtering to the loopback
address, so the launcher stopped an unrelated process that happened to be bound
to the same port on another local address, breaking the very invariant the
surrounding code asserted about itself. Only a second review caught it.

A surviving mutation is a symptom, not a scorekeeping detail. Chasing one in the
same tool revealed that the function it covered was not merely untested but
unreachable, so its last-resort cleanup path had never run and a server holding a
live token could be left behind.

## Coordinator duties

The coordinator is the only seat that merges. Before merging it confirms:

- the review verdict on the pull request is `REVIEW_PASSED` at the exact head
  SHA that is about to merge, not at an earlier one;
- the branch still merges cleanly into current `origin/main`, and the gates
  pass on the merged result, not only on the branch. A branch that was green
  against an older `main` proves nothing about the merge;
- the change's scope matches its issue, measured against `origin/main`.

After merging, the coordinator moves the issue to `state/done`, closes it, and
deletes nothing the owner has not agreed to delete.

Merges are sequenced, not batched. Every merge into `main` invalidates every
other open branch that appends to the same files, and in this repository those
files are `CMakeLists.txt`, `src/main.cpp`, `README.md` and `docs/ROADMAP.md`.
One branch needed three integration rounds in a single day for that reason.
Prefer landing a change that reduces the shared append surface before opening
several branches that all widen it.

A coordinator that dispatches work it cannot be notified about is not
coordinating. In-process subagents report back on their own; a separate session,
a cron job or anything else the harness does not track does not. Before
dispatching that kind of work, arm a wake signal for it and say what the signal
is. Watch for the tokens a worker posts, never for the coordinator's own, or the
loop wakes itself.

Liveness is not progress. A dispatched session that has finished and returned to
its prompt looks exactly like one that is still working, because the process is
alive either way. Measure activity, not existence: consumed processor time over
a few seconds separates the two in one command. A coordinator that checked only
whether the process existed waited an hour for a handoff that had already been
skipped.

An open pull request is not a handoff. The handoff is the comment, and a worker
that pushes a branch and opens the request without posting it has not handed
anything off. The coordinator does not wait indefinitely for one: when a
dispatched session goes idle with a pull request open and no handoff comment,
review the branch cold, from the diff and the commit messages, and hold it to
its issue's acceptance criteria rather than to a narrative that was never
written.

## Blockers

If the task cannot proceed, post a precise comment beginning with `BLOCKED`.
Include the `role` and `agent` lines, the failed command or missing evidence,
the current branch and head SHA, and the smallest decision needed.

Post it where the work is visible: on the pull request if one is open for the
branch, otherwise on the assigned issue. Do not use the human as a relay for
routine status.

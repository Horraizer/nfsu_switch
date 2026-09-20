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
| Reviewer | one pull request | write to the branch under review, or change issue, label or pull request state |

One coordinator at a time. A session is told which seat it holds. A session
that was not told is a worker.

## Independence

The reviewer used to be a different vendor's model. That was never independence
by construction: any reviewer reading the same handoff, description and
repository conventions inherits the worker's framing of what the change is for
and which tests matter, which is most of the correlated-failure surface. What a
different vendor bought was decorrelated training priors - different blind
spots on language semantics and API misuse - real, but far narrower than it
sounds. One model family now fills every seat, so independence here is entirely
procedural, and the rules below bind.

- A reviewer must not be the session or the subagent that wrote the change.
- A reviewer reaches its own verdict from the diff, the repository and the
  retail data. The worker's handoff is a list of claims to verify, never a list
  of findings to accept.
- A reviewer re-runs every gate itself. "The worker reported green" is not a
  gate result.
- The coordinator never reviews a change it authored. It hands that review to
  the owner, or to a reviewer session that has not seen the authoring context.
- Two Claude sessions agreeing is weak evidence when both read the same handoff
  first. The operative variable is shared priming, not shared weights: two
  sessions from different vendors that read it first correlate the same way.
- Read the diff before you read the handoff. Nothing in a finished review
  comment reveals whether that happened, so the verdict states it on the
  `diff-first` line required under Review mode.

## Identity

Every agent posts through the same Gitea account, so the account name identifies
nobody. Every protocol comment begins with its token and carries two lines:

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

Several sessions share one repository, and more than one is routinely live.

- Resolve `origin/main`, never the local `main`. A fetch updates the
  remote-tracking ref and leaves the local branch where it was, so the local
  one goes stale without warning.
- Measure a branch's scope with a three-dot diff. `git diff A..B` reports every
  file that differs between the two tips, so any ref that has moved past the
  merge base drags in files the branch never touched. `origin/main` moves too:
  while this document was under review the two-dot form listed 17 files, then
  23 an hour later as `origin/main` moved again, against the 1 file the branch
  had actually changed throughout. Comparing against a fresher ref does not fix
  a two-dot diff; only the three-dot form, which diffs against the merge base,
  does.
- The main working tree belongs to whichever session's branch is checked out in
  it, and to no seat by default. Any other session, in any seat, takes its own
  copy with `git worktree add --detach <temp path> <sha>`, works there, and
  removes it when finished. A detached worktree still completes the worker
  handoff: commit there and push with an explicit refspec,
  `git push origin HEAD:<branch>`. Do not check the branch out to get a push
  target - two worktrees cannot hold the same branch, so that is the one thing
  the detached copy exists to avoid.
- Never `git checkout` in the main working tree unless your own branch is the
  one already checked out there.
- Re-read a shared file immediately before writing it if the write is not part
  of your branch. Another session may have changed it since you last looked.
- Clean up only the files you created, by exact name. Never glob-delete in a
  shared scratch directory.
- A stale local branch, a stale worktree and a stale process are three separate
  hazards. Prove identity before acting on any of them: a pid can be reused, so
  check the start time too.

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
git cat-file blob HEAD:<path> | Select-String -Pattern '[^\x09\x0A\x0D\x20-\x7E]'
```

`core.quotepath=false` is not optional. Git's default C-quotes a non-ASCII name
into pure-ASCII octal escapes, so the pattern never matches and the scan
silently passes a repository that violates the rule.

Scan blob bytes directly. Redirecting a blob to a temp file through PowerShell
writes it back with a UTF-8 byte order mark that was never in the blob, and
what happens next depends on the scanner: a byte-level scan reports a
three-byte violation it created itself, while `Select-String` consumes the mark
as an encoding declaration and reports nothing at all. Neither answer describes
the repository. Pipe `git cat-file blob` into the pattern and the mark never
exists.

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
| `tools/kanban/start.ps1` | launcher smoke, with `GITEA_TOKEN` set, from the real workspace path: `powershell -NoProfile -Command '$env:KANBAN_SMOKE="1"; .\tools\kanban\start.ps1'` |
| any tracked file | the ASCII scan above |

A change that touches nothing in a row does not have to run that row. A gate
that cannot run is a blocker, not a note. Configuring a build directory, or
exporting a token the gate itself needs, is part of running the gate.

Three environment requirements are part of the gates, not excuses:

- The toolchain the build uses must be on `PATH` before `ctest` runs; a test
  binary that cannot load its libraries fails exactly like a broken change.
- `node` must be a Node 20 or newer interpreter on `PATH` in its own right. A
  gate run through a Node that ships inside another tool is not a gate run.
- The launcher smoke needs a free port. If the default is occupied, rerun with
  `KANBAN_PORT` and say so rather than reporting the conflict as a pass. Run it
  in its own shell: `KANBAN_SMOKE` set in your interactive session stays set,
  and the next launch from that session silently becomes a smoke run.

A differential comparison fails in a way the rows above do not: neither side
produces a real answer and the comparison reports agreement. A binary that
cannot load its libraries exits with no output, so every invocation matches
every other; a run that refuses to overwrite leftover artifacts leaves the
previous run's files on disk, so the hashes match. Asserting non-empty output
catches neither, because an error line is not empty. Delete prior outputs by
exact name first, then confirm that both sides wrote this run's artifact and
that neither printed an error line.

## Required handoff

Complete the entire worker handoff yourself. Do not stop at committed, pushed,
or ready for review:

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

Most work is assigned as a Gitea issue and names it here. The `none` form is
for tooling and protocol changes no issue tracks; it records that no issue
governs the branch, and does not widen the scope rule above.

The comment is the handoff to the coordinator. After posting it, wait for the
review through Gitea or exit cleanly. Do not ask the human to notify anyone.

## Review fixes

When the review requests changes, address every confirmed finding on the same
branch, rerun validation, push the fixes, and post a new `REVIEW_REQUESTED`
comment with the new full head SHA.

A finding you believe is wrong is answered with evidence in the comment, not
silently skipped. Saying why it does not hold is legitimate; silence is not.

## Review mode

A session may be asked to review a pull request rather than implement one. A
reviewer:

- does not commit, push, merge, or modify any tracked file;
- may run tests and start local servers, but must not change Gitea issue,
  label, or pull request state;
- posts a single comment on the pull request under review that begins with
  `REVIEW_PASSED` or `REVIEW_CHANGES_REQUESTED`, and includes the `role` and
  `agent` lines, a `diff-first: yes | no, with the reason` line, the exact head
  SHA reviewed, findings with file and line references, and exact validation
  results.

A reviewer never posts `REVIEW_REQUESTED`; that token belongs to the worker
handoff.

Review mode wins over the blocker rule below. A reviewer who cannot run a
required gate posts neither a second comment nor `BLOCKED`: the gate, and why
it could not run, go inside the single verdict comment. The one exception is a
reviewer that cannot review at all: an unfetchable head or a deleted branch
leaves nothing to render a verdict on, and both verdict tokens would assert
that a review happened. In that case, and only that case, it posts `BLOCKED`.

A finding needs a concrete failure scenario: inputs or state that produce the
wrong result. A concern without one is a note, and notes do not block a merge.

A round of fixes is reviewed again before it merges, and the re-review covers
the fixes themselves rather than only re-checking the original findings.

## Coordinator duties

The coordinator is the only seat that merges. Before merging it confirms:

- the review verdict on the pull request is `REVIEW_PASSED` at the exact head
  SHA that is about to merge, not at an earlier one;
- the branch still merges cleanly into current `origin/main`, and the gates
  pass on the merged result, not only on the branch. Test that in the
  coordinator's own detached worktree at `origin/main` with the head under
  review merged into it, and remove the worktree afterwards;
- the change's scope matches its issue, measured with
  `git diff --name-only origin/main...<head>`. The three-dot form is the
  requirement, not the ref: it diffs against the merge base, so the answer
  stays correct however far `origin/main` has moved since the branch started.

After merging, the coordinator moves the issue to `state/done`, closes it, and
deletes nothing the owner has not agreed to delete.

Merges are sequenced, not batched. Every merge into `main` invalidates every
other open branch that appends to the same files. The contended set drifts, so
measure it rather than trusting a list: count how often each path appears in
the first-parent diff of each pull request merge on `origin/main`. On
2026-09-20 that gave `README.md` at 17, `src/main.cpp` at 12, `CMakeLists.txt`,
`docs/CAR_SCENE.md`, `docs/FORMATS.md` and `docs/ROADMAP.md` tied at 11, and
`docs/RENDERER.md` at 10.

A coordinator that dispatches work it cannot be notified about is not
coordinating. In-process subagents report back on their own; a separate session,
a cron job or anything else the harness does not track does not. Before
dispatching that kind of work, arm a wake signal for it and say what the signal
is. Watch for the tokens a worker posts, never for the coordinator's own, or the
loop wakes itself.

Liveness is not progress: a session sitting at its prompt looks exactly like one
still working. Measure activity, not existence - read
`(Get-Process -Id <pid>).TotalProcessorTime` twice a few seconds apart and treat
an unchanged value as idle.

An open pull request is not a handoff; the handoff is the comment. When a
dispatched session goes idle with a pull request open and no handoff comment,
do not wait: review the branch cold, from the diff and the commit messages,
against its issue's acceptance criteria rather than a narrative that was never
written.

## Blockers

If the task cannot proceed, post a precise comment beginning with `BLOCKED`.
Include the `role` and `agent` lines, the failed command or missing evidence,
the current branch and head SHA, and the smallest decision needed.

Post it where the work is visible: on the pull request if one is open for the
branch, otherwise on the assigned issue. Do not use the human as a relay for
routine status.

## What these rules actually cover

Four failures shaped this document; four equal-weight rules would imply it
closed four. It closed about one. The three-dot scope diff comes closest to a
mechanical closure, because it names a command form whose output cannot contain
a file the branch did not change. It reached that only on the second attempt:
the `origin/main` rule it replaced closed nothing, since the same wrong answer
was still reachable while obeying it, and this document's own review reproduced
it at 17 files against 1. Re-review only codifies a loop the previous protocol
already implied, and that loop had already caught the launcher fix that stopped
an unowned process. Silent truncation has no rule of its own and leans on the
same general reviewer duty that missed it the first time. The surviving mutant
has no rule at all, and no gate here runs mutation testing. Weigh these rules by
what they mechanically prevent.

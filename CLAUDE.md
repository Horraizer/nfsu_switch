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

The reviewer used to be a different vendor's model. One model family now fills
every seat, so independence here is entirely procedural, and the rules below
bind.

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

- Resolve remote-tracking refs, never local branches. A fetch updates
  `origin/<branch>` and leaves the local branch where it was, so the local one
  goes stale without warning. `main` is not a special case: a session that
  commits and pushes from a detached worktree, as the bullet below requires,
  never moves its own local branch ref either.
- Measure a branch's scope with `git diff --name-only origin/main...<head>`.
  Both halves are load-bearing, and neither one alone closes the hazard. A
  two-dot diff reports every file that differs between the two tips, so a ref
  that has moved past the merge base drags in files the branch never touched.
  The three-dot form is not immune, because it diffs against
  `merge-base(LEFT, RIGHT)`: a left-hand ref that predates the branch point
  becomes the merge base itself, and the three-dot answer collapses onto the
  two-dot one. Measured on this document's own branch, head `5867238`, which
  changed 1 file, with local `main` at `15ef449` and `origin/main` at
  `7cb6bd0`, 42 commits ahead of it: `main..5867238` reported 56 files,
  `main...5867238` reported 56, and `origin/main..5867238` reported 23. Only
  `origin/main...5867238` reported 1. Fetch first, then use the three-dot
  form against the remote-tracking ref.
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

Scan with, in Bash, from the repository root:

```bash
git -c core.quotepath=false ls-tree -r --name-only HEAD | LC_ALL=C grep -nP '[^\x20-\x7E]'
LC_ALL=C git grep -n -P '[^\x09\x0A\x0D\x20-\x7E]' HEAD -- .
```

On a clean tree both print nothing and exit 1. The second reads every tracked
blob in the commit, so it replaces any per-path scan; do not add `-I`, which
silently skips a tracked binary file, and a tracked binary file is a violation
of this rule rather than an exemption from it.

`core.quotepath=false` is not optional. Git's default C-quotes a non-ASCII name
into pure-ASCII octal escapes, so the pattern never matches and the scan
silently passes a repository that violates the rule.

Run the scan in Bash, not PowerShell. PowerShell decodes a native command's
output before any pattern sees it, and that decoder consumes a leading UTF-8
byte order mark as an encoding declaration rather than passing it through. A
blob of `EF BB BF 68 69 0A` piped into `Select-String` arrives as the two
characters `hi` and reports no match, while a mid-file `D0 9F` in the same
pipeline is reported correctly. A PowerShell scan is therefore blind to
precisely the bytes this paragraph is about, and a run that passes proves
nothing unless the blob it measured actually carried a mark. `grep` under Bash
sees the bytes and reports both. Do not redirect a blob to a temp file through
PowerShell either: it is written back with a byte order mark that was never in
the blob, so a byte-level scan then reports a three-byte violation it created
itself. Read the blob from `git`, scan it in Bash, and the mark is neither
invented nor swallowed.

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
previous run's files on disk, so the hashes match. Asserting non-empty console
output catches the first and misses the second, because the refusal prints an
error line and an error line is not empty. Delete prior outputs by
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
  `git diff --name-only origin/main...<head>` after a fetch. The three-dot
  form and the freshly fetched ref are both required; Shared checkout above
  measures what each one gets wrong on its own. Prefer this over resolving the
  merge base into a variable and feeding it to a two-dot diff. Both give the
  same answer, but this is one command with no intermediate value to capture,
  quote or let go stale, so it reads and behaves identically in every shell and
  harness.

After merging, the coordinator moves the issue to `state/done`, closes it, and
deletes nothing the owner has not agreed to delete.

Merges are sequenced, not batched. Every merge into `main` invalidates every
other open branch that appends to the same files. The contended set drifts, so
measure it rather than trusting a list: count how often each path appears in
the first-parent diff of each pull request merge on `origin/main`. No list is
printed here, because the one this document originally carried had already
gone stale, in both its counts and its ordering, by the time the document was
reviewed on the same day.

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
closed four. It has mechanically closed none of them, and earlier drafts of
this section claimed otherwise twice.

The scope diff came closest, and it took three rounds of review to state
correctly: written from impression, then corrected to name the wrong
comparison, then over-corrected into a claim that the three-dot form alone
cannot report a file the branch did not change. A stale left-hand ref
disproves that in one command, which is what Shared checkout now records. What
survives is a two-part discipline that still depends on the operator fetching
first and reading the right ref - not a command form that is safe however it
is invoked. The ASCII gate has the same shape: the scan this document
prescribed for two rounds was blind to a leading byte order mark, so the rule
was sound while the gate that enforced it was not, and a passing run proved
nothing. Re-review only codifies a loop the previous protocol already implied,
and that loop had already caught the launcher fix that stopped an unowned
process. Silent truncation has no rule of its own and leans on the same
general reviewer duty that missed it the first time. The surviving mutant has
no rule at all, and no gate here runs mutation testing.

Weigh these rules by what they mechanically prevent, and treat a rule that
names a command as a claim to reproduce rather than a closure to trust.

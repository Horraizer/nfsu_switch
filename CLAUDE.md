# Agent protocol

Claude fills three seats in this repository: coordinator, worker and reviewer.
GPT fills the worker seat, and the reviewer seat for work Claude wrote, when
the owner assigns it an issue or a pull request. The human is the project
owner, not a message relay between agents.

Until 2026-09-20 Codex was the coordinator and the independent reviewer, and
from then until 2026-09-25 it took no part. Since 2026-09-25 the owner runs
OpenAI's GPT (GPT-6 Astra, through Codex on the owner's machine) as a worker
and reviewer again; Claude stays the coordinator. The coordinator recommends
which issues and pull requests go to GPT and labels them `agent/codex`; the
owner assigns them. Comments posted before 2026-09-20 carry the `CODEX_` and
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

Work GPT writes is reviewed by Claude, and GPT may review work Claude wrote:
where that pairing is available, use it. It relaxes nothing below. Shared
priming, not shared weights, is what correlates two reviews, so the rules
below bind whichever vendor fills which seat.

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
  each one found. One that mutates or measures a tree needs its own worktree;
  see Shared checkout.
- Keep retail game files strictly read-only.
- Follow the ASCII-only artifact rule below.
- A worker does not merge pull requests.

## Subagents and relays

- A local subagent never posts to the board. It writes its handoff or verdict
  to a file in its scratch directory, and the coordinator posts that text
  verbatim with one trailing line saying that it relays, and for whom. The
  `role` and `agent` lines inside stay the subagent's own.
- A subagent told to stay out of the main checkout has still written into it.
  After every subagent returns, run `git status --porcelain` in the main
  checkout as well as in the subagent's worktree.
- Choose the model by the job. A cheaper model runs a measurement someone else
  wrote and pinned; review verdicts, and any work whose product is judgement,
  stay with the strongest model available.

## Shared checkout

Several sessions share one repository, and more than one is routinely live. A
subagent is a second writer whose writes its dispatcher does not see until they
have already landed, so these rules bind it too.

- Resolve remote-tracking refs, not local branches, for any work that is not
  your own. A fetch updates `origin/<branch>` and leaves the local branch where
  it was, so the local one goes stale without warning. The one exception is your
  own branch between your commit and your push, where your local ref is the
  fresher of the two. `main` is not an exception: a session that commits and
  pushes from a detached worktree, as the worktree bullet below requires, never
  moves its own local branch ref either.
- Measure a branch's scope with `git diff --name-only origin/main...<head>`.
  Both halves are load-bearing, and neither one alone closes the hazard. A
  two-dot diff reports every file that differs between the two tips, so a ref
  that has moved past the merge base drags in files the branch never touched.
  The three-dot form is not immune, because it diffs against
  `merge-base(LEFT, RIGHT)`: a left-hand ref that predates the branch point
  becomes the merge base itself, and the three-dot answer collapses onto the
  two-dot one. Measured on this document's own branch, head `5867238`, which
  changed 1 file, with local `main` at `15ef449` and `origin/main` then at
  `7cb6bd0`, 42 commits ahead of it: `15ef449..5867238` reported 56 files,
  `15ef449...5867238` reported 56, and `7cb6bd0..5867238` reported 23. Only
  `7cb6bd0...5867238` reported 1. Both sides are pinned to SHAs so those four
  numbers still reproduce; spelled against the live `origin/main` they drift as
  it moves, which is the same staleness the bullet is about. Fetch first, then
  use the three-dot form against the remote-tracking ref.
- The main working tree belongs to whichever session's branch is checked out in
  it, and to no seat by default. Any other session, in any seat, takes its own
  copy with `git worktree add --detach <temp path> <sha>`, works there, and
  removes it when finished. A detached worktree still completes the worker
  handoff: commit there and push with a fully qualified refspec,
  `git push origin HEAD:refs/heads/<branch>`. The short `HEAD:<branch>` form
  fails from a detached HEAD the first time, because git will not guess a
  namespace for a branch the remote does not have yet. Do not check the branch
  out to get a push target - two worktrees cannot hold the same branch, so that
  is the one thing the detached copy exists to avoid.
- A subagent asked to mutate, build, measure or revert works only in a detached
  worktree of its own, which the dispatcher creates, names in the assignment
  with the SHA to work from, and removes once the subagent has reported. The
  dispatcher does not write to that worktree, and does not move any head a
  subagent is measuring: the pinned SHA is what leaves the dispatcher free to
  keep working, and a subagent pointed at a branch has to be told before the
  branch moves.
  Both directions have already cost work. On issue #38 an adversarial subagent
  aimed at the worker's own tree ran `git checkout --` during a mutation pass,
  reverted two uncommitted files, overwrote a scratchpad script, and produced
  no report. On issue #39 it ran the other way: the worker amended one commit
  three times inside eight minutes - `8d8643a`, `ad0d944`, `7b7634f` - while
  its subagent measured the branch, and the report had to be reconciled against
  a head that had moved under it. A dispatcher can stop its own subagent -
  `TaskStop` ends a backgrounded one by name or id - but not one another
  session dispatched, and no stop recalls a write that has already landed.
- A finished subagent looks exactly like a working one, in both directions.
  Apply the liveness test under Coordinator duties to the subagent's own
  output, never to a directory above it, whose mtime moves with your own
  activity. This branch's own adversarial pass showed an empty output file and
  untouched artifacts across two samples, then delivered a full report four
  minutes later. An idle sample is not a finished subagent: the report is the
  only signal that it is done, and it arrives on its own. A pass that reported
  nothing is recorded as not performed: an unread transcript is not assurance.
- Never `git checkout` in the main working tree unless your own branch is the
  one already checked out there.
- Re-read a shared file immediately before writing it if the write is not part
  of your branch. Another session may have changed it since you last looked.
- Prefix every file you write into a shared scratch directory with the issue or
  branch it belongs to and with something that identifies you: a subagent and
  its dispatcher share the issue. A generic name has already cost work twice in
  one day. On issue #87 a worker's helper script was overwritten by another
  session's file of the same name, and the rerun a round later got the
  replacement, which pointed at a different worktree; it failed on that path
  rather than running, so nothing false was concluded, and that was luck - a
  script pointing somewhere valid would have reported a clean mutation sweep
  over a tree the session was not reviewing. The worker on issue #40 lost a
  mutation script to another session mid-run.
- Two rounds of one session reach for the same name, and nothing reserves it, so
  read a scratch file back before you rerun it and confirm the paths inside it
  are still your own: a stale file that reads plausibly is indistinguishable
  from the one you left.
- Clean up only the files you created, by exact name. Never glob-delete in a
  shared scratch directory. A worktree you did not create is not yours to
  remove either, even when it looks abandoned: leave it, and name it in the
  handoff. `git worktree list` marks a dead entry `prunable` without touching
  it. `git worktree prune` is forbidden outright - its usage line takes no path
  argument, so it cannot be aimed at your own worktree, and it drops any
  unlocked registered worktree whose directory git cannot see at that moment,
  with no expiry delay. Reproduced in a throwaway repository: renaming a
  worktree's directory away made `git worktree prune` deregister it on the
  spot, and with the directory put back, `git status` inside it failed with
  `fatal: not a git repository: (NULL)`. Only `git worktree lock` stops it,
  nothing here is locked, and the worktrees here sit on temp paths that come
  and go.
- A stale local branch and a stale process are two separate hazards. Prove
  identity before acting on either: a pid can be reused, so check the start
  time too.

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
regular-file blob in the commit, so it replaces any per-path scan; do not add
`-I`, which silently skips a tracked binary file, and a tracked binary file is
a violation of this rule rather than an exemption from it.

Mode `120000` and `160000` entries - symlinks and submodules - are forbidden
here, and that prohibition is what makes the second command's coverage
complete. `git grep` over a tree does not search a `120000` blob even when the
pathspec names it directly, yet a symlink target is blob content like any
other. Nothing enforces the prohibition automatically; a violation shows in the
mode column of `git ls-tree -r HEAD`.

`core.quotepath=false` is not optional. Git's default C-quotes a non-ASCII name
into pure-ASCII octal escapes, so the pattern never matches and the scan
silently passes a repository that violates the rule.

Run the scan in Bash, not PowerShell. PowerShell decodes a native command's
output before any pattern sees it, and that decoder consumes a leading UTF-8
byte order mark as an encoding declaration: a blob of `EF BB BF 68 69 0A` piped
into `Select-String` arrives as the two characters `hi` and reports no match, so
a ported scan cannot see a leading mark at all. Do not stage a blob through a
PowerShell temp file either - it is written back with a mark that was never in
the blob, so a byte-level scan then reports a three-byte violation it created
itself.

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
| C++ sources, headers, `CMakeLists.txt` | `cmake -S . -B build -G Ninja -DNFSU_RETAIL_ROOT=<retail installation>`, because the build directory is gitignored and a fresh clone has none, then `cmake --build build` then `ctest --test-dir build --output-on-failure`, whose summary must show no skipped retail test; then once in Release, the same three commands in `build-release` with `-DCMAKE_BUILD_TYPE=Release` added, where the plain `ctest` also runs the `retail-full` label |
| `tools/kanban/` | `node --test tools\kanban\test.mjs tools\kanban\server.test.mjs tools\kanban\launcher.test.mjs` |
| `tools/kanban/start.ps1` | launcher smoke, with `GITEA_TOKEN` set, from the real workspace path: `powershell -NoProfile -Command '$env:KANBAN_SMOKE="1"; .\tools\kanban\start.ps1'` |
| any tracked file | the ASCII scan above |

A change that touches nothing in a row does not have to run that row. A gate
that cannot run is a blocker, not a note. Configuring a build directory, or
exporting a token the gate itself needs, is part of running the gate. A second
configuration - a Release build beside the default one - goes in a sibling
directory whose name starts with `build`. `.gitignore` covers those at the
repository root, along with `out` and `cmake-build-*`; a name outside that set
is not ignored, and `git add -A` then stages the whole build tree, binaries
included.

Three environment requirements are part of the gates, not excuses:

- The toolchain the build uses must be on `PATH` before `ctest` runs; a test
  binary that cannot load its libraries fails exactly like a broken change.
- `node` must be a Node 20 or newer interpreter on `PATH` in its own right. A
  gate run through a Node that ships inside another tool is not a gate run.
- The launcher smoke needs a free port. If the default is occupied, rerun with
  `KANBAN_PORT` and say so rather than reporting the conflict as a pass. Run it
  in its own shell: `KANBAN_SMOKE` set in your interactive session stays set,
  and the next launch from that session silently becomes a smoke run.

The retail checks are part of the C++ gate (issue #233). Configured with
`NFSU_RETAIL_ROOT`, CTest registers every check that reads the game's files
with the label `retail` - the replay and frame pins, the cached-versus-
reference draw comparison, the stock frame pins, the 48 MiB loader ceilings,
the track slice, the racing line and the audio pins - passes the root on each
command line, and brackets them with a fixture that fails if any file or
directory under the root changed. Configured without it, the one retail test
is `nfsu_retail_skipped`, which reports SKIPPED: a summary that shows it is not
a C++ gate run. `ctest -L retail` runs only the retail checks, fixture
included. The replay check needs the Python 3.8 or newer interpreter the
configure finds; it is a real interpreter, not one inside another tool.

A differential comparison fails in a way the rows above do not: neither side
produces a real answer and the comparison reports agreement. A binary that
cannot load its libraries exits with no output, so every invocation matches
every other; a run that refuses to overwrite leftover artifacts leaves the
previous run's files on disk, so the hashes match. Asserting non-empty console
output catches the first and misses the second, because the refusal prints an
error line and an error line is not empty. Delete prior outputs by
exact name first, then confirm that both sides wrote this run's artifact and
that neither printed an error line.

A mutation sweep fails the same way, in the direction that produces a false
pass. Some sweeps here already record a crash as unknown and some still read
every non-zero exit as a kill, and branches have been accepted on sweeps of 26,
34, 185 and 200 mutants. A target that crashes for a
reason the mutant did not cause therefore reports that mutant caught, and a
surviving mutant is exactly what a sweep exists to find. **Classify a run by
what it printed, not only by its exit status: a non-zero exit with no failure
line and no summary line is an unknown result, not a kill.** Report unknowns as
their own column. A sweep that cannot tell a kill from a crash has no score,
and re-run its unknowns before quoting a number.

Two harness traps have produced false results here since. Restoring a mutated
source byte for byte gives it an older modification time than the object built
from the mutant, so the next build keeps the mutant compiled in and every later
mutant inherits its failure: touch each restored file. And a case appended
after a CMake guard script's final summary runs after the verdict and cannot
change it: put new cases beside the related ones.

Issue #122 is what this rule was written against, and the crash was real
rather than hypothetical. `nfsu_track_object_tests` named its fixture
`nfsu_track_objects_region.bin` under the system temporary directory with no
process id in it, so two copies - two sessions running `ctest` from their own
build directories, which Shared checkout above says is routine - wrote and
removed one another's file. The reader then got a report with no section and
took an access violation at a container subscript. Two concurrent `ctest`
invocations reproduced it on the eighteenth run of the target while the other
side ran 6000 clean. Any sweep running while another session built would have
recorded those crashes as kills. Both halves were needed: the unique fixture
name stops the collision, and the subscript guard keeps any other transient
filesystem failure a failure rather than a segmentation fault.

The rule is a reporting discipline, not a gate. Nothing here runs a sweep, no
sweep script is tracked, and the scripts that exist live in a shared scratch
directory where they are rewritten and deleted between rounds - the two named
in issue #122's dispatch were already gone when this was written. So this
paragraph binds the next author of one, and nothing checks it.

## Pinned values live in tests

A value a test checks - a frame or state hash, a replay result, a draw,
triangle or byte count, a measured peak - is kept current in that test and
in no document. Documentation names the test that pins it, or the command
that prints it, instead of copying the number. A copied value goes stale
the first time a change moves it, and the stale copy then costs a
documentation edit, a review comment and another round while proving
nothing the test does not already prove. On 2026-09-27 three review
comments came from numbers copied into documents: #206 note R2-N1 (a
desktop measurement quoted without its commit), #209 finding F1 (six
document-only pins left stale, plus a copy of the test pin for frame 690)
and #211 finding F3 (console numbers with no record on the board).

Scope:

- The rule governs documents. Code keeps the constants it runs on, such as
  a retail file's SHA-256 checked before parsing or the replay hash an app
  compares against, and a test may share such a constant instead of
  repeating it.
- The identity of a retail input - the size and SHA-256 of a retail file a
  document describes - is provenance, not a pin, and stays in the document.
- A value that only a document pins, because no test can reach the data it
  comes from, is the pin of record. The retail drive-replay frames that
  tests/drive_replay_guards.cmake leaves to docs/CONTROLS_CAMERA.md are one
  such case. A change that moves it updates it in place and gives the
  reason. Moving it into a test, once one can reach it, is better.

Everything else:

- History. A document may record a measurement as history in the form "at
  <short SHA>, <where and how measured>: <value>". A history line is not
  refreshed when later commits change the number; a newer measurement is a
  new line. A history line found to be wrong is corrected, and the
  correction says so.
- Console results. The record is the board comment that transcribes the
  owner's photo. A document gives a console number only as a history line
  that links that comment.
- A current value copied into a document is removed, not updated, when a
  change touches that document, and the document names the test or the
  command instead. A test that prints nothing on a pass is named by its
  CTest name.
- A reviewer reports such a copy as a note, not a finding, unless the copy
  makes a claim false.

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

The failures named below are the ones these rules were written against; one
equal-weight rule each would imply it closed them all. It has mechanically
closed none of them, and earlier drafts of this section claimed otherwise
twice.

The scope diff came closest, but what survives is a two-part discipline that
still depends on the operator fetching first and reading the right ref - not a
command form that is safe however it is invoked. The ASCII gate had the same
shape: the scan this document prescribed for two rounds was blind to a leading
byte order mark, so the rule was sound while the gate that enforced it was not,
and a passing run proved nothing. That gate is the one thing this commit does
replace - the scan above catches a leading mark, over the entry modes the
section above now allows. Re-review only codifies a loop the previous protocol
already implied, and that loop had already caught the launcher fix that stopped
an unowned process. Silent truncation has no rule of its own and leans on the
same general reviewer duty that missed it the first time. The surviving mutant
still has no rule of its own, and no gate here runs mutation testing; the sweep
rule above governs only how a sweep reports a crash it already ran. The subagent
worktree rule is the least enforced of the set: git does not refuse a second
writer in one tree, and nothing notices a head that moved under a measurement,
so both incidents behind it were found only by reading the damage afterwards.

Weigh these rules by what they mechanically prevent, and treat a rule that
names a command as a claim to reproduce rather than a closure to trust.

# Claude worker protocol

Codex is the coordinator and independent reviewer for this repository. The human is the project owner, not a message relay between agents.

## Routine work

- Work only on the assigned Gitea issue and its explicit dependencies.
- Use Claude Code subagents when the assignment requests them.
- Keep retail game files strictly read-only.
- Follow the ASCII-only artifact rule below.
- Do not merge pull requests.

## ASCII-only artifact rule

Every artifact this repository produces is ASCII. Concretely, each of the following must contain only bytes `0x09`, `0x0A`, `0x0D`, and `0x20` through `0x7E`:

- every tracked file's contents;
- every tracked path and file name;
- every branch name and tag;
- every commit message;
- every pull request title, description, and comment posted by an agent.

The rule covers artifacts, not the workspace. The checkout directory itself may sit under a non-ASCII path, and tools must keep working there. Line endings are not constrained; `CR` is in the allowed set.

Scan with:

```powershell
git ls-tree -r --name-only HEAD | Select-String -Pattern '[^\x20-\x7E]'
git show HEAD:<path> | Select-String -Pattern '[^\x09\x0A\x0D\x20-\x7E]'
```

## Required validation

"Run all required validation" means every gate below that the change can affect. Report each command and its result verbatim in the handoff.

| Area touched | Command |
| --- | --- |
| C++ sources, headers, `CMakeLists.txt` | `cmake --build build` then `ctest --test-dir build --output-on-failure` |
| `tools/kanban/` | `node --test tools\kanban\test.mjs tools\kanban\server.test.mjs` |
| `tools/kanban/start.ps1` | launcher smoke: `$env:KANBAN_SMOKE="1"; .\tools\kanban\start.ps1` from the real workspace path |
| any tracked file | the ASCII scan above |

A change that touches nothing in a row does not have to run that row. If a gate cannot run, that is a blocker, not a note.

## Required handoff

Do not stop after telling the human that code was committed, pushed, or ready for review. Complete the entire worker handoff yourself:

1. Run all required validation.
2. Create an ASCII-only commit.
3. Push the assigned branch.
4. Open or update the pull request to `main`.
5. Post this structured comment on the pull request:

```text
CODEX_REVIEW_REQUESTED
head: <full commit SHA>
issue: #<issue number>
tests: <commands and results>
evidence: <short evidence summary>
blockers: <none | what is blocked and what decision is needed>
```

The comment is the handoff to Codex. After posting it, wait for Codex review through Gitea or exit cleanly. Do not ask the human to notify Codex.

## Review fixes

When Codex requests changes, address every confirmed finding on the same branch, rerun validation, push the fixes, and post a new `CODEX_REVIEW_REQUESTED` comment with the new full head SHA.

## Review mode

A session may instead be asked to review a pull request rather than implement one. A reviewer:

- does not commit, push, merge, or modify any tracked file;
- may run tests and start local servers, but must not change Gitea issue, label, or pull request state;
- posts a single comment on the pull request under review that begins with `CLAUDE_REVIEW_PASSED` or `CLAUDE_REVIEW_CHANGES_REQUESTED`, and includes the exact head SHA reviewed, findings with file and line references, and exact validation results.

A reviewer never posts `CODEX_REVIEW_REQUESTED`; that token belongs to the worker handoff.

## Blockers

If the task cannot proceed, post a precise comment beginning with `CODEX_BLOCKED`. Include the failed command or missing evidence, the current branch and head SHA, and the smallest decision needed.

Post it where the work is visible: on the pull request if one is open for the branch, otherwise on the assigned issue. Do not use the human as a relay for routine status.

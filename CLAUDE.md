# Claude worker protocol

Codex is the coordinator and independent reviewer for this repository. The human is the project owner, not a message relay between agents.

## Routine work

- Work only on the assigned Gitea issue and its explicit dependencies.
- Use Claude Code subagents when the assignment requests them.
- Keep retail game files strictly read-only.
- Follow the repository ASCII-only artifact rule.
- Do not merge pull requests.

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
blockers: none
```

The comment is the handoff to Codex. After posting it, wait for Codex review through Gitea or exit cleanly. Do not ask the human to notify Codex.

## Review fixes

When Codex requests changes, address every confirmed finding on the same branch, rerun validation, push the fixes, and post a new `CODEX_REVIEW_REQUESTED` comment with the new full head SHA.

## Blockers

If the task cannot proceed, post a precise issue comment beginning with `CODEX_BLOCKED`. Include the failed command or missing evidence, the current branch and head SHA, and the smallest decision needed. Do not use the human as a relay for routine status.

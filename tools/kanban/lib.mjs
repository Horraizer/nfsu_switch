export const STATE_LABELS = [
  "state/proposed",
  "state/backlog",
  "state/ready",
  "state/working",
  "state/review",
  "state/verify",
  "state/blocked",
  "state/done",
];

export const COLUMNS = [
  { id: "state/proposed", title: "Proposed", tone: "slate" },
  { id: "state/backlog", title: "Backlog", tone: "gray" },
  { id: "state/ready", title: "Ready", tone: "green" },
  { id: "state/working", title: "Working", tone: "blue" },
  { id: "state/review", title: "Review", tone: "yellow" },
  { id: "state/verify", title: "Verify", tone: "violet" },
  { id: "state/blocked", title: "Blocked", tone: "red" },
  { id: "state/done", title: "Done", tone: "emerald" },
];

export const DONE_COLUMN = "state/done";

// The conditions moveIssue is allowed to report to the browser verbatim. Each
// describes the board's own configuration, so its text is authored here rather
// than produced by the runtime or echoed from upstream.
export class BoardLabelError extends Error {
  constructor(message) {
    super(message);
    this.name = "BoardLabelError";
  }
}

// Gitea marshals an empty label set as [], but older releases and some proxies
// return null, and a proxy that rewrites the payload can put a null or a bare
// string in the array itself. Every reader goes through this, so a malformed
// element is dropped once here instead of becoming a TypeError at a `.name`.
export function labelList(issue) {
  const entries = Array.isArray(issue?.labels) ? issue.labels : [];
  return entries.filter((label) => label !== null && typeof label === "object");
}

// Only ids Gitea can actually accept. A label object with no usable id would
// otherwise be written back as a null inside a PUT labels body. The forward
// path refuses such a label outright; this is for the rollback, which is
// best-effort and must not throw on its way to restoring what it can.
export function labelIds(labels) {
  return labels.map((label) => label?.id).filter((id) => Number.isSafeInteger(id));
}

export function isPullRequest(issue) {
  if (issue?.pull_request) {
    return true;
  }
  return /\/pulls\/\d+\/?$/.test(issue?.html_url ?? "");
}

export function issueColumn(issue) {
  if (issue?.state === "closed") {
    return DONE_COLUMN;
  }

  const stateLabel = labelList(issue)
    .map((label) => label?.name)
    .find((name) => STATE_LABELS.includes(name));
  return stateLabel ?? "state/backlog";
}

export function nextLabelIds(issue, targetLabel, labels) {
  if (!STATE_LABELS.includes(targetLabel)) {
    throw new BoardLabelError(`Unknown board column: ${targetLabel}`);
  }

  const target = labels.find((label) => label?.name === targetLabel);
  if (!target) {
    throw new BoardLabelError(`Missing Gitea label: ${targetLabel}`);
  }

  if (!Number.isSafeInteger(target.id)) {
    throw new BoardLabelError(`Gitea label ${targetLabel} has no id this board can write back.`);
  }

  const retained = labelList(issue).filter((label) => !STATE_LABELS.includes(label?.name));
  // Dropping a label the board cannot write back would delete it from the
  // issue and still report success, so the move fails instead. The name is
  // deliberately not echoed: it is upstream text.
  if (retained.some((label) => !Number.isSafeInteger(label?.id))) {
    throw new BoardLabelError(
      "This issue carries a label with no id this board can write back. Move it in Gitea instead.",
    );
  }
  return [...retained.map((label) => label.id), target.id];
}

export function targetStateFor(targetLabel) {
  return targetLabel === DONE_COLUMN ? "closed" : "open";
}

export function normalizeIssue(issue) {
  return {
    number: issue.number,
    title: issue.title,
    state: issue.state,
    column: issueColumn(issue),
    url: issue.html_url,
    updatedAt: issue.updated_at,
    assignee: issue.assignee?.login ?? null,
    milestone: issue.milestone?.title ?? null,
    // The write path keeps every object it was given, because dropping a label
    // it cannot render would delete that label from the issue on the next move.
    // The render path cannot use a nameless label at all, so it drops it here.
    labels: labelList(issue)
      .filter((label) => typeof label?.name === "string")
      .map((label) => ({
        id: label.id,
        name: label.name,
        color: label.color,
      })),
  };
}

// The response for an accepted move is built from state the server already
// knows, so a move that Gitea applied can never be reported as a failure by a
// read-back that happens to fail afterwards.
export function projectMovedIssue(issue, targetLabel, labels) {
  const targetState = targetStateFor(targetLabel);
  const retained = labelList(issue).filter((label) => !STATE_LABELS.includes(label?.name));
  const target = labels.find((label) => label?.name === targetLabel);
  return normalizeIssue({
    ...issue,
    state: targetState,
    labels: [...retained, ...(target ? [target] : [])],
  });
}

export function isValidIssueNumber(value) {
  return Number.isSafeInteger(value) && value >= 1;
}

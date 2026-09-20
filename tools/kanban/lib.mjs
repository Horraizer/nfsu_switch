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

// Gitea marshals an empty label set as [], but older releases and some proxies
// return null. Every reader goes through this so no call site has to remember.
export function labelList(issue) {
  return Array.isArray(issue?.labels) ? issue.labels : [];
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
    throw new Error(`Unknown board column: ${targetLabel}`);
  }

  const target = labels.find((label) => label.name === targetLabel);
  if (!target) {
    throw new Error(`Missing Gitea label: ${targetLabel}`);
  }

  const retained = labelList(issue)
    .filter((label) => !STATE_LABELS.includes(label.name))
    .map((label) => label.id);
  return [...retained, target.id];
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
    labels: labelList(issue).map((label) => ({
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
  const retained = labelList(issue).filter((label) => !STATE_LABELS.includes(label.name));
  const target = labels.find((label) => label.name === targetLabel);
  return normalizeIssue({
    ...issue,
    state: targetState,
    labels: [...retained, ...(target ? [target] : [])],
  });
}

export function isValidIssueNumber(value) {
  return Number.isSafeInteger(value) && value >= 1;
}

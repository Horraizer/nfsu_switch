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

export function isPullRequest(issue) {
  if (issue?.pull_request) {
    return true;
  }
  return /\/pulls\/\d+\/?$/.test(issue?.html_url ?? "");
}

export function issueColumn(issue) {
  if (issue?.state === "closed") {
    return "state/done";
  }

  const labels = Array.isArray(issue?.labels) ? issue.labels : [];
  const stateLabel = labels
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

  const existing = Array.isArray(issue?.labels) ? issue.labels : [];
  const retained = existing
    .filter((label) => !STATE_LABELS.includes(label.name))
    .map((label) => label.id);
  return [...retained, target.id];
}

export function normalizeIssue(issue) {
  const labels = Array.isArray(issue.labels) ? issue.labels : [];
  return {
    number: issue.number,
    title: issue.title,
    state: issue.state,
    column: issueColumn(issue),
    url: issue.html_url,
    updatedAt: issue.updated_at,
    assignee: issue.assignee?.login ?? null,
    milestone: issue.milestone?.title ?? null,
    labels: labels.map((label) => ({
      id: label.id,
      name: label.name,
      color: label.color,
    })),
  };
}

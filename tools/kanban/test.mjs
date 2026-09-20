import assert from "node:assert/strict";
import test from "node:test";

import {
  isPullRequest,
  issueColumn,
  nextLabelIds,
  normalizeIssue,
} from "./lib.mjs";

test("closed issues are always shown as done", () => {
  assert.equal(issueColumn({ state: "closed", labels: [] }), "state/done");
});

test("open issues use their explicit state label", () => {
  const issue = { state: "open", labels: [{ name: "state/review" }] };
  assert.equal(issueColumn(issue), "state/review");
});

test("unlabelled open issues fall back to backlog", () => {
  assert.equal(issueColumn({ state: "open", labels: [] }), "state/backlog");
});

test("pull requests are excluded by metadata and URL", () => {
  assert.equal(isPullRequest({ pull_request: { merged: false } }), true);
  assert.equal(isPullRequest({ html_url: "https://git.example/a/b/pulls/9" }), true);
  assert.equal(isPullRequest({ html_url: "https://git.example/a/b/issues/9" }), false);
});

test("moving replaces only the workflow label", () => {
  const issue = {
    labels: [
      { id: 10, name: "agent/claude" },
      { id: 11, name: "risk/high" },
      { id: 12, name: "state/working" },
    ],
  };
  const labels = [
    { id: 12, name: "state/working" },
    { id: 13, name: "state/review" },
  ];
  assert.deepEqual(nextLabelIds(issue, "state/review", labels), [10, 11, 13]);
});

test("normalization keeps card metadata", () => {
  const task = normalizeIssue({
    number: 31,
    title: "Recover markers",
    state: "open",
    html_url: "https://git.example/a/b/issues/31",
    updated_at: "2026-09-20T00:00:00Z",
    assignee: { login: "worker" },
    milestone: { title: "Car scene" },
    labels: [{ id: 2, name: "state/working", color: "1d76db" }],
  });
  assert.equal(task.column, "state/working");
  assert.equal(task.assignee, "worker");
  assert.equal(task.milestone, "Car scene");
});

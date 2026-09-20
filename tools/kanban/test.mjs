import assert from "node:assert/strict";
import test from "node:test";

import {
  isPullRequest,
  isValidIssueNumber,
  issueColumn,
  labelList,
  nextLabelIds,
  normalizeIssue,
  projectMovedIssue,
  targetStateFor,
} from "./lib.mjs";

import { columnShortName, resolveMoveOutcome, riskToken } from "./web/shared.mjs";

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

test("a null label set never reaches a call site as null", () => {
  assert.deepEqual(labelList({ labels: null }), []);
  assert.deepEqual(labelList({}), []);
  assert.deepEqual(labelList(null), []);
  assert.equal(issueColumn({ state: "open", labels: null }), "state/backlog");
  assert.deepEqual(normalizeIssue({ number: 1, labels: null }).labels, []);
  assert.deepEqual(nextLabelIds({ labels: null }, "state/ready", [{ id: 5, name: "state/ready" }]), [5]);
});

test("nextLabelIds refuses an unknown column and a label the repository lacks", () => {
  assert.throws(() => nextLabelIds({ labels: [] }, "state/nope", []), /Unknown board column/);
  assert.throws(() => nextLabelIds({ labels: [] }, "state/ready", []), /Missing Gitea label/);
});

test("only the done column closes an issue", () => {
  assert.equal(targetStateFor("state/done"), "closed");
  for (const column of ["state/ready", "state/working", "state/blocked", "state/proposed"]) {
    assert.equal(targetStateFor(column), "open");
  }
});

test("the accepted move response is projected, not re-read", () => {
  const issue = {
    number: 7,
    title: "Trace alpha state",
    state: "open",
    html_url: "https://git.example/a/b/issues/7",
    labels: [
      { id: 10, name: "agent/claude", color: "112233" },
      { id: 12, name: "state/working", color: "1d76db" },
    ],
  };
  const labels = [
    { id: 12, name: "state/working", color: "1d76db" },
    { id: 14, name: "state/done", color: "ddeeff" },
  ];

  const done = projectMovedIssue(issue, "state/done", labels);
  assert.equal(done.state, "closed");
  assert.equal(done.column, "state/done");
  assert.deepEqual(done.labels.map((label) => label.name), ["agent/claude", "state/done"]);

  const back = projectMovedIssue({ ...issue, state: "closed" }, "state/working", labels);
  assert.equal(back.state, "open");
  assert.equal(back.column, "state/working");
});

test("issue numbers must be safe positive integers", () => {
  assert.equal(isValidIssueNumber(1), true);
  assert.equal(isValidIssueNumber(74), true);
  assert.equal(isValidIssueNumber(0), false);
  assert.equal(isValidIssueNumber(-3), false);
  assert.equal(isValidIssueNumber(1.5), false);
  assert.equal(isValidIssueNumber(Number.NaN), false);
  // 1e20 passes Number.isInteger but is not addressable as an issue number.
  assert.equal(Number.isInteger(1e20), true);
  assert.equal(isValidIssueNumber(1e20), false);
  assert.equal(isValidIssueNumber(Number("99999999999999999999")), false);
});

test("a risk label that is not a safe class token falls back", () => {
  assert.equal(riskToken("high"), "high");
  assert.equal(riskToken("very-high"), "very-high");
  assert.equal(riskToken("very high"), "unrated");
  assert.equal(riskToken(""), "unrated");
  assert.equal(riskToken(undefined), "unrated");
  assert.equal(riskToken("high (blocked)"), "unrated");
});

test("column short names drop the state prefix", () => {
  assert.equal(columnShortName("state/review"), "review");
  assert.equal(columnShortName("other"), "other");
  assert.equal(columnShortName(undefined), "");
});

test("an accepted move keeps the server column even if the later refresh fails", () => {
  const outcome = resolveMoveOutcome({
    ok: true,
    previousColumn: "state/working",
    requestedColumn: "state/done",
    serverColumn: "state/done",
  });
  assert.deepEqual(outcome, { column: "state/done", reverted: false, drift: false });
});

test("a rejected move reverts the card", () => {
  const outcome = resolveMoveOutcome({
    ok: false,
    partial: false,
    previousColumn: "state/working",
    requestedColumn: "state/done",
  });
  assert.deepEqual(outcome, { column: "state/working", reverted: true, drift: false });
});

test("a partially applied move is not reverted and is flagged as drift", () => {
  const outcome = resolveMoveOutcome({
    ok: false,
    partial: true,
    previousColumn: "state/working",
    requestedColumn: "state/done",
  });
  assert.deepEqual(outcome, { column: "state/done", reverted: false, drift: true });
});

// ------------------------------------------------------- the board page ----

/**
 * Enough of a document for app.js to load and for its failure paths to run.
 * The success path renders cards from templates and is not exercised here:
 * every fetch in these tests fails, which is the shape both rules are about.
 */
function installDocument() {
  const nodes = new Map();
  const makeElement = () => {
    const tokens = new Set();
    return {
      textContent: "",
      value: "",
      disabled: false,
      className: "",
      lastChild: { textContent: "" },
      classList: {
        add: (...names) => names.forEach((name) => tokens.add(name)),
        remove: (...names) => names.forEach((name) => tokens.delete(name)),
        toggle: (name, on) => (on ? tokens.add(name) : tokens.delete(name)),
        contains: (name) => tokens.has(name),
      },
      setAttribute() {},
      append() {},
      addEventListener() {},
    };
  };
  globalThis.document = {
    querySelector(selector) {
      if (!nodes.has(selector)) {
        nodes.set(selector, makeElement());
      }
      return nodes.get(selector);
    },
    addEventListener() {},
  };
  return nodes;
}

const elements = installDocument();
let nextFetch = async () => {
  throw new Error("no fetch configured");
};
globalThis.fetch = (...args) => nextFetch(...args);

// Imported after the document exists, because app.js reads it on load.
const { loadBoard, showToast } = await import("./web/app.js");
const toast = elements.get("#toast");

function resetToast() {
  showToast.until = 0;
  toast.textContent = "";
}

test("a toast that would leave the screen sooner does not replace a longer hold", () => {
  resetToast();
  showToast("PARTIAL: issue #9 carries state/done while still open", true, 12_000);
  showToast("#6 moved to review");
  assert.match(toast.textContent, /carries state\/done/, "a routine confirmation must not wipe the drift message");
});

test("an error is never suppressed, however long the current hold is", () => {
  resetToast();
  showToast("PARTIAL: issue #9 carries state/done while still open", true, 12_000);
  showToast("Missing Gitea label: state/review", true);
  assert.equal(
    toast.textContent,
    "Missing Gitea label: state/review",
    "the operator cannot act on a problem they are not shown",
  );
});

test("a hold that has already expired suppresses nothing", () => {
  resetToast();
  showToast("done", false, 10);
  showToast.until = Date.now() - 1;
  showToast("later message");
  assert.equal(toast.textContent, "later message");
});

test("a quiet refresh that fails raises no toast, and a requested one does", async () => {
  // The reported interleaving: a drift message is on screen for twelve seconds
  // and the refresh that follows the move fails. That refresh is reporting, not
  // the operator's action, so it must not speak at all.
  nextFetch = async () => {
    throw new Error("Gitea 502: refresh died too");
  };

  resetToast();
  showToast("PARTIAL: issue #9 carries state/done while still open", true, 12_000);
  await assert.rejects(() => loadBoard({ quiet: true }));
  assert.match(toast.textContent, /carries state\/done/, "a quiet refresh must not overwrite the drift message");

  // The same failure, when the operator asked for it, is theirs to see.
  resetToast();
  await assert.rejects(() => loadBoard());
  assert.equal(toast.textContent, "Gitea 502: refresh died too");
});

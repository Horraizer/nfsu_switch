export const STATE_PREFIX = "state/";

export function columnShortName(column) {
  return typeof column === "string" && column.startsWith(STATE_PREFIX)
    ? column.slice(STATE_PREFIX.length)
    : String(column ?? "");
}

// A CSS class token cannot contain whitespace, and label text after "risk/" is
// repository-controlled, so anything unexpected falls back instead of throwing
// out of the render.
export function riskToken(value) {
  return /^[a-z0-9_-]+$/i.test(value ?? "") ? value : "unrated";
}

/**
 * Decides where a card belongs once a move request has settled.
 *
 * The board refresh is deliberately not an input here. A refresh that fails
 * says nothing about whether the write landed, so it must never be able to
 * revert a card that Gitea already accepted.
 */
export function resolveMoveOutcome({
  ok,
  partial = false,
  previousColumn,
  requestedColumn,
  serverColumn = null,
}) {
  if (ok) {
    return { column: serverColumn ?? requestedColumn, reverted: false, drift: false };
  }
  if (partial) {
    // The label write landed but the state write did not, and the rollback
    // failed too. Reverting would show the operator the opposite of what Gitea
    // holds, so hold the requested column and let the refresh supply the truth.
    return { column: requestedColumn, reverted: false, drift: true };
  }
  return { column: previousColumn, reverted: true, drift: false };
}

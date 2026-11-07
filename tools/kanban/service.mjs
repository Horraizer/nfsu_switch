import { open } from "node:fs/promises";
import { createServer } from "node:http";
import path from "node:path";
import { Transform } from "node:stream";
import { pipeline } from "node:stream/promises";

import {
  BoardLabelError,
  COLUMNS,
  STATE_LABELS,
  isPullRequest,
  isValidIssueNumber,
  labelIds,
  labelList,
  nextLabelIds,
  normalizeIssue,
  projectMovedIssue,
  targetStateFor,
} from "./lib.mjs";

export const PAGE_LIMIT = 50;
export const MAX_PAGES = 200;
// Consecutive pages that add no new row before the sweep is declared stuck.
// More than one tolerates the overlap that offset pagination produces when
// rows are deleted mid-sweep.
export const STALLED_PAGE_LIMIT = 3;
export const MAX_BODY_BYTES = 16_384;

const MIME_TYPES = new Map([
  [".css", "text/css; charset=utf-8"],
  [".html", "text/html; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".mjs", "text/javascript; charset=utf-8"],
]);

// Carries the status and the exact body the browser is allowed to see. Anything
// that is not an HttpError never reaches the client as text.
export class HttpError extends Error {
  constructor(status, message, extra = {}, options = {}) {
    super(message, options);
    this.name = "HttpError";
    this.status = status;
    this.extra = extra;
  }
}

// Upstream status is safe to name; `detail` is the upstream body and stays on
// the server, because a proxy or WAF in front of Gitea can echo the request
// headers, and those headers carry the token. `detail` still reaches the log,
// so the caller scrubs the credential out of it before constructing this.
export class GiteaError extends Error {
  constructor(status, detail) {
    super(`Gitea request failed (HTTP ${status})`);
    this.name = "GiteaError";
    this.giteaStatus = status;
    this.detail = detail;
  }
}

// A connection that was never established cannot have delivered the request,
// so a write that failed this way is known not to have been applied.
const UNSENT_ERROR_CODES = new Set([
  "ECONNREFUSED",
  "ENOTFOUND",
  "EAI_AGAIN",
  "EHOSTUNREACH",
  "ENETUNREACH",
  "ERR_INVALID_URL",
  "CERT_HAS_EXPIRED",
  "DEPTH_ZERO_SELF_SIGNED_CERT",
  "UNABLE_TO_VERIFY_LEAF_SIGNATURE",
  "ERR_TLS_CERT_ALTNAME_INVALID",
]);

function isUnsentFailure(error) {
  const codes = [error?.code, error?.cause?.code, error?.cause?.errors?.[0]?.code];
  return codes.some((code) => typeof code === "string" && UNSENT_ERROR_CODES.has(code));
}

// A 4xx is a refusal by something that read and understood the request, so the
// issue is known not to have changed. A 5xx or a request timeout can be
// produced by a gateway that had already forwarded the request -- and
// GITEA_BASE is an https host, so there is a TLS terminator in front of Gitea
// by construction. Rolling back on those would undo a change Gitea applied.
const AMBIGUOUS_UPSTREAM_STATUS = new Set([408]);

export function isDefiniteRejection(error) {
  if (error instanceof GiteaError) {
    return (
      error.giteaStatus >= 400 &&
      error.giteaStatus < 500 &&
      !AMBIGUOUS_UPSTREAM_STATUS.has(error.giteaStatus)
    );
  }
  return error instanceof HttpError && error.requestSent === false;
}

const REDACTED = "[redacted]";
const MAX_SCRUB_DEPTH = 6;
// Keeps the scheme, drops the credential, and stops at the delimiters that end
// a header value inside JSON, HTML or a plain-text proxy page.
const AUTH_HEADER = /((?:authorization|private-token|x-auth-token)"?\s*[:=]\s*"?)(token |bearer |basic )?[^\s"'<>,;)&]+/gi;

const PERCENT_ESCAPE = /^[0-9a-fA-F]{2}$/;

/**
 * Percent-decodes `text` once and records, for each decoded character, where it
 * started in the original, so a match found in the decoded form can be redacted
 * in the original. Decoding is per byte rather than per code point: the token
 * this is compared against is ASCII, and a truncated UTF-8 escape must yield
 * something harmless rather than throw the way `decodeURIComponent` does.
 *
 * One consequence, for a token that is not ASCII after all: a character in
 * U+0080 to U+00FF comes back from its single-byte escape -- `%E9` becomes
 * U+00E9 -- so such a token is matched in that form as well as literally. Its
 * real UTF-8 encoding, `%C3%A9`, is not, and neither is anything above U+00FF
 * in any escaped form. README states the same limit.
 */
function decodeOnce(text) {
  let decoded = "";
  const origin = [];
  let at = 0;
  while (at < text.length) {
    const escaped = text.charAt(at) === "%" && PERCENT_ESCAPE.test(text.slice(at + 1, at + 3));
    decoded += escaped
      ? String.fromCharCode(Number.parseInt(text.slice(at + 1, at + 3), 16))
      : text.charAt(at);
    origin.push(at);
    at += escaped ? 3 : 1;
  }
  // Sentinel, so the last decoded character has an end offset like every other.
  origin.push(text.length);
  return { decoded, origin };
}

function occurrences(haystack, needle) {
  const found = [];
  for (let at = haystack.indexOf(needle); at >= 0; at = haystack.indexOf(needle, at + needle.length)) {
    found.push(at);
  }
  return found;
}

/**
 * Every span of `text` that holds the token, whether its characters arrived
 * literally or percent-encoded. A proxy that echoes the request line sends the
 * credential back through a query string -- `?access_token=gta%5F...` -- and a
 * literal comparison walks straight past that into the log.
 *
 * This was one regular expression per token character, each `(?:literal|%hh)`.
 * Exactly one character can begin both branches at the same position -- `%`,
 * whose encoding is `%25` -- so a token holding a run of them made the engine
 * try every combination before failing: against text that never matches, 24 of
 * them cost 2.3 seconds and each further pair cost four times that again. A
 * real Gitea token is `gta_` plus base32 and holds no `%`, but the value comes
 * from the environment and a malformed one must not be able to wedge the
 * server. Decoding once removes the choice rather than ordering it; both passes
 * below are linear in the length of the text.
 */
function tokenSpans(text, token) {
  const spans = occurrences(text, token).map((at) => [at, at + token.length]);
  const { decoded, origin } = decodeOnce(text);
  // The decoded pass does not subsume the raw one: decoding rewrites a token
  // that contains a `%hh` sequence of its own, so its literal form is not in
  // `decoded` at all and only the raw pass above can see it.
  //
  // That is also where the pair stops, but the gap is narrower than "anything
  // mixed". This pass only loses the token's own `%hh` when that sequence
  // reaches the text wholly literal; let any one of its three characters
  // arrive encoded -- `a%252cb`, `a%%32cb`, `a%25%32cb` -- and decoding puts
  // `%hh` back and the comparison matches. So a miss needs both passes to fail
  // at once: one of the token's own decodable `%hh` sequences arriving wholly
  // literal, which costs it this pass, and at least one token character
  // arriving encoded, which costs it the raw pass above. `a%2cb` inside
  // `x %61%2cb y` is that shape; of the 32 literal-or-encoded mixes of that
  // token only three -- `%61%2cb`, `a%2c%62`, `%61%2c%62` -- reach the gap,
  // and the other 29 are redacted. Closing it means deciding which `%hh` in
  // the text belongs to the token and which to the encoding, which the text
  // does not say. A real Gitea token is `gta_` plus base32 and holds no `%`,
  // so nothing reaches the gap without a malformed one; README records it
  // rather than this pretending otherwise.
  for (const at of occurrences(decoded, token)) {
    spans.push([origin[at], origin[at + token.length]]);
  }
  return spans;
}

function redactSpans(text, spans) {
  let result = "";
  let cursor = 0;
  for (const [start, end] of spans.sort((left, right) => left[0] - right[0] || left[1] - right[1])) {
    if (end <= cursor) {
      continue;
    }
    if (start >= cursor) {
      result += text.slice(cursor, start) + REDACTED;
    }
    // A span that starts inside the previous redaction is the same credential
    // seen a second way, so it extends that redaction instead of adding one.
    cursor = end;
  }
  return result + text.slice(cursor);
}

function scrubText(text, token) {
  const masked = text.replace(AUTH_HEADER, (_match, prefix, scheme) => `${prefix}${scheme ?? ""}${REDACTED}`);
  // Unconditional, so the guarantee does not depend on the token's shape. A
  // pathologically short token over-redacts the log, which is the safe side.
  return token ? redactSpans(masked, tokenSpans(masked, token)) : masked;
}

/**
 * Removes the token from an upstream payload before it is logged. The payload
 * is whatever the proxy chose to send, so this walks strings inside objects and
 * arrays as well, with a depth cap because the shape is not ours.
 */
export function scrubSecrets(value, token, depth = 0) {
  if (typeof value === "string") {
    return scrubText(value, token);
  }
  if (value === null || typeof value !== "object") {
    return value;
  }
  if (depth >= MAX_SCRUB_DEPTH) {
    // Flattening rather than returning the subtree, because returning it would
    // hand every string inside it to the log unscrubbed.
    return scrubText(flatten(value), token);
  }
  if (Array.isArray(value)) {
    return value.map((entry) => scrubSecrets(entry, token, depth + 1));
  }
  const scrubbed = {};
  for (const [key, entry] of Object.entries(value)) {
    // Keys too: a proxy that echoes a header can put the credential in one.
    scrubbed[scrubText(String(key), token)] = scrubSecrets(entry, token, depth + 1);
  }
  return scrubbed;
}

function flatten(value) {
  try {
    return JSON.stringify(value) ?? String(value);
  } catch {
    return String(value);
  }
}

export function createConsoleLogger(stream = console) {
  return {
    info: (message) => stream.log(message),
    warn: (message) => stream.warn(message),
    error: (message, error) => stream.error(error ? `${message}: ${describe(error)}` : message),
  };
}

export const silentLogger = { info() {}, warn() {}, error() {} };

function describe(error) {
  if (error instanceof GiteaError) {
    return `${error.message} detail=${JSON.stringify(error.detail)}`;
  }
  if (error instanceof Error) {
    return error.stack ?? error.message;
  }
  return String(error);
}

function sendJson(response, statusCode, payload) {
  const body = JSON.stringify(payload);
  response.writeHead(statusCode, {
    "Cache-Control": "no-store",
    "Content-Length": Buffer.byteLength(body),
    "Content-Type": "application/json; charset=utf-8",
    "X-Content-Type-Options": "nosniff",
  });
  response.end(body);
}

// Only a value that identifies a row. NaN is excluded deliberately: a Set
// treats every NaN as the same key, so two distinct rows would collapse.
function identityKey(item) {
  const raw = item?.id ?? item?.number;
  if (typeof raw === "number") {
    return Number.isSafeInteger(raw) ? `n${raw}` : null;
  }
  return typeof raw === "string" ? `s${raw}` : null;
}

export function createGiteaClient({
  baseUrl,
  token,
  fetchImpl = fetch,
  pageLimit = PAGE_LIMIT,
  maxPages = MAX_PAGES,
}) {
  async function call(pathname, { method = "GET", body } = {}) {
    if (!token) {
      throw new HttpError(503, "GITEA_TOKEN is not set");
    }

    let response;
    let text;
    try {
      response = await fetchImpl(`${baseUrl}/api/v1${pathname}`, {
        method,
        headers: {
          Accept: "application/json",
          Authorization: `token ${token}`,
          "Content-Type": "application/json",
          "User-Agent": "nfsu-local-kanban",
        },
        body: body === undefined ? undefined : JSON.stringify(body),
      });
      // The body read belongs to the same exchange. A socket that dies after
      // the headers rejects here with a bare TypeError, which is neither an
      // HttpError nor a GiteaError and reached the browser as a 500.
      text = await response.text();
    } catch (error) {
      const failure = new HttpError(502, "Gitea is unreachable", {}, { cause: error });
      failure.requestSent = response !== undefined || !isUnsentFailure(error);
      throw failure;
    }

    let payload = null;
    if (text) {
      try {
        payload = JSON.parse(text);
      } catch {
        payload = text;
      }
    }

    if (!response.ok) {
      const detail = payload?.message ?? payload ?? response.statusText;
      throw new GiteaError(response.status, scrubSecrets(detail, token));
    }
    return { payload, headers: response.headers };
  }

  return {
    async request(pathname, options) {
      return (await call(pathname, options)).payload;
    },

    // Stops on an empty page, or on a page that is shorter than the pages
    // before it and satisfies X-Total-Count. Those two are the whole set: a
    // sweep that stops advancing is a failure rather than a third terminator,
    // and raises 502 below however exactly the rows in hand match the stated
    // total. It must not stop on a short page by itself: Gitea clamps `limit`
    // to its own maximum, so a short first page is the normal shape on a
    // conservatively configured instance and used to truncate the board.
    //
    // Offset pagination overlaps whenever rows are inserted or reordered
    // mid-sweep, which is exactly what agents moving cards do, so rows are
    // counted by identity. Counting raw rows let a repeated page satisfy the
    // total while real tasks were still unfetched.
    async paginate(pathname) {
      const result = [];
      const seen = new Set();
      let trustedTotal = null;
      let stalled = 0;
      // The largest page upstream has actually delivered. Gitea clamps `limit`
      // to its own maximum, so the page size in force is discovered rather than
      // assumed, and a page no shorter than that is not evidence that the sweep
      // has reached the end of the collection.
      let observedPageSize = 0;
      const separator = pathname.includes("?") ? "&" : "?";
      for (let page = 1; page <= maxPages; page += 1) {
        const { payload, headers } = await call(
          `${pathname}${separator}page=${page}&limit=${pageLimit}`,
        );
        if (!Array.isArray(payload)) {
          throw new HttpError(502, "Gitea returned an invalid paginated response");
        }
        if (payload.length === 0) {
          return result;
        }
        const before = result.length;
        for (const item of payload) {
          const key = identityKey(item);
          if (key === null) {
            // Nothing to compare, so it cannot be proved to be a repeat. Keeping
            // it risks a duplicate; dropping it would lose a real row.
            result.push(item);
            continue;
          }
          if (!seen.has(key)) {
            seen.add(key);
            result.push(item);
          }
        }
        if (result.length === before) {
          // A page that contributed nothing new is not a sweep in progress. A
          // proxy that ignores `page` would otherwise be asked for every page
          // up to the safety limit, three times over, before the board failed.
          stalled += 1;
          if (stalled >= STALLED_PAGE_LIMIT) {
            // Unconditional, including when the rows in hand match the stated
            // total exactly. That looks like the one benign stall -- an
            // upstream clamping `page` on a collection that is an exact
            // multiple of the page size -- but a healthy upstream never
            // arrives here with that shape: the page after its last full one
            // is empty and returns above. The only upstream that can reach a
            // total-matched stall is one that is already broken, and the
            // header is the weakest evidence there is about such an upstream.
            // A narrow or stale total -- the shapes the block below documents
            // as real -- names the wrong number, and a truncating upstream
            // landing on it would have served a short board with a 200 and
            // nothing in the log.
            throw new HttpError(502, "Gitea pagination stopped advancing");
          }
        } else {
          stalled = 0;
        }
        const wasFullPage = payload.length >= observedPageSize;
        observedPageSize = Math.max(observedPageSize, payload.length);
        if (page === 1) {
          // A header claiming fewer rows than the first page already delivered
          // is not describing this collection, so it is not a terminator.
          const total = Number.parseInt(headers?.get?.("x-total-count") ?? "", 10);
          trustedTotal = Number.isSafeInteger(total) && total >= result.length ? total : null;
        }
        // The total is an upper bound, never a terminator on its own. A header
        // that describes a narrower set than the request -- an open-only count
        // answering `state=all`, or a stale cached value -- lands at or above
        // the first page's length, passes the check above, and used to end the
        // sweep on page one: the board then served part of the collection with
        // a 200 and nothing in the log. Only a page shorter than the ones
        // before it is evidence that there is nothing left to ask for.
        if (trustedTotal !== null && !wasFullPage && result.length >= trustedTotal) {
          return result;
        }
      }
      throw new HttpError(502, "Gitea pagination exceeded the safety limit");
    },
  };
}

export async function fetchBoard({ client, repository, giteaBaseUrl }) {
  const [labels, issueItems, pulls] = await Promise.all([
    client.paginate(`/repos/${repository}/labels`),
    client.paginate(`/repos/${repository}/issues?state=all`),
    client.paginate(`/repos/${repository}/pulls?state=open`),
  ]);

  const tasks = issueItems
    .filter((issue) => !isPullRequest(issue))
    .map(normalizeIssue)
    .sort((left, right) => right.number - left.number);

  const availableStateLabels = labels
    .filter((label) => STATE_LABELS.includes(label.name))
    .map((label) => label.name);

  return {
    repository,
    repositoryUrl: `${giteaBaseUrl}/${repository}`,
    generatedAt: new Date().toISOString(),
    columns: COLUMNS.map((column) => ({
      ...column,
      available: availableStateLabels.includes(column.id),
    })),
    tasks,
    summary: {
      total: tasks.length,
      open: tasks.filter((task) => task.state === "open").length,
      closed: tasks.filter((task) => task.state === "closed").length,
      active: tasks.filter((task) => task.column === "state/working").length,
      pullRequests: pulls.length,
    },
    availableStateLabels,
  };
}

export async function moveIssue({ client, repository, number, targetLabel, logger = silentLogger }) {
  if (!isValidIssueNumber(number)) {
    throw new HttpError(400, "Issue number must be a positive integer");
  }
  if (!STATE_LABELS.includes(targetLabel)) {
    throw new HttpError(400, "Target column is not valid");
  }

  const issuePath = `/repos/${repository}/issues/${number}`;
  const [issue, labels] = await Promise.all([
    client.request(issuePath),
    client.paginate(`/repos/${repository}/labels`),
  ]);

  // Before any write. A payload that is not an issue would otherwise reach the
  // label PUT with an empty label list, destroying the issue's labels, and then
  // throw a runtime error on `issue.state`.
  if (issue === null || typeof issue !== "object" || Array.isArray(issue)) {
    throw new HttpError(502, "Gitea returned an unexpected issue payload");
  }

  if (isPullRequest(issue)) {
    throw new HttpError(409, "Pull requests cannot be moved on the task board");
  }

  let targetLabelIds;
  try {
    targetLabelIds = nextLabelIds(issue, targetLabel, labels);
  } catch (error) {
    // Only the board's own authored conditions are safe to relay as text.
    // Anything else here is a defect, and a runtime message is neither a 409
    // nor something the browser is allowed to read.
    if (error instanceof BoardLabelError) {
      throw new HttpError(409, error.message, {}, { cause: error });
    }
    throw error;
  }

  const originalLabelIds = labelIds(labelList(issue));
  const targetState = targetStateFor(targetLabel);

  await client.request(`${issuePath}/labels`, { method: "PUT", body: { labels: targetLabelIds } });

  if (issue.state !== targetState) {
    try {
      await client.request(issuePath, { method: "PATCH", body: { state: targetState } });
    } catch (error) {
      logger.error(`issue #${number} state change failed`, error);

      // Roll back only a refusal that is known not to have been applied: a 4xx
      // from something that read the request, or a connection that was never
      // established. A gateway 5xx, a timeout or a dropped socket leaves the
      // outcome unknown, and rolling those back would undo a state change
      // Gitea applied while telling the operator that nothing changed.
      if (!isDefiniteRejection(error)) {
        throw new HttpError(
          502,
          `Gitea did not answer the state change for issue #${number}, so it is unknown whether the issue is now ${targetState}. The board label was left on ${targetLabel}. Check the issue in Gitea.`,
          { reverted: false, partial: true },
          { cause: error },
        );
      }

      let reverted = false;
      try {
        await client.request(`${issuePath}/labels`, {
          method: "PUT",
          body: { labels: originalLabelIds },
        });
        reverted = true;
      } catch (rollbackError) {
        logger.error(`issue #${number} label rollback failed`, rollbackError);
      }

      if (reverted) {
        throw new HttpError(
          502,
          "Gitea rejected the state change. The board label was rolled back, so nothing changed.",
          { reverted: true, partial: false },
          { cause: error },
        );
      }

      // Both writes failed to converge. The issue now carries the target label
      // without the matching state, and issueColumn would render that as a
      // normal card, so the drift has to be named instead of hidden.
      throw new HttpError(
        409,
        `Gitea rejected the state change and the label rollback also failed. Issue #${number} now carries ${targetLabel} while still ${issue.state}. Fix it in Gitea.`,
        { reverted: false, partial: true },
        { cause: error },
      );
    }
  }

  return projectMovedIssue(issue, targetLabel, labels);
}

export function validateLocalRequest(request, { allowedHosts, allowedOrigins }) {
  const host = request.headers.host ?? "";
  if (!allowedHosts.has(host.trim().toLowerCase())) {
    throw new HttpError(403, "Request host is not local");
  }

  const origin = request.headers.origin;
  if (origin === undefined) {
    return;
  }

  let parsed;
  try {
    parsed = new URL(origin);
  } catch {
    throw new HttpError(403, "Request origin is not local");
  }
  // Full origin, so a foreign scheme on a loopback authority is rejected too.
  if (!allowedOrigins.has(parsed.origin.toLowerCase())) {
    throw new HttpError(403, "Request origin is not local");
  }
}

export async function readJsonBody(request) {
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > MAX_BODY_BYTES) {
      throw new HttpError(413, "Request body is too large");
    }
    chunks.push(chunk);
  }
  const text = Buffer.concat(chunks).toString("utf8");
  if (!text) {
    return {};
  }
  try {
    return JSON.parse(text);
  } catch {
    throw new HttpError(400, "Request body is not valid JSON");
  }
}

export function resolveStaticPath(pathname, webRoot) {
  const relative = pathname === "/" ? "index.html" : pathname.slice(1);
  let decoded;
  try {
    decoded = decodeURIComponent(relative);
  } catch {
    // A malformed escape is an unknown path, not a server fault.
    return null;
  }
  if (decoded.includes("\0")) {
    return null;
  }
  const filePath = path.resolve(webRoot, decoded);
  // webRoot keeps its trailing separator, so a sibling directory whose name
  // merely starts with the root's name cannot pass this check.
  return filePath.startsWith(webRoot) ? filePath : null;
}

// Emits at most `expected` bytes and fails the stream if the source ran out
// early, so the bytes on the wire always match the Content-Length header.
export function limitToDeclaredLength(expected, onOverrun = () => {}) {
  let sent = 0;
  let dropped = 0;
  return new Transform({
    transform(chunk, _encoding, callback) {
      const room = expected - sent;
      const slice = room <= 0 ? chunk.subarray(0, 0) : chunk.subarray(0, Math.min(chunk.length, room));
      dropped += chunk.length - slice.length;
      sent += slice.length;
      callback(null, slice.length > 0 ? slice : undefined);
    },
    flush(callback) {
      if (dropped > 0) {
        // Correct on the wire, but the file is not what was served, and
        // editing an asset while a browser loads it should not be silent.
        onOverrun(dropped);
      }
      callback(
        sent === expected
          ? null
          : new Error(`static body was ${sent} bytes, header declared ${expected}`),
      );
    },
  });
}

async function serveStatic(request, response, url, { webRoot, logger }) {
  const filePath = resolveStaticPath(url.pathname, webRoot);
  if (filePath === null) {
    sendJson(response, 404, { error: "Not found" });
    return;
  }

  let handle;
  try {
    handle = await open(filePath, "r");
  } catch {
    sendJson(response, 404, { error: "Not found" });
    return;
  }

  try {
    // stat the open handle, not the path, so the declared length describes the
    // file this request opened rather than whatever the path names by the time
    // the read happens.
    const fileStat = await handle.stat();
    if (!fileStat.isFile()) {
      sendJson(response, 404, { error: "Not found" });
      return;
    }

    response.writeHead(200, {
      "Cache-Control": "no-store",
      "Content-Length": fileStat.size,
      "Content-Type": MIME_TYPES.get(path.extname(filePath).toLowerCase()) ?? "application/octet-stream",
      "X-Content-Type-Options": "nosniff",
    });
    if (request.method === "HEAD") {
      response.end();
      return;
    }

    // stat'ing the handle closes the path-to-open race, not the stat-to-read
    // one: a file that grows between the two would overrun Content-Length, and
    // on a keep-alive connection the excess is parsed as the next response.
    // The limiter makes the declared length true by construction, and turns a
    // file that shrank into a destroyed connection rather than a short body
    // that looks complete.
    //
    // pipeline propagates a read error instead of leaving it unhandled on the
    // source stream, where it would take the whole process down.
    await pipeline(
      handle.createReadStream({ autoClose: false }),
      limitToDeclaredLength(fileStat.size, (dropped) =>
        logger.warn(`${filePath} grew by ${dropped} bytes while it was being sent; the response was truncated to its declared length`),
      ),
      response,
    );
  } catch (error) {
    logger.error(`static read failed for ${filePath}`, error);
    if (!response.headersSent) {
      sendJson(response, 404, { error: "Not found" });
      return;
    }
    response.destroy();
  } finally {
    await handle.close().catch(() => {});
  }
}

function failureResponse(error) {
  if (error instanceof HttpError) {
    return { status: error.status, body: { error: error.message, ...error.extra } };
  }
  if (error instanceof GiteaError) {
    return { status: 502, body: { error: error.message } };
  }
  return { status: 500, body: { error: "Internal server error" } };
}

export function createRequestHandler({
  client,
  repository,
  giteaBaseUrl,
  webRoot,
  allowedHosts,
  allowedOrigins,
  instanceId,
  logger = silentLogger,
}) {
  return async function handle(request, response) {
    try {
      validateLocalRequest(request, { allowedHosts, allowedOrigins });
      const url = new URL(request.url ?? "/", "http://127.0.0.1");

      if (request.method === "GET" && url.pathname === "/api/instance") {
        // Identity only, no secrets. The launcher uses it to prove that the
        // listener on the port is the process it just spawned.
        sendJson(response, 200, { pid: process.pid, instance: instanceId });
        return;
      }

      if (request.method === "GET" && url.pathname === "/api/board") {
        sendJson(response, 200, await fetchBoard({ client, repository, giteaBaseUrl }));
        return;
      }

      const moveMatch = url.pathname.match(/^\/api\/issues\/(\d+)\/move$/);
      if (moveMatch) {
        if (request.method !== "POST") {
          throw new HttpError(405, "Method not allowed");
        }
        const contentType = (request.headers["content-type"] ?? "").split(";")[0].trim().toLowerCase();
        if (contentType !== "application/json") {
          throw new HttpError(415, "Content-Type must be application/json");
        }
        const body = await readJsonBody(request);
        const task = await moveIssue({
          client,
          repository,
          number: Number(moveMatch[1]),
          targetLabel: body.column,
          logger,
        });
        sendJson(response, 200, { task });
        return;
      }

      if (request.method === "GET" || request.method === "HEAD") {
        await serveStatic(request, response, url, { webRoot, logger });
        return;
      }

      throw new HttpError(405, "Method not allowed");
    } catch (error) {
      if (!(error instanceof HttpError)) {
        logger.error(`unhandled failure for ${request.method} ${request.url}`, error);
      } else if (error.status >= 500) {
        logger.error(`${request.method} ${request.url} failed`, error.cause ?? error);
      }
      if (response.headersSent) {
        response.destroy();
        return;
      }
      const { status, body } = failureResponse(error);
      sendJson(response, status, body);
    }
  };
}

export function buildAllowLists(host, port) {
  const names = [host, "localhost"];
  const hosts = new Set(names.map((name) => `${name}:${port}`.toLowerCase()));
  const origins = new Set(names.map((name) => `http://${name}:${port}`.toLowerCase()));
  if (port === 80) {
    // Browsers omit the default port from Host and Origin.
    for (const name of names) {
      hosts.add(name.toLowerCase());
      origins.add(`http://${name}`.toLowerCase());
    }
  }
  return { allowedHosts: hosts, allowedOrigins: origins };
}

export function createKanbanServer(options) {
  const { host, port } = options;
  const { allowedHosts, allowedOrigins } = buildAllowLists(host, port);
  const handler = createRequestHandler({ ...options, allowedHosts, allowedOrigins });
  const server = createServer(handler);
  // A dead socket must never take the process down.
  server.on("clientError", (error, socket) => {
    if (socket.writable) {
      socket.end("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
    }
  });
  return server;
}

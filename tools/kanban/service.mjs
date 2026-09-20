import { open } from "node:fs/promises";
import { createServer } from "node:http";
import path from "node:path";
import { pipeline } from "node:stream/promises";

import {
  COLUMNS,
  STATE_LABELS,
  isPullRequest,
  isValidIssueNumber,
  labelList,
  nextLabelIds,
  normalizeIssue,
  projectMovedIssue,
  targetStateFor,
} from "./lib.mjs";

export const PAGE_LIMIT = 50;
export const MAX_PAGES = 200;
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
// headers, and those headers carry the token.
export class GiteaError extends Error {
  constructor(status, detail) {
    super(`Gitea request failed (HTTP ${status})`);
    this.name = "GiteaError";
    this.giteaStatus = status;
    this.detail = detail;
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
    } catch (error) {
      throw new HttpError(502, "Gitea is unreachable", {}, { cause: error });
    }

    const text = await response.text();
    let payload = null;
    if (text) {
      try {
        payload = JSON.parse(text);
      } catch {
        payload = text;
      }
    }

    if (!response.ok) {
      throw new GiteaError(response.status, payload?.message ?? payload ?? response.statusText);
    }
    return { payload, headers: response.headers };
  }

  return {
    async request(pathname, options) {
      return (await call(pathname, options)).payload;
    },

    // Stops on an empty page, or as soon as X-Total-Count is satisfied. It must
    // not stop on a short page: Gitea clamps `limit` to its own maximum, so a
    // short first page is the normal shape on a conservatively configured
    // instance and used to truncate the board silently.
    async paginate(pathname) {
      const result = [];
      const separator = pathname.includes("?") ? "&" : "?";
      for (let page = 1; page <= maxPages; page += 1) {
        const { payload, headers } = await call(
          `${pathname}${separator}page=${page}&limit=${pageLimit}`,
        );
        if (!Array.isArray(payload)) {
          throw new HttpError(502, "Gitea returned an invalid paginated response");
        }
        result.push(...payload);
        if (payload.length === 0) {
          return result;
        }
        const total = Number.parseInt(headers?.get?.("x-total-count") ?? "", 10);
        if (Number.isSafeInteger(total) && result.length >= total) {
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

  if (isPullRequest(issue)) {
    throw new HttpError(409, "Pull requests cannot be moved on the task board");
  }

  let labelIds;
  try {
    labelIds = nextLabelIds(issue, targetLabel, labels);
  } catch (error) {
    throw new HttpError(409, error.message, {}, { cause: error });
  }

  const originalLabelIds = labelList(issue).map((label) => label.id);
  const targetState = targetStateFor(targetLabel);

  await client.request(`${issuePath}/labels`, { method: "PUT", body: { labels: labelIds } });

  if (issue.state !== targetState) {
    try {
      await client.request(issuePath, { method: "PATCH", body: { state: targetState } });
    } catch (error) {
      logger.error(`issue #${number} state change failed`, error);
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
    // stat the open handle, not the path, so the length that goes on the wire
    // is the length of the bytes actually being streamed.
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

    // pipeline propagates a read error instead of leaving it unhandled on the
    // source stream, where it would take the whole process down.
    await pipeline(handle.createReadStream({ autoClose: false }), response);
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

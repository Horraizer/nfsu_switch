import { createReadStream } from "node:fs";
import { stat } from "node:fs/promises";
import { createServer } from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";

import {
  COLUMNS,
  STATE_LABELS,
  isPullRequest,
  nextLabelIds,
  normalizeIssue,
} from "./lib.mjs";

const HOST = "127.0.0.1";
const PORT = Number.parseInt(process.env.KANBAN_PORT ?? "4173", 10);
const GITEA_BASE = (process.env.GITEA_BASE ?? "https://git.accad.pro").replace(/\/$/, "");
const REPOSITORY = process.env.GITEA_REPOSITORY ?? "murzintsevav/nfsu-switch";
const TOKEN = process.env.GITEA_TOKEN ?? "";
const WEB_ROOT = fileURLToPath(new URL("./web/", import.meta.url));

const MIME_TYPES = new Map([
  [".css", "text/css; charset=utf-8"],
  [".html", "text/html; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".svg", "image/svg+xml; charset=utf-8"],
]);

function sendJson(response, statusCode, payload) {
  const body = JSON.stringify(payload);
  response.writeHead(statusCode, {
    "Cache-Control": "no-store",
    "Content-Length": Buffer.byteLength(body),
    "Content-Type": "application/json; charset=utf-8",
  });
  response.end(body);
}

function errorMessage(error) {
  return error instanceof Error ? error.message : String(error);
}

function validateLocalRequest(request) {
  const host = request.headers.host ?? "";
  const allowedHosts = new Set([`${HOST}:${PORT}`, `localhost:${PORT}`]);
  if (!allowedHosts.has(host)) {
    throw new Error("Request host is not local");
  }

  const origin = request.headers.origin;
  if (origin && !allowedHosts.has(new URL(origin).host)) {
    throw new Error("Request origin is not local");
  }
}

async function readJson(request) {
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > 16_384) {
      throw new Error("Request body is too large");
    }
    chunks.push(chunk);
  }
  const text = Buffer.concat(chunks).toString("utf8");
  return text ? JSON.parse(text) : {};
}

async function gitea(pathname, options = {}) {
  if (!TOKEN) {
    throw new Error("GITEA_TOKEN is not set");
  }

  const response = await fetch(`${GITEA_BASE}/api/v1${pathname}`, {
    ...options,
    headers: {
      Accept: "application/json",
      Authorization: `token ${TOKEN}`,
      "Content-Type": "application/json",
      "User-Agent": "nfsu-local-kanban",
      ...options.headers,
    },
  });

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
    const detail = payload?.message ?? payload ?? response.statusText;
    throw new Error(`Gitea ${response.status}: ${detail}`);
  }
  return payload;
}

async function giteaPages(pathname) {
  const result = [];
  const separator = pathname.includes("?") ? "&" : "?";
  for (let page = 1; page <= 100; page += 1) {
    const chunk = await gitea(`${pathname}${separator}page=${page}&limit=50`);
    if (!Array.isArray(chunk)) {
      throw new Error("Gitea returned an invalid paginated response");
    }
    result.push(...chunk);
    if (chunk.length < 50) {
      return result;
    }
  }
  throw new Error("Gitea pagination exceeded the safety limit");
}

async function boardData() {
  const [labels, issueItems, pulls] = await Promise.all([
    giteaPages(`/repos/${REPOSITORY}/labels`),
    giteaPages(`/repos/${REPOSITORY}/issues?state=all`),
    giteaPages(`/repos/${REPOSITORY}/pulls?state=open`),
  ]);

  const tasks = issueItems
    .filter((issue) => !isPullRequest(issue))
    .map(normalizeIssue)
    .sort((left, right) => right.number - left.number);

  return {
    repository: REPOSITORY,
    repositoryUrl: `${GITEA_BASE}/${REPOSITORY}`,
    generatedAt: new Date().toISOString(),
    columns: COLUMNS,
    tasks,
    summary: {
      total: tasks.length,
      open: tasks.filter((task) => task.state === "open").length,
      closed: tasks.filter((task) => task.state === "closed").length,
      active: tasks.filter((task) => task.column === "state/working").length,
      pullRequests: pulls.length,
    },
    availableStateLabels: labels
      .filter((label) => STATE_LABELS.includes(label.name))
      .map((label) => label.name),
  };
}

async function moveIssue(number, targetLabel) {
  if (!Number.isInteger(number) || number < 1) {
    throw new Error("Issue number must be a positive integer");
  }
  if (!STATE_LABELS.includes(targetLabel)) {
    throw new Error("Target column is not valid");
  }

  const [issue, labels] = await Promise.all([
    gitea(`/repos/${REPOSITORY}/issues/${number}`),
    giteaPages(`/repos/${REPOSITORY}/labels`),
  ]);
  if (isPullRequest(issue)) {
    throw new Error("Pull requests cannot be moved on the task board");
  }

  const labelIds = nextLabelIds(issue, targetLabel, labels);
  const originalLabelIds = issue.labels.map((label) => label.id);
  const issuePath = `/repos/${REPOSITORY}/issues/${number}`;
  await gitea(`${issuePath}/labels`, {
    method: "PUT",
    body: JSON.stringify({ labels: labelIds }),
  });

  const targetState = targetLabel === "state/done" ? "closed" : "open";
  try {
    if (issue.state !== targetState) {
      await gitea(issuePath, {
        method: "PATCH",
        body: JSON.stringify({ state: targetState }),
      });
    }
  } catch (error) {
    try {
      await gitea(`${issuePath}/labels`, {
        method: "PUT",
        body: JSON.stringify({ labels: originalLabelIds }),
      });
    } catch {
      throw new Error(`${errorMessage(error)}; label rollback also failed`);
    }
    throw error;
  }

  return normalizeIssue(await gitea(issuePath));
}

async function serveStatic(request, response, url) {
  const relative = url.pathname === "/" ? "index.html" : url.pathname.slice(1);
  const decoded = decodeURIComponent(relative);
  const filePath = path.resolve(WEB_ROOT, decoded);
  if (!filePath.startsWith(WEB_ROOT)) {
    sendJson(response, 404, { error: "Not found" });
    return;
  }

  try {
    const fileStat = await stat(filePath);
    if (!fileStat.isFile()) {
      throw new Error("Not a file");
    }
    response.writeHead(200, {
      "Cache-Control": "no-store",
      "Content-Length": fileStat.size,
      "Content-Type": MIME_TYPES.get(path.extname(filePath)) ?? "application/octet-stream",
    });
    if (request.method === "HEAD") {
      response.end();
      return;
    }
    createReadStream(filePath).pipe(response);
  } catch {
    sendJson(response, 404, { error: "Not found" });
  }
}

const server = createServer(async (request, response) => {
  try {
    validateLocalRequest(request);
    const url = new URL(request.url ?? "/", `http://${HOST}:${PORT}`);

    if (request.method === "GET" && url.pathname === "/api/board") {
      sendJson(response, 200, await boardData());
      return;
    }

    const moveMatch = url.pathname.match(/^\/api\/issues\/(\d+)\/move$/);
    if (request.method === "POST" && moveMatch) {
      if (request.headers["content-type"] !== "application/json") {
        sendJson(response, 415, { error: "Content-Type must be application/json" });
        return;
      }
      const body = await readJson(request);
      const task = await moveIssue(Number(moveMatch[1]), body.column);
      sendJson(response, 200, { task });
      return;
    }

    if (request.method === "GET" || request.method === "HEAD") {
      await serveStatic(request, response, url);
      return;
    }

    sendJson(response, 405, { error: "Method not allowed" });
  } catch (error) {
    sendJson(response, 500, { error: errorMessage(error) });
  }
});

server.listen(PORT, HOST, () => {
  console.log(`NFSU Kanban: http://${HOST}:${PORT}`);
  if (!TOKEN) {
    console.warn("GITEA_TOKEN is not set; board requests will fail until it is provided.");
  }
});

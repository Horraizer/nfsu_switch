import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { appendFileSync, renameSync, unlinkSync, writeFileSync } from "node:fs";
import { mkdtemp, rm, stat, writeFile } from "node:fs/promises";
import { createServer } from "node:http";
import os from "node:os";
import net from "node:net";
import path from "node:path";
import { Readable } from "node:stream";
import { pipeline } from "node:stream/promises";
import test from "node:test";
import { fileURLToPath } from "node:url";

import {
  GiteaError,
  HttpError,
  createConsoleLogger,
  STALLED_PAGE_LIMIT,
  createGiteaClient,
  createKanbanServer,
  isDefiniteRejection,
  limitToDeclaredLength,
  moveIssue,
  resolveStaticPath,
  scrubSecrets,
  silentLogger,
} from "./service.mjs";

const WEB_ROOT = fileURLToPath(new URL("./web/", import.meta.url));
const SERVER_ENTRY = fileURLToPath(new URL("./server.mjs", import.meta.url));

// ---------------------------------------------------------------- harness ---

function listen(server, port = 0) {
  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(port, "127.0.0.1", () => resolve(server.address().port));
  });
}

function close(server) {
  return new Promise((resolve) => {
    server.closeAllConnections?.();
    server.close(() => resolve());
  });
}

async function freePort() {
  const probe = createServer();
  const port = await listen(probe);
  await close(probe);
  return port;
}

/**
 * A stand-in Gitea. Every request is recorded, and the handler decides the
 * reply, so a test can fail exactly one call in exactly one way.
 */
async function startFakeGitea(handler) {
  const calls = [];
  const server = createServer(async (request, response) => {
    let raw = "";
    for await (const chunk of request) {
      raw += chunk;
    }
    const url = new URL(request.url, "http://127.0.0.1");
    const call = {
      method: request.method,
      path: url.pathname.replace("/api/v1", ""),
      query: Object.fromEntries(url.searchParams),
      body: raw ? JSON.parse(raw) : null,
    };
    calls.push(call);

    let result;
    try {
      result = (await handler(call, calls)) ?? { status: 404, payload: { message: "unhandled" } };
    } catch (error) {
      result = { status: 500, payload: { message: String(error) } };
    }
    if (result.destroy) {
      // The request arrived and may well have been applied; the answer never
      // comes back. This is the shape that makes an outcome unknown.
      request.socket.destroy();
      return;
    }
    const text = result.text ?? JSON.stringify(result.payload ?? null);
    response.writeHead(result.status ?? 200, {
      "Content-Type": result.contentType ?? "application/json",
      "Content-Length": Buffer.byteLength(text),
      ...(result.headers ?? {}),
    });
    response.end(text);
  });
  const port = await listen(server);
  return { origin: `http://127.0.0.1:${port}`, calls, close: () => close(server) };
}

async function startKanban({
  giteaOrigin,
  token = "test-token",
  repository = "owner/repo",
  logger = silentLogger,
  webRoot = WEB_ROOT,
}) {
  const port = await freePort();
  const client = createGiteaClient({ baseUrl: giteaOrigin, token });
  const server = createKanbanServer({
    client,
    repository,
    giteaBaseUrl: giteaOrigin,
    webRoot,
    host: "127.0.0.1",
    port,
    instanceId: "test-instance",
    logger,
  });
  await listen(server, port);
  return { port, origin: `http://127.0.0.1:${port}`, close: () => close(server) };
}

function parseResponse(text) {
  const separator = text.indexOf("\r\n\r\n");
  const head = separator < 0 ? text : text.slice(0, separator);
  const body = separator < 0 ? "" : text.slice(separator + 4);
  const lines = head.split("\r\n");
  const status = Number.parseInt((lines[0] ?? "").split(" ")[1] ?? "0", 10);
  let json = null;
  try {
    json = JSON.parse(body);
  } catch {
    json = null;
  }
  return { status, head, body, json };
}

// Raw sockets, because the point of several of these tests is a header shape
// that no HTTP client will let you send.
function rawRequest(port, lines, body = "") {
  return new Promise((resolve, reject) => {
    const socket = net.connect(port, "127.0.0.1");
    let data = "";
    socket.setTimeout(10_000);
    socket.on("connect", () => socket.write(`${lines.join("\r\n")}\r\n\r\n${body}`));
    socket.on("data", (chunk) => {
      data += chunk;
    });
    socket.on("timeout", () => {
      socket.destroy();
      reject(new Error("raw request timed out"));
    });
    socket.on("error", reject);
    socket.on("close", () => resolve(parseResponse(data)));
  });
}

// ---------------------------------------------------------------- fixtures --

const LABELS = [
  { id: 10, name: "agent/claude", color: "112233" },
  { id: 12, name: "state/working", color: "1d76db" },
  { id: 13, name: "state/review", color: "aabbcc" },
  { id: 14, name: "state/done", color: "ddeeff" },
];

function openIssue(overrides = {}) {
  return {
    number: 42,
    title: "Stub issue",
    state: "open",
    html_url: "https://git.example/owner/repo/issues/42",
    updated_at: "2026-09-20T00:00:00Z",
    assignee: null,
    milestone: null,
    labels: [LABELS[0], LABELS[1]],
    ...overrides,
  };
}

function pagedReply(items, { pageSize, totalHeader = true }) {
  return (call) => {
    const page = Number.parseInt(call.query.page ?? "1", 10);
    const start = (page - 1) * pageSize;
    return {
      status: 200,
      payload: items.slice(start, start + pageSize),
      headers: totalHeader ? { "X-Total-Count": String(items.length) } : {},
    };
  };
}

async function withServers(handler, run, kanbanOptions = {}) {
  const gitea = await startFakeGitea(handler);
  const kanban = await startKanban({ giteaOrigin: gitea.origin, ...kanbanOptions });
  try {
    return await run({ gitea, kanban });
  } finally {
    await kanban.close();
    await gitea.close();
  }
}

function moveBody(column) {
  return JSON.stringify({ column });
}

function movePost(port, number, column, extraHeaders = []) {
  const body = moveBody(column);
  return rawRequest(port, [
    `POST /api/issues/${number}/move HTTP/1.1`,
    `Host: 127.0.0.1:${port}`,
    "Content-Type: application/json",
    `Content-Length: ${Buffer.byteLength(body)}`,
    ...extraHeaders,
    "Connection: close",
  ], body);
}

// ------------------------------------------------- request validation ------

test("local hosts are accepted, foreign and missing hosts are refused with 403", async () => {
  await withServers(() => ({ status: 200, payload: [] }), async ({ kanban }) => {
    const port = kanban.port;
    const accepted = [`127.0.0.1:${port}`, `localhost:${port}`, `LOCALHOST:${port}`, ` 127.0.0.1:${port} `];
    for (const host of accepted) {
      const res = await rawRequest(port, ["GET /api/instance HTTP/1.1", `Host: ${host}`, "Connection: close"]);
      assert.equal(res.status, 200, `host ${JSON.stringify(host)} should be accepted`);
    }

    const refused = [
      `evil.com:${port}`,
      `127.0.0.1:${port}.`,
      `127.1:${port}`,
      `[::1]:${port}`,
      "127.0.0.1",
      `127.0.0.1:0${port}`,
      "",
    ];
    for (const host of refused) {
      const res = await rawRequest(port, ["GET /api/instance HTTP/1.1", `Host: ${host}`, "Connection: close"]);
      assert.equal(res.status, 403, `host ${JSON.stringify(host)} should be refused`);
      assert.equal(res.json.error, "Request host is not local");
    }
  });
});

test("origin is checked as a whole origin, and a bad origin is 403 rather than 500", async () => {
  await withServers(() => ({ status: 200, payload: [] }), async ({ kanban }) => {
    const port = kanban.port;
    const probe = (origin) =>
      rawRequest(port, [
        "GET /api/instance HTTP/1.1",
        `Host: 127.0.0.1:${port}`,
        `Origin: ${origin}`,
        "Connection: close",
      ]);

    assert.equal((await probe(`http://127.0.0.1:${port}`)).status, 200);
    assert.equal((await probe(`http://localhost:${port}`)).status, 200);

    for (const origin of [
      `https://127.0.0.1:${port}`,
      `ftp://127.0.0.1:${port}`,
      "https://evil.com",
      `http://127.0.0.1:${port}@evil.com`,
      "null",
      "abc",
      "",
    ]) {
      const res = await probe(origin);
      assert.equal(res.status, 403, `origin ${JSON.stringify(origin)} should be refused`);
      assert.equal(res.json.error, "Request origin is not local");
    }
  });
});

test("the move endpoint gates method, media type, body size and body shape", async () => {
  await withServers(() => ({ status: 200, payload: [] }), async ({ kanban }) => {
    const port = kanban.port;

    const options = await rawRequest(port, ["OPTIONS /api/board HTTP/1.1", `Host: 127.0.0.1:${port}`, "Connection: close"]);
    assert.equal(options.status, 405);
    assert.equal(options.head.includes("Access-Control-Allow-Origin"), false);

    const getMove = await rawRequest(port, [`GET /api/issues/42/move HTTP/1.1`, `Host: 127.0.0.1:${port}`, "Connection: close"]);
    assert.equal(getMove.status, 405);

    for (const contentType of ["text/plain", "multipart/form-data", "application/x-www-form-urlencoded"]) {
      const res = await rawRequest(port, [
        "POST /api/issues/42/move HTTP/1.1",
        `Host: 127.0.0.1:${port}`,
        `Content-Type: ${contentType}`,
        "Content-Length: 0",
        "Connection: close",
      ]);
      assert.equal(res.status, 415, `${contentType} should be refused`);
    }

    const oversize = "x".repeat(20_000);
    const tooBig = await rawRequest(port, [
      "POST /api/issues/42/move HTTP/1.1",
      `Host: 127.0.0.1:${port}`,
      "Content-Type: application/json",
      `Content-Length: ${oversize.length}`,
      "Connection: close",
    ], oversize);
    assert.equal(tooBig.status, 413);

    const badJson = await rawRequest(port, [
      "POST /api/issues/42/move HTTP/1.1",
      `Host: 127.0.0.1:${port}`,
      "Content-Type: application/json",
      "Content-Length: 3",
      "Connection: close",
    ], "{ x");
    assert.equal(badJson.status, 400);
    // The authored text, not the parser's, which would name a byte offset.
    assert.equal(badJson.json.error, "Request body is not valid JSON");
  });
});

test("a charset parameter on the JSON media type is accepted", async () => {
  const handler = (call) => {
    if (call.method === "GET" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: openIssue() };
    }
    if (call.path === "/repos/owner/repo/labels") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    if (call.method === "PUT") {
      return { status: 200, payload: LABELS };
    }
    return { status: 200, payload: {} };
  };
  await withServers(handler, async ({ kanban }) => {
    const res = await movePost(kanban.port, 42, "state/review");
    assert.equal(res.status, 200);

    const body = moveBody("state/review");
    const withCharset = await rawRequest(kanban.port, [
      "POST /api/issues/42/move HTTP/1.1",
      `Host: 127.0.0.1:${kanban.port}`,
      "Content-Type: application/json; charset=utf-8",
      `Content-Length: ${Buffer.byteLength(body)}`,
      "Connection: close",
    ], body);
    assert.equal(withCharset.status, 200);
  });
});

test("static paths cannot escape the web root, and a malformed escape is 404 not 500", async () => {
  await withServers(() => ({ status: 200, payload: [] }), async ({ kanban }) => {
    const port = kanban.port;
    const ok = await rawRequest(port, ["GET / HTTP/1.1", `Host: 127.0.0.1:${port}`, "Connection: close"]);
    assert.equal(ok.status, 200);

    // `new URL` normalizes a dot segment before the guard is reached, so
    // "/../lib.mjs", "/%2e%2e/lib.mjs" and "/../web-secrets/f.txt" arrive as
    // ordinary paths inside the web root and 404 because no such file exists.
    // They are kept because that normalization is itself load-bearing. The
    // backslash forms are not normalized and do reach the guard, and the
    // sibling-directory escape is pinned by the unit test below.
    for (const path of [
      "/../lib.mjs",
      "/%2e%2e/lib.mjs",
      "/..%5Clib.mjs",
      "/%2e%2e%5C%2e%2e%5Cserver.mjs",
      "/C:/Windows/win.ini",
      "/%",
      "/%zz",
      "/%00index.html",
      "/../web-secrets/f.txt",
    ]) {
      const res = await rawRequest(port, [`GET ${path} HTTP/1.1`, `Host: 127.0.0.1:${port}`, "Connection: close"]);
      assert.equal(res.status, 404, `${path} should be 404, got ${res.status}`);
      assert.equal(res.body.includes("STATE_LABELS"), false);
      assert.equal(res.body.includes("createGiteaClient"), false);
    }
  });
});

test("resolveStaticPath keeps the trailing separator that blocks sibling directories", () => {
  const root = path.resolve("kanban-root", "web") + path.sep;
  // A sibling whose name merely starts with the root's name is the escape the
  // trailing separator exists to block.
  assert.equal(resolveStaticPath("/../web-secrets/f.txt", root), null);
  assert.equal(resolveStaticPath("/../webx/f.txt", root), null);
  assert.equal(resolveStaticPath("/%2e%2e/lib.mjs", root), null);
  assert.equal(resolveStaticPath("/..%5Clib.mjs", root), null);
  assert.equal(resolveStaticPath("/%", root), null, "a malformed escape resolves to nothing");
  assert.equal(resolveStaticPath("/%00x", root), null);
  assert.equal(resolveStaticPath("/", root), path.join(root, "index.html"));
  assert.equal(resolveStaticPath("/app.js", root), path.join(root, "app.js"));
  assert.ok(root.endsWith(path.sep), "the web root must keep its trailing separator");
});

// ------------------------------------------------------------ pagination ---

test("pagination does not stop on a short page when the instance clamps limit", async () => {
  const issues = Array.from({ length: 74 }, (_, index) => ({
    number: index + 1,
    title: `Issue ${index + 1}`,
    state: "open",
    html_url: `https://git.example/owner/repo/issues/${index + 1}`,
    labels: [],
  }));

  // The server asks for 50; this instance clamps every page to 30, which is
  // exactly the shape that used to end pagination after one page.
  const handler = (call) => {
    if (call.path === "/repos/owner/repo/labels") {
      return pagedReply(LABELS, { pageSize: 30 })(call);
    }
    if (call.path === "/repos/owner/repo/issues") {
      return pagedReply(issues, { pageSize: 30 })(call);
    }
    if (call.path === "/repos/owner/repo/pulls") {
      return pagedReply([], { pageSize: 30 })(call);
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ gitea, kanban }) => {
    const res = await rawRequest(kanban.port, ["GET /api/board HTTP/1.1", `Host: 127.0.0.1:${kanban.port}`, "Connection: close"]);
    assert.equal(res.status, 200);
    assert.equal(res.json.tasks.length, 74);
    assert.equal(res.json.summary.total, 74);
    const issuePages = gitea.calls.filter((call) => call.path === "/repos/owner/repo/issues");
    assert.ok(issuePages.length >= 3, `expected at least 3 pages, saw ${issuePages.length}`);
  });
});

test("pagination terminates on an empty page when no total header is supplied", async () => {
  const issues = Array.from({ length: 65 }, (_, index) => ({
    number: index + 1,
    title: `Issue ${index + 1}`,
    state: "open",
    html_url: `https://git.example/owner/repo/issues/${index + 1}`,
    labels: [],
  }));
  const handler = (call) => {
    if (call.path === "/repos/owner/repo/labels") {
      return pagedReply(LABELS, { pageSize: 50, totalHeader: false })(call);
    }
    if (call.path === "/repos/owner/repo/issues") {
      return pagedReply(issues, { pageSize: 50, totalHeader: false })(call);
    }
    if (call.path === "/repos/owner/repo/pulls") {
      return pagedReply([], { pageSize: 50, totalHeader: false })(call);
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ kanban }) => {
    const res = await rawRequest(kanban.port, ["GET /api/board HTTP/1.1", `Host: 127.0.0.1:${kanban.port}`, "Connection: close"]);
    assert.equal(res.status, 200);
    assert.equal(res.json.tasks.length, 65);
  });
});

test("a page count that exceeds the safety limit fails instead of looping", async () => {
  const client = createGiteaClient({
    baseUrl: "http://127.0.0.1:1",
    token: "t",
    maxPages: 2,
    fetchImpl: async () =>
      new Response(JSON.stringify([{ number: 1 }]), {
        status: 200,
        headers: { "Content-Type": "application/json" },
      }),
  });
  await assert.rejects(() => client.paginate("/repos/owner/repo/issues"), /pagination exceeded/);
});

test("a non-array paginated payload is rejected", async () => {
  const client = createGiteaClient({
    baseUrl: "http://127.0.0.1:1",
    token: "t",
    fetchImpl: async () =>
      new Response(JSON.stringify({ message: "not a list" }), {
        status: 200,
        headers: { "Content-Type": "application/json" },
      }),
  });
  await assert.rejects(() => client.paginate("/repos/owner/repo/issues"), /invalid paginated response/);
});

// ------------------------------------------------- move and rollback -------

test("an accepted move answers from projected state and never re-reads the issue", async () => {
  const handler = (call) => {
    if (call.method === "GET" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: openIssue() };
    }
    if (call.path === "/repos/owner/repo/labels" && call.method === "GET") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    if (call.method === "PUT" && call.path === "/repos/owner/repo/issues/42/labels") {
      return { status: 200, payload: LABELS };
    }
    if (call.method === "PATCH" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: { ...openIssue(), state: "closed" } };
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ gitea, kanban }) => {
    const res = await movePost(kanban.port, 42, "state/done");
    assert.equal(res.status, 200);
    assert.equal(res.json.task.column, "state/done");
    assert.equal(res.json.task.state, "closed");
    assert.deepEqual(res.json.task.labels.map((label) => label.name), ["agent/claude", "state/done"]);

    const patchIndex = gitea.calls.findIndex((call) => call.method === "PATCH");
    assert.ok(patchIndex >= 0, "the state change should have been issued");
    // Nothing after the last mutation: a read-back that failed here used to
    // turn an applied move into a reported failure.
    const after = gitea.calls.slice(patchIndex + 1);
    assert.deepEqual(after, [], `no call should follow the state change, saw ${JSON.stringify(after)}`);
  });
});

test("a move that needs no state change does not issue a state write", async () => {
  const handler = (call) => {
    if (call.method === "GET" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: openIssue() };
    }
    if (call.path === "/repos/owner/repo/labels" && call.method === "GET") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    if (call.method === "PUT") {
      return { status: 200, payload: LABELS };
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ gitea, kanban }) => {
    const res = await movePost(kanban.port, 42, "state/review");
    assert.equal(res.status, 200);
    assert.equal(res.json.task.column, "state/review");
    assert.equal(res.json.task.state, "open");
    assert.equal(gitea.calls.some((call) => call.method === "PATCH"), false);
  });
});

test("a failed state change rolls the label back and reports that nothing changed", async () => {
  const handler = (call) => {
    if (call.method === "GET" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: openIssue() };
    }
    if (call.path === "/repos/owner/repo/labels" && call.method === "GET") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    if (call.method === "PUT" && call.path === "/repos/owner/repo/issues/42/labels") {
      return { status: 200, payload: LABELS };
    }
    if (call.method === "PATCH") {
      return { status: 403, payload: { message: "state change forbidden" } };
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ gitea, kanban }) => {
    const res = await movePost(kanban.port, 42, "state/done");
    assert.equal(res.status, 502);
    assert.equal(res.json.reverted, true);
    assert.equal(res.json.partial, false);

    const puts = gitea.calls.filter((call) => call.method === "PUT");
    assert.equal(puts.length, 2, "the label write and its rollback");
    assert.deepEqual(puts[0].body.labels, [10, 14]);
    assert.deepEqual(puts[1].body.labels, [10, 12], "rollback restores the original label ids");
  });
});

test("a failed rollback is reported as drift rather than hidden", async () => {
  const handler = (call, calls) => {
    if (call.method === "GET" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: openIssue() };
    }
    if (call.path === "/repos/owner/repo/labels" && call.method === "GET") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    if (call.method === "PUT" && call.path === "/repos/owner/repo/issues/42/labels") {
      const puts = calls.filter((entry) => entry.method === "PUT");
      // First PUT applies the label; the rollback PUT fails.
      return puts.length === 1
        ? { status: 200, payload: LABELS }
        : { status: 500, payload: { message: "rollback refused" } };
    }
    if (call.method === "PATCH") {
      return { status: 403, payload: { message: "state change forbidden" } };
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ kanban }) => {
    const res = await movePost(kanban.port, 42, "state/done");
    assert.equal(res.status, 409);
    assert.equal(res.json.partial, true);
    assert.equal(res.json.reverted, false);
    assert.match(res.json.error, /rollback also failed/);
    assert.match(res.json.error, /#42/);
    assert.match(res.json.error, /state\/done/);
    assert.match(res.json.error, /still open/);
  });
});

test("an issue whose label set is null is moved without crashing", async () => {
  const handler = (call) => {
    if (call.method === "GET" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: openIssue({ labels: null }) };
    }
    if (call.path === "/repos/owner/repo/labels" && call.method === "GET") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    if (call.method === "PUT") {
      return { status: 200, payload: LABELS };
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ gitea, kanban }) => {
    const res = await movePost(kanban.port, 42, "state/review");
    assert.equal(res.status, 200);
    const put = gitea.calls.find((call) => call.method === "PUT");
    assert.deepEqual(put.body.labels, [13]);
  });
});

test("invalid move inputs are refused before any write", async () => {
  const handler = (call) => {
    if (call.method === "GET" && call.path.startsWith("/repos/owner/repo/issues/")) {
      return { status: 200, payload: openIssue() };
    }
    if (call.path === "/repos/owner/repo/labels" && call.method === "GET") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    return { status: 500, payload: { message: "should not be reached" } };
  };

  await withServers(handler, async ({ gitea, kanban }) => {
    const badColumn = await movePost(kanban.port, 42, "state/nope");
    assert.equal(badColumn.status, 400);
    assert.equal(badColumn.json.error, "Target column is not valid");

    const missingColumn = await movePost(kanban.port, 42, undefined);
    assert.equal(missingColumn.status, 400);

    const huge = await movePost(kanban.port, 99999999999999999999, "state/review");
    assert.equal(huge.status, 400);
    assert.equal(huge.json.error, "Issue number must be a positive integer");

    assert.equal(gitea.calls.some((call) => call.method === "PUT" || call.method === "PATCH"), false);
  });
});

test("a pull request cannot be moved, and a missing label is named", async () => {
  const asPull = (call) => {
    if (call.method === "GET" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: openIssue({ html_url: "https://git.example/owner/repo/pulls/42" }) };
    }
    if (call.path === "/repos/owner/repo/labels" && call.method === "GET") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    return { status: 404, payload: { message: "unhandled" } };
  };
  await withServers(asPull, async ({ kanban }) => {
    const res = await movePost(kanban.port, 42, "state/review");
    assert.equal(res.status, 409);
    assert.match(res.json.error, /Pull requests cannot be moved/);
  });

  const withoutLabel = (call) => {
    if (call.method === "GET" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: openIssue() };
    }
    if (call.path === "/repos/owner/repo/labels" && call.method === "GET") {
      return pagedReply([LABELS[0], LABELS[1]], { pageSize: 50 })(call);
    }
    return { status: 404, payload: { message: "unhandled" } };
  };
  await withServers(withoutLabel, async ({ gitea, kanban }) => {
    const res = await movePost(kanban.port, 42, "state/done");
    assert.equal(res.status, 409);
    assert.match(res.json.error, /Missing Gitea label: state\/done/);
    assert.equal(gitea.calls.some((call) => call.method === "PUT"), false);
  });
});

// ------------------------------------------------------- token secrecy -----

test("upstream error text never reaches the client", async () => {
  const SECRET = "SUPERSECRET-TOKEN-abc123";
  const echoing = () => ({
    status: 401,
    payload: { message: `unauthorized; you sent header: token ${SECRET}` },
  });

  await withServers(echoing, async ({ kanban }) => {
    const board = await rawRequest(kanban.port, ["GET /api/board HTTP/1.1", `Host: 127.0.0.1:${kanban.port}`, "Connection: close"]);
    assert.equal(board.status, 502);
    assert.equal(board.json.error, "Gitea request failed (HTTP 401)");
    assert.equal(board.body.includes(SECRET), false, "the upstream body must not be relayed");
  });

  const echoingHtml = () => ({
    status: 500,
    text: `<html><body>proxy error, Authorization: token ${SECRET}</body></html>`,
    contentType: "text/html",
  });
  await withServers(echoingHtml, async ({ kanban }) => {
    const board = await rawRequest(kanban.port, ["GET /api/board HTTP/1.1", `Host: 127.0.0.1:${kanban.port}`, "Connection: close"]);
    assert.equal(board.status, 502);
    assert.equal(board.body.includes(SECRET), false, "a non-JSON upstream body must not be relayed either");
  });
});

test("an unreachable Gitea is a 502 without internal detail", async () => {
  const port = await freePort();
  const client = createGiteaClient({ baseUrl: `http://127.0.0.1:${port}`, token: "t" });
  await assert.rejects(
    () => client.request("/repos/owner/repo/labels"),
    (error) => error instanceof HttpError && error.status === 502 && error.message === "Gitea is unreachable",
  );
});

test("a missing token fails closed without contacting Gitea", async () => {
  const client = createGiteaClient({
    baseUrl: "http://127.0.0.1:1",
    token: "",
    fetchImpl: () => assert.fail("fetch must not be called without a token"),
  });
  await assert.rejects(
    () => client.request("/repos/owner/repo/labels"),
    (error) => error instanceof HttpError && error.status === 503,
  );
});

// ------------------------------------------------------ board projection ---

test("the board marks columns whose Gitea label is missing as unavailable", async () => {
  const partialLabels = [LABELS[0], LABELS[1], LABELS[3]];
  const handler = (call) => {
    if (call.path === "/repos/owner/repo/labels") {
      return pagedReply(partialLabels, { pageSize: 50 })(call);
    }
    if (call.path === "/repos/owner/repo/issues") {
      return pagedReply([openIssue()], { pageSize: 50 })(call);
    }
    if (call.path === "/repos/owner/repo/pulls") {
      return pagedReply([], { pageSize: 50 })(call);
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ kanban }) => {
    const res = await rawRequest(kanban.port, ["GET /api/board HTTP/1.1", `Host: 127.0.0.1:${kanban.port}`, "Connection: close"]);
    assert.equal(res.status, 200);
    const byId = new Map(res.json.columns.map((column) => [column.id, column.available]));
    assert.equal(byId.get("state/working"), true);
    assert.equal(byId.get("state/done"), true);
    assert.equal(byId.get("state/review"), false);
    assert.equal(byId.get("state/ready"), false);
    assert.deepEqual(res.json.availableStateLabels.sort(), ["state/done", "state/working"]);
  });
});

test("pull requests are counted but never rendered as tasks", async () => {
  const handler = (call) => {
    if (call.path === "/repos/owner/repo/labels") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    if (call.path === "/repos/owner/repo/issues") {
      return pagedReply([
        openIssue(),
        { number: 74, title: "A PR", state: "open", html_url: "https://git.example/owner/repo/pulls/74", labels: [], pull_request: {} },
      ], { pageSize: 50 })(call);
    }
    if (call.path === "/repos/owner/repo/pulls") {
      return pagedReply([{ number: 74 }, { number: 75 }], { pageSize: 50 })(call);
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ kanban }) => {
    const res = await rawRequest(kanban.port, ["GET /api/board HTTP/1.1", `Host: 127.0.0.1:${kanban.port}`, "Connection: close"]);
    assert.equal(res.json.tasks.length, 1);
    assert.equal(res.json.tasks[0].number, 42);
    assert.equal(res.json.summary.pullRequests, 2);
  });
});

// ------------------------------------------------------ process lifetime ---

test("the server exits when the launcher that owns it disappears", { timeout: 60_000 }, async () => {
  const port = await freePort();
  // A stand-in launcher that does nothing but exist, so killing it is the only
  // signal the server gets.
  const parent = spawn(process.execPath, ["-e", "setTimeout(() => {}, 120000)"], { stdio: "ignore" });
  const server = spawn(process.execPath, [SERVER_ENTRY], {
    stdio: ["ignore", "pipe", "pipe"],
    env: {
      ...process.env,
      KANBAN_PORT: String(port),
      KANBAN_PARENT_PID: String(parent.pid),
      KANBAN_INSTANCE: "lifetime-test",
      GITEA_TOKEN: "fake-token-do-not-use",
      GITEA_BASE: "http://127.0.0.1:1",
    },
  });

  const exited = new Promise((resolve) => server.once("exit", (code) => resolve(code)));
  try {
    await waitForInstance(port, "lifetime-test");
    parent.kill();
    // The losing side of this race must be cleared. An uncleared 30 second
    // timer held the event loop open long after the assertions were done and
    // made a mandated gate run twenty times longer than its own work.
    let timer = null;
    const code = await Promise.race([
      exited,
      new Promise((resolve) => {
        timer = setTimeout(() => resolve("timeout"), 30_000);
      }),
    ]);
    clearTimeout(timer);
    assert.equal(code, 0, "the orphaned server should shut itself down");
  } finally {
    parent.kill();
    if (server.exitCode === null) {
      server.kill();
    }
  }
});

async function waitForInstance(port, expected) {
  for (let attempt = 0; attempt < 200; attempt += 1) {
    try {
      const res = await rawRequest(port, ["GET /api/instance HTTP/1.1", `Host: 127.0.0.1:${port}`, "Connection: close"]);
      if (res.status === 200 && res.json?.instance === expected) {
        return res.json;
      }
    } catch {
      // not listening yet
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`server did not report instance ${expected} on port ${port}`);
}

test("the instance endpoint identifies the process that owns the port", async () => {
  await withServers(() => ({ status: 200, payload: [] }), async ({ kanban }) => {
    const res = await rawRequest(kanban.port, ["GET /api/instance HTTP/1.1", `Host: 127.0.0.1:${kanban.port}`, "Connection: close"]);
    assert.equal(res.status, 200);
    assert.equal(res.json.instance, "test-instance");
    assert.equal(res.json.pid, process.pid);
  });
});

// ------------------------------------------------------ malformed labels ---

test("a label array with malformed elements neither crashes nor writes a null id", async () => {
  const brokenLabels = [null, LABELS[0], "state/working", 7, LABELS[1]];
  const handler = (call) => {
    if (call.method === "GET" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: openIssue({ labels: brokenLabels }) };
    }
    if (call.path === "/repos/owner/repo/labels" && call.method === "GET") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    if (call.path === "/repos/owner/repo/issues") {
      return pagedReply([openIssue({ labels: brokenLabels })], { pageSize: 50 })(call);
    }
    if (call.path === "/repos/owner/repo/pulls") {
      return pagedReply([], { pageSize: 50 })(call);
    }
    if (call.method === "PUT") {
      return { status: 200, payload: LABELS };
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ gitea, kanban }) => {
    const board = await rawRequest(kanban.port, ["GET /api/board HTTP/1.1", `Host: 127.0.0.1:${kanban.port}`, "Connection: close"]);
    assert.equal(board.status, 200, "a malformed label element must not become a 500");
    assert.equal(board.json.tasks.length, 1);
    // A label with no name cannot be rendered, and the client reads .name.
    assert.deepEqual(board.json.tasks[0].labels.map((label) => label.name), ["agent/claude", "state/working"]);

    const moved = await movePost(kanban.port, 42, "state/review");
    assert.equal(moved.status, 200, `expected an accepted move, got ${moved.body}`);

    const put = gitea.calls.find((call) => call.method === "PUT");
    assert.deepEqual(put.body.labels, [10, 13], "only real label ids may be written back");
  });
});

// ------------------------------------------------- unknown move outcomes ---

test("an unanswered state change is reported as unknown instead of rolled back", async () => {
  const handler = (call) => {
    if (call.method === "GET" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: openIssue() };
    }
    if (call.path === "/repos/owner/repo/labels" && call.method === "GET") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    if (call.method === "PUT" && call.path === "/repos/owner/repo/issues/42/labels") {
      return { status: 200, payload: LABELS };
    }
    if (call.method === "PATCH") {
      // Gitea may well have applied this; the answer just never arrives.
      return { destroy: true };
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ gitea, kanban }) => {
    const res = await movePost(kanban.port, 42, "state/done");
    assert.equal(res.status, 502);
    assert.equal(res.json.partial, true, "an unknown outcome must not be reported as a clean failure");
    assert.equal(res.json.reverted, false);
    assert.match(res.json.error, /unknown whether/i);
    assert.match(res.json.error, /#42/);

    const puts = gitea.calls.filter((call) => call.method === "PUT");
    assert.equal(puts.length, 1, "rolling back an outcome Gitea may have applied creates the inverse drift");
  });
});

test("a refused state change is still rolled back, so the two outcomes stay distinct", async () => {
  const handler = (call) => {
    if (call.method === "GET" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: openIssue() };
    }
    if (call.path === "/repos/owner/repo/labels" && call.method === "GET") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    if (call.method === "PUT") {
      return { status: 200, payload: LABELS };
    }
    if (call.method === "PATCH") {
      return { status: 403, payload: { message: "state change forbidden" } };
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ gitea, kanban }) => {
    const res = await movePost(kanban.port, 42, "state/done");
    assert.equal(res.status, 502);
    assert.equal(res.json.reverted, true);
    assert.equal(res.json.partial, false);
    assert.equal(gitea.calls.filter((call) => call.method === "PUT").length, 2);
  });
});

// -------------------------------------------------- token secrecy, again ---

test("the token reaches neither the response nor the log", async () => {
  const CANARY = "gta_review_canary_0123456789abcdef0123456789abcd";
  const echoing = () => ({
    status: 502,
    text: `<html>proxy error. Upstream received: Authorization: token ${CANARY}</html>`,
    contentType: "text/html",
    headers: { "X-Echoed-Authorization": `token ${CANARY}` },
  });

  const logged = [];
  const record = (message) => logged.push(String(message));
  const logger = createConsoleLogger({ log: record, warn: record, error: record });

  await withServers(
    echoing,
    async ({ kanban }) => {
      const res = await rawRequest(kanban.port, ["GET /api/board HTTP/1.1", `Host: 127.0.0.1:${kanban.port}`, "Connection: close"]);
      assert.equal(res.status, 502);
      assert.equal(res.json.error, "Gitea request failed (HTTP 502)");
      // Head and body both, so a leak into a response header is caught too.
      assert.equal(res.head.includes(CANARY), false, "the token must not appear in any response header");
      assert.equal(res.body.includes(CANARY), false, "the token must not appear in the response body");
    },
    { token: CANARY, logger },
  );

  assert.ok(logged.length > 0, "the upstream failure should have been logged");
  const transcript = logged.join("\n");
  assert.equal(transcript.includes(CANARY), false, `the token leaked to the log: ${transcript}`);
  assert.match(transcript, /\[redacted\]/);
});

test("scrubSecrets removes the credential from every string it can reach", () => {
  const token = "gta_secret_value_0123456789abcdef";
  // The literal value goes wherever it appears, with or without a header shape.
  assert.equal(scrubSecrets(`sent: token ${token}`, token), "sent: token [redacted]");
  assert.equal(scrubSecrets(`bare ${token} here`, token), "bare [redacted] here");
  // A credential this process never held is still scrubbed by its header shape.
  assert.equal(
    scrubSecrets("Authorization: token someone-elses-secret", token),
    "Authorization: token [redacted]",
  );
  assert.deepEqual(
    scrubSecrets({ message: `Authorization: token ${token}`, nested: [`bare ${token}`] }, token),
    { message: "Authorization: token [redacted]", nested: ["bare [redacted]"] },
  );
  // An empty token must not turn every character into a redaction.
  assert.equal(scrubSecrets("nothing to hide", ""), "nothing to hide");
  assert.equal(scrubSecrets(42, token), 42);
  assert.equal(scrubSecrets(null, token), null);
});

test("a percent-encoded token is scrubbed as well as the literal one", () => {
  // A proxy that echoes the request line sends the credential back inside a
  // query string, where it arrives percent-encoded. A literal comparison walks
  // straight past that and the token reaches the local log whole.
  const token = "gta_CANARY_0123456789abcdefABCDEF0123456789";
  const encoded = "gta%5FCANARY%5F0123456789abcdefABCDEF0123456789";
  const scrubbed = scrubSecrets({ detail: `proxy: /x?access_token=${encoded}`, nested: [encoded] }, token);
  assert.equal(scrubbed.detail.includes("CANARY"), false, `the encoded token survived: ${scrubbed.detail}`);
  assert.equal(scrubbed.detail, "proxy: /x?access_token=[redacted]");
  assert.equal(scrubbed.nested[0], "[redacted]");
  // Lower and upper case escapes, and a mixture of encoded and literal
  // characters, are all the same credential.
  assert.equal(scrubSecrets("gta%5fCANARY%5F0123456789abcdefABCDEF0123456789", token), "[redacted]");
  assert.equal(scrubSecrets("%67ta_CANARY_0123456789abcdefABCDEF0123456789", token), "[redacted]");
  // The literal form still goes, and an unrelated escape is left alone.
  assert.equal(scrubSecrets(`bare ${token} here`, token), "bare [redacted] here");
  assert.equal(scrubSecrets("a%20b", token), "a%20b");
});

test("a token whose own characters are percent signs does not wedge the scrub", () => {
  // Every token character used to become `(?:literal|%hh)`, and `%` is the one
  // character whose literal form and whose encoding, `%25`, can both begin at
  // the same position. Against text that never matches, the engine explored
  // both at every one of them: 24 percent signs took 2.3 s on the machine this
  // was written on, and each further pair cost four times that again. The
  // token is read from the environment, so a malformed one must not be able to
  // hang the server process on its first upstream error.
  const token = `${"%".repeat(26)}Z`;
  const text = "%25".repeat(40);
  const started = process.hrtime.bigint();
  const scrubbed = scrubSecrets(text, token);
  const elapsedMs = Number(process.hrtime.bigint() - started) / 1e6;
  assert.ok(
    elapsedMs < 1000,
    `scrubbing a ${token.length}-character token took ${elapsedMs.toFixed(1)} ms`,
  );
  // Nothing in that text is the token, so it must come back untouched.
  assert.equal(scrubbed, text);

  // A `%` in the token is still matched in both of its forms, which is the
  // behaviour the pattern existed for.
  assert.equal(scrubSecrets("a%25b", "a%b"), "[redacted]");
  assert.equal(scrubSecrets("a%b", "a%b"), "[redacted]");
  // And a token holding what looks like an escape of its own is still found
  // literally, which a decode-only comparison would walk straight past.
  assert.equal(scrubSecrets("x a%2cb y", "a%2cb"), "x [redacted] y");
});

// ------------------------------------------------------- content length ----

test("the declared content length is what the response actually delivers", async () => {
  await withServers(() => ({ status: 200, payload: [] }), async ({ kanban }) => {
    for (const asset of ["/", "/app.js", "/styles.css", "/shared.mjs"]) {
      const res = await rawRequest(kanban.port, [`GET ${asset} HTTP/1.1`, `Host: 127.0.0.1:${kanban.port}`, "Connection: close"]);
      assert.equal(res.status, 200, `${asset} should be served`);
      const declared = Number.parseInt(/content-length: (\d+)/i.exec(res.head)?.[1] ?? "-1", 10);
      assert.equal(Buffer.byteLength(res.body, "utf8"), declared, `${asset} delivered a body of a different length`);
    }
  });
});

test("the length limiter truncates an overrun and fails a short read", async () => {
  const collect = async (chunks, expected) => {
    const seen = [];
    await pipeline(
      Readable.from(chunks),
      limitToDeclaredLength(expected),
      async function (stream) {
        for await (const chunk of stream) {
          seen.push(chunk);
        }
      },
    );
    return Buffer.concat(seen);
  };

  // A file that grew after the header went out must not spill into the next
  // response on a keep-alive connection.
  const truncated = await collect([Buffer.from("AAAA"), Buffer.from("BBBB")], 4);
  assert.equal(truncated.toString(), "AAAA");

  const exact = await collect([Buffer.from("AAAA"), Buffer.from("BB")], 6);
  assert.equal(exact.toString(), "AAAABB");

  // A file that shrank is a destroyed connection, not a short body that looks
  // complete.
  await assert.rejects(
    () => collect([Buffer.from("AAA")], 6),
    /static body was 3 bytes, header declared 6/,
  );
});

// ------------------------------------------------- pagination robustness ---

function stubbedClient({ pages, total, maxPages = 6 }) {
  const requested = [];
  return {
    requested,
    client: createGiteaClient({
      baseUrl: "http://127.0.0.1:1",
      token: "t",
      maxPages,
      fetchImpl: async (url) => {
        const page = Number.parseInt(new URL(url).searchParams.get("page") ?? "1", 10);
        requested.push(page);
        return new Response(JSON.stringify(pages(page)), {
          status: 200,
          headers: {
            "Content-Type": "application/json",
            ...(total === null ? {} : { "X-Total-Count": String(total) }),
          },
        });
      },
    }),
  };
}

test("a total that is smaller than the first page is not trusted as a terminator", async () => {
  const rows = Array.from({ length: 74 }, (_, index) => ({ id: index + 1, number: index + 1 }));
  for (const total of ["1", "0", "-5", "1e3", "not-a-number"]) {
    const { client } = stubbedClient({
      pages: (page) => rows.slice((page - 1) * 30, page * 30),
      total,
    });
    const collected = await client.paginate("/repos/owner/repo/issues");
    assert.equal(collected.length, 74, `X-Total-Count: ${total} silently truncated the sweep`);
  }
});

test("a total at or above the first page is not a terminator while the pages stay full", async () => {
  // The header describes a narrower set than the request: an open-only count
  // answering `state=all`, or a stale cached one. It clears the "smaller than
  // the first page" check above, and trusting it ended the sweep on page one --
  // 50 rows of 120 served as the whole collection, with a 200 and nothing said.
  const rows = Array.from({ length: 120 }, (_, index) => ({ id: index + 1, number: index + 1 }));
  const { client } = stubbedClient({
    pages: (page) => rows.slice((page - 1) * 50, page * 50),
    total: "50",
    maxPages: 10,
  });
  const collected = await client.paginate("/repos/owner/repo/issues?state=all");
  assert.equal(collected.length, 120, "X-Total-Count: 50 silently truncated a 120-row sweep");
  assert.equal(new Set(collected.map((row) => row.id)).size, 120, "every row should be present exactly once");
});

test("overlapping pages are counted by identity, so the sweep cannot end early", async () => {
  const rows = Array.from({ length: 74 }, (_, index) => ({ id: index + 1, number: index + 1 }));
  // Offset pagination overlaps whenever rows move mid-sweep, which is what
  // agents dragging cards do. Page 2 repeats page 1 and page 3 resumes.
  const shape = (page) => {
    if (page === 1 || page === 2) {
      return rows.slice(0, 30);
    }
    return rows.slice((page - 2) * 30, (page - 1) * 30);
  };
  const { client } = stubbedClient({ pages: shape, total: "74", maxPages: 10 });
  const collected = await client.paginate("/repos/owner/repo/issues");
  assert.equal(collected.length, 74);
  assert.equal(new Set(collected.map((row) => row.id)).size, 74, "every row should be present exactly once");
});

test("a page that never advances fails loudly instead of truncating or hammering", async () => {
  const rows = Array.from({ length: 30 }, (_, index) => ({ id: index + 1, number: index + 1 }));
  const { client, requested } = stubbedClient({ pages: () => rows, total: "74", maxPages: 200 });
  await assert.rejects(() => client.paginate("/repos/owner/repo/issues"), /stopped advancing/);
  // Without the stall check this shape asked for every page up to the safety
  // limit, on each of the three endpoints the board loads.
  // The first page genuinely advanced; the next three added nothing.
  assert.equal(requested.length, STALLED_PAGE_LIMIT + 1, "a stuck sweep must not run to the safety limit");
});

test("a sweep that stops advancing on exactly the stated total is complete, not a failure", async () => {
  // A collection that is an exact multiple of the page size never produces a
  // short page, so the "shorter than the pages before it" terminator cannot
  // fire, and an upstream that ignores or clamps `page` then repeats its last
  // page forever. The stall detector sees that, and at that point the total
  // the first response stated is the evidence in hand: exactly that many
  // distinct rows have been collected, so the sweep is finished. Hard-failing
  // a board that is demonstrably complete is the regression this closes.
  const rows = Array.from({ length: 100 }, (_, index) => ({ id: index + 1, number: index + 1 }));
  // Clamps `page` to the last page it has, which is what a proxy that caps an
  // offset does.
  const clamped = (page) => rows.slice((Math.min(page, 2) - 1) * 50, Math.min(page, 2) * 50);
  const { client, requested } = stubbedClient({ pages: clamped, total: "100", maxPages: 200 });
  const collected = await client.paginate("/repos/owner/repo/issues");
  assert.equal(collected.length, 100, "a complete collection was reported as a pagination failure");
  assert.equal(new Set(collected.map((row) => row.id)).size, 100, "every row should be present exactly once");
  assert.equal(
    requested.length,
    2 + STALLED_PAGE_LIMIT,
    "the sweep must stop as soon as the pages stop advancing, not run to the safety limit",
  );

  // The same shape from an upstream that ignores `page` outright: one
  // collection that fills exactly one page, and one that does not fill it.
  for (const size of [50, 30]) {
    const fixed = Array.from({ length: size }, (_, index) => ({ id: index + 1, number: index + 1 }));
    const single = stubbedClient({ pages: () => fixed, total: String(size), maxPages: 200 });
    const swept = await single.client.paginate("/repos/owner/repo/issues");
    assert.equal(swept.length, size, `${size} rows behind an upstream that ignores page were rejected`);
  }
});

test("a sweep that stops advancing short of the stated total still fails", async () => {
  // The other side of the case above, and the reason it is safe. The pages
  // stopped advancing here too, but the rows in hand do not account for the
  // collection the first response described, so this is an upstream that is
  // truncating and the board must not be served as though it were whole.
  const rows = Array.from({ length: 50 }, (_, index) => ({ id: index + 1, number: index + 1 }));
  const { client } = stubbedClient({ pages: () => rows, total: "120", maxPages: 200 });
  await assert.rejects(() => client.paginate("/repos/owner/repo/issues"), /stopped advancing/);

  // With no total at all there is no evidence either way, so a stall stays a
  // loud failure rather than becoming a silent truncation.
  const { client: headerless } = stubbedClient({ pages: () => rows, total: null, maxPages: 200 });
  await assert.rejects(() => headerless.paginate("/repos/owner/repo/issues"), /stopped advancing/);
});

test("a collection that is an exact multiple of the page size is served as a board", async () => {
  // The end-to-end shape of the case above: 100 issues at a page size of 50,
  // behind an upstream that clamps `page` to the last page it has. Every page
  // is full, so nothing but the total can end the sweep, and answering the
  // board with a 502 is worse than answering it with the collection.
  const issues = Array.from({ length: 100 }, (_, index) => ({
    id: 1000 + index,
    number: index + 1,
    title: `Issue ${index + 1}`,
    state: "open",
    html_url: `https://git.example/owner/repo/issues/${index + 1}`,
    labels: [],
  }));
  const handler = (call) => {
    if (call.path === "/repos/owner/repo/labels") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    if (call.path === "/repos/owner/repo/pulls") {
      return pagedReply([], { pageSize: 50 })(call);
    }
    if (call.path === "/repos/owner/repo/issues") {
      const page = Math.min(Number.parseInt(call.query.page ?? "1", 10), 2);
      return {
        status: 200,
        payload: issues.slice((page - 1) * 50, page * 50),
        headers: { "X-Total-Count": "100" },
      };
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ kanban }) => {
    const res = await rawRequest(kanban.port, ["GET /api/board HTTP/1.1", `Host: 127.0.0.1:${kanban.port}`, "Connection: close"]);
    assert.equal(res.status, 200, "a complete board was refused as a pagination failure");
    assert.equal(res.json.tasks.length, 100);
    assert.equal(new Set(res.json.tasks.map((task) => task.number)).size, 100);
  });
});

test("overlap that eventually resolves is tolerated rather than called a stall", async () => {
  const rows = Array.from({ length: 74 }, (_, index) => ({ id: index + 1, number: index + 1 }));
  // Two consecutive repeats, which is within the tolerance, then progress.
  const shape = (page) => {
    if (page <= 3) {
      return rows.slice(0, 30);
    }
    return rows.slice((page - 3) * 30, (page - 2) * 30);
  };
  const { client } = stubbedClient({ pages: shape, total: "74", maxPages: 20 });
  const collected = await client.paginate("/repos/owner/repo/issues");
  assert.equal(collected.length, 74);
});

test("a duplicated page in a real board request does not hide tasks", async () => {
  const issues = Array.from({ length: 40 }, (_, index) => ({
    id: 1000 + index,
    number: index + 1,
    title: `Issue ${index + 1}`,
    state: "open",
    html_url: `https://git.example/owner/repo/issues/${index + 1}`,
    labels: [],
  }));
  const handler = (call) => {
    if (call.path === "/repos/owner/repo/labels") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    if (call.path === "/repos/owner/repo/pulls") {
      return pagedReply([], { pageSize: 50 })(call);
    }
    if (call.path === "/repos/owner/repo/issues") {
      const page = Number.parseInt(call.query.page ?? "1", 10);
      const slice = page <= 2 ? issues.slice(0, 20) : issues.slice((page - 2) * 20, (page - 1) * 20);
      return { status: 200, payload: slice, headers: { "X-Total-Count": "40" } };
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ kanban }) => {
    const res = await rawRequest(kanban.port, ["GET /api/board HTTP/1.1", `Host: 127.0.0.1:${kanban.port}`, "Connection: close"]);
    assert.equal(res.status, 200);
    assert.equal(res.json.tasks.length, 40);
    assert.equal(new Set(res.json.tasks.map((task) => task.number)).size, 40);
  });
});

test("a total that describes a narrower set than the request does not empty the done column", async () => {
  // The end-to-end shape of the case above: an upstream or caching proxy whose
  // X-Total-Count counts only the open issues while the board asked for
  // state=all. Every closed task lives past that boundary, so the truncation
  // that used to happen here rendered Done as an empty column.
  const issues = Array.from({ length: 120 }, (_, index) => ({
    id: 1000 + index,
    number: index + 1,
    title: `Issue ${index + 1}`,
    state: index < 50 ? "open" : "closed",
    html_url: `https://git.example/owner/repo/issues/${index + 1}`,
    labels: [],
  }));
  const handler = (call) => {
    if (call.path === "/repos/owner/repo/labels") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    if (call.path === "/repos/owner/repo/pulls") {
      return pagedReply([], { pageSize: 50 })(call);
    }
    if (call.path === "/repos/owner/repo/issues") {
      const page = Number.parseInt(call.query.page ?? "1", 10);
      return {
        status: 200,
        payload: issues.slice((page - 1) * 50, page * 50),
        headers: { "X-Total-Count": "50" },
      };
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ kanban }) => {
    const res = await rawRequest(kanban.port, ["GET /api/board HTTP/1.1", `Host: 127.0.0.1:${kanban.port}`, "Connection: close"]);
    assert.equal(res.status, 200);
    assert.equal(res.json.tasks.length, 120, "a truncated collection was served as a complete board");
    assert.equal(res.json.summary.total, 120);
    assert.equal(res.json.summary.closed, 70, "every closed task belongs in the done column");
  });
});

// --------------------------------------------- upstream failure classes ---

/**
 * moveIssue talks only to `client`, so a stub is the precise way to present a
 * single upstream failure of a single shape to a single call.
 */
function stubClient({ issue = openIssue(), labels = LABELS, patchFailure }) {
  const calls = [];
  return {
    calls,
    request: async (pathname, options = {}) => {
      const method = options.method ?? "GET";
      calls.push({ method, pathname, body: options.body });
      if (method === "PATCH") {
        throw patchFailure();
      }
      if (method === "PUT") {
        return labels;
      }
      return issue;
    },
    paginate: async () => labels,
  };
}

function moveWithFailure(patchFailure) {
  const client = stubClient({ patchFailure });
  return {
    client,
    run: () =>
      moveIssue({ client, repository: "owner/repo", number: 42, targetLabel: "state/done" }),
  };
}

function unreachable({ requestSent }) {
  const error = new HttpError(502, "Gitea is unreachable");
  error.requestSent = requestSent;
  return error;
}

test("a gateway status is an unknown outcome, not a refusal to roll back against", async () => {
  // GITEA_BASE is an https host, so there is a TLS terminator in front of
  // Gitea and a 502/504 can be emitted after the PATCH was forwarded.
  for (const status of [500, 502, 503, 504, 408]) {
    const { client, run } = moveWithFailure(() => new GiteaError(status, "gateway"));
    await assert.rejects(run, (error) => {
      assert.equal(error.status, 502, `status ${status}`);
      assert.equal(error.extra.partial, true, `status ${status} must be reported as unknown`);
      assert.equal(error.extra.reverted, false);
      assert.match(error.message, /unknown whether/i);
      return true;
    });
    const puts = client.calls.filter((call) => call.method === "PUT");
    assert.equal(puts.length, 1, `status ${status} must not roll back a change Gitea may have applied`);
  }
});

test("a refusal by something that read the request is rolled back", async () => {
  for (const status of [400, 401, 403, 404, 409, 422, 429]) {
    const { client, run } = moveWithFailure(() => new GiteaError(status, "refused"));
    await assert.rejects(run, (error) => {
      assert.equal(error.extra.reverted, true, `status ${status} should roll back`);
      assert.equal(error.extra.partial, false);
      return true;
    });
    const puts = client.calls.filter((call) => call.method === "PUT");
    assert.equal(puts.length, 2, `status ${status} should have written and rolled back`);
  }
});

test("a connection that was never established is rolled back, a dropped one is not", async () => {
  const refused = moveWithFailure(() => unreachable({ requestSent: false }));
  await assert.rejects(refused.run, (error) => {
    assert.equal(error.extra.reverted, true, "a request that never arrived did not change anything");
    return true;
  });
  assert.equal(refused.client.calls.filter((call) => call.method === "PUT").length, 2);

  const dropped = moveWithFailure(() => unreachable({ requestSent: true }));
  await assert.rejects(dropped.run, (error) => {
    assert.equal(error.extra.partial, true, "a dropped answer leaves the outcome unknown");
    return true;
  });
  assert.equal(dropped.client.calls.filter((call) => call.method === "PUT").length, 1);
});

test("isDefiniteRejection separates a read request from an unanswered one", () => {
  assert.equal(isDefiniteRejection(new GiteaError(403, "x")), true);
  assert.equal(isDefiniteRejection(new GiteaError(422, "x")), true);
  assert.equal(isDefiniteRejection(new GiteaError(408, "x")), false);
  assert.equal(isDefiniteRejection(new GiteaError(504, "x")), false);
  assert.equal(isDefiniteRejection(unreachable({ requestSent: false })), true);
  assert.equal(isDefiniteRejection(unreachable({ requestSent: true })), false);
  assert.equal(isDefiniteRejection(new TypeError("terminated")), false);
});

test("a connection refused by the OS is marked as never sent", async () => {
  const port = await freePort();
  const client = createGiteaClient({ baseUrl: `http://127.0.0.1:${port}`, token: "t" });
  await assert.rejects(
    () => client.request("/repos/owner/repo/labels"),
    (error) => {
      assert.equal(error.status, 502);
      assert.equal(error.requestSent, false, "nothing was listening, so nothing was delivered");
      assert.equal(isDefiniteRejection(error), true);
      return true;
    },
  );
});

test("a socket that dies during the body is a 502, not a runtime error as a 500", async () => {
  // The body read used to sit outside the try around fetch, so this rejected
  // with a bare TypeError and reached the browser as a 500.
  const server = createServer((request, response) => {
    response.writeHead(200, { "Content-Type": "application/json", "Content-Length": "4096" });
    response.write("[");
    // The pause matters: fetch must resolve its headers first, so the failure
    // lands on the body read rather than on the fetch itself.
    setTimeout(() => request.socket.destroy(), 150);
  });
  const port = await listen(server);
  try {
    const client = createGiteaClient({ baseUrl: `http://127.0.0.1:${port}`, token: "t" });
    await assert.rejects(
      () => client.request("/repos/owner/repo/issues/42"),
      (error) => {
        assert.ok(error instanceof HttpError, `expected an HttpError, got ${error?.name}: ${error?.message}`);
        assert.equal(error.status, 502);
        assert.equal(error.message, "Gitea is unreachable", "no runtime text may reach the browser");
        assert.equal(error.requestSent, true, "the request reached the server before the body died");
        return true;
      },
    );
  } finally {
    await close(server);
  }
});

test("a body read that fails with a connect-shaped code is still a request that was sent", async () => {
  // The headers had already arrived, so the request provably reached Gitea and
  // may well have been applied. A connect-time code turning up on the body read
  // is a misattribution, and it must not reclassify the exchange as never
  // delivered: that classification is what a rollback turns on.
  const client = createGiteaClient({
    baseUrl: "http://127.0.0.1:1",
    token: "t",
    fetchImpl: async () => ({
      ok: true,
      status: 200,
      headers: new Headers(),
      text: async () => {
        const error = new Error("connection reset");
        error.code = "ECONNREFUSED";
        throw error;
      },
    }),
  });
  await assert.rejects(
    () => client.request("/repos/owner/repo/issues/42"),
    (error) => {
      assert.equal(error.status, 502);
      assert.equal(error.requestSent, true, "a response was already received, so the request was delivered");
      assert.equal(isDefiniteRejection(error), false, "an outcome that may have been applied must not be rolled back");
      return true;
    },
  );
});

test("an issue payload that is not an issue is refused before any write", async () => {
  for (const payload of [null, "not an issue", 7, []]) {
    const calls = [];
    const client = {
      request: async (pathname, options = {}) => {
        calls.push({ method: options.method ?? "GET", pathname });
        return payload;
      },
      paginate: async () => LABELS,
    };
    await assert.rejects(
      () => moveIssue({ client, repository: "owner/repo", number: 42, targetLabel: "state/done" }),
      (error) => {
        assert.equal(error.status, 502);
        assert.equal(error.message, "Gitea returned an unexpected issue payload");
        return true;
      },
    );
    assert.equal(
      calls.some((call) => call.method === "PUT" || call.method === "PATCH"),
      false,
      `payload ${JSON.stringify(payload)} must not reach a write that would clear the labels`,
    );
  }
});

// ------------------------------------------------ label ids that cannot be --

test("a label the board cannot write back fails the move instead of being deleted", async () => {
  const unusableId = [{ id: Number.MAX_SAFE_INTEGER + 2, name: "area/render" }, LABELS[1]];
  const handler = (call) => {
    if (call.method === "GET" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: openIssue({ labels: unusableId }) };
    }
    if (call.path === "/repos/owner/repo/labels" && call.method === "GET") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    return { status: 200, payload: LABELS };
  };

  await withServers(handler, async ({ gitea, kanban }) => {
    const res = await movePost(kanban.port, 42, "state/review");
    assert.equal(res.status, 409);
    assert.match(res.json.error, /no id this board can write back/);
    // Silently dropping it would have deleted area/render and reported success.
    assert.equal(gitea.calls.some((call) => call.method === "PUT"), false);
  });
});

test("a target label with no usable id fails the move", async () => {
  const handler = (call) => {
    if (call.method === "GET" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: openIssue() };
    }
    if (call.path === "/repos/owner/repo/labels" && call.method === "GET") {
      return pagedReply([LABELS[0], LABELS[1], { id: "13", name: "state/review" }], { pageSize: 50 })(call);
    }
    return { status: 200, payload: LABELS };
  };

  await withServers(handler, async ({ gitea, kanban }) => {
    const res = await movePost(kanban.port, 42, "state/review");
    assert.equal(res.status, 409);
    assert.match(res.json.error, /state\/review has no id/);
    assert.equal(gitea.calls.some((call) => call.method === "PUT"), false);
  });
});

// ------------------------------------------------------ scrubbing corners ---

test("the token is removed from keys and from values below the depth cap", () => {
  const token = "gta_deep_canary_0123456789abcdef";
  const deep = { a: { b: { c: { d: { e: { f: { g: `token ${token}` } } } } } } };
  assert.equal(JSON.stringify(scrubSecrets(deep, token)).includes(token), false, "a deep value must not survive");

  const nestedArrays = [[[[[[[`token ${token}`]]]]]]];
  assert.equal(JSON.stringify(scrubSecrets(nestedArrays, token)).includes(token), false, "a deep array must not survive");

  const inKey = { [`hdr-token ${token}`]: "x" };
  assert.equal(JSON.stringify(scrubSecrets(inKey, token)).includes(token), false, "a key must not survive");
});

// ------------------------------------------------- untested guarantees -----

test("a rollback never writes a null label id, even for the state label", async () => {
  // nextLabelIds vets every label it keeps, but it removes state labels before
  // that check, so the rollback set is the one place an unusable state-label id
  // can still reach a PUT body.
  const unsafeStateLabel = { id: Number.MAX_SAFE_INTEGER + 2, name: "state/working" };
  const handler = (call) => {
    if (call.method === "GET" && call.path === "/repos/owner/repo/issues/42") {
      return { status: 200, payload: openIssue({ labels: [LABELS[0], unsafeStateLabel] }) };
    }
    if (call.path === "/repos/owner/repo/labels" && call.method === "GET") {
      return pagedReply(LABELS, { pageSize: 50 })(call);
    }
    if (call.method === "PUT") {
      return { status: 200, payload: LABELS };
    }
    if (call.method === "PATCH") {
      return { status: 403, payload: { message: "state change forbidden" } };
    }
    return { status: 404, payload: { message: "unhandled" } };
  };

  await withServers(handler, async ({ gitea, kanban }) => {
    const res = await movePost(kanban.port, 42, "state/done");
    assert.equal(res.status, 502);
    assert.equal(res.json.reverted, true);

    const puts = gitea.calls.filter((call) => call.method === "PUT");
    assert.equal(puts.length, 2);
    assert.deepEqual(puts[1].body.labels, [10], "the rollback must not write a null id");
    assert.equal(puts[1].body.labels.includes(null), false);
  });
});

test("a runtime failure inside nextLabelIds is not relayed as a 409", async () => {
  // moveIssue takes any client, so this is the shape that still reaches
  // nextLabelIds with something it cannot use. The narrowed catch must let it
  // through as a defect rather than dress it up as a board condition.
  const client = {
    request: async () => openIssue(),
    paginate: async () => "not an array",
  };
  await assert.rejects(
    () => moveIssue({ client, repository: "owner/repo", number: 42, targetLabel: "state/review" }),
    (error) => {
      assert.equal(error instanceof HttpError, false, "a runtime failure is not an answer for the browser");
      assert.equal(error.status, undefined);
      return true;
    },
  );
});

test("a file that grows while it is being sent is truncated to its declared length", async () => {
  // The limiter has its own unit test; this one proves it is actually in the
  // response pipeline. A large file so the append lands mid-transfer.
  const webRoot = await mkdtemp(path.join(os.tmpdir(), "kanban-webroot-"));
  const assetPath = path.join(webRoot, "big.js");
  const half = Buffer.alloc(4 * 1024 * 1024, "A");
  await writeFile(assetPath, half);

  const warnings = [];
  const logger = { info() {}, warn: (message) => warnings.push(String(message)), error() {} };

  await withServers(
    () => ({ status: 200, payload: [] }),
    async ({ kanban }) => {
      const { declared, delivered } = await new Promise((resolve, reject) => {
        const socket = net.connect(kanban.port, "127.0.0.1");
        let head = "";
        let headerEnd = -1;
        let bodyBytes = 0;
        let grown = false;
        socket.on("connect", () => {
          socket.write(`GET /big.js HTTP/1.1\r\nHost: 127.0.0.1:${kanban.port}\r\nConnection: close\r\n\r\n`);
        });
        socket.on("data", (chunk) => {
          if (headerEnd < 0) {
            head += chunk.toString("latin1");
            headerEnd = head.indexOf("\r\n\r\n");
            if (headerEnd >= 0) {
              bodyBytes += Buffer.byteLength(head.slice(headerEnd + 4), "latin1");
              head = head.slice(0, headerEnd);
            }
            if (!grown) {
              grown = true;
              appendFileSync(assetPath, Buffer.alloc(4 * 1024 * 1024, "B"));
            }
            return;
          }
          bodyBytes += chunk.length;
        });
        socket.on("error", reject);
        socket.on("close", () => {
          const match = /content-length: (\d+)/i.exec(head);
          resolve({ declared: Number.parseInt(match?.[1] ?? "-1", 10), delivered: bodyBytes });
        });
      });

      assert.equal(declared, half.length, "the header should describe the file as it was opened");
      assert.equal(
        delivered,
        declared,
        `a file that grew must not overrun its declared length: declared ${declared}, delivered ${delivered}`,
      );
      assert.ok(
        (await stat(assetPath)).size > declared,
        "the harness failed to grow the file, so nothing was exercised",
      );
      assert.ok(
        warnings.some((message) => message.includes("grew by")),
        `an overrun should be logged, saw ${JSON.stringify(warnings)}`,
      );
    },
    { webRoot, logger },
  ).finally(() => rm(webRoot, { recursive: true, force: true }));
});

test("the declared length describes the file that was opened, not what the path names later", async () => {
  // The stat is taken from the open handle rather than from the path, so a
  // request that has already opened a file can never be handed the length of
  // whatever replaces it a moment later. Windows refuses to rename over an open
  // file, so a swap here is an unlink followed by a rename, which also leaves
  // the path briefly absent: a 404 is a legitimate outcome and only a served
  // body is checked.
  //
  // The window between the open and the stat is microseconds wide and there is
  // no seam to pause it at, so this races it repeatedly instead. It cannot fail
  // against a correct implementation -- with the handle as the source, content
  // and declared length come from one file by construction -- and against a
  // path stat it fails as soon as the race lands.
  const webRoot = await mkdtemp(path.join(os.tmpdir(), "kanban-swap-"));
  const asset = path.join(webRoot, "asset.js");
  const spare = path.join(webRoot, "spare.tmp");
  const shapes = [
    { letter: "A", size: 48 * 1024 },
    { letter: "B", size: 3 * 1024 },
  ];
  const sizeOf = new Map(shapes.map((shape) => [shape.letter, shape.size]));
  let next = 0;
  writeFileSync(asset, Buffer.alloc(shapes[0].size, shapes[0].letter));

  function swapAsset() {
    next = (next + 1) % shapes.length;
    const shape = shapes[next];
    writeFileSync(spare, Buffer.alloc(shape.size, shape.letter));
    try {
      unlinkSync(asset);
    } catch {
      // Already gone; the rename below puts one back.
    }
    renameSync(spare, asset);
  }

  function fetchAsset(port) {
    return new Promise((resolve) => {
      const socket = net.connect(port, "127.0.0.1");
      let head = "";
      let headerEnd = -1;
      let delivered = 0;
      let reset = false;
      const letters = new Set();
      const note = (buffer) => {
        delivered += buffer.length;
        for (const byte of buffer) {
          letters.add(String.fromCharCode(byte));
        }
      };
      socket.setTimeout(20_000);
      socket.on("connect", () => {
        socket.write(`GET /asset.js HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\nConnection: close\r\n\r\n`);
      });
      socket.on("data", (chunk) => {
        if (headerEnd < 0) {
          head += chunk.toString("latin1");
          headerEnd = head.indexOf("\r\n\r\n");
          if (headerEnd < 0) {
            return;
          }
          const body = Buffer.from(head.slice(headerEnd + 4), "latin1");
          head = head.slice(0, headerEnd);
          note(body);
          return;
        }
        note(chunk);
      });
      socket.on("timeout", () => {
        reset = true;
        socket.destroy();
      });
      socket.on("error", () => {
        reset = true;
      });
      socket.on("close", () => {
        resolve({
          status: Number.parseInt((head.split("\r\n")[0] ?? "").split(" ")[1] ?? "0", 10),
          declared: Number.parseInt(/content-length: (\d+)/i.exec(head)?.[1] ?? "-1", 10),
          delivered,
          letters: [...letters],
          reset,
        });
      });
    });
  }

  const warnings = [];
  const logger = { info() {}, warn: (message) => warnings.push(String(message)), error() {} };

  await withServers(
    () => ({ status: 200, payload: [] }),
    async ({ kanban }) => {
      const pending = [];
      for (let round = 0; round < 12; round += 1) {
        for (let request = 0; request < 12; request += 1) {
          pending.push(fetchAsset(kanban.port));
        }
        for (let swap = 0; swap < 24; swap += 1) {
          swapAsset();
          // The swap is synchronous, so the event loop has to be handed back
          // for the in-flight opens and stats to resolve around it.
          await new Promise((resolve) => setImmediate(resolve));
        }
      }

      const results = await Promise.all(pending);
      const served = results.filter((result) => result.status === 200);
      assert.ok(served.length > 0, "the harness served nothing, so nothing was exercised");
      for (const result of served) {
        assert.equal(result.reset, false, `a served response was cut short: ${JSON.stringify(result)}`);
        assert.equal(
          result.delivered,
          result.declared,
          `a served body did not match its declared length: ${JSON.stringify(result)}`,
        );
        assert.deepEqual(
          result.letters.length,
          1,
          `a served body mixed two files together: ${JSON.stringify(result)}`,
        );
        assert.equal(
          result.declared,
          sizeOf.get(result.letters[0]),
          `the declared length belongs to a different file than the body: ${JSON.stringify(result)}`,
        );
      }
      assert.deepEqual(warnings, [], `no response should have been truncated: ${JSON.stringify(warnings)}`);
    },
    { webRoot, logger },
  ).finally(() => rm(webRoot, { recursive: true, force: true }));
});

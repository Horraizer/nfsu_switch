import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import net from "node:net";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

import {
  HttpError,
  createGiteaClient,
  createKanbanServer,
  resolveStaticPath,
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

async function startKanban({ giteaOrigin, token = "test-token", repository = "owner/repo" }) {
  const port = await freePort();
  const client = createGiteaClient({ baseUrl: giteaOrigin, token });
  const server = createKanbanServer({
    client,
    repository,
    giteaBaseUrl: giteaOrigin,
    webRoot: WEB_ROOT,
    host: "127.0.0.1",
    port,
    instanceId: "test-instance",
    logger: silentLogger,
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

async function withServers(handler, run) {
  const gitea = await startFakeGitea(handler);
  const kanban = await startKanban({ giteaOrigin: gitea.origin });
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
    assert.equal(badJson.json.error, "Request body is not valid JSON");
    // The raw parser message must not be handed to the browser.
    assert.equal(/position|JSON at/i.test(badJson.json.error), false);
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
    const code = await Promise.race([
      exited,
      new Promise((resolve) => setTimeout(() => resolve("timeout"), 30_000)),
    ]);
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

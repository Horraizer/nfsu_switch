import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { createServer } from "node:http";
import { mkdtemp, readFile, readdir, rm, utimes, writeFile } from "node:fs/promises";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const LAUNCHER = fileURLToPath(new URL("./start.ps1", import.meta.url));

// server.mjs's own watchdog polls once a second, so a reaped server is expected
// within a couple of ticks rather than instantly.
const REAP_TIMEOUT_MS = 20_000;

// ---------------------------------------------------------------- harness ---

function probeHost(exe) {
  const probe = spawnSync(exe, ["-NoProfile", "-Command", "$PSVersionTable.PSVersion.Major"], {
    encoding: "utf8",
    timeout: 30_000,
  });
  if (probe.error || probe.status !== 0) {
    return null;
  }
  const major = Number.parseInt((probe.stdout ?? "").trim(), 10);
  return Number.isSafeInteger(major) ? { exe, major } : null;
}

const HOSTS = ["pwsh.exe", "powershell.exe"].map(probeHost).filter(Boolean);
// pwsh is the edition whose Invoke-RestMethod honours proxy variables for
// loopback, so the proxy test needs it specifically.
const PWSH = HOSTS.find((host) => host.major >= 6) ?? null;
const HOST = PWSH ?? HOSTS[0] ?? null;
// A skipped suite still exits 0, so on the platform this launcher is for, a
// missing host has to fail rather than quietly remove the whole gate.
const NO_HOST = process.platform === "win32" ? false : "not Windows";

function freePort() {
  return new Promise((resolve, reject) => {
    const probe = net.createServer();
    probe.once("error", reject);
    probe.listen(0, "127.0.0.1", () => {
      const { port } = probe.address();
      probe.close(() => resolve(port));
    });
  });
}

function launcherEnv(port, extra = {}) {
  return {
    ...process.env,
    GITEA_TOKEN: "launcher-test-token-not-a-real-credential",
    // Nothing listens here, so no test can reach a real Gitea even by accident.
    GITEA_BASE: "http://127.0.0.1:1",
    KANBAN_PORT: String(port),
    KANBAN_OPEN_BROWSER: "0",
    ...extra,
  };
}

function startLauncher(host, env) {
  const child = spawn(host.exe, ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", LAUNCHER], {
    env,
    stdio: ["ignore", "pipe", "pipe"],
  });
  let stdout = "";
  let stderr = "";
  child.stdout.on("data", (chunk) => {
    stdout += chunk;
  });
  child.stderr.on("data", (chunk) => {
    stderr += chunk;
  });
  const exited = new Promise((resolve) => {
    child.once("exit", (code) => resolve(code));
  });
  return {
    child,
    exited,
    // PowerShell's error view hard-wraps a thrown message and prefixes every
    // continuation line with a pipe, which splits a sentence mid-phrase. The
    // assertions below are about what the operator reads, not how it wrapped.
    output: () => `${stdout}\n${stderr}`.replace(/\r?\n\s+\|\s?/g, " "),
  };
}

async function runLauncher(host, env, timeoutMs = 120_000) {
  const started = Date.now();
  const run = startLauncher(host, env);
  const timer = setTimeout(() => run.child.kill(), timeoutMs);
  const code = await run.exited;
  clearTimeout(timer);
  return { code, output: run.output(), elapsedMs: Date.now() - started };
}

/**
 * A node.cmd first on PATH, which is what Chocolatey shimgen, Scoop, Volta and
 * a corporate wrapper all look like: Start-Process gets a handle to cmd.exe
 * while the process that binds the port is its child.
 */
async function makeWrapperDir(scriptPath = null, { detach = false } = {}) {
  const dir = await mkdtemp(path.join(os.tmpdir(), "kanban-wrapper-"));
  const forwarded = scriptPath === null ? "%*" : `"${scriptPath}"`;
  // `start /b` hands off and returns, so the launcher's handle exits while the
  // server is still coming up and the process tree it can see is empty.
  const body = detach
    ? `@echo off\r\nstart "" /b "${process.execPath}" ${forwarded}\r\n`
    : `@echo off\r\n"${process.execPath}" ${forwarded}\r\n`;
  await writeFile(path.join(dir, "node.cmd"), body, "ascii");
  return dir;
}

// Windows environment names are case-insensitive, but a spread of process.env
// is an ordinary object, so the real key has to be found rather than assumed.
function envKey(env, name) {
  return Object.keys(env).find((key) => key.toLowerCase() === name) ?? name;
}

function withWrapper(env, wrapperDir) {
  const key = envKey(env, "path");
  return { ...env, [key]: `${wrapperDir};${env[key] ?? ""}` };
}

function withoutNoProxy(env) {
  const copy = { ...env };
  for (const key of Object.keys(copy)) {
    if (key.toLowerCase() === "no_proxy") {
      delete copy[key];
    }
  }
  return copy;
}

function instanceProbe(port) {
  return new Promise((resolve) => {
    const request = net.connect({ port, host: "127.0.0.1" }, () => {
      request.write(`GET /api/instance HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\nConnection: close\r\n\r\n`);
    });
    let data = "";
    request.setTimeout(3000);
    request.on("data", (chunk) => {
      data += chunk;
    });
    request.on("timeout", () => {
      request.destroy();
      resolve(null);
    });
    request.on("error", () => resolve(null));
    request.on("close", () => {
      const separator = data.indexOf("\r\n\r\n");
      if (separator < 0) {
        resolve(null);
        return;
      }
      try {
        resolve(JSON.parse(data.slice(separator + 4)));
      } catch {
        resolve(null);
      }
    });
  });
}

async function waitForInstance(port, deadlineMs = 30_000) {
  const deadline = Date.now() + deadlineMs;
  while (Date.now() < deadline) {
    const identity = await instanceProbe(port);
    if (identity?.instance) {
      return identity;
    }
    await delay(100);
  }
  return null;
}

async function waitForGone(port, deadlineMs = REAP_TIMEOUT_MS) {
  const deadline = Date.now() + deadlineMs;
  while (Date.now() < deadline) {
    if ((await instanceProbe(port)) === null) {
      return true;
    }
    await delay(200);
  }
  return false;
}

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function processAlive(pid) {
  if (!Number.isSafeInteger(pid) || pid <= 0) {
    return false;
  }
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    return error.code === "EPERM";
  }
}

function killQuietly(pid) {
  if (!processAlive(pid)) {
    return;
  }
  try {
    process.kill(pid);
  } catch {
    // already gone
  }
}

// --------------------------------------------------------------- the tests --

test("the launcher completes its smoke run when node resolves through a cmd wrapper", { timeout: 180_000, skip: HOST ? false : NO_HOST }, async () => {
  const port = await freePort();
  const wrapper = await makeWrapperDir();
  try {
    const run = await runLauncher(HOST, withWrapper(launcherEnv(port, { KANBAN_SMOKE: "1" }), wrapper));
    assert.equal(run.code, 0, `launcher should succeed behind a wrapper, output: ${run.output}`);
    // The spawned handle was cmd.exe, so this also proves the identity check no
    // longer compares the listener's pid with the shim's.
    assert.equal(await waitForGone(port, 10_000), true, "the wrapped server must not survive the smoke run");
  } finally {
    await rm(wrapper, { recursive: true, force: true });
  }
});

test("output abandoned by a force-killed launcher is swept by the next run", { timeout: 180_000, skip: HOST ? false : NO_HOST }, async () => {
  // A launcher that is force-killed never reaches its own cleanup, which is
  // the one path that genuinely leaves its redirected output behind. A private
  // TEMP, because launchers elsewhere on this machine write into the shared one.
  const tempDir = await mkdtemp(path.join(os.tmpdir(), "kanban-private-temp-"));
  const abandoned = [
    "kanban-11111111-2222-3333-4444-555555555555.out.txt",
    "kanban-11111111-2222-3333-4444-555555555555.err.txt",
  ];
  const spared = ["kanban-notes.txt", "unrelated.txt"];
  const stale = new Date(Date.now() - 3 * 60 * 60 * 1000);
  for (const name of [...abandoned, ...spared]) {
    await writeFile(path.join(tempDir, name), "left behind", "ascii");
    await utimes(path.join(tempDir, name), stale, stale);
  }
  // Recent output belongs to a launcher that may still be running.
  const recent = "kanban-99999999-8888-7777-6666-555555555555.out.txt";
  await writeFile(path.join(tempDir, recent), "in flight", "ascii");

  const port = await freePort();
  try {
    const run = await runLauncher(HOST, launcherEnv(port, { KANBAN_SMOKE: "1", TEMP: tempDir, TMP: tempDir }));
    assert.equal(run.code, 0, `launcher should have started, output: ${run.output}`);

    const left = (await readdir(tempDir)).sort();
    for (const name of abandoned) {
      assert.equal(left.includes(name), false, `${name} should have been swept`);
    }
    for (const name of [...spared, recent]) {
      assert.equal(left.includes(name), true, `${name} is not this tool's abandoned output and must be left alone`);
    }
  } finally {
    await rm(tempDir, { recursive: true, force: true });
  }
});

test("a stalled readiness probe fails on a wall-clock deadline and leaves no orphan behind a wrapper", { timeout: 240_000, skip: HOST ? false : NO_HOST }, async () => {
  const port = await freePort();
  const dir = await mkdtemp(path.join(os.tmpdir(), "kanban-stall-"));
  const pidFile = path.join(dir, "child.pid");
  const stub = path.join(dir, "stub.js");
  // Binds nothing and never exits, so the launcher can only fail by timing out,
  // and anything left running afterwards is exactly the orphan finding 15 is
  // about: a child of a shim that the launcher has no handle to.
  await writeFile(
    stub,
    `require("node:fs").writeFileSync(${JSON.stringify(pidFile)}, String(process.pid));\nsetTimeout(() => {}, 600000);\n`,
    "ascii",
  );
  const wrapper = await makeWrapperDir(stub);

  // Accepts the connection and never answers, so every probe burns its full
  // timeout instead of being refused. Under an attempt-count budget this shape
  // took about 210 seconds to give up.
  const sockets = [];
  const staller = net.createServer((socket) => sockets.push(socket));
  await new Promise((resolve) => staller.listen(port, "127.0.0.1", resolve));

  let stubPid = 0;
  try {
    const run = await runLauncher(HOST, withWrapper(launcherEnv(port), wrapper), 120_000);
    assert.notEqual(run.code, 0, "a server that never answers must fail the launch");
    assert.match(run.output, /did not answer/i);
    assert.ok(
      run.elapsedMs < 75_000,
      `the launch should give up on a wall-clock budget, took ${run.elapsedMs} ms`,
    );

    stubPid = Number.parseInt((await readFile(pidFile, "ascii")).trim(), 10);
    assert.ok(stubPid > 0, "the wrapped child should have recorded its pid");
    assert.equal(processAlive(stubPid), false, `pid ${stubPid} was left running after a failed launch`);
  } finally {
    if (stubPid === 0) {
      // An assertion may have failed before the pid was read, and the stub
      // outlives this test by ten minutes if nobody stops it.
      try {
        stubPid = Number.parseInt((await readFile(pidFile, "ascii")).trim(), 10);
      } catch {
        stubPid = 0;
      }
    }
    killQuietly(stubPid);
    for (const socket of sockets) {
      socket.destroy();
    }
    await new Promise((resolve) => staller.close(resolve));
    await rm(wrapper, { recursive: true, force: true });
    await rm(dir, { recursive: true, force: true });
  }
});

test("a foreign listener on the port is named and left alone", { timeout: 180_000, skip: HOST ? false : NO_HOST }, async () => {
  const port = await freePort();
  let served = 0;
  const decoy = createServer((request, response) => {
    served += 1;
    const body = JSON.stringify({ pid: 999999, instance: "a-foreign-instance" });
    response.writeHead(200, { "Content-Type": "application/json", "Content-Length": Buffer.byteLength(body) });
    response.end(body);
  });
  await new Promise((resolve) => decoy.listen(port, "127.0.0.1", resolve));

  try {
    const run = await runLauncher(HOST, launcherEnv(port, { KANBAN_SMOKE: "1" }));
    assert.notEqual(run.code, 0, "a port owned by someone else must fail the launch");
    assert.match(run.output, /is owned by/i);
    // The self-reported 999999 must not be what the operator is sent after.
    assert.equal(/process 999999/.test(run.output), false, "the launcher must report the owner the OS knows");
    assert.ok(served > 0, "the decoy should have been probed");

    const stillThere = await instanceProbe(port);
    assert.equal(stillThere?.instance, "a-foreign-instance", "the foreign listener must survive untouched");
  } finally {
    await new Promise((resolve) => decoy.close(resolve));
  }
});

test("a hard-killed launcher does not leave a server holding the token", { timeout: 180_000, skip: HOST ? false : NO_HOST }, async () => {
  const port = await freePort();
  const run = startLauncher(HOST, launcherEnv(port));
  let serverPid = 0;
  let instance = "";
  try {
    const identity = await waitForInstance(port);
    assert.ok(identity, `the launcher should have brought a server up, output: ${run.output()}`);
    instance = String(identity.instance ?? "");
    serverPid = Number.parseInt(String(identity.pid), 10);
    assert.ok(processAlive(serverPid), "the reported pid should be a live process");

    // TerminateProcess, so the launcher's finally block never runs. Only the
    // server's own parent watch can clean this up.
    run.child.kill();
    await run.exited;

    assert.equal(await waitForGone(port), true, "the port should be released after the launcher is killed");
    const deadline = Date.now() + REAP_TIMEOUT_MS;
    while (processAlive(serverPid) && Date.now() < deadline) {
      await delay(200);
    }
    assert.equal(processAlive(serverPid), false, `server pid ${serverPid} outlived the launcher that owned it`);
  } finally {
    run.child.kill();
    killQuietly(serverPid);
    // This launcher was killed before it could tidy up, which is the one path
    // that leaves its redirected output behind for the next run to sweep.
    for (const suffix of ["out", "err"]) {
      await rm(path.join(os.tmpdir(), `kanban-${instance}.${suffix}.txt`), { force: true });
    }
  }
});

test("a proxy environment variable does not break the loopback readiness probe", { timeout: 180_000, skip: PWSH ? false : (process.platform === "win32" ? false : "not Windows") }, async () => {
  const port = await freePort();
  // 127.0.0.1:9 is the discard port: a proxy that will never answer, which is
  // what a corporate HTTP_PROXY or a stopped Fiddler looks like.
  const run = await runLauncher(
    PWSH,
    withoutNoProxy(
      launcherEnv(port, {
        KANBAN_SMOKE: "1",
        HTTP_PROXY: "http://127.0.0.1:9",
        ALL_PROXY: "http://127.0.0.1:9",
      }),
    ),
  );
  assert.equal(run.code, 0, `a proxy variable must not break a loopback probe, output: ${run.output}`);
  assert.equal(await waitForGone(port, 10_000), true, "the smoke run should stop its own server");
});

test("a shim that detaches before the server binds is still started and still cleaned up", { timeout: 180_000, skip: HOST ? false : NO_HOST }, async () => {
  assert.ok(HOST, "a PowerShell host is required for the launcher gate");
  const port = await freePort();
  // The worst shim shape: cmd hands off with `start /b` and exits, so the
  // handle the launcher holds is dead before readiness and the process tree it
  // can walk is empty. Only the instance id can identify the server after that.
  const wrapper = await makeWrapperDir(null, { detach: true });
  try {
    const run = await runLauncher(HOST, withWrapper(launcherEnv(port, { KANBAN_SMOKE: "1" }), wrapper));
    assert.equal(run.code, 0, `a detaching shim should still start the server, output: ${run.output}`);
    assert.equal(await waitForGone(port, 15_000), true, "a detached server must not survive the smoke run");
  } finally {
    await rm(wrapper, { recursive: true, force: true });
  }
});

test("a port already in use is reported as such, under every PowerShell host present", { timeout: 240_000, skip: HOST ? false : NO_HOST }, async () => {
  assert.ok(HOSTS.length > 0, "a PowerShell host is required for the launcher gate");
  // A raw listener, so nothing ever answers the readiness probe and the only
  // signal is the server's own EADDRINUSE exit code. Windows PowerShell 5.1
  // reports a null ExitCode for a redirected child unless the launcher opened
  // the process handle first, so this runs under each host that exists.
  for (const host of HOSTS) {
    const port = await freePort();
    const sockets = [];
    const squatter = net.createServer((socket) => sockets.push(socket));
    await new Promise((resolve) => squatter.listen(port, "127.0.0.1", resolve));
    try {
      const run = await runLauncher(host, launcherEnv(port, { KANBAN_SMOKE: "1" }));
      assert.notEqual(run.code, 0, `${host.exe}: an occupied port must fail the launch`);
      assert.match(run.output, /already in use/i, `${host.exe}: the cause should be named`);
      assert.match(run.output, /process \d+/, `${host.exe}: the owner the OS reports should be named`);
      // server.mjs reserves exit code 2 for this, and its own message goes to a
      // hidden window, so the launcher has to surface it.
      assert.match(run.output, /Stop it or set KANBAN_PORT/, `${host.exe}: the server's own output should be surfaced`);
    } finally {
      for (const socket of sockets) {
        socket.destroy();
      }
      await new Promise((resolve) => squatter.close(resolve));
    }
  }
});

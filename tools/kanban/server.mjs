import { randomUUID } from "node:crypto";
import { fileURLToPath } from "node:url";

import { createConsoleLogger, createGiteaClient, createKanbanServer } from "./service.mjs";

const HOST = "127.0.0.1";
const DEFAULT_PORT = 4173;
const PARENT_POLL_MS = 1000;
const SHUTDOWN_GRACE_MS = 2000;

const logger = createConsoleLogger();

function readPort(raw) {
  if (raw === undefined || raw.trim() === "") {
    return DEFAULT_PORT;
  }
  const text = raw.trim();
  if (!/^[0-9]+$/.test(text)) {
    throw new Error(`KANBAN_PORT must be a decimal port number, got "${raw}"`);
  }
  const port = Number.parseInt(text, 10);
  if (!Number.isSafeInteger(port) || port < 1 || port > 65535) {
    throw new Error(`KANBAN_PORT must be between 1 and 65535, got "${raw}"`);
  }
  return port;
}

function readParentPid(raw) {
  if (raw === undefined || raw.trim() === "") {
    return null;
  }
  const pid = Number.parseInt(raw.trim(), 10);
  return Number.isSafeInteger(pid) && pid > 1 ? pid : null;
}

function processAlive(pid) {
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    // EPERM means the pid exists but belongs to another user.
    return error.code === "EPERM";
  }
}

const PORT = readPort(process.env.KANBAN_PORT);
const GITEA_BASE = (process.env.GITEA_BASE ?? "https://git.accad.pro").replace(/\/$/, "");
const REPOSITORY = process.env.GITEA_REPOSITORY ?? "murzintsevav/nfsu-switch";
const TOKEN = process.env.GITEA_TOKEN ?? "";
const WEB_ROOT = fileURLToPath(new URL("./web/", import.meta.url));
const INSTANCE_ID = process.env.KANBAN_INSTANCE?.trim() || randomUUID();
const PARENT_PID = readParentPid(process.env.KANBAN_PARENT_PID);

if (!/^https:\/\//i.test(GITEA_BASE)) {
  logger.warn(`GITEA_BASE is not https (${GITEA_BASE}); the token would travel in cleartext.`);
}

const client = createGiteaClient({ baseUrl: GITEA_BASE, token: TOKEN });
const server = createKanbanServer({
  client,
  repository: REPOSITORY,
  giteaBaseUrl: GITEA_BASE,
  webRoot: WEB_ROOT,
  host: HOST,
  port: PORT,
  instanceId: INSTANCE_ID,
  logger,
});

let parentTimer = null;
let shuttingDown = false;

function shutdown(code, reason) {
  if (shuttingDown) {
    return;
  }
  shuttingDown = true;
  if (reason) {
    logger.info(reason);
  }
  if (parentTimer) {
    clearInterval(parentTimer);
  }
  server.close(() => process.exit(code));
  server.closeAllConnections?.();
  setTimeout(() => process.exit(code), SHUTDOWN_GRACE_MS).unref();
}

server.on("error", (error) => {
  if (error.code === "EADDRINUSE") {
    logger.error(
      `port ${PORT} is already in use; another kanban server or a foreign process owns it. Stop it or set KANBAN_PORT.`,
    );
    process.exit(2);
  }
  logger.error("server failed", error);
  process.exit(1);
});

server.listen(PORT, HOST, () => {
  logger.info(`NFSU Kanban: http://${HOST}:${PORT}`);
  logger.info(`instance ${INSTANCE_ID} pid ${process.pid}`);
  if (!TOKEN) {
    logger.warn("GITEA_TOKEN is not set; board requests will fail until it is provided.");
  }

  // The server holds a Gitea token with write scope, so it must not outlive the
  // launcher that owns it. Start-Process gives no job object on Windows, and a
  // closed console window never runs the launcher's finally block, so the
  // server watches its own parent instead of trusting the parent to clean up.
  if (PARENT_PID !== null) {
    parentTimer = setInterval(() => {
      if (!processAlive(PARENT_PID)) {
        shutdown(0, `launcher process ${PARENT_PID} is gone; shutting down.`);
      }
    }, PARENT_POLL_MS);
  }
});

for (const signal of ["SIGINT", "SIGTERM", "SIGHUP", "SIGBREAK"]) {
  process.on(signal, () => shutdown(0, `received ${signal}; shutting down.`));
}

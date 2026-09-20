# Local task board

This tool displays Gitea issues as a local kanban board and writes workflow changes back to the repository.

## Start

From the repository root:

```powershell
.\tools\kanban\start.ps1
```

The launcher requires `GITEA_TOKEN` and opens `http://127.0.0.1:4173` in the default browser. Set `KANBAN_PORT` to use another local port. Set `GITEA_BASE` or `GITEA_REPOSITORY` to override the defaults. Set `KANBAN_OPEN_BROWSER=0` to start the server without opening a browser.

The launcher proves the listener is the server this run started by matching a one-time instance id, not by comparing process ids: when `node` resolves through a shim such as a Chocolatey, Scoop or Volta wrapper, the process the launcher spawned is the shim and the process that binds the port is its child. If the port is already taken, the launcher names the owner the OS reports and stops without touching it. Startup has a wall-clock budget of 20 seconds, and the server's own output is surfaced in the failure message.

## Stop

Press Ctrl+C in the launcher window, or close it.

The server is a hidden process, and two mechanisms stop it. The launcher stops the whole process tree it started on every exit path it reaches, including a failed launch, and as a final check it stops any listener that proves with the run's instance id that it belongs to this launcher, falling back to the owner the OS reports for the port when that listener answers without a usable pid of its own. If the launcher is force-killed and never reaches that cleanup, the server shuts itself down instead: it is told which process launched it and polls for that process once a second.

A server started by hand, without `KANBAN_PARENT_PID` in its environment, has no such watch and will keep running until it is stopped.

The server runs with no visible window, so the launcher redirects its output to two files in the temporary directory and deletes them when it exits. A launcher that is force-killed cannot delete its own; the next launch sweeps anything of that shape older than an hour.

## Workflow

- Drag a card between columns or use its move selector.
- Moving a task to `Done` applies `state/done` and closes the issue.
- Moving a task out of `Done` applies the selected state label and reopens the issue.
- Non-workflow labels are preserved.
- A column whose Gitea label does not exist is marked unavailable and is not offered as a destination.
- Pull requests are counted in the header but are not shown as task cards.

A state change is rolled back only when it is known not to have happened: a refusal from something that read the request, or a connection that was never established. If Gitea answers with a gateway error or a timeout, or drops the answer, the outcome is unknown instead, because `GITEA_BASE` is an https host and a proxy in front of Gitea can fail a request it has already forwarded. In that case the label is deliberately left in place, since undoing a state change Gitea may have applied would create the opposite drift while claiming nothing happened.

If a rollback itself fails, the board says so explicitly and names the issue. Both drift messages are held on screen for twelve seconds, and neither the refresh that follows a move nor a later confirmation can replace them; an error is never suppressed.

A move is refused outright, before anything is written, if the issue carries a label whose id the board cannot write back. Dropping that label would delete it from the issue and still report success.

## Security model

The server binds only to `127.0.0.1`, requires a local `Host`, and rejects any cross-origin `Origin`. The Gitea token stays in the server process: it is never sent to the browser, and upstream error text is never relayed to the browser, so a proxy that echoes request headers cannot leak it through an error page.

Upstream error text is still written to the server's own log, scrubbed first. The scrub removes the token value wherever it appears, literally or with any of its characters percent-encoded, and redacts an `Authorization`-shaped header value up to the first whitespace or delimiter. It is a best-effort pass over text of a shape this tool does not control, not a guarantee: a credential that the upstream page wrapped across lines leaves the part after the break behind, because the header match stops at the line break. The log is local to the machine running the server.

The loopback API itself is **unauthenticated**. Any process or user session on this machine can drive authenticated Gitea writes through it while the server is running. That is why the server does not outlive its launcher.

## Test

```powershell
node --test tools\kanban\test.mjs tools\kanban\server.test.mjs tools\kanban\launcher.test.mjs
```

`test.mjs` covers the pure board logic, and the two rules the board page makes about its own messages: a message that must survive the refresh which follows a move, and an error that must never be suppressed. `server.test.mjs` runs the real server over real HTTP against a fake Gitea and covers request validation, host and origin gating, static path traversal, `Content-Length` correctness against a file that is replaced mid-request, pagination against clamped, duplicated, mis-labelled and under-counted pages, move rollback, unknown move outcomes, accepted-write behaviour, malformed label payloads, and token secrecy: a canary token is asserted absent from the response head, the response body, and everything the console logger writes, in percent-encoded form as well as literal.

`launcher.test.mjs` drives `start.ps1` itself under a real PowerShell host. It covers a `node` that resolves through a `cmd` wrapper, a shim that detaches before the server binds, a failed launch leaving no orphaned server, the wall-clock startup deadline, a foreign listener being named and left alone, an occupied port being reported as such under every installed PowerShell edition, a force-killed launcher being reaped by the server's own parent watch, a server that answers for this run but reports no usable pid still being stopped, and the sweep that removes the output a force-killed launcher could not delete.

The proxy case needs PowerShell 6 or later: `-NoProxy` exists only there, and Windows PowerShell 5.1 bypasses a proxy for loopback addresses already, so under 5.1 alone that one test has nothing to assert and is skipped with a reason naming the host that is missing. On Windows a missing PowerShell host altogether fails rather than skips, because a skipped gate still exits zero.

No test contacts a real Gitea instance; every test points at a loopback stub.

Of `web/app.js`, only the message rules above are covered; the rendering path is not. `web/shared.mjs`, which holds the decision the move path delegates to, is covered.

Launcher smoke, from the repository root:

```powershell
$env:KANBAN_SMOKE="1"; .\tools\kanban\start.ps1
```

It starts the server, proves the listener belongs to that run, and stops it again. It needs `GITEA_TOKEN` set, like any other launcher run.

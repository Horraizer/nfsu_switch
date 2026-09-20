# Local task board

This tool displays Gitea issues as a local kanban board and writes workflow changes back to the repository.

## Start

From the repository root:

```powershell
.\tools\kanban\start.ps1
```

The launcher requires `GITEA_TOKEN` and opens `http://127.0.0.1:4173` in the default browser. Set `KANBAN_PORT` to use another local port. Set `GITEA_BASE` or `GITEA_REPOSITORY` to override the defaults.

## Stop

Press Ctrl+C in the launcher window, or close it. The server is a hidden process, so it is told which process launched it and shuts itself down when that process is gone. Nothing is left holding the token.

## Workflow

- Drag a card between columns or use its move selector.
- Moving a task to `Done` applies `state/done` and closes the issue.
- Moving a task out of `Done` applies the selected state label and reopens the issue.
- Non-workflow labels are preserved.
- A column whose Gitea label does not exist is marked unavailable and is not offered as a destination.
- Pull requests are counted in the header but are not shown as task cards.

If Gitea accepts the label change but refuses the state change, the label is rolled back and the board reports that nothing changed. If that rollback also fails, the board says so explicitly and names the issue, instead of quietly showing the card in the column it failed to reach.

## Security model

The server binds only to `127.0.0.1`, requires a local `Host`, and rejects any cross-origin `Origin`. The Gitea token stays in the server process: it is never sent to the browser, never logged, and upstream error text is never relayed, so a proxy that echoes request headers cannot leak it through an error page.

The loopback API itself is **unauthenticated**. Any process or user session on this machine can drive authenticated Gitea writes through it while the server is running. That is why the server does not outlive its launcher.

## Test

```powershell
node --test tools\kanban\test.mjs tools\kanban\server.test.mjs
```

`test.mjs` covers the pure board logic. `server.test.mjs` runs the real server against a fake Gitea and covers request validation, pagination, move rollback, accepted-write behaviour, token secrecy, and launcher-orphan shutdown. No test contacts a real Gitea instance.

Launcher smoke, from the repository root:

```powershell
$env:KANBAN_SMOKE="1"; .\tools\kanban\start.ps1
```

It starts the server, proves the listener is the process it just spawned, and stops it again.

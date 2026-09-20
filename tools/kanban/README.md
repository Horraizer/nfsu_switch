# Local task board

This tool displays Gitea issues as a local kanban board and writes workflow changes back to the repository.

## Start

From the repository root:

```powershell
.\tools\kanban\start.ps1
```

The launcher requires `GITEA_TOKEN` and opens `http://127.0.0.1:4173` in the default browser. Set `KANBAN_PORT` to use another local port. Set `GITEA_BASE` or `GITEA_REPOSITORY` to override the defaults.

## Workflow

- Drag a card between columns or use its move selector.
- Moving a task to `Done` applies `state/done` and closes the issue.
- Moving a task out of `Done` applies the selected state label and reopens the issue.
- Non-workflow labels are preserved.
- Pull requests are counted in the header but are not shown as task cards.

The server binds only to `127.0.0.1`. The Gitea token remains in the server process and is never sent to the browser.

## Test

```powershell
node --test tools\kanban\test.mjs
```

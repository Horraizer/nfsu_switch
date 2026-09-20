$ErrorActionPreference = "Stop"

if (-not $env:GITEA_TOKEN) {
    throw "GITEA_TOKEN is not set."
}

$port = if ($env:KANBAN_PORT) { $env:KANBAN_PORT } else { "4173" }
$url = "http://127.0.0.1:$port"
$server = Start-Process -FilePath "node" -ArgumentList @("server.mjs") -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru

try {
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        try {
            Invoke-WebRequest -Uri $url -Method Head -TimeoutSec 1 | Out-Null
            if ($env:KANBAN_SMOKE -eq "1") {
                exit 0
            }
            Start-Process $url
            Wait-Process -Id $server.Id
            exit 0
        }
        catch {
            if ($server.HasExited) {
                throw "Kanban server stopped before startup completed."
            }
            Start-Sleep -Milliseconds 100
        }
    }
    throw "Kanban server did not start at $url."
}
finally {
    if (-not $server.HasExited) {
        Stop-Process -Id $server.Id
    }
}

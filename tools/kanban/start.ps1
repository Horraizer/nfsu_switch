$ErrorActionPreference = "Stop"

if (-not $env:GITEA_TOKEN) {
    throw "GITEA_TOKEN is not set."
}

$port = if ($env:KANBAN_PORT) { $env:KANBAN_PORT } else { "4173" }
if ($port -notmatch '^[0-9]+$') {
    throw "KANBAN_PORT must be a decimal port number, got '$port'."
}
$url = "http://127.0.0.1:$port"
$instance = [guid]::NewGuid().ToString()

# The child inherits these. KANBAN_PARENT_PID is what lets the server shut
# itself down if this launcher dies without running its finally block, which is
# what happens when the console window is closed. KANBAN_INSTANCE is what lets
# the readiness probe below prove that the listener is the process we spawned.
$previousInstance = $env:KANBAN_INSTANCE
$previousParent = $env:KANBAN_PARENT_PID
$server = $null
try {
    $env:KANBAN_INSTANCE = $instance
    $env:KANBAN_PARENT_PID = $PID
    $server = Start-Process -FilePath "node" -ArgumentList @("server.mjs") `
        -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru
}
finally {
    $env:KANBAN_INSTANCE = $previousInstance
    $env:KANBAN_PARENT_PID = $previousParent
}

try {
    $identity = $null
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if ($server.HasExited) {
            throw "Kanban server exited with code $($server.ExitCode) before startup completed."
        }
        try {
            $identity = Invoke-RestMethod -Uri "$url/api/instance" -TimeoutSec 2
        }
        catch {
            $identity = $null
        }
        if ($identity) {
            break
        }
        Start-Sleep -Milliseconds 100
    }

    if (-not $identity) {
        throw "Kanban server did not answer at $url."
    }

    # A listening port is not proof of readiness: a stale server left over from
    # an earlier run answers just as well, and it would still be holding the
    # token this launcher was started to replace.
    if ($identity.instance -ne $instance -or [int]$identity.pid -ne $server.Id) {
        throw "$url is owned by process $($identity.pid), not the server just started (pid $($server.Id)). Stop that process or set KANBAN_PORT."
    }

    if ($env:KANBAN_SMOKE -eq "1") {
        exit 0
    }

    Start-Process $url
    Write-Host "Kanban server pid $($server.Id) on $url."
    Write-Host "Press Ctrl+C or close this window to stop it."
    Wait-Process -Id $server.Id -ErrorAction SilentlyContinue
    exit 0
}
finally {
    if ($null -ne $server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
    }
}

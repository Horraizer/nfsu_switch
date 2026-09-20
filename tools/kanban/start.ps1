$ErrorActionPreference = "Stop"

# The startup budget is wall-clock. An attempt count multiplied by a probe that
# times out, instead of failing fast, is minutes rather than seconds, and the
# launcher then kills a server that was healthy the whole time.
$readyTimeoutSeconds = 20
$probeTimeoutSeconds = 2
$probeIntervalMs = 100
$exitGraceSeconds = 2
$stopConfirmAttempts = 20

if (-not $env:GITEA_TOKEN) {
    throw "GITEA_TOKEN is not set."
}

$port = if ($env:KANBAN_PORT) { $env:KANBAN_PORT } else { "4173" }
if ($port -notmatch '^[0-9]+$') {
    throw "KANBAN_PORT must be a decimal port number, got '$port'."
}
$url = "http://127.0.0.1:$port"
$instance = [guid]::NewGuid().ToString()

# PowerShell 6+ honours HTTP_PROXY and ALL_PROXY even for 127.0.0.1, so a
# corporate proxy, Fiddler or mitmproxy makes every loopback probe time out and
# the launcher blames the server. Windows PowerShell 5.1 bypasses local
# addresses already and has no -NoProxy to pass.
$probeOptions = @{}
if ((Get-Command Invoke-RestMethod).Parameters.ContainsKey("NoProxy")) {
    $probeOptions["NoProxy"] = $true
}

function Get-InstanceIdentity {
    try {
        return Invoke-RestMethod -Uri "$url/api/instance" -TimeoutSec $probeTimeoutSeconds @probeOptions
    }
    catch {
        return $null
    }
}

function Test-OwnedIdentity {
    param($Identity)

    # The per-run GUID reaches only the process this launcher spawned, and only
    # through its environment, so an instance match is the ownership proof. A
    # pid is not: when node resolves through a shim -- Chocolatey, Scoop, Volta,
    # a corporate node.cmd -- the spawned handle is the shim while the listener
    # is its child, and a foreign listener reports its own pid about itself.
    if ($null -eq $Identity) {
        return $false
    }
    return ([string] $Identity.instance) -eq $instance
}

function Get-IdentityPid {
    param($Identity)

    $parsed = 0
    if ($null -ne $Identity -and [int]::TryParse([string] $Identity.pid, [ref] $parsed) -and $parsed -gt 0) {
        return $parsed
    }
    return 0
}

function Read-TextFileShared {
    param([string] $Path)

    try {
        $stream = [System.IO.File]::Open(
            $Path,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite)
    }
    catch {
        return ""
    }
    try {
        $reader = New-Object System.IO.StreamReader($stream)
        return $reader.ReadToEnd()
    }
    catch {
        return ""
    }
    finally {
        $stream.Dispose()
    }
}

function Remove-StaleServerLogs {
    # A launcher that is force-killed never reaches its own cleanup, so that
    # run's redirected output stays behind. An hour old and not locked by a
    # live child is the safe definition of abandoned; anything a running
    # launcher owns is held open and is skipped.
    $cutoff = (Get-Date).AddHours(-1)
    $files = @()
    try {
        $files = @(Get-ChildItem -LiteralPath ([System.IO.Path]::GetTempPath()) -Filter "kanban-*.txt" -File -ErrorAction Stop)
    }
    catch {
        return
    }
    foreach ($file in $files) {
        if ($file.Name -notmatch '^kanban-[0-9a-fA-F-]{36}\.(out|err)\.txt$') {
            continue
        }
        if ($file.LastWriteTime -gt $cutoff) {
            continue
        }
        try {
            Remove-Item -LiteralPath $file.FullName -Force -ErrorAction Stop
        }
        catch {
        }
    }
}

function Get-ServerOutputSuffix {
    # The child runs in a hidden window, so everything it says about why it
    # failed is otherwise thrown away and two different causes look alike.
    $text = (Read-TextFileShared $stdoutPath) + (Read-TextFileShared $stderrPath)
    $lines = @($text -split "\r?\n" | Where-Object { $_.Trim() -ne "" })
    if ($lines.Count -eq 0) {
        return ""
    }
    return " Server output: " + ((@($lines | Select-Object -Last 5)) -join " | ")
}

function Get-PortOwnerDescription {
    # The owner the OS reports, not the pid a foreign listener chose to claim
    # about itself: the launcher sends the operator after a real process.
    $connections = @()
    try {
        $connections = @(Get-NetTCPConnection -LocalPort ([int] $port) -State Listen -ErrorAction Stop)
    }
    catch {
        $connections = @()
    }
    foreach ($connection in $connections) {
        $ownerPid = [int] $connection.OwningProcess
        $name = ""
        try {
            $name = (Get-Process -Id $ownerPid -ErrorAction Stop).ProcessName
        }
        catch {
            $name = ""
        }
        if ($name) {
            return "process $ownerPid ($name)"
        }
        return "process $ownerPid"
    }
    return "another process"
}

function Get-ProcessTree {
    param([int] $RootPid)

    # pid plus start time, because Windows reuses pids and this list is acted on
    # after the walk. A pid that is already gone is skipped rather than
    # descended into: Windows never clears ParentProcessId when a parent dies,
    # so a walk rooted at a reused pid would collect strangers.
    $entries = @()
    $seen = @{}
    $rootStartedUtc = $null
    $queue = New-Object System.Collections.Generic.Queue[int]
    $queue.Enqueue($RootPid)
    while ($queue.Count -gt 0) {
        $current = $queue.Dequeue()
        if ($seen.ContainsKey($current)) {
            continue
        }
        $seen[$current] = $true

        $started = $null
        try {
            $started = (Get-Process -Id $current -ErrorAction Stop).StartTime
        }
        catch {
            continue
        }
        if ($null -eq $rootStartedUtc) {
            # Slack, because the child clock comes from CIM and the parent's
            # from the process table, and only a stranger predates the root by
            # a meaningful margin.
            $rootStartedUtc = $started.ToUniversalTime().AddSeconds(-5)
        }
        $entries += [pscustomobject]@{ Id = $current; Started = $started }

        $children = @()
        try {
            $children = @(Get-CimInstance -ClassName Win32_Process -Filter "ParentProcessId=$current" -ErrorAction Stop)
        }
        catch {
            $children = @()
        }
        foreach ($child in $children) {
            $childId = [int] $child.ProcessId
            if ($childId -le 0 -or $childId -eq $current) {
                continue
            }
            $createdUtc = $null
            try {
                $createdUtc = ([datetime] $child.CreationDate).ToUniversalTime()
            }
            catch {
                $createdUtc = $null
            }
            if ($null -ne $createdUtc -and $createdUtc -lt $rootStartedUtc) {
                # Older than the tree it claims to belong to: this is a stale
                # parent link, not a child of the process being stopped.
                continue
            }
            $queue.Enqueue($childId)
        }
    }
    return $entries
}

function Stop-ProcessEntries {
    param($Entries)

    foreach ($entry in @($Entries)) {
        if ($null -eq $entry) {
            continue
        }
        try {
            $live = Get-Process -Id $entry.Id -ErrorAction Stop
            if ($null -ne $entry.Started -and $live.StartTime -ne $entry.Started) {
                # The pid was reused between the walk and the kill.
                continue
            }
            Stop-Process -InputObject $live -Force -ErrorAction Stop
        }
        catch {
        }
    }
}

function Stop-OwnedListener {
    # Last resort, and the one that holds whatever node resolved to: a listener
    # is stopped only if it proves, with this run's one-time GUID, that it is
    # the server this launcher started. A foreign process is never touched.
    for ($attempt = 0; $attempt -lt $stopConfirmAttempts; $attempt++) {
        $identity = Get-InstanceIdentity
        if (-not (Test-OwnedIdentity $identity)) {
            return
        }
        $ownedPid = Get-IdentityPid $identity
        if ($ownedPid -eq 0) {
            return
        }
        Stop-ProcessEntries (Get-ProcessTree $ownedPid)
        Start-Sleep -Milliseconds $probeIntervalMs
    }
}

function Stop-SpawnedServer {
    # Three passes, because none of them is sufficient alone. The spawned handle
    # may be a shim whose node child is the process holding the token; the
    # identified pid is the real listener; and the GUID probe catches a child
    # this launcher never got a handle to.
    if ($null -ne $server) {
        $root = $null
        try {
            $root = Get-Process -Id $server.Id -ErrorAction Stop
        }
        catch {
            $root = $null
        }
        if ($null -ne $root -and ($null -eq $serverStartedAt -or $root.StartTime -eq $serverStartedAt)) {
            Stop-ProcessEntries (Get-ProcessTree $server.Id)
        }
    }
    if ($serverPid -gt 0) {
        Stop-ProcessEntries (Get-ProcessTree $serverPid)
    }
    Stop-OwnedListener
}

function Test-SpawnedExited {
    try {
        return [bool] $server.HasExited
    }
    catch {
        return $true
    }
}

function Get-SpawnedExitCode {
    try {
        return [int] $server.ExitCode
    }
    catch {
        return -1
    }
}

function Format-StartupFailure {
    param([int] $ExitCode)

    if ($ExitCode -eq 2) {
        # server.mjs reserves 2 for EADDRINUSE. Its own explanation goes to the
        # stdout of a hidden window, so the distinct code has to mean something
        # here or the most likely first-run failure is indistinguishable from a
        # bad port number.
        return "Port $port is already in use, so the kanban server could not start. It is held by $(Get-PortOwnerDescription). Stop that process or set KANBAN_PORT.$(Get-ServerOutputSuffix)"
    }
    return "Kanban server exited with code $ExitCode before startup completed.$(Get-ServerOutputSuffix)"
}

Remove-StaleServerLogs
$stdoutPath = Join-Path ([System.IO.Path]::GetTempPath()) "kanban-$instance.out.txt"
$stderrPath = Join-Path ([System.IO.Path]::GetTempPath()) "kanban-$instance.err.txt"
$previousInstance = $env:KANBAN_INSTANCE
$previousParent = $env:KANBAN_PARENT_PID
$server = $null
$serverPid = 0
$serverStartedAt = $null

# The child inherits these. KANBAN_PARENT_PID is the pid of the process running
# this script, which is the process whose death must reap the server when this
# script never reaches its finally block: a closed console window, or a
# force-killed session. KANBAN_INSTANCE is what lets the readiness probe below
# prove that the listener is the process this run spawned.
try {
    $env:KANBAN_INSTANCE = $instance
    $env:KANBAN_PARENT_PID = $PID
    $server = Start-Process -FilePath "node" -ArgumentList @("server.mjs") `
        -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
}
finally {
    $env:KANBAN_INSTANCE = $previousInstance
    $env:KANBAN_PARENT_PID = $previousParent
}

# Windows PowerShell 5.1 reports a null ExitCode for a Start-Process handle
# unless the process handle was opened while the child was still alive.
try {
    $null = $server.Handle
    $serverStartedAt = $server.StartTime
}
catch {
    $serverStartedAt = $null
}

try {
    $identity = $null
    $exitDeadline = $null
    $deadline = (Get-Date).AddSeconds($readyTimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $identity = Get-InstanceIdentity
        if ($null -ne $identity) {
            break
        }

        # A shim that hands off to node.exe and returns leaves the spawned
        # handle exited while the real server is still binding, so the handle's
        # exit is only fatal once a grace period passes with nothing answering.
        if (Test-SpawnedExited) {
            if ($null -eq $exitDeadline) {
                $exitDeadline = (Get-Date).AddSeconds($exitGraceSeconds)
            }
            elseif ((Get-Date) -ge $exitDeadline) {
                throw (Format-StartupFailure (Get-SpawnedExitCode))
            }
        }
        Start-Sleep -Milliseconds $probeIntervalMs
    }

    if ($null -eq $identity) {
        throw "Kanban server did not answer at $url within $readyTimeoutSeconds seconds.$(Get-ServerOutputSuffix)"
    }

    # A listening port is not proof of readiness: a stale server left over from
    # an earlier run answers just as well, and it would still be holding the
    # token this launcher was started to replace.
    if (-not (Test-OwnedIdentity $identity)) {
        throw "$url is owned by $(Get-PortOwnerDescription), not the server this launcher started. Stop that process or set KANBAN_PORT."
    }

    $serverPid = Get-IdentityPid $identity
    if ($serverPid -eq 0) {
        throw "$url answered for this launcher's instance but reported no usable pid."
    }

    if ($env:KANBAN_SMOKE -eq "1") {
        exit 0
    }

    if ($env:KANBAN_OPEN_BROWSER -ne "0") {
        Start-Process $url
    }
    Write-Host "Kanban server pid $serverPid on $url."
    Write-Host "Press Ctrl+C or close this window to stop it."
    Wait-Process -Id $serverPid -ErrorAction SilentlyContinue
    exit 0
}
finally {
    Stop-SpawnedServer
    Remove-Item -LiteralPath $stdoutPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
}

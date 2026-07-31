# Start Unreal MCP compatibility proxy (Grok <-> UE Editor).
# Prerequisites: Unreal Editor running, Unreal MCP auto-start on port 8000.
# Proxy: http://127.0.0.1:8010/mcp
#
# Uses Tools/_spawn_ue_mcp_proxy.py so the server process breaks away from
# parent Job Objects (agent shells kill non-detached children when a command ends).

$ErrorActionPreference = "Stop"
$ToolsDir = $PSScriptRoot
if (-not (Test-Path (Join-Path $ToolsDir "_spawn_ue_mcp_proxy.py"))) {
    $ToolsDir = "F:\Unreal Projects\newtest\mp\Tools"
}
$Root = Split-Path -Parent $ToolsDir
$Spawner = Join-Path $ToolsDir "_spawn_ue_mcp_proxy.py"

function Test-PortOpen([int]$Port) {
    try {
        $c = New-Object System.Net.Sockets.TcpClient
        $iar = $c.BeginConnect("127.0.0.1", $Port, $null, $null)
        $ok = $iar.AsyncWaitHandle.WaitOne(800)
        if (-not $ok) { $c.Close(); return $false }
        $c.EndConnect($iar)
        $c.Close()
        return $true
    } catch {
        return $false
    }
}

if (-not (Test-PortOpen 8000)) {
    Write-Host "WARNING: nothing listening on 127.0.0.1:8000"
    Write-Host "  Open Unreal Editor with ModelContextProtocol Auto Start,"
    Write-Host "  or run console: ModelContextProtocol.StartServer"
}

if (-not (Test-Path $Spawner)) {
    Write-Host "ERROR: missing $Spawner"
    exit 1
}

Write-Host "Starting detached proxy via: $Spawner"
# Run spawner in-process; it only lives long enough to Popen the real server
& python -u $Spawner
$code = $LASTEXITCODE

if ($code -eq 0 -and (Test-PortOpen 8010)) {
    $owner = (Get-NetTCPConnection -LocalPort 8010 -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1).OwningProcess
    Write-Host "OK  http://127.0.0.1:8010/mcp  ->  http://127.0.0.1:8000/mcp  (pid=$owner)"
    Write-Host "  logs: $ToolsDir\_ue_mcp_proxy.log"
    exit 0
}

Write-Host "FAILED to start proxy (exit=$code)"
$err = Join-Path $ToolsDir "_ue_mcp_proxy.err.log"
if (Test-Path $err) {
    Write-Host "--- err log ---"
    Get-Content $err -Tail 40
}
exit 1

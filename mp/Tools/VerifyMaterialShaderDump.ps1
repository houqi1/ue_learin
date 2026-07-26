# Verify Unreal material CustomExpression compile products under Saved/ShaderDebugInfo.
# Usage:
#   powershell -File Tools/VerifyMaterialShaderDump.ps1 -MaterialName "M_PhoneixGlass" -Require "RefractStrength","StepLen"
#   powershell -File Tools/VerifyMaterialShaderDump.ps1 -MaterialName "M_PhoneixGlass_Back" -Require "StepDistDbg","DebugOutputRefractR > 2.5"
#
# Exit 0 = all markers found in the newest material hash dump.
# Exit 1 = missing markers / no dump / dump too old.

param(
    [Parameter(Mandatory = $true)]
    [string]$MaterialName,

    # Comma-separated or repeated: -Require "A,B,C"  OR  -Require A,B,C
    [Parameter(Mandatory = $true)]
    [string[]]$Require,

    [string]$ShaderDebugRoot = "",

    [int]$MaxAgeMinutes = 120,

    [switch]$PreferBasePass
)

# Normalize -Require: allow "a,b,c" single string
$Require = @($Require | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })

$ErrorActionPreference = "Stop"

if (-not $ShaderDebugRoot) {
    $projRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    # Tools is under project root: .../mp/Tools
    $projRoot = Split-Path $PSScriptRoot -Parent
    $ShaderDebugRoot = Join-Path $projRoot "Saved\ShaderDebugInfo\PCD3D_SM6"
}

if (-not (Test-Path $ShaderDebugRoot)) {
    Write-Host "FAIL: ShaderDebugRoot not found: $ShaderDebugRoot"
    Write-Host "  Ensure editor has r.DumpShaderDebugInfo=1 and materials have been compiled."
    exit 1
}

$dirs = Get-ChildItem $ShaderDebugRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "${MaterialName}_*" } |
    Sort-Object LastWriteTime -Descending

if (-not $dirs -or $dirs.Count -eq 0) {
    Write-Host "FAIL: No dump folders matching ${MaterialName}_*"
    Write-Host "  Root: $ShaderDebugRoot"
    exit 1
}

$latest = $dirs[0]
$ageMin = [int]((Get-Date) - $latest.LastWriteTime).TotalMinutes
Write-Host "Latest dump: $($latest.Name)"
Write-Host "  Time: $($latest.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')) (age ${ageMin}m)"

if ($ageMin -gt $MaxAgeMinutes) {
    Write-Host "WARN: dump older than MaxAgeMinutes=$MaxAgeMinutes (may be stale)"
}

$usfs = Get-ChildItem $latest.FullName -Recurse -Filter "*.usf" -ErrorAction SilentlyContinue
if ($PreferBasePass) {
    $bp = $usfs | Where-Object { $_.Name -eq "BasePassPixelShader.usf" } | Sort-Object LastWriteTime -Descending
    if ($bp) { $usfs = $bp }
}

$bestFile = $null
$bestHits = -1
$bestDetail = @{}

foreach ($f in $usfs) {
    $c = Get-Content $f.FullName -Raw -ErrorAction SilentlyContinue
    if (-not $c) { continue }
    $hits = 0
    $detail = @{}
    foreach ($pat in $Require) {
        $ok = $c -match [regex]::Escape($pat) -or $c -match $pat
        $detail[$pat] = [bool]$ok
        if ($ok) { $hits++ }
    }
    if ($hits -gt $bestHits) {
        $bestHits = $hits
        $bestFile = $f
        $bestDetail = $detail
    }
    if ($hits -eq $Require.Count) { break }
}

if (-not $bestFile) {
    Write-Host "FAIL: no .usf under dump folder"
    exit 1
}

Write-Host "Checked: $($bestFile.FullName.Replace($ShaderDebugRoot + '\', ''))"
$allOk = $true
foreach ($pat in $Require) {
    $ok = $bestDetail[$pat]
    if ($ok) {
        Write-Host "  OK   $pat"
    } else {
        Write-Host "  MISS $pat"
        $allOk = $false
    }
}

# Extra: print CustomExpression0 signature snippet if present
$c = Get-Content $bestFile.FullName -Raw
$m = [regex]::Match($c, "float[234]? CustomExpression0\([^)]{0,500}\)")
if ($m.Success) {
    $sig = $m.Value
    if ($sig.Length -gt 280) { $sig = $sig.Substring(0, 280) + "..." }
    Write-Host "  SIG  $sig"
}

if ($allOk) {
    Write-Host "PASS: all required markers present"
    exit 0
} else {
    Write-Host "FAIL: missing markers (shader map likely stale — force recompile)"
    exit 1
}

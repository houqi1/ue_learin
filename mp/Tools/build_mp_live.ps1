# Build / Live Coding helper for mp module (ScreenFluidActor etc.)
# Usage:
#   .\Tools\build_mp_live.ps1            # Live Coding if editor open, else full build
#   .\Tools\build_mp_live.ps1 -Full      # Stop editor, full DLL rebuild, restart editor

param([switch]$Full)

$ErrorActionPreference = 'Stop'
$proj = 'F:\Unreal Projects\newtest\mp\mp.uproject'
$build = 'F:\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat'
$editor = 'F:\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$mpDir = 'F:\Unreal Projects\newtest\mp\Intermediate\Build\Win64\x64\UnrealEditor\Development\mp'

function Fix-LiveCodingRsp {
    Get-ChildItem $mpDir -Filter '*.lc.rsp' -ErrorAction SilentlyContinue | ForEach-Object {
        $c = Get-Content $_.FullName -Raw
        $fixed = $c -replace '/sourceDependencies"', '/sourceDependencies "'
        if ($fixed -ne $c) {
            Set-Content $_.FullName $fixed -NoNewline -Encoding utf8
            Write-Host "Fixed sourceDependencies space: $($_.Name)"
        }
    }
}

$editorRunning = [bool](Get-Process UnrealEditor -ErrorAction SilentlyContinue)

if ($Full -or -not $editorRunning) {
    if ($editorRunning) {
        Write-Host 'Stopping UnrealEditor for full link...'
        Get-Process UnrealEditor, LiveCodingConsole -ErrorAction SilentlyContinue | Stop-Process -Force
        Start-Sleep -Seconds 3
    }
    Fix-LiveCodingRsp
    & $build mpEditor Win64 Development "-Project=$proj" -WaitMutex
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Host 'DLL:' (Get-Item 'F:\Unreal Projects\newtest\mp\Binaries\Win64\UnrealEditor-mp.dll').LastWriteTime
    Write-Host 'Starting editor...'
    Start-Process -FilePath $editor -ArgumentList "`"$proj`""
    exit 0
}

# Live Coding path (editor open)
Fix-LiveCodingRsp
& $build mpEditor Win64 Development "-Project=$proj" -WaitMutex -LiveCoding
if ($LASTEXITCODE -ne 0) {
    Write-Host 'LiveCoding build failed; fixing rsp and retrying ScreenFluidActor cl...'
    Fix-LiveCodingRsp
    $lcRsp = Join-Path $mpDir 'ScreenFluidActor.cpp.obj.lc.rsp'
    if (Test-Path $lcRsp) {
        $cl = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433\bin\Hostx64\x64\cl.exe'
        Push-Location 'F:\UnrealEngine-5.8\Engine\Source'
        & $cl "@$lcRsp"
        Pop-Location
        & $build mpEditor Win64 Development "-Project=$proj" -WaitMutex -LiveCoding
    }
}
exit $LASTEXITCODE

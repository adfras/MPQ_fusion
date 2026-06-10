[CmdletBinding()]
param(
    [int]$MaxParallelActions = 4,
    [int]$StallSeconds = 120,
    [switch]$CloseEditor,
    [switch]$KillStuckBuild
)

$ErrorActionPreference = "Stop"

$ProjectRoot = "D:\Epic\Unreal_Projects\TestingKit5"
$ProjectFile = Join-Path $ProjectRoot "TestingKit5.uproject"
$BuildBat = "D:\Epic\UE_5.8\Engine\Build\BatchFiles\Build.bat"
$BuildLogDir = Join-Path $ProjectRoot "Saved\CodexAgent\BuildLogs"
$UbtLog = Join-Path $env:LOCALAPPDATA "UnrealBuildTool\Log.txt"

function Stop-ProcessTree {
    param([int]$RootPid)

    $all = @(Get-CimInstance Win32_Process)
    $queue = New-Object System.Collections.Generic.Queue[int]
    $seen = @{}
    $queue.Enqueue($RootPid)

    while ($queue.Count -gt 0) {
        $current = $queue.Dequeue()
        if ($seen.ContainsKey($current)) {
            continue
        }

        $seen[$current] = $true
        foreach ($child in $all | Where-Object { $_.ParentProcessId -eq $current }) {
            $queue.Enqueue([int]$child.ProcessId)
        }
    }

    foreach ($processId in ($seen.Keys | Sort-Object -Descending)) {
        Stop-Process -Id $processId -Force -ErrorAction SilentlyContinue
    }
}

function Get-TestingKitBuildProcesses {
    Get-CimInstance Win32_Process |
        Where-Object {
            $_.Name -in @("UnrealBuildTool.exe", "dotnet.exe", "cl.exe", "link.exe") -and
            $_.CommandLine -match "UnrealBuildTool|TestingKit5|MediaPipeDriver|Intermediate\\Build\\Win64"
        }
}

if ($CloseEditor) {
    $editors = @(Get-Process UnrealEditor -ErrorAction SilentlyContinue)
    foreach ($editor in $editors) {
        [void]$editor.CloseMainWindow()
    }
    if ($editors.Count -gt 0) {
        Start-Sleep -Seconds 10
    }
    Get-Process UnrealEditor -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Get-Process LiveCodingConsole -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

$blockingEditors = @(Get-Process UnrealEditor, LiveCodingConsole -ErrorAction SilentlyContinue)
if ($blockingEditors.Count -gt 0) {
    $names = ($blockingEditors | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ", "
    throw "Unreal editor or Live Coding is still running: $names. Close it first or rerun with -CloseEditor."
}

if ($KillStuckBuild) {
    $stuck = @(Get-TestingKitBuildProcesses)
    foreach ($proc in $stuck) {
        Stop-ProcessTree -RootPid ([int]$proc.ProcessId)
    }
    Start-Sleep -Seconds 2
}

$remaining = @(Get-TestingKitBuildProcesses)
if ($remaining.Count -gt 0) {
    $names = ($remaining | ForEach-Object { "$($_.Name):$($_.ProcessId)" }) -join ", "
    throw "TestingKit5 build processes are still running: $names. Rerun with -KillStuckBuild only if they are stale."
}

New-Item -ItemType Directory -Force -Path $BuildLogDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$stdoutLog = Join-Path $BuildLogDir "TestingKit5Editor_fast_$stamp.out.log"
$stderrLog = Join-Path $BuildLogDir "TestingKit5Editor_fast_$stamp.err.log"

$arguments = @(
    "TestingKit5Editor",
    "Win64",
    "Development",
    "-Project=`"$ProjectFile`"",
    "-WaitMutex",
    "-NoUBA",
    "-UBANoDetour",
    "-MaxParallelActions=$MaxParallelActions"
)

Write-Host "Fast build command:"
Write-Host "  $BuildBat $($arguments -join ' ')"
Write-Host "Logs:"
Write-Host "  $stdoutLog"
Write-Host "  $stderrLog"

$process = Start-Process -FilePath $BuildBat `
    -ArgumentList $arguments `
    -WorkingDirectory $ProjectRoot `
    -NoNewWindow `
    -RedirectStandardOutput $stdoutLog `
    -RedirectStandardError $stderrLog `
    -PassThru

$lastProgress = Get-Date
$lastOutLength = 0
$lastErrLength = 0
$lastUbtWrite = if (Test-Path $UbtLog) { (Get-Item $UbtLog).LastWriteTimeUtc } else { [datetime]::MinValue }

while (-not $process.HasExited) {
    Start-Sleep -Seconds 5

    $outLength = if (Test-Path $stdoutLog) { (Get-Item $stdoutLog).Length } else { 0 }
    $errLength = if (Test-Path $stderrLog) { (Get-Item $stderrLog).Length } else { 0 }
    $ubtWrite = if (Test-Path $UbtLog) { (Get-Item $UbtLog).LastWriteTimeUtc } else { [datetime]::MinValue }

    if ($outLength -ne $lastOutLength -or $errLength -ne $lastErrLength -or $ubtWrite -ne $lastUbtWrite) {
        $lastProgress = Get-Date
        $lastOutLength = $outLength
        $lastErrLength = $errLength
        $lastUbtWrite = $ubtWrite
    }

    $idleSeconds = ((Get-Date) - $lastProgress).TotalSeconds
    if ($idleSeconds -ge $StallSeconds) {
        Stop-ProcessTree -RootPid $process.Id
        throw "Build stalled for $([int]$idleSeconds)s with no stdout/stderr/UBT log progress. Killed build tree. See $stdoutLog and $stderrLog."
    }
}

$exitCode = $process.ExitCode
if (Test-Path $stdoutLog) {
    Get-Content $stdoutLog -Tail 120
}
if ($exitCode -ne 0) {
    if (Test-Path $stderrLog) {
        Get-Content $stderrLog -Tail 120
    }
    throw "Fast build failed with exit code $exitCode. See $stdoutLog and $stderrLog."
}

Write-Host "Fast build succeeded."

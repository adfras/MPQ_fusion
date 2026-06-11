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
# Cache the process handle now; without this, reading .ExitCode after the process exits
# can return $null and a successful build is misreported as a failure.
$null = $process.Handle

function Get-TestingKitCompilerActivity {
    # Composite activity signature of this project's build processes. Large single-TU
    # compiles (the MediaPipe anim-instance TU runs minutes with no stdout/UBT-log output,
    # sometimes at near-zero CPU while page-mapping PCHs) still move CPU time, IO transfer
    # counts, or page-fault counts every few seconds. ANY change in the signature counts as
    # progress; only a build whose processes are completely frozen for the stall window is
    # treated as stalled.
    $cpu = 0.0; $io = [double]0; $faults = [double]0; $count = 0
    foreach ($proc in @(Get-TestingKitBuildProcesses)) {
        $cpu += ([double]$proc.UserModeTime + [double]$proc.KernelModeTime) / 1e7
        $io += [double]$proc.ReadTransferCount + [double]$proc.WriteTransferCount
        $faults += [double]$proc.PageFaults
        $count++
    }
    return [PSCustomObject]@{ Cpu = [math]::Round($cpu, 2); Io = $io; Faults = $faults; Count = $count }
}

$lastProgress = Get-Date
$lastOutLength = 0
$lastErrLength = 0
$lastUbtWrite = if (Test-Path $UbtLog) { (Get-Item $UbtLog).LastWriteTimeUtc } else { [datetime]::MinValue }
$lastActivity = Get-TestingKitCompilerActivity

while (-not $process.HasExited) {
    Start-Sleep -Seconds 5

    $outLength = if (Test-Path $stdoutLog) { (Get-Item $stdoutLog).Length } else { 0 }
    $errLength = if (Test-Path $stderrLog) { (Get-Item $stderrLog).Length } else { 0 }
    $ubtWrite = if (Test-Path $UbtLog) { (Get-Item $UbtLog).LastWriteTimeUtc } else { [datetime]::MinValue }
    $activity = Get-TestingKitCompilerActivity

    $logsAdvanced = ($outLength -ne $lastOutLength -or $errLength -ne $lastErrLength -or $ubtWrite -ne $lastUbtWrite)
    $procsAdvanced = ($activity.Cpu -ne $lastActivity.Cpu -or
        $activity.Io -ne $lastActivity.Io -or
        $activity.Faults -ne $lastActivity.Faults -or
        $activity.Count -ne $lastActivity.Count)

    if ($logsAdvanced -or $procsAdvanced) {
        $lastProgress = Get-Date
        $lastOutLength = $outLength
        $lastErrLength = $errLength
        $lastUbtWrite = $ubtWrite
    }
    $lastActivity = $activity

    $idleSeconds = ((Get-Date) - $lastProgress).TotalSeconds
    if ($idleSeconds -ge $StallSeconds) {
        Stop-ProcessTree -RootPid $process.Id
        throw "Build stalled for $([int]$idleSeconds)s: no stdout/stderr/UBT log progress AND build processes frozen (procs=$($activity.Count) cpu=$($activity.Cpu)s io=$($activity.Io)B faults=$($activity.Faults) all unchanged). Killed build tree. See $stdoutLog and $stderrLog."
    }
}

$process.WaitForExit()
$exitCode = $process.ExitCode
if ($null -eq $exitCode) {
    # Last-resort fallback: trust UBT's own result line rather than failing a good build.
    $resultLine = if (Test-Path $stdoutLog) { Select-String -Path $stdoutLog -Pattern "^Result: " | Select-Object -Last 1 } else { $null }
    $exitCode = if ($resultLine -and $resultLine.Line -match "Succeeded") { 0 } else { 1 }
}
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

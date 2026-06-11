param(
    [int]$IntervalSeconds = 2,
    [int]$DurationSeconds = 0,
    [int]$WaitForMirrorTimeoutSeconds = 90,
    [string]$RunName = "",
    [string]$OutputRoot = "Saved/QuestScreenshots",
    [string]$StopFile = "",
    [string]$OculusMirrorPath = "$env:ProgramFiles\Oculus\Support\oculus-diagnostics\OculusMirror.exe",
    [string]$TitlePattern = "^Oculus Mirror$",
    [string]$BridgeUrl = "http://127.0.0.1:8765",
    [string]$LogPath = "Saved/Logs/TestingKit3.log",
    [int]$DiagnosticsDelaySeconds = 3,
    [switch]$NoUnrealDiagnostics,
    [double]$MinimumNonDarkRatio = 0.01,
    [switch]$NoLaunchMirror,
    [switch]$NoFocus,
    [switch]$SkipArmTwitchAnalysis,
    [double]$ArmTwitchPositionJumpThresholdCm = 6.0,
    [double]$ArmTwitchAngleJumpThresholdDeg = 15.0,
    [double]$ArmTwitchSpeedThresholdCmSec = 120.0
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public class CodexQuestMirrorEvidenceNative {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
"@

function ConvertTo-WorkspaceRelativePath {
    param([string]$Path)

    $full = [System.IO.Path]::GetFullPath($Path)
    $root = [System.IO.Path]::GetFullPath((Get-Location).Path).TrimEnd("\", "/")
    if ($full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        return ($full.Substring($root.Length).TrimStart("\", "/") -replace "\\", "/")
    }
    return $full
}

function Get-VisibleWindows {
    $windows = New-Object System.Collections.Generic.List[object]
    [CodexQuestMirrorEvidenceNative]::EnumWindows({
        param($Handle, $LParam)

        if ([CodexQuestMirrorEvidenceNative]::IsWindowVisible($Handle)) {
            $titleBuilder = New-Object System.Text.StringBuilder 512
            [void][CodexQuestMirrorEvidenceNative]::GetWindowText($Handle, $titleBuilder, $titleBuilder.Capacity)
            if ($titleBuilder.Length -gt 0) {
                $classBuilder = New-Object System.Text.StringBuilder 256
                [void][CodexQuestMirrorEvidenceNative]::GetClassName($Handle, $classBuilder, $classBuilder.Capacity)

                $rect = New-Object CodexQuestMirrorEvidenceNative+RECT
                [void][CodexQuestMirrorEvidenceNative]::GetWindowRect($Handle, [ref]$rect)

                $processIdValue = 0
                [void][CodexQuestMirrorEvidenceNative]::GetWindowThreadProcessId($Handle, [ref]$processIdValue)
                $processName = ""
                try {
                    $processName = (Get-Process -Id $processIdValue -ErrorAction Stop).ProcessName
                }
                catch {
                    $processName = ""
                }

                $windows.Add([pscustomobject]@{
                    Handle = $Handle.ToInt64()
                    Title = $titleBuilder.ToString()
                    Class = $classBuilder.ToString()
                    ProcessName = $processName
                    ProcessId = [int]$processIdValue
                    X = $rect.Left
                    Y = $rect.Top
                    Width = $rect.Right - $rect.Left
                    Height = $rect.Bottom - $rect.Top
                }) | Out-Null
            }
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null

    return $windows.ToArray()
}

function Get-OculusMirrorWindow {
    $candidates = @(Get-VisibleWindows | Where-Object {
        $_.Title -match $TitlePattern -and
        $_.ProcessName -match "^OculusMirror$"
    } | Sort-Object @{ Expression = { $_.Width * $_.Height }; Descending = $true })

    if ($candidates.Count -eq 0) {
        return $null
    }

    $selected = $candidates[0]
    $handle = [IntPtr]([int64]$selected.Handle)
    [void][CodexQuestMirrorEvidenceNative]::ShowWindow($handle, 9)
    Start-Sleep -Milliseconds 150

    $refreshed = @(Get-VisibleWindows | Where-Object { $_.Handle -eq $selected.Handle } | Select-Object -First 1)
    if ($refreshed.Count -gt 0) {
        return $refreshed[0]
    }

    return $selected
}

function Ensure-OculusMirror {
    if ($NoLaunchMirror) {
        return
    }

    $running = @(Get-Process OculusMirror -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) {
        return
    }

    if (-not (Test-Path -LiteralPath $OculusMirrorPath)) {
        throw "OculusMirror.exe not found: $OculusMirrorPath"
    }

    Start-Process -FilePath $OculusMirrorPath -WindowStyle Normal | Out-Null
}

function Save-WindowScreenshot {
    param(
        [object]$Window,
        [string]$OutPath
    )

    if ($null -eq $Window) {
        throw "No Oculus Mirror window was available."
    }
    if ($Window.Width -le 0 -or $Window.Height -le 0) {
        throw "Oculus Mirror window has invalid bounds: $($Window | ConvertTo-Json -Compress)"
    }

    $handle = [IntPtr]([int64]$Window.Handle)
    if (-not $NoFocus) {
        [void][CodexQuestMirrorEvidenceNative]::ShowWindow($handle, 9)
        [void][CodexQuestMirrorEvidenceNative]::SetForegroundWindow($handle)
        Start-Sleep -Milliseconds 150

        $refreshed = @(Get-VisibleWindows | Where-Object { $_.Handle -eq $Window.Handle } | Select-Object -First 1)
        if ($refreshed.Count -gt 0) {
            $Window = $refreshed[0]
        }
    }

    $outFullPath = [System.IO.Path]::GetFullPath($OutPath)
    $outDir = Split-Path -Parent $outFullPath
    if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    }

    $bitmap = New-Object System.Drawing.Bitmap $Window.Width, $Window.Height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($Window.X, $Window.Y, 0, 0, (New-Object System.Drawing.Size $Window.Width, $Window.Height))
        $bitmap.Save($outFullPath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }

    return [pscustomobject]@{
        path = ConvertTo-WorkspaceRelativePath -Path $outFullPath
        title = $Window.Title
        class = $Window.Class
        processName = $Window.ProcessName
        processId = $Window.ProcessId
        handle = $Window.Handle
        x = $Window.X
        y = $Window.Y
        width = $Window.Width
        height = $Window.Height
    }
}

function Get-ImageStats {
    param([string]$Path)

    $bitmap = [System.Drawing.Bitmap]::new($Path)
    try {
        $step = [Math]::Max(1, [int][Math]::Floor([Math]::Sqrt(($bitmap.Width * $bitmap.Height) / 6000)))
        $count = 0
        $sum = 0.0
        $nonDark = 0
        $max = 0
        for ($y = 0; $y -lt $bitmap.Height; $y += $step) {
            for ($x = 0; $x -lt $bitmap.Width; $x += $step) {
                $c = $bitmap.GetPixel($x, $y)
                $lum = [int](0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B)
                $sum += $lum
                $count++
                if ($lum -gt 8) { $nonDark++ }
                if ($lum -gt $max) { $max = $lum }
            }
        }

        return [pscustomobject]@{
            width = $bitmap.Width
            height = $bitmap.Height
            samples = $count
            meanLuma = [Math]::Round($sum / [Math]::Max(1, $count), 2)
            nonDarkRatio = [Math]::Round($nonDark / [Math]::Max(1, $count), 4)
            maxLuma = $max
        }
    }
    finally {
        $bitmap.Dispose()
    }
}

function Write-ManifestRecord {
    param(
        [string]$Path,
        [object]$Record
    )

    $Record | ConvertTo-Json -Compress -Depth 8 | Add-Content -LiteralPath $Path
}

function Get-LogLineCount {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return 0
    }

    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    $count = 0
    $stream = [System.IO.File]::Open($resolvedPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object System.IO.StreamReader($stream)
        try {
            while ($null -ne $reader.ReadLine()) {
                $count++
            }
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }

    return $count
}

function Invoke-ArmTwitchAnalysis {
    param(
        [string]$RunEvidenceDir,
        [int]$StartLine,
        [string]$ManifestPath
    )

    if ($SkipArmTwitchAnalysis) {
        return
    }

    $analyzerPath = Join-Path (Get-Location) "Tools\AnalyzeWallaceArmTwitchLog.ps1"
    if (-not (Test-Path -LiteralPath $analyzerPath)) {
        Write-Warning "Arm twitch analyzer not found: $analyzerPath"
        return
    }

    $analysisDir = Join-Path $RunEvidenceDir "arm_twitch"
    New-Item -ItemType Directory -Force -Path $analysisDir | Out-Null
    $stdoutPath = Join-Path $analysisDir "analyzer_stdout.txt"

    try {
        $analysisOutput = & $analyzerPath `
            -LogPath $LogPath `
            -Actor "MP_LiveMetaHumanWallace" `
            -Side All `
            -SinceLine $StartLine `
            -OutDir $analysisDir `
            -PositionJumpThresholdCm $ArmTwitchPositionJumpThresholdCm `
            -AngleJumpThresholdDeg $ArmTwitchAngleJumpThresholdDeg `
            -SpeedThresholdCmSec $ArmTwitchSpeedThresholdCmSec `
            -TopRows 30 2>&1
        $analysisOutput | Set-Content -LiteralPath $stdoutPath

        Write-ManifestRecord -Path $ManifestPath -Record ([pscustomobject]@{
            timestamp = (Get-Date).ToString("o")
            event = "arm_twitch_analysis"
            success = $true
            sinceLine = $StartLine
            summary = ConvertTo-WorkspaceRelativePath -Path (Join-Path $analysisDir "summary.txt")
            spikesCsv = ConvertTo-WorkspaceRelativePath -Path (Join-Path $analysisDir "arm_twitch_spikes.csv")
            rowsCsv = ConvertTo-WorkspaceRelativePath -Path (Join-Path $analysisDir "arm_sanity_rows.csv")
            stdout = ConvertTo-WorkspaceRelativePath -Path $stdoutPath
        })
        Write-Host "Arm twitch summary: $(ConvertTo-WorkspaceRelativePath -Path (Join-Path $analysisDir "summary.txt"))"
        Write-Host "Arm twitch spikes CSV: $(ConvertTo-WorkspaceRelativePath -Path (Join-Path $analysisDir "arm_twitch_spikes.csv"))"
    }
    catch {
        Write-ManifestRecord -Path $ManifestPath -Record ([pscustomobject]@{
            timestamp = (Get-Date).ToString("o")
            event = "arm_twitch_analysis"
            success = $false
            sinceLine = $StartLine
            error = [string]$_
        })
        Write-Warning "Arm twitch analysis failed: $_"
    }
}

function Enable-UnrealDiagnostics {
    if ($NoUnrealDiagnostics) {
        return $false
    }

    $code = @'
import unreal

try:
    world = unreal.EditorLevelLibrary.get_game_world()
except Exception:
    world = None
if world is None:
    world = unreal.EditorLevelLibrary.get_editor_world()

cmds = [
    "mp.BodyFusion.Debug 1",
    "mp.QuestWristTrace 1",
    "mp.QuestWristDebug 1",
    "mp.QuestFingerDebug 1",
    "mp.QuestWristTraceLogIntervalSeconds 0.10",
    "mp.MetaHumanArmSanity 1",
    "mp.MetaHumanArmSanityLogIntervalSeconds 0.10",
    "mp.AutoQuestMirrorDebug 1"
]

for cmd in cmds:
    unreal.SystemLibrary.execute_console_command(world, cmd)
    print("CODEX_QUEST_MIRROR_DIAG_CMD " + cmd)

readbacks = [
    "mp.BodyFusion.Enable",
    "mp.BodyFusion.Debug",
    "mp.BodyFusion.MediaPipeAuthority",
    "mp.BodyFusion.CalibrationStableFrames",
    "mp.BodyFusion.CalibrationHoldSeconds",
    "mp.AutoQuestArmRollDiagnostic",
    "mp.QuestWristInvertTwist",
    "mp.QuestWristInvertTwistLeft",
    "mp.QuestWristInvertTwistRight",
    "mp.QuestWristRequireTrackedForApply",
    "mp.QuestWristRejectSwingClamp",
    "mp.QuestWristTwistDrivesForearm",
    "mp.QuestWristForearmTwistBlend",
    "mp.QuestWristForearmRollDriveTwistHelpers",
    "mp.QuestWristUpperArmRollDriveTwistHelpers",
    "mp.QuestWristUpperArmTwistBlend",
    "mp.QuestWristUpperArmMaxTwistDegrees",
    "mp.QuestWristDriveTwistCorrection",
    "mp.QuestWristTwistCorrectionBlend",
    "mp.QuestWristTwistCorrectionMaxDegrees",
    "mp.QuestWristTwistCorrectionStartDegrees",
    "mp.QuestWristTwistCorrectionFullDegrees",
    "mp.QuestWristTwistCorrectionUpperArmShare",
    "mp.MediaPipeDriveArmTwistBones",
    "mp.MediaPipeUpperArmTwistWeight",
    "mp.MediaPipeLowerArmTwistWeight",
    "mp.MediaPipeUpperArmTwistClampDegrees",
    "mp.MediaPipeLowerArmTwistClampDegrees",
    "mp.QuestFingerJointRetarget",
    "mp.QuestFingerCurlOnly",
    "mp.QuestConstrainedArmWristAuthority",
    "mp.QuestConstrainedArmWristAuthorityMin",
    "mp.QuestConstrainedArmWristAuthorityFadeStartCm",
    "mp.QuestConstrainedArmElbowHalfLife",
    "mp.QuestConstrainedArmMaxElbowStepCm",
    "mp.AutoQuestEmbodiedCameraForwardOffsetCm",
    "r.Streaming.PoolSize",
]
for name in readbacks:
    try:
        print("CODEX_QUEST_MIRROR_CVAR %s=%s" % (name, unreal.SystemLibrary.get_console_variable_float_value(name)))
    except Exception as exc:
        print("CODEX_QUEST_MIRROR_CVAR_ERR %s=%s:%s" % (name, type(exc).__name__, exc))
'@

    $body = @{
        tool = "run_unreal_python"
        args = @{
            code = $code
            timeout = 30000
        }
    } | ConvertTo-Json -Depth 6

    try {
        $result = Invoke-RestMethod -Method Post -Uri "$BridgeUrl/tool" -ContentType "application/json" -Body $body -TimeoutSec 35
        if ($result.success) {
            if ($result.payload -and $result.payload.output) {
                Write-Host $result.payload.output
            }
            Write-Host "Enabled Unreal Quest wrist/arm diagnostics through $BridgeUrl"
            return $true
        }

        Write-Warning "Unreal diagnostics request returned success=false: $($result | ConvertTo-Json -Compress -Depth 6)"
        return $false
    }
    catch {
        Write-Warning "Could not enable Unreal diagnostics through $BridgeUrl`: $_"
        return $false
    }
}

if ($RunName.Trim().Length -eq 0) {
    $RunName = "quest_mirror_evidence_{0}" -f (Get-Date -Format "yyyyMMdd_HHmmss")
}

$outputRootFull = [System.IO.Path]::GetFullPath($OutputRoot)
$evidenceDir = Join-Path $outputRootFull $RunName
New-Item -ItemType Directory -Force -Path $evidenceDir | Out-Null

if ($StopFile.Trim().Length -eq 0) {
    $StopFile = Join-Path $evidenceDir "stop_capture.txt"
}
$stopFileFull = [System.IO.Path]::GetFullPath($StopFile)
if (Test-Path -LiteralPath $stopFileFull) {
    Remove-Item -LiteralPath $stopFileFull -Force
}

$manifestPath = Join-Path $evidenceDir "manifest.jsonl"
$activePath = Join-Path $outputRootFull "active_quest_mirror_capture.json"
$latestPath = Join-Path $outputRootFull "latest_quest_mirror_capture.json"
$logStartLine = Get-LogLineCount -Path $LogPath
$activeRecord = [pscustomobject]@{
    runName = $RunName
    startedAt = (Get-Date).ToString("o")
    evidenceDir = ConvertTo-WorkspaceRelativePath -Path $evidenceDir
    manifest = ConvertTo-WorkspaceRelativePath -Path $manifestPath
    stopFile = $stopFileFull
    intervalSeconds = [Math]::Max(1, $IntervalSeconds)
    logPath = ConvertTo-WorkspaceRelativePath -Path $LogPath
    logStartLine = $logStartLine
}
$activeRecord | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $activePath
$activeRecord | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $latestPath

Write-Host "Quest mirror evidence capture"
Write-Host "Run: $RunName"
Write-Host "Evidence dir: $evidenceDir"
Write-Host "Stop file: $stopFileFull"

Ensure-OculusMirror

$deadline = (Get-Date).AddSeconds([Math]::Max(1, $WaitForMirrorTimeoutSeconds))
$window = $null
do {
    $window = Get-OculusMirrorWindow
    if ($null -ne $window) {
        break
    }
    Start-Sleep -Milliseconds 500
} while ((Get-Date) -lt $deadline)

if ($null -eq $window) {
    $visibleSummary = Get-VisibleWindows |
        Where-Object { $_.Title -match "Oculus|Quest|Meta|Mirror|VR|Preview" -or $_.ProcessName -match "Oculus|OVR|Meta|Quest|Unreal" } |
        Sort-Object @{ Expression = { $_.Width * $_.Height }; Descending = $true } |
        Select-Object -First 25 |
        Format-Table Handle, Title, Class, ProcessName, X, Y, Width, Height -AutoSize |
        Out-String
    if (Test-Path -LiteralPath $activePath) {
        Remove-Item -LiteralPath $activePath -Force
    }
    throw "Timed out waiting for Oculus Mirror window. Visible candidates:`n$visibleSummary"
}

$interval = [Math]::Max(1, $IntervalSeconds)
$frameIndex = 0
$startTime = Get-Date
$lastCaptureNonBlank = $false
$diagnosticsApplied = $NoUnrealDiagnostics

try {
    while ($true) {
        if (Test-Path -LiteralPath $stopFileFull) {
            Write-Host "Stop file detected."
            break
        }

        if ($DurationSeconds -gt 0 -and ((Get-Date) - $startTime).TotalSeconds -ge $DurationSeconds) {
            Write-Host "Duration reached."
            break
        }

        if (@(Get-Process OculusMirror -ErrorAction SilentlyContinue).Count -eq 0) {
            Write-Host "Oculus Mirror process exited."
            break
        }

        $frameIndex++
        $timestamp = Get-Date
        $fileName = "{0}_{1:000}_{2}.png" -f $RunName, $frameIndex, $timestamp.ToString("HHmmss")
        $outPath = Join-Path $evidenceDir $fileName
        $record = [ordered]@{
            timestamp = $timestamp.ToString("o")
            frame = $frameIndex
            success = $false
            path = ConvertTo-WorkspaceRelativePath -Path $outPath
            nonBlank = $false
            error = ""
        }

        try {
            $window = Get-OculusMirrorWindow
            $capture = Save-WindowScreenshot -Window $window -OutPath $outPath
            $stats = Get-ImageStats -Path $outPath
            $nonBlank = [double]$stats.nonDarkRatio -ge $MinimumNonDarkRatio
            $lastCaptureNonBlank = $nonBlank

            $record.success = $true
            $record.path = $capture.path
            $record.nonBlank = $nonBlank
            $record.window = $capture
            $record.stats = $stats

            Write-Host ("[{0}] {1} nonBlank={2} nonDarkRatio={3} meanLuma={4}" -f $frameIndex, $capture.path, $nonBlank, $stats.nonDarkRatio, $stats.meanLuma)
        }
        catch {
            $record.error = [string]$_
            Write-Warning "Capture failed: $_"
        }

        Write-ManifestRecord -Path $manifestPath -Record ([pscustomobject]$record)

        if ($frameIndex -eq 1 -and -not $lastCaptureNonBlank) {
            Write-Warning "First Oculus Mirror capture was blank/dark. Keep the headset awake and make sure Link/VR Preview is rendering."
        }

        if (-not $diagnosticsApplied -and ((Get-Date) - $startTime).TotalSeconds -ge [Math]::Max(0, $DiagnosticsDelaySeconds)) {
            $diagnosticsApplied = Enable-UnrealDiagnostics
            Write-ManifestRecord -Path $manifestPath -Record ([pscustomobject]@{
                timestamp = (Get-Date).ToString("o")
                event = "unreal_diagnostics"
                applied = $diagnosticsApplied
                bridgeUrl = $BridgeUrl
            })
        }

        Start-Sleep -Seconds $interval
    }
}
finally {
    Invoke-ArmTwitchAnalysis -RunEvidenceDir $evidenceDir -StartLine $logStartLine -ManifestPath $manifestPath

    $finishRecord = [pscustomobject]@{
        timestamp = (Get-Date).ToString("o")
        event = "finished"
        frames = $frameIndex
        evidenceDir = ConvertTo-WorkspaceRelativePath -Path $evidenceDir
        manifest = ConvertTo-WorkspaceRelativePath -Path $manifestPath
    }
    Write-ManifestRecord -Path $manifestPath -Record $finishRecord
    $finishRecord | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $latestPath

    if (Test-Path -LiteralPath $activePath) {
        try {
            $active = Get-Content -LiteralPath $activePath -Raw | ConvertFrom-Json
            if ($active.runName -eq $RunName) {
                Remove-Item -LiteralPath $activePath -Force
            }
        }
        catch {
        }
    }

    Write-Host "Finished. Manifest: $manifestPath"
}

param(
    [string]$BridgeUrl = "http://127.0.0.1:8765",
    [string]$LogPath = "Saved/Logs/TestingKit3.log",
    [int]$DurationSeconds = 60,
    [int]$IntervalSeconds = 2,
    [int]$WaitForReadyTimeoutSeconds = 90,
    [int]$WaitForPieTimeoutSeconds = 120,
    [switch]$NoStartVrPreview,
    [switch]$StopVrPreviewAtEnd,
    [switch]$ContinueIfNotReady,
    [switch]$LeaveDebugOn,
    [ValidateSet("HmdMirror", "Viewport", "Both")]
    [string]$CaptureSource = "HmdMirror",
    [string]$HmdMirrorTitlePattern = "^Oculus Mirror$",
    [string]$HmdMirrorProcessPattern = "^OculusMirror$",
    [string]$OculusMirrorPath = "$env:ProgramFiles\Oculus\Support\oculus-diagnostics\OculusMirror.exe",
    [switch]$NoLaunchOculusMirror,
    [switch]$NoFocusHmdMirrorWindow,
    [int]$HmdMirrorFocusDelayMs = 250,
    [switch]$SkipArmTwitchAnalysis,
    [double]$ArmTwitchPositionJumpThresholdCm = 6.0,
    [double]$ArmTwitchAngleJumpThresholdDeg = 15.0,
    [double]$ArmTwitchSpeedThresholdCmSec = 120.0,
    [string]$Profile = "Wallace",
    [string]$Actor = "",
    [string]$RunName = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Actor)) {
    $Actor = "MP_LiveMetaHuman$Profile"
}

Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public class CodexHmdMirrorCaptureNative {
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
    $full
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
        [int]$StartLine
    )

    if ($SkipArmTwitchAnalysis) {
        return
    }

    $analyzerPath = Join-Path (Get-Location) "Tools\AnalyzeMetaHumanArmTwitchLog.ps1"
    if (-not (Test-Path -LiteralPath $analyzerPath)) {
        $analyzerPath = Join-Path (Get-Location) "Tools\AnalyzeWallaceArmTwitchLog.ps1"
    }
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
            -Actor $Actor `
            -Side All `
            -SinceLine $StartLine `
            -OutDir $analysisDir `
            -PositionJumpThresholdCm $ArmTwitchPositionJumpThresholdCm `
            -AngleJumpThresholdDeg $ArmTwitchAngleJumpThresholdDeg `
            -SpeedThresholdCmSec $ArmTwitchSpeedThresholdCmSec `
            -TopRows 30 2>&1
        $analysisOutput | Set-Content -LiteralPath $stdoutPath

        $record = [pscustomobject]@{
            timestamp = (Get-Date).ToString("o")
            event = "arm_twitch_analysis"
            success = $true
            sinceLine = $StartLine
            summary = ConvertTo-WorkspaceRelativePath -Path (Join-Path $analysisDir "summary.txt")
            spikesCsv = ConvertTo-WorkspaceRelativePath -Path (Join-Path $analysisDir "arm_twitch_spikes.csv")
            rowsCsv = ConvertTo-WorkspaceRelativePath -Path (Join-Path $analysisDir "arm_sanity_rows.csv")
            stdout = ConvertTo-WorkspaceRelativePath -Path $stdoutPath
        }
        $record | ConvertTo-Json -Compress -Depth 6 | Add-Content -Path $manifestPath
        Write-Host "Arm twitch summary: $(ConvertTo-WorkspaceRelativePath -Path (Join-Path $analysisDir "summary.txt"))"
        Write-Host "Arm twitch spikes CSV: $(ConvertTo-WorkspaceRelativePath -Path (Join-Path $analysisDir "arm_twitch_spikes.csv"))"
    }
    catch {
        $record = [pscustomobject]@{
            timestamp = (Get-Date).ToString("o")
            event = "arm_twitch_analysis"
            success = $false
            sinceLine = $StartLine
            error = [string]$_
        }
        $record | ConvertTo-Json -Compress -Depth 6 | Add-Content -Path $manifestPath
        Write-Warning "Arm twitch analysis failed: $_"
    }
}

function Get-VisibleCaptureWindows {
    $windows = New-Object System.Collections.Generic.List[object]
    [CodexHmdMirrorCaptureNative]::EnumWindows({
        param($Handle, $LParam)

        if ([CodexHmdMirrorCaptureNative]::IsWindowVisible($Handle)) {
            $titleBuilder = New-Object System.Text.StringBuilder 512
            [void][CodexHmdMirrorCaptureNative]::GetWindowText($Handle, $titleBuilder, $titleBuilder.Capacity)
            if ($titleBuilder.Length -gt 0) {
                $classBuilder = New-Object System.Text.StringBuilder 256
                [void][CodexHmdMirrorCaptureNative]::GetClassName($Handle, $classBuilder, $classBuilder.Capacity)

                $rect = New-Object CodexHmdMirrorCaptureNative+RECT
                [void][CodexHmdMirrorCaptureNative]::GetWindowRect($Handle, [ref]$rect)

                $processIdValue = 0
                [void][CodexHmdMirrorCaptureNative]::GetWindowThreadProcessId($Handle, [ref]$processIdValue)
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
                    W = $rect.Right - $rect.Left
                    H = $rect.Bottom - $rect.Top
                }) | Out-Null
            }
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null

    return $windows.ToArray()
}

function Get-HmdMirrorWindow {
    $allWindows = @(Get-VisibleCaptureWindows | Where-Object {
        $_.Title -match $HmdMirrorTitlePattern -and
        ([string]::IsNullOrWhiteSpace($HmdMirrorProcessPattern) -or $_.ProcessName -match $HmdMirrorProcessPattern)
    })

    $nonEditorWindows = @($allWindows | Where-Object { $_.Title -notmatch "Unreal Editor" })
    $candidates = if ($nonEditorWindows.Count -gt 0) { $nonEditorWindows } else { @() }
    if ($candidates.Count -eq 0) {
        $visibleSummary = @(Get-VisibleCaptureWindows |
            Where-Object { $_.Title -match "TestingKit|Unreal|VR|Preview|Quest|Oculus|Meta|OpenXR" -or $_.ProcessName -match "Unreal|TestingKit|Oculus|OVR|Meta|Quest" } |
            Sort-Object @{ Expression = { $_.W * $_.H }; Descending = $true } |
            Select-Object -First 30 |
            Format-Table Handle, Title, Class, ProcessName, X, Y, W, H -AutoSize |
            Out-String)
        throw "No non-editor VR Preview/HMD mirror window matched TitlePattern '$HmdMirrorTitlePattern' and ProcessPattern '$HmdMirrorProcessPattern'. Visible candidates:`n$visibleSummary"
    }

    $scored = @($candidates | ForEach-Object {
        $score = 0
        if ($_.Title -match "VR|Preview|PIE|Play") { $score += 1000000000 }
        if ($_.Class -match "UnrealWindow") { $score += 1000000 }
        $score += [int]($_.W * $_.H)
        [pscustomobject]@{
            Score = $score
            Window = $_
        }
    } | Sort-Object Score -Descending)

    $selected = $scored[0].Window
    $handle = [IntPtr]([int64]$selected.Handle)
    [void][CodexHmdMirrorCaptureNative]::ShowWindow($handle, 9)
    Start-Sleep -Milliseconds 150

    $refreshed = @(Get-VisibleCaptureWindows | Where-Object { $_.Handle -eq $selected.Handle } | Select-Object -First 1)
    if ($refreshed.Count -gt 0) {
        return $refreshed[0]
    }

    $selected
}

function Ensure-OculusMirror {
    if ($NoLaunchOculusMirror) {
        return
    }
    if ($CaptureSource -ne "HmdMirror" -and $CaptureSource -ne "Both") {
        return
    }
    if (-not $HmdMirrorProcessPattern -or $HmdMirrorProcessPattern -notmatch "OculusMirror") {
        return
    }
    if (@(Get-Process OculusMirror -ErrorAction SilentlyContinue).Count -gt 0) {
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

    $handle = [IntPtr]([int64]$Window.Handle)
    if (-not $NoFocusHmdMirrorWindow) {
        [void][CodexHmdMirrorCaptureNative]::ShowWindow($handle, 9)
        [void][CodexHmdMirrorCaptureNative]::SetForegroundWindow($handle)
        Start-Sleep -Milliseconds ([Math]::Max(0, $HmdMirrorFocusDelayMs))

        $refreshed = @(Get-VisibleCaptureWindows | Where-Object { $_.Handle -eq $Window.Handle } | Select-Object -First 1)
        if ($refreshed.Count -gt 0) {
            $Window = $refreshed[0]
        }
    }

    if ($Window.W -le 0 -or $Window.H -le 0) {
        throw "Selected HMD mirror window has invalid bounds: $($Window | ConvertTo-Json -Compress)"
    }

    $outFullPath = [System.IO.Path]::GetFullPath($OutPath)
    $outDir = Split-Path -Parent $outFullPath
    if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    }

    $bitmap = New-Object System.Drawing.Bitmap $Window.W, $Window.H
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($Window.X, $Window.Y, 0, 0, (New-Object System.Drawing.Size $Window.W, $Window.H))
        $bitmap.Save($outFullPath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }

    [pscustomobject]@{
        path = ConvertTo-WorkspaceRelativePath -Path $outFullPath
        title = $Window.Title
        class = $Window.Class
        processName = $Window.ProcessName
        processId = $Window.ProcessId
        handle = $Window.Handle
        x = $Window.X
        y = $Window.Y
        width = $Window.W
        height = $Window.H
    }
}

function Invoke-BridgeTool {
    param(
        [string]$Tool,
        [hashtable]$ToolArgs = @{}
    )

    $body = @{
        tool = $Tool
        args = $ToolArgs
    } | ConvertTo-Json -Depth 12

    Invoke-RestMethod -Method Post -Uri "$BridgeUrl/tool" -ContentType "application/json" -Body $body -TimeoutSec 30
}

function Invoke-UnrealPython {
    param([string]$Code)

    $result = Invoke-BridgeTool -Tool "run_unreal_python" -ToolArgs @{ code = $Code }
    if (-not $result.success) {
        throw "run_unreal_python failed: $($result | ConvertTo-Json -Depth 8)"
    }
    [string]$result.payload.output
}

function Invoke-UnrealConsoleCommands {
    param([string[]]$Commands)

    $commandsLiteral = ($Commands | ForEach-Object { "'" + ($_ -replace "\\", "\\\\" -replace "'", "\\'") + "'" }) -join ",`n    "
    $code = @"
import unreal

world = None
try:
    world = unreal.EditorLevelLibrary.get_game_world()
except Exception:
    world = None
if world is None:
    world = unreal.EditorLevelLibrary.get_editor_world()

cmds = [
    $commandsLiteral
]

for cmd in cmds:
    unreal.SystemLibrary.execute_console_command(world, cmd)
    print("CODEX_CAPTURE_CMD " + cmd)

readbacks = [
    "mp.AutoQuestArmRollDiagnostic",
    "mp.AutoQuestHandCompareMode",
    "mp.QuestHandCompare",
    "mp.QuestHandHud",
    "mp.QuestHandDebug",
    "mp.QuestFingerDebug",
    "mp.QuestFingerJointRetarget",
    "mp.QuestWristDebug",
    "mp.QuestWristTrace",
    "mp.QuestWristTraceLogIntervalSeconds",
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
    "mp.MetaHumanArmSanity",
    "mp.MetaHumanArmSanityLogIntervalSeconds",
    "mp.MetaHumanActiveProfile",
    "mp.MetaHumanArmSource",
    "mp.MetaHumanFullArmChainTrace",
    "mp.MetaHumanFullArmChainTraceLogIntervalSeconds",
    "mp.MetaHumanFullArmChainMaxAgeSeconds",
    "mp.AutoQuestMediaPipeStats",
    "mp.AutoQuestMediaPipeStatsHud",
    "mp.AutoQuestMediaPipeStatsIntervalSeconds",
    "mp.MediaPipeTorsoDebug",
    "mp.AutoQuestMirrorDebug",
    "mp.AutoQuestWebcamHandsHz",
    "mp.MediaPipeInputMaxDimension",
    "mp.QuestArmMode",
    "mp.QuestWristPositionBlend",
    "mp.QuestConstrainedArmElbowHalfLife",
    "mp.QuestConstrainedArmMaxElbowStepCm",
    "mp.MediaPipeUseArmIK",
    "mp.QuestWristForceArmIK",
]
for name in readbacks:
    try:
        print("CODEX_CAPTURE_CVAR %s=%s" % (name, unreal.SystemLibrary.get_console_variable_float_value(name)))
    except Exception as exc:
        print("CODEX_CAPTURE_CVAR_ERR %s=%s:%s" % (name, type(exc).__name__, exc))
"@

    Invoke-UnrealPython -Code $code
}

function Get-XrReadiness {
    $code = @'
import json
import unreal

hmd = unreal.HeadMountedDisplayFunctionLibrary
state = {}

def set_value(name, value):
    try:
        state[name] = str(value)
    except Exception:
        state[name] = "<unprintable>"

def set_bool(name, func_name):
    try:
        state[name] = bool(getattr(hmd, func_name)())
    except Exception as exc:
        state[name + "_error"] = "%s:%s" % (type(exc).__name__, exc)

set_bool("connected", "is_head_mounted_display_connected")
set_bool("enabled", "is_head_mounted_display_enabled")
set_bool("tracking_pos_valid", "has_valid_tracking_position")

try:
    set_value("device_name", hmd.get_hmd_device_name())
except Exception as exc:
    set_value("device_name_error", "%s:%s" % (type(exc).__name__, exc))

try:
    set_value("worn_state", hmd.get_hmd_worn_state())
except Exception as exc:
    set_value("worn_state_error", "%s:%s" % (type(exc).__name__, exc))

try:
    data = hmd.get_hmd_data(None)
    state["hmd_data_valid"] = bool(data.valid)
    set_value("hmd_tracking_status", data.tracking_status)
    set_value("hmd_position", data.position)
except Exception as exc:
    set_value("hmd_data_error", "%s:%s" % (type(exc).__name__, exc))

try:
    world = unreal.EditorLevelLibrary.get_game_world()
except Exception:
    world = None
if world is None:
    world = unreal.EditorLevelLibrary.get_editor_world()
try:
    unreal.SystemLibrary.execute_console_command(world, "mp.DumpQuestHands")
    state["dump_quest_hands"] = "sent"
except Exception as exc:
    set_value("dump_quest_hands_error", "%s:%s" % (type(exc).__name__, exc))

print("CODEX_XR_READY_JSON " + json.dumps(state, sort_keys=True))
'@

    $output = Invoke-UnrealPython -Code $code
    $jsonLine = @($output -split "`r?`n" | Where-Object { $_ -match "^CODEX_XR_READY_JSON " } | Select-Object -Last 1)
    if ($jsonLine.Count -eq 0) {
        throw "XR readiness output did not include CODEX_XR_READY_JSON. Output was: $output"
    }
    ($jsonLine[0] -replace "^CODEX_XR_READY_JSON ", "") | ConvertFrom-Json
}

function Get-EditorState {
    $result = Invoke-BridgeTool -Tool "get_editor_state" -ToolArgs @{}
    if (-not $result.success -or -not $result.payload -or -not $result.payload.output) {
        return $null
    }
    $result.payload.output | ConvertFrom-Json
}

function Test-XrReady {
    param($State)

    $State.connected -eq $true -and
        $State.enabled -eq $true -and
        $State.tracking_pos_valid -eq $true -and
        $State.hmd_data_valid -eq $true -and
        ([string]$State.worn_state -notmatch "NOT_WORN")
}

$status = Invoke-RestMethod -Method Get -Uri "$BridgeUrl/status" -TimeoutSec 5
if (-not $status.success) {
    throw "Bridge status failed at $BridgeUrl"
}
Write-Host "Bridge ready: $BridgeUrl"

if ([string]::IsNullOrWhiteSpace($RunName)) {
    $RunName = "{0}_quest_vr_evidence_{1}" -f ($Profile.ToLowerInvariant()), (Get-Date -Format "yyyyMMdd_HHmmss")
}

$evidenceDir = Join-Path (Get-Location) "Saved\CodexAgent\QuestVrEvidence\$RunName"
New-Item -ItemType Directory -Force -Path $evidenceDir | Out-Null
$manifestPath = Join-Path $evidenceDir "manifest.jsonl"
$logStartLine = Get-LogLineCount -Path $LogPath

$diagnosticCommands = @(
    "mp.AutoQuestHandCompareMode 2",
    "mp.QuestHandCompare 2",
    "mp.QuestHandHud 1",
    "mp.QuestHandDebug 1",
    "mp.QuestFingerDebug 1",
    "mp.QuestWristDebug 1",
    "mp.QuestWristTrace 1",
    "mp.QuestWristTraceLogIntervalSeconds 0.10",
    "mp.MetaHumanArmSanity 1",
    "mp.MetaHumanArmSanityLogIntervalSeconds 0.10",
    "mp.MetaHumanActiveProfile $Profile",
    "mp.MetaHumanArmSource -1",
    "mp.MetaHumanFullArmChainTrace 1",
    "mp.MetaHumanFullArmChainTraceLogIntervalSeconds 0.10",
    "mp.MetaHumanFullArmChainMaxAgeSeconds 0.25",
    "mp.AutoQuestMediaPipeStats 1",
    "mp.AutoQuestMediaPipeStatsHud 1",
    "mp.AutoQuestMediaPipeStatsIntervalSeconds 1.0",
    "mp.MediaPipeTorsoDebug 1",
    "mp.AutoQuestMirrorDebug 1"
)

$restoreCommands = @(
    "mp.AutoQuestHandCompareMode 0",
    "mp.QuestHandCompare 0",
    "mp.QuestHandHud 0",
    "mp.QuestHandDebug 0",
    "mp.QuestFingerDebug 0",
    "mp.QuestWristDebug 0",
    "mp.QuestWristTrace 0",
    "mp.QuestWristTraceLogIntervalSeconds 0.25",
    "mp.MetaHumanArmSanity 0",
    "mp.MetaHumanArmSanityLogIntervalSeconds 0.25",
    "mp.MetaHumanActiveProfile Wallace",
    "mp.MetaHumanArmSource -1",
    "mp.MetaHumanFullArmChainTrace -1",
    "mp.MetaHumanFullArmChainTraceLogIntervalSeconds -1",
    "mp.MetaHumanFullArmChainMaxAgeSeconds -1",
    "mp.AutoQuestMediaPipeStats 0",
    "mp.AutoQuestMediaPipeStatsHud 0",
    "mp.AutoQuestMediaPipeStatsIntervalSeconds 1.0",
    "mp.MediaPipeTorsoDebug 0",
    "mp.AutoQuestMirrorDebug 0"
)

try {
    Write-Host "Pre-arming diagnostic CVars."
    $preOutput = Invoke-UnrealConsoleCommands -Commands $diagnosticCommands
    $preOutput | Set-Content -Path (Join-Path $evidenceDir "cvar_prearm.txt")

    if (-not $NoStartVrPreview) {
        Write-Host "Starting raw ChiR24 VR Preview."
        $start = Invoke-BridgeTool -Tool "call_chir24_mcp" -ToolArgs @{
            mcpTool = "control_editor"
            arguments = @{
                action = "play"
                vrPreview = $true
            }
        }
        if (-not $start.success) {
            throw "VR Preview start failed: $($start | ConvertTo-Json -Depth 8)"
        }
        Write-Host $start.text
        Start-Sleep -Seconds 3

        Write-Host "Re-applying diagnostic CVars after Auto Quest startup."
        $postOutput = Invoke-UnrealConsoleCommands -Commands $diagnosticCommands
        $postOutput | Set-Content -Path (Join-Path $evidenceDir "cvar_after_vrpreview.txt")
    }
    else {
        Write-Host "Not starting VR Preview because -NoStartVrPreview was passed."
        $pieDeadline = (Get-Date).AddSeconds([Math]::Max(0, $WaitForPieTimeoutSeconds))
        do {
            $editorState = Get-EditorState
            if ($null -ne $editorState -and $editorState.pieRunning) {
                Write-Host "Detected existing PIE/VR Preview: world=$($editorState.pieWorld)"
                break
            }
            Write-Host "Waiting for you to press VR Preview..."
            Start-Sleep -Seconds 1
        } while ((Get-Date) -lt $pieDeadline)

        $editorState = Get-EditorState
        if ($null -eq $editorState -or -not $editorState.pieRunning) {
            throw "Timed out waiting for an existing PIE/VR Preview session. Press VR Preview and rerun, or omit -NoStartVrPreview for automated launch."
        }

        Write-Host "Re-applying diagnostic CVars inside the existing VR Preview."
        $postOutput = Invoke-UnrealConsoleCommands -Commands $diagnosticCommands
        $postOutput | Set-Content -Path (Join-Path $evidenceDir "cvar_existing_vrpreview.txt")
    }

    $ready = $false
    $deadline = (Get-Date).AddSeconds([Math]::Max(0, $WaitForReadyTimeoutSeconds))
    do {
        $state = Get-XrReadiness
        $ready = Test-XrReady -State $state
        $stateRecord = [pscustomobject]@{
            timestamp = (Get-Date).ToString("o")
            ready = $ready
            state = $state
        }
        $stateRecord | ConvertTo-Json -Compress -Depth 8 | Add-Content -Path $manifestPath

        Write-Host ("XR ready={0} device={1} enabled={2} worn={3} tracking={4} hmdValid={5}" -f `
            $ready, $state.device_name, $state.enabled, $state.worn_state, $state.tracking_pos_valid, $state.hmd_data_valid)

        if (-not $ready) {
            Start-Sleep -Seconds 1
        }
    } while (-not $ready -and (Get-Date) -lt $deadline)

    if (-not $ready -and -not $ContinueIfNotReady) {
        throw "Quest HMD was not ready before timeout. Put on/wake the headset and rerun, or pass -ContinueIfNotReady for a dry capture."
    }
    if (-not $ready) {
        Write-Warning "Continuing even though the HMD is not ready because -ContinueIfNotReady was passed."
    }

    if ($CaptureSource -eq "HmdMirror" -or $CaptureSource -eq "Both") {
        Ensure-OculusMirror

        $windowReportPath = Join-Path $evidenceDir "window_candidates_after_vrpreview.txt"
        Get-VisibleCaptureWindows |
            Where-Object { $_.Title -match "TestingKit|Unreal|VR|Preview|Quest|Oculus|Meta|OpenXR" -or $_.ProcessName -match "Unreal|TestingKit|Oculus|OVR|Meta|Quest" } |
            Sort-Object @{ Expression = { $_.W * $_.H }; Descending = $true } |
            Select-Object -First 50 |
            Format-Table Handle, Title, Class, ProcessName, X, Y, W, H -AutoSize |
            Out-String |
            Set-Content -Path $windowReportPath
        Write-Host "Window candidates: $windowReportPath"
    }

    $captureCount = if ($DurationSeconds -le 0) { 0 } else { [Math]::Ceiling($DurationSeconds / [Math]::Max(1, $IntervalSeconds)) }
    Write-Host "Capturing $captureCount screenshot sets at ${IntervalSeconds}s intervals. captureSource=$CaptureSource"

    for ($i = 0; $i -lt $captureCount; $i++) {
        $filename = "{0}_{1:D3}.png" -f $RunName, ($i + 1)
        $captures = @()

        if ($CaptureSource -eq "HmdMirror" -or $CaptureSource -eq "Both") {
            $mirrorPath = Join-Path $evidenceDir ("{0}_hmdmirror_{1:D3}.png" -f $RunName, ($i + 1))
            try {
                $mirrorWindow = Get-HmdMirrorWindow
                $mirrorCapture = Save-WindowScreenshot -Window $mirrorWindow -OutPath $mirrorPath
                $captures += [pscustomobject]@{
                    source = "hmdMirror"
                    success = $true
                    path = $mirrorCapture.path
                    text = "captured visible VR Preview/HMD mirror window"
                    error = ""
                    window = $mirrorCapture
                }
            }
            catch {
                $captures += [pscustomobject]@{
                    source = "hmdMirror"
                    success = $false
                    path = ConvertTo-WorkspaceRelativePath -Path $mirrorPath
                    text = ""
                    error = [string]$_
                    window = $null
                }
            }
        }

        if ($CaptureSource -eq "Viewport" -or $CaptureSource -eq "Both") {
            $viewportCapture = $null
            $viewportError = ""
            try {
                $viewportCapture = Invoke-BridgeTool -Tool "capture_viewport_screenshot" -ToolArgs @{ filename = $filename }
            }
            catch {
                $viewportError = [string]$_
            }

            $captures += [pscustomobject]@{
                source = "editorViewport"
                success = ($null -ne $viewportCapture -and [bool]$viewportCapture.success)
                path = "Saved/CodexAgent/Screenshots/$filename"
                text = if ($null -ne $viewportCapture) { [string]$viewportCapture.text } else { "" }
                error = $viewportError
                window = $null
            }
        }

        $state = $null
        $stateError = ""
        try {
            $state = Get-XrReadiness
        }
        catch {
            $stateError = [string]$_
        }

        $xrReady = if ($null -ne $state) { Test-XrReady -State $state } else { $false }
        $record = [pscustomobject]@{
            timestamp = (Get-Date).ToString("o")
            captureSource = $CaptureSource
            screenshot = if (@($captures).Count -gt 0) { @($captures)[0].path } else { "" }
            captures = @($captures)
            captureSuccess = (@($captures).Count -gt 0 -and -not (@($captures | Where-Object { -not $_.success }).Count -gt 0))
            captureText = ($captures | ForEach-Object { "$($_.source):$($_.text)" }) -join "; "
            captureError = ($captures | Where-Object { -not $_.success } | ForEach-Object { "$($_.source):$($_.error)" }) -join "; "
            xrReady = $xrReady
            xr = $state
            xrError = $stateError
        }
        $record | ConvertTo-Json -Compress -Depth 8 | Add-Content -Path $manifestPath

        $wornText = if ($null -ne $state) { $state.worn_state } else { "unknown" }
        $trackingText = if ($null -ne $state) { $state.tracking_pos_valid } else { "unknown" }
        $captureSummary = (@($captures) | ForEach-Object { "{0}={1}" -f $_.source, $_.success }) -join " "
        Write-Host ("[{0}/{1}] {2} {3} ready={4} worn={5} tracking={6}" -f `
            ($i + 1), $captureCount, $filename, $captureSummary, $record.xrReady, $wornText, $trackingText)

        if (-not $record.captureSuccess -and -not [string]::IsNullOrWhiteSpace($record.captureError)) {
            Write-Warning "Capture request failed; ending this run cleanly: $($record.captureError)"
            break
        }

        if ($i -lt $captureCount - 1) {
            Start-Sleep -Seconds ([Math]::Max(1, $IntervalSeconds))
        }
    }

    Write-Host "Evidence manifest: $manifestPath"
    if ($CaptureSource -eq "HmdMirror" -or $CaptureSource -eq "Both") {
        Write-Host "HMD mirror screenshots: $evidenceDir\${RunName}_hmdmirror_NNN.png"
    }
    if ($CaptureSource -eq "Viewport" -or $CaptureSource -eq "Both") {
        Write-Host "Editor viewport screenshots: Saved/CodexAgent/Screenshots/${RunName}_NNN.png"
    }

    Invoke-ArmTwitchAnalysis -RunEvidenceDir $evidenceDir -StartLine $logStartLine
}
finally {
    if (-not $LeaveDebugOn) {
        Write-Host "Restoring heavy diagnostic CVars to defaults."
        try {
            Invoke-UnrealConsoleCommands -Commands $restoreCommands | Out-Null
        }
        catch {
            Write-Warning "Failed to restore diagnostic CVars: $_"
        }
    }
    else {
        Write-Host "Leaving diagnostic CVars enabled because -LeaveDebugOn was passed."
    }

    if ($StopVrPreviewAtEnd) {
        Write-Host "Stopping PIE/VR Preview."
        try {
            Invoke-BridgeTool -Tool "call_chir24_mcp" -ToolArgs @{
                mcpTool = "control_editor"
                arguments = @{
                    action = "stop"
                }
            } | Out-Null
        }
        catch {
            Write-Warning "Failed to stop VR Preview: $_"
        }
    }
}

param(
    [string]$BridgeUrl = "http://127.0.0.1:8765/tool",
    [string]$LogPath = "Saved/Logs/TestingKit3.log",
    [switch]$RequireBothHands
)

$ErrorActionPreference = "Stop"

function Read-SharedLogLines {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return @()
    }

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $stream = [System.IO.File]::Open($resolved, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object System.IO.StreamReader($stream)
        try {
            $lines = New-Object System.Collections.Generic.List[string]
            while (($line = $reader.ReadLine()) -ne $null) {
                $lines.Add($line) | Out-Null
            }
            return @($lines)
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

$beforeLineCount = (Read-SharedLogLines -Path $LogPath).Count

$code = @'
import unreal

print("CODEX_XR_READY_START")
hmd = unreal.HeadMountedDisplayFunctionLibrary

def emit(name, value):
    print("CODEX_XR_READY %s=%s" % (name, value))

def call_bool(name, func_name):
    try:
        emit(name, int(bool(getattr(hmd, func_name)())))
    except Exception as exc:
        emit(name + "_error", "%s:%s" % (type(exc).__name__, exc))

call_bool("connected", "is_head_mounted_display_connected")
call_bool("enabled", "is_head_mounted_display_enabled")
call_bool("tracking_pos_valid", "has_valid_tracking_position")

try:
    emit("device_name", hmd.get_hmd_device_name())
except Exception as exc:
    emit("device_name_error", "%s:%s" % (type(exc).__name__, exc))

try:
    emit("worn_state", hmd.get_hmd_worn_state())
except Exception as exc:
    emit("worn_state_error", "%s:%s" % (type(exc).__name__, exc))

try:
    emit("tracking_origin", hmd.get_tracking_origin())
except Exception as exc:
    emit("tracking_origin_error", "%s:%s" % (type(exc).__name__, exc))

try:
    orientation, position = hmd.get_orientation_and_position()
    emit("orientation", orientation)
    emit("position", position)
except Exception as exc:
    emit("orientation_position_error", "%s:%s" % (type(exc).__name__, exc))

try:
    data = hmd.get_hmd_data(None)
    emit("hmd_data_valid", int(bool(data.valid)))
    emit("hmd_tracking_status", data.tracking_status)
    emit("hmd_position", data.position)
    emit("hmd_rotation", data.rotation)
except Exception as exc:
    emit("hmd_data_error", "%s:%s" % (type(exc).__name__, exc))

try:
    unreal.SystemLibrary.execute_console_command(None, "mp.DumpQuestHands")
    emit("dump_quest_hands", "sent")
except Exception as exc:
    emit("dump_quest_hands_error", "%s:%s" % (type(exc).__name__, exc))

print("CODEX_XR_READY_END")
'@

$body = @{
    tool = "run_unreal_python"
    args = @{
        code = $code
    }
} | ConvertTo-Json -Depth 8

$response = Invoke-RestMethod -Uri $BridgeUrl -Method Post -Body $body -ContentType "application/json"
if (-not $response.success) {
    throw "Bridge call failed: $($response | ConvertTo-Json -Depth 8)"
}

$output = [string]$response.payload.output
$afterLines = Read-SharedLogLines -Path $LogPath
$newLines = @($afterLines | Select-Object -Skip $beforeLineCount)
$handLines = @($newLines | Where-Object { $_ -match "mp\.DumpQuestHands:" })
$trackerLine = @($handLines | Where-Object { $_ -match "tracker\[" } | Select-Object -Last 1)

$failures = New-Object System.Collections.Generic.List[string]

if ($output -notmatch "CODEX_XR_READY connected=1") {
    $failures.Add("Quest HMD is not connected according to Unreal.") | Out-Null
}
if ($output -notmatch "CODEX_XR_READY enabled=1") {
    $failures.Add("HMD is not enabled in the active Unreal XR session. Run this while VR Preview is active and the headset is awake.") | Out-Null
}
if ($output -match "worn_state=.*NOT_WORN") {
    $failures.Add("HMD worn state is NOT_WORN.") | Out-Null
}
if ($output -notmatch "CODEX_XR_READY tracking_pos_valid=1") {
    $failures.Add("HMD tracking position is not valid.") | Out-Null
}
if ($output -notmatch "CODEX_XR_READY hmd_data_valid=1") {
    $failures.Add("Unreal HMD data is not valid.") | Out-Null
}

if ($null -eq $trackerLine -or $trackerLine.Count -eq 0) {
    $failures.Add("No fresh mp.DumpQuestHands tracker line was written.") | Out-Null
}
else {
    $trackerText = [string]$trackerLine
    if ($trackerText -notmatch "stateValid=1") {
        $failures.Add("OpenXR hand tracker state is not valid.") | Out-Null
    }

    $leftTracked = $trackerText -match "left\(success=1 tracked=1"
    $rightTracked = $trackerText -match "right\(success=1 tracked=1"
    if ($RequireBothHands) {
        if (-not ($leftTracked -and $rightTracked)) {
            $failures.Add("Both Quest hands are not tracked.") | Out-Null
        }
    }
    elseif (-not ($leftTracked -or $rightTracked)) {
        $failures.Add("No Quest hand is tracked.") | Out-Null
    }
}

Write-Host "Quest XR live readiness"
$output -split "`r?`n" | Where-Object { $_ -match "^CODEX_XR_READY " } | ForEach-Object { Write-Host $_ }
if ($null -ne $trackerLine -and $trackerLine.Count -gt 0) {
    Write-Host "Latest hand tracker: $trackerLine"
}

if ($failures.Count -gt 0) {
    Write-Host "FAIL"
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host "PASS"
exit 0

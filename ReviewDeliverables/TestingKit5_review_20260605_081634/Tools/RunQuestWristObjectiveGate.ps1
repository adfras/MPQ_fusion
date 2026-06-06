param(
    [string]$BridgeUrl = "http://127.0.0.1:8765",
    [string]$LogPath = "Saved/Logs/TestingKit3.log",
    [int]$WaitSeconds = 20,
    [int]$PreResetDelaySeconds = 0,
    [int]$MinTrackingRows = 6,
    [int]$TailRows = 24,
    [switch]$SkipApplyCvars,
    [switch]$AllowResetWithoutPie,
    [switch]$ResetNow
)

$ErrorActionPreference = "Stop"

function Read-LogLines {
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

function Get-PostResetRightRows {
    param([string]$Path)

    $lines = Read-LogLines -Path $Path
    $lastResetIndex = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match "mp\.ResetQuestWristCalibration: applied") {
            $lastResetIndex = $i
        }
    }

    $rightRows = 0
    $trackingRows = 0
    $lastCalibrationState = ""
    $lastCalibrationRejectReason = ""
    if ($lastResetIndex -ge 0) {
        for ($i = $lastResetIndex + 1; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -match "mp\.QuestWristRollCompact:.*side=R") {
                $rightRows++
                if ($lines[$i] -match "calibrationState=([^ ]+)") {
                    $lastCalibrationState = $Matches[1]
                }
                if ($lines[$i] -match 'calibrationRejectReason="([^"]*)"') {
                    $lastCalibrationRejectReason = $Matches[1]
                }
                elseif ($lines[$i] -match "calibrationRejectReason=([^ ]+)") {
                    $lastCalibrationRejectReason = $Matches[1]
                }
                if ($lines[$i] -match "calibrationState=Tracking") {
                    $trackingRows++
                }
            }
        }
    }

    [pscustomobject]@{
        HasReset = $lastResetIndex -ge 0
        ResetLine = if ($lastResetIndex -ge 0) { $lastResetIndex + 1 } else { 0 }
        RightRows = $rightRows
        TrackingRows = $trackingRows
        LastCalibrationState = $lastCalibrationState
        LastCalibrationRejectReason = $lastCalibrationRejectReason
    }
}

function Invoke-BridgeTool {
    param(
        [string]$Tool,
        [hashtable]$ToolArgs
    )

    $body = @{
        tool = $Tool
        args = $ToolArgs
    } | ConvertTo-Json -Depth 8
    Invoke-RestMethod -Method Post -Uri "$BridgeUrl/tool" -ContentType "application/json" -Body $body -TimeoutSec 15
}

function Get-EditorState {
    $result = Invoke-BridgeTool -Tool "get_editor_state" -ToolArgs @{}
    if (-not $result.success) {
        throw "get_editor_state failed: $($result | ConvertTo-Json -Depth 6)"
    }
    if (-not $result.payload -or -not $result.payload.output) {
        return $null
    }
    $result.payload.output | ConvertFrom-Json
}

$status = Invoke-RestMethod -Method Get -Uri "$BridgeUrl/status" -TimeoutSec 5
if (-not $status.success) {
    throw "Bridge status failed at $BridgeUrl"
}

Write-Host "Bridge ready: $BridgeUrl"

$editorState = Get-EditorState
if ($null -ne $editorState) {
    Write-Host "Editor state: map=$($editorState.editorMap) pieRunning=$($editorState.pieRunning)"
}

if (-not $SkipApplyCvars) {
    Write-Host "Applying objective-gate CVars: current profile 4 arm path, trace on, calibration gate on, tracked-required Quest wrist apply with constrained continuity, arm IK off."
    $code = @"
import unreal
world = unreal.EditorLevelLibrary.get_editor_world()
cmds = [
    'mp.AutoQuestArmReachAssistProfile 4',
    'mp.QuestArmMode 3',
    'mp.QuestHandHud 1',
    'mp.QuestWristTrace 1',
    'mp.QuestWristTraceLogIntervalSeconds 0.25',
    'mp.MetaHumanArmSanity 1',
    'mp.MetaHumanArmSanityLogIntervalSeconds 0.10',
    'mp.QuestWristPositionBlend 1.0',
    'mp.QuestWristMaxRelativeDeltaCm 82.0',
    'mp.QuestWristLostTrackingGraceSeconds 0.35',
    'mp.QuestWristUseBasisDelta 1',
    'mp.QuestWristUseJointRotation 1',
    'mp.QuestWristUseJointRotationRight 1',
    'mp.QuestWristUseJointRotationLeft 1',
    'mp.QuestWristRequireTrackedForApply 1',
    'mp.QuestWristPositionAdaptiveFilter 1',
    'mp.QuestWristPositionFilterStillHalfLife 0.11',
    'mp.QuestWristPositionFilterMovingHalfLife 0.018',
    'mp.QuestWristPositionFilterResetDistanceCm 45.0',
    'mp.QuestConstrainedArmSolve 1',
    'mp.QuestConstrainedArmSolveBlend 1.0',
    'mp.QuestConstrainedArmWristAuthority 1.0',
    'mp.QuestConstrainedArmWristAuthorityMin 1.0',
    'mp.QuestConstrainedArmMaxReachFraction 0.997',
    'mp.QuestConstrainedArmMaxReachStepCm 0.0',
    'mp.QuestConstrainedArmSolvedPlaneMinSin 0.08',
    'mp.QuestConstrainedArmBodyFallback 0',
    'mp.QuestConstrainedArmBodyFallbackWristHalfLife 0.08',
    'mp.QuestConstrainedArmBodyFallbackMaxWristStepCm 14.0',
    'mp.QuestConstrainedArmDownStraighten 0',
    'mp.QuestConstrainedArmDownStraightenThresholdCm 0.0',
    'mp.QuestConstrainedArmDownStraightenMaxCm 0.0',
    'mp.QuestConstrainedArmDownStraightenMinBelowShoulderRatio 0.0',
    'mp.QuestConstrainedArmDownStraightenReachFloorFraction 0.0',
    'mp.QuestConstrainedArmDownStraightenMaxReachFraction 0.0',
    'mp.QuestConstrainedArmReachScaleCalibration 1',
    'mp.QuestConstrainedArmReachScaleUniform 1',
    'mp.QuestConstrainedArmReachScaleMinObservedFraction 0.88',
    'mp.QuestConstrainedArmReachScaleApplyStartFraction 0.0',
    'mp.QuestConstrainedArmReachScaleApplyFullFraction 1.0',
    'mp.QuestConstrainedArmReachScaleMin 0.82',
    'mp.QuestConstrainedArmReachScaleMax 1.18',
    'mp.QuestArmLengthCalibrationStartup 1',
    'mp.QuestArmLengthCalibrationHud 1',
    'mp.QuestArmLengthCalibrationHoldSeconds 2.5',
    'mp.QuestArmLengthCalibrationStableFrames 20',
    'mp.QuestArmLengthCalibrationMaxHandVelocityCmSec 30.0',
    'mp.QuestArmLengthCalibrationForwardMinReachFraction 0.88',
    'mp.QuestArmLengthCalibrationDownMinBelowShoulderFraction 0.40',
    'mp.QuestArmLengthCalibrationDownMinVerticalDominance 0.65',
    'mp.QuestArmLengthCalibrationDownMinCorrectedReachFraction 0.95',
    'mp.QuestArmDownFrameCorrection 1',
    'mp.QuestArmDownFrameCorrectionMaxScale 1.80',
    'mp.QuestArmDropoutDownFallback 1',
    'mp.QuestArmDropoutDownFallbackRecentTrackedSeconds 4.0',
    'mp.QuestArmDropoutDownFallbackMinDownDominance 0.55',
    'mp.QuestArmDropoutDownFallbackBlendHalfLife 0.08',
    'mp.QuestConstrainedArmElbowHalfLife 0.0',
    'mp.QuestConstrainedArmMaxElbowStepCm 0.0',
    'mp.QuestWristTwistBlend 1.0',
    'mp.QuestWristSwingBlend 1.0',
    'mp.QuestWristTwistDrivesForearm 0',
    'mp.QuestWristForearmTwistBlend 0.0',
    'mp.QuestWristForearmMaxTwistDegrees 55',
    'mp.QuestWristForearmRollDriveTwistHelpers 0',
    'mp.QuestWristUpperArmRollDriveTwistHelpers 0',
    'mp.QuestWristUpperArmTwistBlend 0.0',
    'mp.QuestWristDriveTwistCorrection 0',
    'mp.QuestWristTwistCorrectionBlend 0.0',
    'mp.QuestWristMaxTwistDegrees 170',
    'mp.QuestWristMaxSwingDegrees 140',
    'mp.QuestWristSemanticRollMinPalmProjection 0.45',
    'mp.QuestWristRequireNeutralCalibration 1',
    'mp.QuestWristCalibrationGate 1',
    'mp.QuestWristCalibrationHoldSeconds 1.25',
    'mp.QuestWristCalibrationStableFrames 30',
    'mp.QuestWristCalibrationMaxBasisErrorDegrees 90',
    'mp.QuestWristCalibrationMaxNeutralTwistDegrees 35',
    'mp.QuestWristCalibrationHud 1',
    'mp.QuestWristDebug 0',
    'mp.QuestWristForceArmIK 0',
    'mp.MediaPipeUseArmIK 0',
    'mp.MediaPipeDriveClavicles 0',
    'mp.MediaPipeDriveSpine 0',
    'mp.MediaPipeDrivePelvisTranslation 0',
    'mp.MediaPipeDriveLegs 0',
    'mp.MediaPipeUseLegIK 0',
    'mp.MediaPipeUseFkRootGrounding 0',
    'mp.MediaPipeDriveFootRotation 0',
    'mp.MediaPipeDriveArmTwistBones 1',
    'mp.MediaPipeDriveMetaHumanArmHelpers 0',
    'mp.QuestFingerJointRetarget 0',
    'mp.QuestFingerCurlOnly 0',
]
for cmd in cmds:
    unreal.SystemLibrary.execute_console_command(world, cmd)
for name in [
    'mp.AutoQuestArmReachAssistProfile',
    'mp.QuestArmMode',
    'mp.QuestWristTrace',
    'mp.MetaHumanArmSanity',
    'mp.QuestWristPositionBlend',
    'mp.QuestWristLostTrackingGraceSeconds',
    'mp.QuestWristPositionFilterResetDistanceCm',
    'mp.QuestWristRequireTrackedForApply',
    'mp.QuestConstrainedArmSolve',
    'mp.QuestConstrainedArmBodyFallback',
    'mp.QuestConstrainedArmBodyFallbackWristHalfLife',
    'mp.QuestConstrainedArmBodyFallbackMaxWristStepCm',
    'mp.QuestConstrainedArmReachScaleCalibration',
    'mp.QuestConstrainedArmReachScaleUniform',
    'mp.QuestConstrainedArmReachScaleMinObservedFraction',
    'mp.QuestConstrainedArmReachScaleApplyStartFraction',
    'mp.QuestConstrainedArmReachScaleApplyFullFraction',
    'mp.QuestConstrainedArmReachScaleMin',
    'mp.QuestConstrainedArmReachScaleMax',
    'mp.QuestArmLengthCalibrationStartup',
    'mp.QuestArmLengthCalibrationHud',
    'mp.QuestArmLengthCalibrationHoldSeconds',
    'mp.QuestArmLengthCalibrationStableFrames',
    'mp.QuestArmLengthCalibrationMaxHandVelocityCmSec',
    'mp.QuestArmLengthCalibrationForwardMinReachFraction',
    'mp.QuestArmLengthCalibrationDownMinBelowShoulderFraction',
    'mp.QuestArmLengthCalibrationDownMinVerticalDominance',
    'mp.QuestArmLengthCalibrationDownMinCorrectedReachFraction',
    'mp.QuestArmDownFrameCorrection',
    'mp.QuestArmDownFrameCorrectionMaxScale',
    'mp.QuestArmDropoutDownFallback',
    'mp.QuestArmDropoutDownFallbackRecentTrackedSeconds',
    'mp.QuestArmDropoutDownFallbackMinDownDominance',
    'mp.QuestArmDropoutDownFallbackBlendHalfLife',
    'mp.QuestConstrainedArmMaxReachFraction',
    'mp.QuestConstrainedArmMaxReachStepCm',
    'mp.QuestConstrainedArmSolvedPlaneMinSin',
    'mp.QuestConstrainedArmDownStraighten',
    'mp.QuestConstrainedArmDownStraightenReachFloorFraction',
    'mp.QuestConstrainedArmDownStraightenMaxReachFraction',
    'mp.QuestConstrainedArmElbowHalfLife',
    'mp.QuestConstrainedArmMaxElbowStepCm',
    'mp.QuestWristUseBasisDelta',
    'mp.QuestWristSemanticRollMinPalmProjection',
    'mp.QuestWristTwistBlend',
    'mp.QuestWristSwingBlend',
    'mp.QuestWristRequireNeutralCalibration',
    'mp.QuestWristCalibrationGate',
    'mp.QuestWristCalibrationHoldSeconds',
    'mp.QuestWristCalibrationStableFrames',
    'mp.QuestWristCalibrationMaxBasisErrorDegrees',
    'mp.QuestWristCalibrationMaxNeutralTwistDegrees',
    'mp.QuestWristTwistDrivesForearm',
    'mp.QuestWristForearmTwistBlend',
    'mp.QuestWristForearmMaxTwistDegrees',
    'mp.QuestWristForearmRollDriveTwistHelpers',
    'mp.QuestWristDriveTwistCorrection',
    'mp.QuestWristTwistCorrectionBlend',
    'mp.QuestWristForceArmIK',
    'mp.MediaPipeUseArmIK',
    'mp.MediaPipeDriveClavicles',
    'mp.MediaPipeDriveSpine',
    'mp.MediaPipeDrivePelvisTranslation',
    'mp.MediaPipeDriveLegs',
    'mp.MediaPipeDriveArmTwistBones',
    'mp.MediaPipeDriveMetaHumanArmHelpers',
    'mp.QuestFingerJointRetarget',
    'mp.QuestFingerCurlOnly',
]:
    print(f'{name}={unreal.SystemLibrary.get_console_variable_float_value(name)}')
"@
    $result = Invoke-BridgeTool -Tool "run_unreal_python" -ToolArgs @{ code = $code }
    if (-not $result.success) {
        throw "CVar setup failed: $($result | ConvertTo-Json -Depth 6)"
    }
    Write-Host $result.text
}
else {
    Write-Host "Skipping CVar setup because -SkipApplyCvars was passed."
}

if ($ResetNow) {
    if ($null -ne $editorState -and -not $editorState.pieRunning -and -not $AllowResetWithoutPie) {
        throw "Refusing to reset because PIE/VR Preview is not running. Start VR Preview first, or pass -AllowResetWithoutPie only for deliberate non-VR diagnostics."
    }
    if ($PreResetDelaySeconds -gt 0) {
        Write-Host "Reset will be sent in $PreResetDelaySeconds seconds. Put the headset on and get into the neutral calibration pose now."
        for ($remaining = $PreResetDelaySeconds; $remaining -gt 0; $remaining--) {
            if ($remaining -le 5 -or ($remaining % 5) -eq 0) {
                Write-Host "Reset in $remaining..."
            }
            Start-Sleep -Seconds 1
        }
    }
    Write-Host "Sending mp.ResetQuestWristCalibration. Reset enters calibration-wait mode; it does not need to accept on this exact frame."
    $code = @"
import unreal
world = unreal.EditorLevelLibrary.get_editor_world()
unreal.SystemLibrary.execute_console_command(world, 'mp.ResetQuestWristCalibration')
print('mp.ResetQuestWristCalibration sent')
"@
    $result = Invoke-BridgeTool -Tool "run_unreal_python" -ToolArgs @{ code = $code }
    if (-not $result.success) {
        throw "Reset command failed: $($result | ConvertTo-Json -Depth 6)"
    }
    Write-Host $result.text
}
else {
    Write-Host "Reset not sent. Pass -ResetNow while VR Preview is running. Add -PreResetDelaySeconds if you need time to put the headset on first."
}

if ($WaitSeconds -gt 0) {
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    $lastProgress = ""
    do {
        $state = Get-PostResetRightRows -Path $LogPath
        if ($state.HasReset) {
            $progress = "rightRows=$($state.RightRows) trackingRows=$($state.TrackingRows) state=$($state.LastCalibrationState) reason=$($state.LastCalibrationRejectReason)"
            if ($progress -ne $lastProgress) {
                Write-Host "Calibration progress after reset line $($state.ResetLine): $progress"
                $lastProgress = $progress
            }
        }
        if ($state.HasReset -and $state.TrackingRows -ge $MinTrackingRows) {
            Write-Host "Detected $($state.TrackingRows) right-wrist Tracking rows after reset line $($state.ResetLine)."
            break
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
}

$analyzer = Join-Path $PSScriptRoot "AnalyzeQuestWristRollLog.ps1"
& powershell -NoProfile -ExecutionPolicy Bypass -File $analyzer -LogPath $LogPath -Side R -AfterLastReset -ObjectiveGate -TailRows $TailRows
exit $LASTEXITCODE

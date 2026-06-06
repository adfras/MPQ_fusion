param(
    [string]$LogPath = "Saved/Logs/TestingKit3.log",
    [int]$TailQuestWristRows = 8,
    [switch]$RequireWornHeadsetTrace
)

$ErrorActionPreference = "Stop"

function Read-LogLines {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Log file not found: $Path"
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

function Get-LatestMatch {
    param(
        [string[]]$Lines,
        [string]$Pattern
    )

    for ($i = $Lines.Count - 1; $i -ge 0; $i--) {
        if ($Lines[$i] -match $Pattern) {
            return [pscustomobject]@{
                LineNumber = $i + 1
                Text = $Lines[$i]
            }
        }
    }
    return $null
}

function Test-Contains {
    param(
        [string]$Name,
        [string]$Text,
        [string]$Pattern,
        [System.Collections.Generic.List[string]]$Failures
    )

    if ($Text -notmatch $Pattern) {
        $Failures.Add("$Name missing pattern: $Pattern") | Out-Null
    }
}

function Get-LogNumber {
    param(
        [string]$Text,
        [string]$Name
    )

    if ($Text -match "$([regex]::Escape($Name))=(?<value>[-+0-9.eE]+)") {
        $number = 0.0
        if ([double]::TryParse($Matches.value, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$number)) {
            return $number
        }
    }
    return $null
}

$lines = Read-LogLines -Path $LogPath
$failures = New-Object System.Collections.Generic.List[string]
$warnings = New-Object System.Collections.Generic.List[string]

$profile = Get-LatestMatch -Lines $lines -Pattern "Auto Quest profile applied:"
$viewer = Get-LatestMatch -Lines $lines -Pattern "Auto Quest mirror: fixed viewer pawn="
$embodied = Get-LatestMatch -Lines $lines -Pattern "Auto Quest embodied:.*viewPawn="
$presentation = Get-LatestMatch -Lines $lines -Pattern "Auto Quest presentation mesh:"
$gameClass = Get-LatestMatch -Lines $lines -Pattern "Game class is"

if ($null -eq $profile) {
    $failures.Add("No Auto Quest profile line found.") | Out-Null
}
else {
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "armProfile=4" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "stableBody=1" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "clavicles=0" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "spine=0" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "questArmMode=3" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "wristBlend=1\.00" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "wristRequireTracked=1" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "reachAssist=1" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "driftGuard=1" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "constrainedArmSolve=1" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "constrainedArmBodyFallback=0" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "armHoldLoss=1" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "armDropoutDown=1" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "reachScale=1" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "armReachStepCm=0\.0" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "wristFilter=1" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "armTargetHL=0\.00" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "armRotHL=0\.00" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "handRotHL=0\.00" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "handRotStep=0\.0" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "handRotGrace=0\.20" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "wristMaxRel=82\.0" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "roomDeadband=8\.0" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "roomMaxStep=12\.0" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "roomMaxOffset=400\.0" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "armIK=0" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "forceArmIK=0" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "legs=0" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "legIK=0" -Failures $failures
    Test-Contains -Name "profile" -Text $profile.Text -Pattern "pelvisTranslation=0" -Failures $failures
}

if ($null -eq $viewer) {
    $failures.Add("No fixed viewer pawn line found.") | Out-Null
}
else {
    Test-Contains -Name "viewer" -Text $viewer.Text -Pattern "fixed viewer pawn=DefaultPawn_0" -Failures $failures
    Test-Contains -Name "viewer" -Text $viewer.Text -Pattern "springArms=0" -Failures $failures
    Test-Contains -Name "viewer" -Text $viewer.Text -Pattern "cameras=0" -Failures $failures
}

if ($null -eq $embodied) {
    $failures.Add("No embodied view line found.") | Out-Null
}
else {
    Test-Contains -Name "embodied" -Text $embodied.Text -Pattern "viewPawn=DefaultPawn_0" -Failures $failures
    Test-Contains -Name "embodied" -Text $embodied.Text -Pattern "forwardOffset=0\.0" -Failures $failures
    Test-Contains -Name "embodied" -Text $embodied.Text -Pattern "anchorMode=1" -Failures $failures
}

if ($null -eq $presentation) {
    $failures.Add("No Auto Quest presentation mesh line found. Rebuild/run with the Wallace MetaHuman post-process proof log.") | Out-Null
}
else {
    Test-Contains -Name "presentation" -Text $presentation.Text -Pattern "asset=.*m_med_unw_body" -Failures $failures
    Test-Contains -Name "presentation" -Text $presentation.Text -Pattern "postProcessClass=.*m_med_unw_animbp_Cinematic" -Failures $failures
    Test-Contains -Name "presentation" -Text $presentation.Text -Pattern "postProcessDisabled=0" -Failures $failures
}

$startLine = if ($null -ne $profile) { $profile.LineNumber } else { 1 }
$afterProfile = @($lines | Select-Object -Skip ($startLine - 1))
if (@($afterProfile | Where-Object { $_ -match "camera pinned" }).Count -gt 0) {
    $failures.Add("Found camera pinned line after latest Auto Quest profile.") | Out-Null
}

$oldVerticalRecenterRows = @($afterProfile | Where-Object {
    $_ -match "reset HMD origin for stable Wallace view" -and
    ($_ -notmatch "horizontalErrorBefore=" -or $_ -notmatch "rawZErrorBefore=")
})
if ($oldVerticalRecenterRows.Count -gt 0) {
    $failures.Add("Found stable HMD recenter rows without horizontal/raw-Z error fields. Rebuild/run with the horizontal-only recenter guard: $($oldVerticalRecenterRows.Count)") | Out-Null
}

$oldRoomScaleRows = @($afterProfile | Where-Object {
    $_ -match "room-scale follow applied" -and
    ($_ -notmatch "appliedCm=" -or $_ -notmatch "deadband=" -or $_ -notmatch "maxStep=")
})
if ($oldRoomScaleRows.Count -gt 0) {
    $failures.Add("Found room-scale follow rows without capped-step/deadband fields. Rebuild/run with the anti-bump room-scale guard: $($oldRoomScaleRows.Count)") | Out-Null
}

$thirdPersonUnexpected = @($afterProfile | Where-Object {
    $_ -match "BP_ThirdPersonCharacter" -and $_ -notmatch "destroying default Third Person viewer pawn"
})
if ($thirdPersonUnexpected.Count -gt 0) {
    $warnings.Add("Unexpected Third Person character mentions after profile: $($thirdPersonUnexpected.Count)") | Out-Null
}

$snapshotRows = @($afterProfile | Where-Object { $_ -match "mp\.QuestWristSnapshot:" })
$wallaceSnapshotRows = @($snapshotRows | Where-Object { $_ -match "actor=MP_LiveMetaHumanWallace" })
$wallaceHmdSnapshotRows = @($wallaceSnapshotRows | Where-Object { $_ -match "hmdPose=1" })
$wallaceTrackedHandSnapshotRows = @($wallaceSnapshotRows | Where-Object {
    ($_ -match "left\(has=1 tracked=1") -or
    ($_ -match "right\(has=1 tracked=1")
})

$wristRows = @($afterProfile | Where-Object { $_ -match "mp\.QuestWristSolve:" })
$wallaceWristRows = @($wristRows | Where-Object { $_ -match "actor=MP_LiveMetaHumanWallace" })
$armSanityRows = @($afterProfile | Where-Object { $_ -match "mp\.MetaHumanArmSanity:" })
$wallaceArmSanityRows = @($armSanityRows | Where-Object { $_ -match "actor=MP_LiveMetaHumanWallace" })
if ($wristRows.Count -eq 0) {
    $warnings.Add("No mp.QuestWristSolve rows found after profile. That is normal for a plain PIE smoke, but a worn-headset arm test should produce rows if wrist tracing is enabled.") | Out-Null
}
else {
    $badWristRows = @($wristRows | Where-Object {
        $_ -notmatch "questArmMode=3" -or
        $_ -notmatch "requestedBlend=1\.00"
    })
    if ($badWristRows.Count -gt 0) {
        $failures.Add("Found Quest wrist rows that are not on the HMD-relative avatar sync arm path: $($badWristRows.Count)") | Out-Null
    }
}

if ($RequireWornHeadsetTrace) {
    if ($snapshotRows.Count -eq 0) {
        $failures.Add("RequireWornHeadsetTrace was set, but no mp.QuestWristSnapshot rows were found after the latest profile. Enable mp.QuestWristTrace=1 before VR Preview.") | Out-Null
    }
    if ($wallaceHmdSnapshotRows.Count -eq 0) {
        $failures.Add("RequireWornHeadsetTrace was set, but no Wallace wrist snapshot had hmdPose=1 after the latest profile. This is not a worn/woken HMD proof.") | Out-Null
    }
    if ($wallaceTrackedHandSnapshotRows.Count -eq 0) {
        $failures.Add("RequireWornHeadsetTrace was set, but no Wallace wrist snapshot had a tracked Quest hand after the latest profile.") | Out-Null
    }
    if ($wallaceWristRows.Count -eq 0) {
        $failures.Add("RequireWornHeadsetTrace was set, but no Wallace mp.QuestWristSolve rows were found after the latest profile.") | Out-Null
    }
    else {
        $wallaceQuestWorldRows = @($wallaceWristRows | Where-Object {
            $_ -match "questArmMode=3" -and
            $_ -match "positionApplied=1" -and
            $_ -match "requireTrackedApply=1" -and
            $_ -match "requestedBlend=1\.00" -and
            $_ -match "calib=HMD_AVATAR" -and
            $_ -match "mapped=1"
        })
        if ($wallaceQuestWorldRows.Count -eq 0) {
            $failures.Add("RequireWornHeadsetTrace was set, but no Wallace wrist solve row proved the HMD-relative avatar sync path: questArmMode=3, positionApplied=1, requireTrackedApply=1, requestedBlend=1.00, calib=HMD_AVATAR, mapped=1.") | Out-Null
        }

        $wallaceConstrainedSourceHintRows = @($wallaceQuestWorldRows | Where-Object {
            $_ -match "questArmSolve=1" -and
            $_ -match "questArmBodyFallback=0" -and
            $_ -match "questArmSourceElbowHint=1" -and
            $_ -match "questArmSourceElbow="
        })
        if ($wallaceConstrainedSourceHintRows.Count -eq 0) {
            $failures.Add("RequireWornHeadsetTrace was set, but no non-body-fallback Wallace constrained arm row proved the source-elbow-hint path: questArmSolve=1, questArmBodyFallback=0, questArmSourceElbowHint=1, questArmSourceElbow=...") | Out-Null
        }

        $wallaceConstrainedRowsMissingSourceHint = @($wallaceQuestWorldRows | Where-Object {
            $_ -match "questArmSolve=1" -and
            $_ -match "questArmBodyFallback=0" -and
            ($_ -notmatch "questArmSourceElbowHint=" -or $_ -notmatch "questArmSourceElbow=")
        })
        if ($wallaceConstrainedRowsMissingSourceHint.Count -gt 0) {
            $failures.Add("Wallace constrained arm wrist rows are missing source-elbow-hint diagnostics. Rebuild/run with the 2026-05-20 source-elbow-hint formatter fields: $($wallaceConstrainedRowsMissingSourceHint.Count)") | Out-Null
        }

        $continuousUntrackedApplyRows = @($wallaceWristRows | Where-Object {
            $_ -match "requireTrackedApply=1" -and
            $_ -match "questTracked=0" -and
            $_ -match "untrackedData=1" -and
            $_ -match "positionApplied=1"
        })
        if ($continuousUntrackedApplyRows.Count -gt 0) {
            $warnings.Add("Wallace wrist solve consumed continuous untracked Quest wrist data under the constrained continuity policy: $($continuousUntrackedApplyRows.Count). Latest: $($continuousUntrackedApplyRows[-1])") | Out-Null
        }

        $fallbackRows = @($wallaceWristRows | Where-Object {
            $_ -match "questArmBodyFallback=1"
        })
        $fallbackRowsMissingReach = @($fallbackRows | Where-Object {
            $_ -notmatch "questArmBodyFallbackTargetReachCm=" -or
            $_ -notmatch "questArmBodyFallbackTargetReachFrac=" -or
            $_ -notmatch "questArmBodyFallbackDown="
        })
        if ($fallbackRowsMissingReach.Count -gt 0) {
            $failures.Add("Wallace body-fallback wrist rows are missing target reach/down-straighten diagnostics. Rebuild/run with the 2026-05-20 fallback diagnostic fields: $($fallbackRowsMissingReach.Count)") | Out-Null
        }

        $badFallbackDownRows = New-Object System.Collections.Generic.List[string]
        foreach ($fallbackRow in $fallbackRows) {
            if ($fallbackRow -match "questArmBodyFallbackDown=1") {
                $targetReachFraction = Get-LogNumber -Text $fallbackRow -Name "questArmBodyFallbackTargetReachFrac"
                if ($null -eq $targetReachFraction -or [double]$targetReachFraction -lt 0.996) {
                    $badFallbackDownRows.Add($fallbackRow) | Out-Null
                }
            }
        }
        if ($badFallbackDownRows.Count -gt 0) {
            $failures.Add("Wallace body fallback reported arms-down straightening but did not reach the near-full target fraction >=0.996: $($badFallbackDownRows.Count). Latest: $($badFallbackDownRows[-1])") | Out-Null
        }

        $wallaceTrackedHandSolveRows = @($wallaceWristRows | Where-Object {
            $_ -match "questHandTracked=1" -and
            $_ -match "handLocal=1"
        })
        if ($wallaceTrackedHandSolveRows.Count -eq 0) {
            $failures.Add("RequireWornHeadsetTrace was set, but no Wallace wrist solve row showed tracked Quest hand rotation applied with handLocal=1.") | Out-Null
        }
    }

    if ($wallaceArmSanityRows.Count -eq 0) {
        $failures.Add("RequireWornHeadsetTrace was set, but no Wallace mp.MetaHumanArmSanity rows were found after the latest profile. This run did not check the actual posed MetaHuman arm chain.") | Out-Null
    }
    else {
        $brokenArmRows = @($wallaceArmSanityRows | Where-Object { $_ -match "broken=1" })
        if ($brokenArmRows.Count -gt 0) {
            $failures.Add("Wallace MetaHuman arm sanity reported broken posed arms after the latest profile: $($brokenArmRows.Count). Latest: $($brokenArmRows[-1])") | Out-Null
        }

        $goodTrackedArmRows = @($wallaceArmSanityRows | Where-Object {
            $_ -match "questTracked=1" -and
            $_ -match "targetMapped=1" -and
            $_ -match "positionApplied=1" -and
            $_ -match "broken=0"
        })
        if ($goodTrackedArmRows.Count -eq 0) {
            $failures.Add("RequireWornHeadsetTrace was set, but no Wallace MetaHuman arm sanity row proved tracked mapped Quest input with broken=0.") | Out-Null
        }
    }
}

Write-Host "Wallace Quest VR log gate: $LogPath"
if ($RequireWornHeadsetTrace) { Write-Host "Worn-headset trace required: true" }
if ($null -ne $gameClass) { Write-Host "Game class line $($gameClass.LineNumber): $($gameClass.Text)" }
if ($null -ne $profile) { Write-Host "Profile line $($profile.LineNumber): $($profile.Text)" }
if ($null -ne $viewer) { Write-Host "Viewer line $($viewer.LineNumber): $($viewer.Text)" }
if ($null -ne $embodied) { Write-Host "Embodied line $($embodied.LineNumber): $($embodied.Text)" }
if ($null -ne $presentation) { Write-Host "Presentation line $($presentation.LineNumber): $($presentation.Text)" }

if ($snapshotRows.Count -gt 0) {
    Write-Host "Quest wrist snapshots after profile: total=$($snapshotRows.Count) wallace=$($wallaceSnapshotRows.Count) wallaceHmdPose=$($wallaceHmdSnapshotRows.Count) wallaceTrackedHand=$($wallaceTrackedHandSnapshotRows.Count)"
}

if ($wristRows.Count -gt 0) {
    Write-Host "Latest Quest wrist solve rows:"
    $wristRows | Select-Object -Last $TailQuestWristRows | ForEach-Object { Write-Host $_ }
}

if ($armSanityRows.Count -gt 0) {
    Write-Host "Latest MetaHuman arm sanity rows:"
    $armSanityRows | Select-Object -Last $TailQuestWristRows | ForEach-Object { Write-Host $_ }
}

foreach ($warning in $warnings) {
    Write-Warning $warning
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

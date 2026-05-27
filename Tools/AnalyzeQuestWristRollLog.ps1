param(
    [string]$LogPath = "Saved/Logs/TestingKit3.log",
    [ValidateSet("L", "R", "All")]
    [string]$Side = "R",
    [int]$TailRows = 12,
    [int]$AfterLine = 0,
    [switch]$AfterLastReset,
    [switch]$ObjectiveGate,
    [switch]$WristAlignmentGate,
    [int]$MinTrackedAppliedRows = 6,
    [double]$MaxQuestToMannyDeg = 25.0,
    [double]$TwistOnlyMaxSwingAppliedDeg = 1.0,
    [double]$TwistOnlyMinRawTwistDeg = 45.0,
    [string]$OutCsv = ""
)

$ErrorActionPreference = "Stop"

function Convert-ToNumber {
    param([string]$Value)
    $number = 0.0
    if ([double]::TryParse($Value, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$number)) {
        return $number
    }
    return $null
}

function Get-Stats {
    param(
        [object[]]$Rows,
        [string]$Name
    )
    $values = @($Rows | ForEach-Object { $_.$Name } | Where-Object { $null -ne $_ })
    if ($values.Count -eq 0) {
        return $null
    }

    $sum = 0.0
    foreach ($value in $values) {
        $sum += [double]$value
    }
    [pscustomobject]@{
        Field = $Name
        Count = $values.Count
        Min = "{0:n1}" -f (($values | Measure-Object -Minimum).Minimum)
        Avg = "{0:n1}" -f ($sum / [double]$values.Count)
        Max = "{0:n1}" -f (($values | Measure-Object -Maximum).Maximum)
    }
}

function Count-Flag {
    param(
        [object[]]$Rows,
        [string]$Name
    )
    @($Rows | Where-Object { $_.$Name -eq 1 }).Count
}

if (-not (Test-Path -LiteralPath $LogPath)) {
    throw "Log file not found: $LogPath"
}

$rows = New-Object System.Collections.Generic.List[object]
$lineNumber = 0
$lastResetLine = 0
$lastResetTime = ""
$lastResetActor = ""
$lastResetSerial = ""
$resolvedLogPath = (Resolve-Path -LiteralPath $LogPath).Path
$stream = [System.IO.File]::Open($resolvedLogPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
try {
    $reader = New-Object System.IO.StreamReader($stream)
    try {
        while (($line = $reader.ReadLine()) -ne $null) {
            $lineNumber++
            if ($line -match "mp\.ResetQuestWristCalibration: applied") {
                $lastResetLine = $lineNumber
                if ($line -match "^\[(?<time>[^\]]+)\]") {
                    $lastResetTime = $Matches.time
                }
                if ($line -match "actor=(?<actor>[^ ]+)") {
                    $lastResetActor = $Matches.actor
                }
                if ($line -match "serial=(?<serial>[-0-9]+)") {
                    $lastResetSerial = $Matches.serial
                }
            }
            if ($line -notmatch "mp\.QuestWristRollCompact:") {
                continue
            }

            $record = [ordered]@{
                Line = $lineNumber
                Time = ""
            }
            if ($line -match "^\[(?<time>[^\]]+)\]") {
                $record.Time = $Matches.time
            }

            foreach ($match in [regex]::Matches($line, '([A-Za-z][A-Za-z0-9_]*)=("[^"]*"|[^ ]+)')) {
                $key = $match.Groups[1].Value
                $raw = $match.Groups[2].Value.TrimEnd(",").Trim('"')
                $number = Convert-ToNumber $raw
                if ($null -ne $number) {
                    $record[$key] = $number
                }
                else {
                    $record[$key] = $raw
                }
            }

            if ($Side -ne "All" -and $record.side -ne $Side) {
                continue
            }

            $rows.Add([pscustomobject]$record) | Out-Null
        }
    }
    finally {
        $reader.Dispose()
    }
}
finally {
    $stream.Dispose()
}

if ($AfterLastReset) {
    if ($lastResetLine -gt 0) {
        $rows = @($rows | Where-Object { $_.Line -gt $lastResetLine })
    }
    else {
        Write-Host "AfterLastReset requested, but no mp.ResetQuestWristCalibration marker was found in $LogPath"
    }
}
if ($AfterLine -gt 0) {
    $rows = @($rows | Where-Object { $_.Line -gt $AfterLine })
}

if ($rows.Count -eq 0) {
    if ($AfterLine -gt 0) {
        Write-Host "No mp.QuestWristRollCompact rows found for side=$Side after line $AfterLine in $LogPath"
    }
    elseif ($AfterLastReset -and $lastResetLine -gt 0) {
        Write-Host "No mp.QuestWristRollCompact rows found for side=$Side after reset line $lastResetLine time=$lastResetTime actor=$lastResetActor serial=$lastResetSerial in $LogPath"
    }
    else {
        Write-Host "No mp.QuestWristRollCompact rows found for side=$Side in $LogPath"
    }
    if ($ObjectiveGate -or $WristAlignmentGate) {
        exit 2
    }
    exit 0
}

$fields = @(
    "stableFrameCount",
    "calibErrDeg",
    "neutralTwistDeg",
    "calibReject",
    "rawTwistDeg",
    "limitedTwistDeg",
    "sourceHandTwistDeg",
    "palmFallback",
    "palmHeld",
    "swingRawDeg",
    "swingAppliedDeg",
    "forearmTargetDeg",
    "forearmAppliedDeg",
    "forearmStepDeg",
    "forearmVelDegSec",
    "forearmHelperScale",
    "forearmTwist01Deg",
    "forearmTwist02Deg",
    "handDeltaDeg",
    "handAppliedDeltaDeg",
    "questToMannyDeg",
    "questToRollTargetDeg",
    "rollTargetToMannyDeg",
    "questFwdErrDeg",
    "questUpErrDeg",
    "rollFwdErrDeg",
    "rollUpErrDeg",
    "questBasisFwdErrDeg",
    "questBasisUpErrDeg",
    "questBasisRollFwdErrDeg",
    "questBasisRollUpErrDeg"
)

$orderedRows = @($rows | Sort-Object Line)
$jumpRows = New-Object System.Collections.Generic.List[object]
for ($i = 1; $i -lt $orderedRows.Count; $i++) {
    $prev = $orderedRows[$i - 1]
    $cur = $orderedRows[$i]
    $jumpRows.Add([pscustomobject]@{
        Line = $cur.Line
        side = $cur.side
        rawTwistJumpDeg = [math]::Abs([double]$cur.rawTwistDeg - [double]$prev.rawTwistDeg)
        swingAppliedJumpDeg = [math]::Abs([double]$cur.swingAppliedDeg - [double]$prev.swingAppliedDeg)
        forearmAppliedJumpDeg = [math]::Abs([double]$cur.forearmAppliedDeg - [double]$prev.forearmAppliedDeg)
        handAppliedJumpDeg = [math]::Abs([double]$cur.handAppliedDeltaDeg - [double]$prev.handAppliedDeltaDeg)
        rollTargetToMannyJumpDeg = [math]::Abs([double]$cur.rollTargetToMannyDeg - [double]$prev.rollTargetToMannyDeg)
        questBasisFwdErrJumpDeg = if ($null -ne $cur.questBasisFwdErrDeg -and $null -ne $prev.questBasisFwdErrDeg) { [math]::Abs([double]$cur.questBasisFwdErrDeg - [double]$prev.questBasisFwdErrDeg) } else { $null }
        questBasisUpErrJumpDeg = if ($null -ne $cur.questBasisUpErrDeg -and $null -ne $prev.questBasisUpErrDeg) { [math]::Abs([double]$cur.questBasisUpErrDeg - [double]$prev.questBasisUpErrDeg) } else { $null }
    }) | Out-Null
}

Write-Host "Quest wrist compact trace: $LogPath"
if ($AfterLastReset -and $lastResetLine -gt 0) {
    Write-Host "Filter: rows after last mp.ResetQuestWristCalibration line=$lastResetLine time=$lastResetTime actor=$lastResetActor serial=$lastResetSerial"
}
if ($AfterLine -gt 0) {
    Write-Host "Filter: rows after log line $AfterLine"
}
Write-Host "Rows: $($rows.Count) side=$Side firstLine=$($orderedRows[0].Line) lastLine=$($orderedRows[-1].Line)"
Write-Host ("State counts: applied={0}/{1} tracked={2}/{1} mapped={3}/{1} semantic={4}/{1} semanticLocal={5}/{1} semanticBasis={6}/{1} handLocal={7}/{1} twistCorrection={8}/{1} lowerarmMainDriven={9}/{1} armIK={10}/{1} forceIK={11}/{1}" -f `
    (Count-Flag $orderedRows "applied"),
    $orderedRows.Count,
    (Count-Flag $orderedRows "tracked"),
    (Count-Flag $orderedRows "mapped"),
    (Count-Flag $orderedRows "semantic"),
    (Count-Flag $orderedRows "semanticLocal"),
    (Count-Flag $orderedRows "semanticBasis"),
    (Count-Flag $orderedRows "handLocal"),
    (Count-Flag $orderedRows "twistCorrection"),
    (Count-Flag $orderedRows "lowerarmMainDriven"),
    (Count-Flag $orderedRows "armIK"),
    (Count-Flag $orderedRows "forceIK"))

$trackedAppliedRows = @($orderedRows | Where-Object { $_.applied -eq 1 -and $_.tracked -eq 1 })
$questMismatchRows = @($trackedAppliedRows | Where-Object {
    $null -ne $_.questToMannyDeg -and [double]$_.questToMannyDeg -gt $MaxQuestToMannyDeg
})
$twistOnlyMismatchRows = @($trackedAppliedRows | Where-Object {
    $null -ne $_.questToMannyDeg -and
    $null -ne $_.swingAppliedDeg -and
    $null -ne $_.rawTwistDeg -and
    [double]$_.questToMannyDeg -gt $MaxQuestToMannyDeg -and
    [math]::Abs([double]$_.swingAppliedDeg) -le $TwistOnlyMaxSwingAppliedDeg -and
    [math]::Abs([double]$_.rawTwistDeg) -ge $TwistOnlyMinRawTwistDeg
})
Write-Host ("Tracked applied alignment: rows={0} questToManny>{1:n1}={2} twistOnlyMismatch={3} thresholds(swing<={4:n1}, absRawTwist>={5:n1})" -f `
    $trackedAppliedRows.Count,
    $MaxQuestToMannyDeg,
    $questMismatchRows.Count,
    $twistOnlyMismatchRows.Count,
    $TwistOnlyMaxSwingAppliedDeg,
    $TwistOnlyMinRawTwistDeg)
if ($questMismatchRows.Count -gt 0) {
    Write-Host "Top Quest-to-Manny mismatch rows:"
    $questMismatchRows |
        Sort-Object { [double]$_.questToMannyDeg } -Descending |
        Select-Object -First 12 Line,Time,side,applied,tracked,mapped,calibrationState,semantic,semanticLocal,semanticBasis,handLocal,rawTwistDeg,limitedTwistDeg,swingAppliedDeg,handAppliedDeltaDeg,questToMannyDeg,rollTargetToMannyDeg,questBasisFwdErrDeg,questBasisUpErrDeg,axis,score |
        Format-Table -AutoSize
}
if ($orderedRows[0].PSObject.Properties.Name -contains "calibrationState") {
    $calibrationSummary = @($orderedRows | Group-Object calibrationState | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Count)" }) -join " "
    Write-Host "Calibration states: $calibrationSummary"
}
if ($orderedRows[0].PSObject.Properties.Name -contains "calibrationRejectReason") {
    $rejectSummary = @($orderedRows | Group-Object calibrationRejectReason | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Count)" }) -join " "
    Write-Host "Calibration reject reasons: $rejectSummary"
}
Write-Host ("Clamp counts: twist={0} forearmLimit={1} forearmRate={2}" -f `
    (Count-Flag $orderedRows "twistClamp"),
    (Count-Flag $orderedRows "forearmClamp"),
    (Count-Flag $orderedRows "forearmRateClamp"))

$semanticAxisRows = @($orderedRows | Where-Object { $_.semantic -eq 1 -and $null -ne $_.axis })
if ($semanticAxisRows.Count -gt 0) {
    $axisSummary = @($semanticAxisRows | Group-Object axis | Sort-Object Name | ForEach-Object { "axis$($_.Name)=$($_.Count)" }) -join " "
    Write-Host "Semantic axis distribution: $axisSummary"
}

$swingRows = @($orderedRows | Where-Object { $null -ne $_.swingAppliedDeg -and [math]::Abs([double]$_.swingAppliedDeg) -gt 20.0 })
if ($swingRows.Count -gt 0) {
    $twistSum = 0.0
    $ratioSum = 0.0
    foreach ($row in $swingRows) {
        $twistAbs = [math]::Abs([double]$row.rawTwistDeg)
        $swingAbs = [math]::Max(1.0, [math]::Abs([double]$row.swingAppliedDeg))
        $twistSum += $twistAbs
        $ratioSum += ($twistAbs / $swingAbs)
    }
    Write-Host ("Up/down coupling heuristic: swingFrames>20={0} avgAbsTwistWhenSwing={1:n1} avgAbsTwistPerSwing={2:n2} highTwistWhileSwing>45={3}" -f `
        $swingRows.Count,
        ($twistSum / [double]$swingRows.Count),
        ($ratioSum / [double]$swingRows.Count),
        @($swingRows | Where-Object { [math]::Abs([double]$_.rawTwistDeg) -gt 45.0 }).Count)
}

Write-Host ""
Write-Host "Ranges:"
$stats = foreach ($field in $fields) {
    Get-Stats $orderedRows $field
}
$stats | Where-Object { $null -ne $_ } | Format-Table -AutoSize

if ($jumpRows.Count -gt 0) {
    Write-Host ""
    Write-Host "Sample-to-sample jumps:"
    $jumpStats = foreach ($field in @("rawTwistJumpDeg", "swingAppliedJumpDeg", "forearmAppliedJumpDeg", "handAppliedJumpDeg", "rollTargetToMannyJumpDeg", "questBasisFwdErrJumpDeg", "questBasisUpErrJumpDeg")) {
        Get-Stats $jumpRows $field
    }
    $jumpStats | Where-Object { $null -ne $_ } | Format-Table -AutoSize
    Write-Host ("Large jumps: raw>30={0} raw>50={1} swing>20={2} forearm>20={3} forearm>40={4} handApplied>20={5} rollTargetToManny>20={6} basisFwd>20={7} basisUp>20={8}" -f `
        @($jumpRows | Where-Object { $_.rawTwistJumpDeg -gt 30.0 }).Count,
        @($jumpRows | Where-Object { $_.rawTwistJumpDeg -gt 50.0 }).Count,
        @($jumpRows | Where-Object { $_.swingAppliedJumpDeg -gt 20.0 }).Count,
        @($jumpRows | Where-Object { $_.forearmAppliedJumpDeg -gt 20.0 }).Count,
        @($jumpRows | Where-Object { $_.forearmAppliedJumpDeg -gt 40.0 }).Count,
        @($jumpRows | Where-Object { $_.handAppliedJumpDeg -gt 20.0 }).Count,
        @($jumpRows | Where-Object { $_.rollTargetToMannyJumpDeg -gt 20.0 }).Count,
        @($jumpRows | Where-Object { $null -ne $_.questBasisFwdErrJumpDeg -and $_.questBasisFwdErrJumpDeg -gt 20.0 }).Count,
        @($jumpRows | Where-Object { $null -ne $_.questBasisUpErrJumpDeg -and $_.questBasisUpErrJumpDeg -gt 20.0 }).Count)
}

if ($TailRows -gt 0) {
    Write-Host ""
    Write-Host "Tail rows:"
    $orderedRows |
        Select-Object -Last $TailRows Line,Time,side,applied,tracked,mapped,calibrationState,calibrationRejectReason,stableFrameCount,semantic,semanticLocal,semanticBasis,palmFallback,palmHeld,handLocal,twistCorrection,lowerarmMainDriven,axis,score,calibErrDeg,neutralTwistDeg,calibReject,rawTwistDeg,limitedTwistDeg,sourceHandTwistDeg,swingAppliedDeg,forearmAppliedDeg,forearmHelperScale,forearmTwist01Deg,forearmTwist02Deg,forearmRateClamp,handAppliedDeltaDeg,rollTargetToMannyDeg,questBasisFwdErrDeg,questBasisUpErrDeg,armIK,forceIK |
        Format-Table -AutoSize
}

if ($OutCsv -ne "") {
    $orderedRows | Export-Csv -LiteralPath $OutCsv -NoTypeInformation
    Write-Host ""
    Write-Host "Wrote CSV: $OutCsv"
}

if ($ObjectiveGate) {
    $issues = New-Object System.Collections.Generic.List[string]
    $warnings = New-Object System.Collections.Generic.List[string]
    $preTrackingRows = @($orderedRows | Where-Object { $_.calibrationState -ne "Tracking" })
    $trackingRows = @($orderedRows | Where-Object { $_.calibrationState -eq "Tracking" })
    $gateRows = if ($trackingRows.Count -gt 0) { $trackingRows } else { @() }
    $rowCount = $gateRows.Count

    if ($Side -ne "R") {
        $issues.Add("ObjectiveGate is for the right wrist; run with -Side R.") | Out-Null
    }
    if (-not $AfterLastReset) {
        $issues.Add("ObjectiveGate requires -AfterLastReset so stale rows cannot pass.") | Out-Null
    }
    if ($lastResetLine -le 0) {
        $issues.Add("No mp.ResetQuestWristCalibration marker was found.") | Out-Null
    }
    if ($orderedRows.Count -lt 6) {
        $issues.Add("Too few right-wrist rows after reset: $($orderedRows.Count).") | Out-Null
    }
    if ($trackingRows.Count -lt 6) {
        $issues.Add("Too few right-wrist Tracking rows after calibration acceptance: $($trackingRows.Count).") | Out-Null
    }
    $appliedBeforeTracking = @($preTrackingRows | Where-Object { $_.applied -eq 1 }).Count
    if ($appliedBeforeTracking -gt 0) {
        $issues.Add("Quest wrist rotation applied before calibration reached Tracking: $appliedBeforeTracking rows.") | Out-Null
    }

    foreach ($flagName in @("applied", "tracked", "mapped", "semantic", "semanticLocal", "semanticBasis", "handLocal", "twistCorrection")) {
        $count = Count-Flag $gateRows $flagName
        if ($count -ne $rowCount) {
            $issues.Add("$flagName count in Tracking rows was $count/$rowCount.") | Out-Null
        }
    }

    foreach ($flagName in @("armIK", "forceIK", "calibReject", "lowerarmMainDriven")) {
        $count = Count-Flag $gateRows $flagName
        if ($count -ne 0) {
            $issues.Add("$flagName must be 0 for all Tracking rows but was $count/$rowCount.") | Out-Null
        }
    }

    $badAxisRows = @($gateRows | Where-Object { $_.semantic -eq 1 -and $null -ne $_.axis -and [int]$_.axis -ne 3 })
    if ($badAxisRows.Count -gt 0) {
        $issues.Add("Semantic basis roll must stay on projected palm-normal axis=3 for the right-local solve; bad rows=$($badAxisRows.Count).") | Out-Null
    }

    if ($jumpRows.Count -gt 0) {
        $rawTwistLarge = @($jumpRows | Where-Object { $_.rawTwistJumpDeg -gt 50.0 }).Count
        $swingLarge = @($jumpRows | Where-Object { $_.swingAppliedJumpDeg -gt 35.0 }).Count
        $forearmLarge = @($jumpRows | Where-Object { $_.forearmAppliedJumpDeg -gt 25.0 }).Count
        if ($rawTwistLarge -gt 0) {
            $warnings.Add("Large raw twist jumps >50 deg: $rawTwistLarge.") | Out-Null
        }
        if ($swingLarge -gt 0) {
            $warnings.Add("Large swing jumps >35 deg: $swingLarge.") | Out-Null
        }
        if ($forearmLarge -gt 0) {
            $warnings.Add("Large forearm roll jumps >25 deg: $forearmLarge.") | Out-Null
        }
    }

    $highTwistWhileSwing = @($gateRows | Where-Object {
        $null -ne $_.swingAppliedDeg -and
        $null -ne $_.rawTwistDeg -and
        [math]::Abs([double]$_.swingAppliedDeg) -gt 20.0 -and
        [math]::Abs([double]$_.rawTwistDeg) -gt 45.0
    }).Count
    if ($highTwistWhileSwing -gt 0) {
        $warnings.Add("High twist while swing is large: $highTwistWhileSwing rows.") | Out-Null
    }

    Write-Host ""
    Write-Host "Objective gate:"
    if ($issues.Count -eq 0) {
        Write-Host "PASS required evidence: fresh right-wrist rows, no applied wrist rotation before Tracking, Tracking rows with semanticLocal=1, semanticBasis=1, handLocal=1, twistCorrection=1, lowerarmMainDriven=0, armIK=0, forceIK=0."
    }
    else {
        foreach ($issue in $issues) {
            Write-Host "FAIL: $issue"
        }
    }
    foreach ($warning in $warnings) {
        Write-Host "WARN: $warning"
    }

    if ($issues.Count -gt 0) {
        exit 2
    }
}

if ($WristAlignmentGate) {
    $issues = New-Object System.Collections.Generic.List[string]
    $warnings = New-Object System.Collections.Generic.List[string]

    if ($Side -eq "All") {
        $warnings.Add("WristAlignmentGate is most useful with one side at a time; current side=All.") | Out-Null
    }
    if ($trackedAppliedRows.Count -lt $MinTrackedAppliedRows) {
        $issues.Add("Too few tracked applied wrist rows: $($trackedAppliedRows.Count), required $MinTrackedAppliedRows.") | Out-Null
    }
    if ($questMismatchRows.Count -gt 0) {
        $issues.Add("Tracked Quest wrist applied but Manny diverged from the Quest hand target in $($questMismatchRows.Count) rows; max allowed questToMannyDeg is $MaxQuestToMannyDeg.") | Out-Null
    }
    if ($twistOnlyMismatchRows.Count -gt 0) {
        $issues.Add("Detected $($twistOnlyMismatchRows.Count) twist-only mismatch rows: swingAppliedDeg <= $TwistOnlyMaxSwingAppliedDeg, abs(rawTwistDeg) >= $TwistOnlyMinRawTwistDeg, and questToMannyDeg > $MaxQuestToMannyDeg.") | Out-Null
    }

    Write-Host ""
    Write-Host "Wrist alignment gate:"
    if ($issues.Count -eq 0) {
        Write-Host "PASS required evidence: tracked applied Quest wrist rows keep Manny aligned with the Quest hand target within $MaxQuestToMannyDeg degrees."
    }
    else {
        foreach ($issue in $issues) {
            Write-Host "FAIL: $issue"
        }
    }
    foreach ($warning in $warnings) {
        Write-Host "WARN: $warning"
    }

    if ($issues.Count -gt 0) {
        exit 2
    }
}

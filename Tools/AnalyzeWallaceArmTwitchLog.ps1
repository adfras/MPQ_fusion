param(
    [string]$LogPath = "Saved/Logs/TestingKit3.log",
    [string]$Actor = "MP_LiveMetaHumanWallace",
    [ValidateSet("L", "R", "All")]
    [string]$Side = "All",
    [switch]$AfterLastVrPreview,
    [int]$SinceLine = 0,
    [string]$OutDir = "",
    [double]$PositionJumpThresholdCm = 6.0,
    [double]$AngleJumpThresholdDeg = 15.0,
    [double]$SpeedThresholdCmSec = 120.0,
    [int]$TopRows = 20,
    [switch]$RequireRows,
    [switch]$FailOnSpikes,
    [switch]$FailOnBroken
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

function Convert-ToLogTime {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $null
    }
    try {
        return [datetime]::ParseExact($Value, "yyyy.MM.dd-HH.mm.ss:fff", [Globalization.CultureInfo]::InvariantCulture)
    }
    catch {
        return $null
    }
}

function ConvertTo-WorkspaceRelativePath {
    param([string]$Path)

    $full = [System.IO.Path]::GetFullPath($Path)
    $root = [System.IO.Path]::GetFullPath((Get-Location).Path).TrimEnd("\", "/")
    if ($full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        return ($full.Substring($root.Length).TrimStart("\", "/") -replace "\\", "/")
    }
    return $full
}

function Convert-ToVector {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value) -or $Value -eq "V(0)") {
        return $null
    }
    if ($Value -match '^V\(X=(?<x>[-+0-9.eE]+), Y=(?<y>[-+0-9.eE]+), Z=(?<z>[-+0-9.eE]+)\)$') {
        return [pscustomobject]@{
            X = [double]::Parse($Matches.x, [Globalization.CultureInfo]::InvariantCulture)
            Y = [double]::Parse($Matches.y, [Globalization.CultureInfo]::InvariantCulture)
            Z = [double]::Parse($Matches.z, [Globalization.CultureInfo]::InvariantCulture)
        }
    }
    return $null
}

function Get-VectorDistance {
    param($A, $B)
    if ($null -eq $A -or $null -eq $B) {
        return $null
    }
    $dx = [double]$A.X - [double]$B.X
    $dy = [double]$A.Y - [double]$B.Y
    $dz = [double]$A.Z - [double]$B.Z
    return [math]::Sqrt(($dx * $dx) + ($dy * $dy) + ($dz * $dz))
}

function Get-PropertyValue {
    param(
        [object]$Object,
        [string]$Name
    )
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    return $property.Value
}

function Get-NumberDelta {
    param($Current, $Previous, [string]$Name)
    $curValue = Get-PropertyValue -Object $Current -Name $Name
    $prevValue = Get-PropertyValue -Object $Previous -Name $Name
    if ($null -eq $curValue -or $null -eq $prevValue) {
        return $null
    }
    return [math]::Abs([double]$curValue - [double]$prevValue)
}

function Count-Flag {
    param(
        [object[]]$Rows,
        [string]$Name
    )
    @($Rows | Where-Object { (Get-PropertyValue -Object $_ -Name $Name) -eq 1 }).Count
}

function Get-Stats {
    param(
        [object[]]$Rows,
        [string]$Name
    )
    $values = @($Rows | ForEach-Object { Get-PropertyValue -Object $_ -Name $Name } | Where-Object { $null -ne $_ })
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
        Min = "{0:n2}" -f (($values | Measure-Object -Minimum).Minimum)
        Avg = "{0:n2}" -f ($sum / [double]$values.Count)
        Max = "{0:n2}" -f (($values | Measure-Object -Maximum).Maximum)
    }
}

if (-not (Test-Path -LiteralPath $LogPath)) {
    throw "Log file not found: $LogPath"
}

$resolvedLogPath = (Resolve-Path -LiteralPath $LogPath).Path
$startLine = [math]::Max(0, $SinceLine)
$lineNumber = 0

if ($AfterLastVrPreview) {
    $stream = [System.IO.File]::Open($resolvedLogPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object System.IO.StreamReader($stream)
        try {
            while (($line = $reader.ReadLine()) -ne $null) {
                $lineNumber++
                if ($line -match "Repeating last play command: VR Preview" -or
                    $line -match "PlayLevel: Creating play world package" -or
                    $line -match "Auto Quest profile applied:") {
                    $startLine = $lineNumber
                }
            }
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

$rows = New-Object System.Collections.Generic.List[object]
$lineNumber = 0
$stream = [System.IO.File]::Open($resolvedLogPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
try {
    $reader = New-Object System.IO.StreamReader($stream)
    try {
        while (($line = $reader.ReadLine()) -ne $null) {
            $lineNumber++
            if ($lineNumber -le $startLine) {
                continue
            }
            if ($line -notmatch "mp\.MetaHumanArmSanity:" -or $line -notmatch "actor=$([regex]::Escape($Actor))") {
                continue
            }

            $record = [ordered]@{
                Line = $lineNumber
                Time = ""
                LogTime = $null
                Frame = $null
                Actor = $Actor
            }
            if ($line -match "^\[(?<time>[^\]]+)\]\[(?<frame>[^\]]+)\]") {
                $record.Time = $Matches.time
                $record.LogTime = Convert-ToLogTime $Matches.time
                $frameText = $Matches.frame.Trim()
                $frameNumber = Convert-ToNumber $frameText
                if ($null -ne $frameNumber) {
                    $record.Frame = [int]$frameNumber
                }
            }

            foreach ($match in [regex]::Matches($line, '([A-Za-z][A-Za-z0-9_]*)=(V\([^)]*\)|"[^"]*"|[^ ]+)')) {
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

            foreach ($vectorField in @("posedShoulder", "posedElbow", "posedHand", "finalWrist", "mappedWrist", "solveShoulder", "solveElbow")) {
                $value = if ($record.Contains($vectorField)) { [string]$record[$vectorField] } else { "" }
                $vector = Convert-ToVector $value
                if ($null -ne $vector) {
                    $record["${vectorField}X"] = $vector.X
                    $record["${vectorField}Y"] = $vector.Y
                    $record["${vectorField}Z"] = $vector.Z
                }
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

if ($rows.Count -eq 0) {
    Write-Host "No mp.MetaHumanArmSanity rows found for actor=$Actor side=$Side in $LogPath after line $startLine"
    if ($RequireRows -or $FailOnSpikes -or $FailOnBroken) {
        Write-Host "FAIL: required arm sanity rows were not found."
        exit 1
    }
    exit 0
}

$orderedRows = @($rows | Sort-Object Line)
$spikes = New-Object System.Collections.Generic.List[object]
$previousBySide = @{}

foreach ($row in $orderedRows) {
    $sideKey = [string]$row.side
    if (-not $previousBySide.ContainsKey($sideKey)) {
        $previousBySide[$sideKey] = $row
        continue
    }

    $prev = $previousBySide[$sideKey]
    $dtSec = $null
    if ($null -ne $row.LogTime -and $null -ne $prev.LogTime) {
        $dtSec = ([datetime]$row.LogTime - [datetime]$prev.LogTime).TotalSeconds
        if ($dtSec -le 0.0) {
            $dtSec = $null
        }
    }

    $posedElbowDelta = Get-VectorDistance (Convert-ToVector ([string]$row.posedElbow)) (Convert-ToVector ([string]$prev.posedElbow))
    $solveElbowDelta = Get-VectorDistance (Convert-ToVector ([string]$row.solveElbow)) (Convert-ToVector ([string]$prev.solveElbow))
    $posedHandDelta = Get-VectorDistance (Convert-ToVector ([string]$row.posedHand)) (Convert-ToVector ([string]$prev.posedHand))
    $finalWristDelta = Get-VectorDistance (Convert-ToVector ([string]$row.finalWrist)) (Convert-ToVector ([string]$prev.finalWrist))
    $mappedWristDelta = Get-VectorDistance (Convert-ToVector ([string]$row.mappedWrist)) (Convert-ToVector ([string]$prev.mappedWrist))

    $maxPositionDelta = @($posedElbowDelta, $solveElbowDelta, $posedHandDelta, $finalWristDelta, $mappedWristDelta) |
        Where-Object { $null -ne $_ } |
        Measure-Object -Maximum |
        Select-Object -ExpandProperty Maximum
    if ($null -eq $maxPositionDelta) {
        $maxPositionDelta = 0.0
    }

    $maxSpeed = $null
    if ($null -ne $dtSec -and $dtSec -gt 0.0) {
        $maxSpeed = [double]$maxPositionDelta / [double]$dtSec
    }

    $elbowBendJump = Get-NumberDelta $row $prev "elbowBendDeg"
    $targetReachJump = Get-NumberDelta $row $prev "targetReachCm"
    $posedReachJump = Get-NumberDelta $row $prev "posedReachCm"
    $swingJump = Get-NumberDelta $row $prev "swingAppliedDeg"
    $twistJump = Get-NumberDelta $row $prev "twistLimitedDeg"
    $wristErrJump = Get-NumberDelta $row $prev "mappedWristErrCm"

    $reasons = New-Object System.Collections.Generic.List[string]
    foreach ($check in @(
        @("posedElbowJumpCm", $posedElbowDelta, $PositionJumpThresholdCm),
        @("solveElbowJumpCm", $solveElbowDelta, $PositionJumpThresholdCm),
        @("posedHandJumpCm", $posedHandDelta, $PositionJumpThresholdCm),
        @("finalWristJumpCm", $finalWristDelta, $PositionJumpThresholdCm),
        @("mappedWristJumpCm", $mappedWristDelta, $PositionJumpThresholdCm),
        @("elbowBendJumpDeg", $elbowBendJump, $AngleJumpThresholdDeg),
        @("swingJumpDeg", $swingJump, $AngleJumpThresholdDeg),
        @("twistJumpDeg", $twistJump, $AngleJumpThresholdDeg)
    )) {
        if ($null -ne $check[1] -and [double]$check[1] -ge [double]$check[2]) {
            $reasons.Add($check[0]) | Out-Null
        }
    }
    if ($null -ne $maxSpeed -and $maxSpeed -ge $SpeedThresholdCmSec) {
        $reasons.Add("speedCmSec") | Out-Null
    }
    foreach ($flagName in @("questTracked", "positionApplied", "targetMapped", "handApplied", "handLocal", "targetReachClamped", "constrainedArmSolve", "armIKEntered", "broken")) {
        $curFlag = Get-PropertyValue $row $flagName
        $prevFlag = Get-PropertyValue $prev $flagName
        if ($null -ne $curFlag -and $null -ne $prevFlag -and [int]$curFlag -ne [int]$prevFlag) {
            $reasons.Add("${flagName}Toggle") | Out-Null
        }
    }
    if (([string](Get-PropertyValue $row "reasons")) -match "swingClamp") {
        $reasons.Add("swingClamp") | Out-Null
    }

    $deltaRow = [pscustomobject]@{
        Line = $row.Line
        PrevLine = $prev.Line
        Time = $row.Time
        Frame = $row.Frame
        side = $sideKey
        dtSec = if ($null -ne $dtSec) { [math]::Round($dtSec, 4) } else { $null }
        reasons = ([string[]]$reasons -join "|")
        maxPositionDeltaCm = [math]::Round([double]$maxPositionDelta, 2)
        maxPositionSpeedCmSec = if ($null -ne $maxSpeed) { [math]::Round([double]$maxSpeed, 1) } else { $null }
        posedElbowJumpCm = if ($null -ne $posedElbowDelta) { [math]::Round([double]$posedElbowDelta, 2) } else { $null }
        solveElbowJumpCm = if ($null -ne $solveElbowDelta) { [math]::Round([double]$solveElbowDelta, 2) } else { $null }
        posedHandJumpCm = if ($null -ne $posedHandDelta) { [math]::Round([double]$posedHandDelta, 2) } else { $null }
        finalWristJumpCm = if ($null -ne $finalWristDelta) { [math]::Round([double]$finalWristDelta, 2) } else { $null }
        mappedWristJumpCm = if ($null -ne $mappedWristDelta) { [math]::Round([double]$mappedWristDelta, 2) } else { $null }
        elbowBendJumpDeg = if ($null -ne $elbowBendJump) { [math]::Round([double]$elbowBendJump, 2) } else { $null }
        targetReachJumpCm = if ($null -ne $targetReachJump) { [math]::Round([double]$targetReachJump, 2) } else { $null }
        posedReachJumpCm = if ($null -ne $posedReachJump) { [math]::Round([double]$posedReachJump, 2) } else { $null }
        swingJumpDeg = if ($null -ne $swingJump) { [math]::Round([double]$swingJump, 2) } else { $null }
        twistJumpDeg = if ($null -ne $twistJump) { [math]::Round([double]$twistJump, 2) } else { $null }
        mappedWristErrJumpCm = if ($null -ne $wristErrJump) { [math]::Round([double]$wristErrJump, 2) } else { $null }
        currentQuestTracked = Get-PropertyValue $row "questTracked"
        currentPositionApplied = Get-PropertyValue $row "positionApplied"
        currentTargetMapped = Get-PropertyValue $row "targetMapped"
        currentHandApplied = Get-PropertyValue $row "handApplied"
        currentHandLocal = Get-PropertyValue $row "handLocal"
        currentTargetReachClamped = Get-PropertyValue $row "targetReachClamped"
        currentConstrainedArmSolve = Get-PropertyValue $row "constrainedArmSolve"
        currentArmIKEntered = Get-PropertyValue $row "armIKEntered"
        currentBroken = Get-PropertyValue $row "broken"
        currentArmReasons = Get-PropertyValue $row "reasons"
        currentElbowBendDeg = Get-PropertyValue $row "elbowBendDeg"
        currentTargetReachCm = Get-PropertyValue $row "targetReachCm"
        currentPosedReachCm = Get-PropertyValue $row "posedReachCm"
        currentSwingAppliedDeg = Get-PropertyValue $row "swingAppliedDeg"
        currentTwistLimitedDeg = Get-PropertyValue $row "twistLimitedDeg"
        currentMappedWristErrCm = Get-PropertyValue $row "mappedWristErrCm"
        currentWristTargetErrCm = Get-PropertyValue $row "wristTargetErrCm"
    }

    if ($reasons.Count -gt 0) {
        $spikes.Add($deltaRow) | Out-Null
    }

    $previousBySide[$sideKey] = $row
}

$runStamp = Get-Date -Format "yyyyMMdd_HHmmss"
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = "Saved\CodexAgent\ArmTwitchData\wallace_arm_twitch_$runStamp"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$rowsCsv = Join-Path $OutDir "arm_sanity_rows.csv"
$spikesCsv = Join-Path $OutDir "arm_twitch_spikes.csv"
$summaryPath = Join-Path $OutDir "summary.txt"

$orderedRows |
    Select-Object Line,Time,Frame,actor,side,broken,reasons,questArmMode,targetMapped,positionApplied,questTracked,handApplied,handLocal,wristTargetErrCm,mappedWristErrCm,maxWristErrCm,elbowBendDeg,targetReachCm,posedReachCm,targetReachClamped,constrainedArmSolve,armIKEntered,swingAppliedDeg,twistLimitedDeg,posedShoulderX,posedShoulderY,posedShoulderZ,posedElbowX,posedElbowY,posedElbowZ,posedHandX,posedHandY,posedHandZ,finalWristX,finalWristY,finalWristZ,mappedWristX,mappedWristY,mappedWristZ,solveElbowX,solveElbowY,solveElbowZ |
    Export-Csv -LiteralPath $rowsCsv -NoTypeInformation

$spikeRows = @($spikes | Sort-Object @{ Expression = { $_.maxPositionDeltaCm }; Descending = $true }, @{ Expression = { $_.swingJumpDeg }; Descending = $true })
$spikeRows | Export-Csv -LiteralPath $spikesCsv -NoTypeInformation
$brokenRows = @($orderedRows | Where-Object { (Get-PropertyValue -Object $_ -Name "broken") -eq 1 })

$sideGroups = @($orderedRows | Group-Object side | Sort-Object Name)
$spikeGroups = @($spikeRows | Group-Object side | Sort-Object Name)
$reasonGroups = @($spikeRows | Where-Object { -not [string]::IsNullOrWhiteSpace($_.reasons) } | ForEach-Object {
    foreach ($reason in ([string]$_.reasons -split "\|")) {
        if (-not [string]::IsNullOrWhiteSpace($reason)) {
            [pscustomobject]@{ side = $_.side; reason = $reason }
        }
    }
} | Group-Object side,reason | Sort-Object Count -Descending)

$summaryLines = New-Object System.Collections.Generic.List[string]
$summaryLines.Add("Wallace arm twitch data") | Out-Null
$summaryLines.Add("LogPath: $LogPath") | Out-Null
$summaryLines.Add("Actor: $Actor") | Out-Null
$summaryLines.Add("Side: $Side") | Out-Null
$summaryLines.Add("StartLine: $startLine") | Out-Null
$summaryLines.Add("Rows: $($orderedRows.Count)") | Out-Null
$summaryLines.Add("BrokenRows: $($brokenRows.Count)") | Out-Null
$summaryLines.Add("First: line=$($orderedRows[0].Line) time=$($orderedRows[0].Time)") | Out-Null
$summaryLines.Add("Last: line=$($orderedRows[-1].Line) time=$($orderedRows[-1].Time)") | Out-Null
$summaryLines.Add("Thresholds: positionJumpCm>=$PositionJumpThresholdCm angleJumpDeg>=$AngleJumpThresholdDeg speedCmSec>=$SpeedThresholdCmSec") | Out-Null
$summaryLines.Add("") | Out-Null
$summaryLines.Add("Rows by side:") | Out-Null
foreach ($group in $sideGroups) {
    $sideRows = @($group.Group)
    $summaryLines.Add(("  {0}: rows={1} broken={2} questTracked={3} positionApplied={4} targetClamped={5}" -f `
        $group.Name,
        $sideRows.Count,
        (Count-Flag $sideRows "broken"),
        (Count-Flag $sideRows "questTracked"),
        (Count-Flag $sideRows "positionApplied"),
        (Count-Flag $sideRows "targetReachClamped"))) | Out-Null
}
$summaryLines.Add("") | Out-Null
$summaryLines.Add("Spike rows by side:") | Out-Null
foreach ($group in $spikeGroups) {
    $summaryLines.Add(("  {0}: spikes={1}" -f $group.Name, $group.Count)) | Out-Null
}
if ($spikeGroups.Count -eq 0) {
    $summaryLines.Add("  none") | Out-Null
}
$summaryLines.Add("") | Out-Null
$summaryLines.Add("Spike reasons:") | Out-Null
foreach ($group in $reasonGroups) {
    $summaryLines.Add(("  {0}: {1}" -f $group.Name, $group.Count)) | Out-Null
}
if ($reasonGroups.Count -eq 0) {
    $summaryLines.Add("  none") | Out-Null
}
$summaryLines.Add("") | Out-Null
$summaryLines.Add("Ranges:") | Out-Null
foreach ($field in @("mappedWristErrCm", "wristTargetErrCm", "elbowBendDeg", "targetReachCm", "posedReachCm", "swingAppliedDeg", "twistLimitedDeg")) {
    $stats = Get-Stats $orderedRows $field
    if ($null -ne $stats) {
        $summaryLines.Add(("  {0}: count={1} min={2} avg={3} max={4}" -f $stats.Field, $stats.Count, $stats.Min, $stats.Avg, $stats.Max)) | Out-Null
    }
}
$summaryLines.Add("") | Out-Null
$summaryLines.Add("Top spikes:") | Out-Null
foreach ($spike in @($spikeRows | Select-Object -First ([math]::Max(0, $TopRows)))) {
    $summaryLines.Add(("  line={0} time={1} side={2} reasons={3} maxDeltaCm={4} speedCmSec={5} elbowJumpCm={6} handJumpCm={7} wristJumpCm={8} swingJumpDeg={9} twistJumpDeg={10} tracked={11} posApplied={12} armReasons={13}" -f `
        $spike.Line,
        $spike.Time,
        $spike.side,
        $spike.reasons,
        $spike.maxPositionDeltaCm,
        $spike.maxPositionSpeedCmSec,
        $spike.posedElbowJumpCm,
        $spike.posedHandJumpCm,
        $spike.finalWristJumpCm,
        $spike.swingJumpDeg,
        $spike.twistJumpDeg,
        $spike.currentQuestTracked,
        $spike.currentPositionApplied,
        $spike.currentArmReasons)) | Out-Null
}

$summaryLines | Set-Content -LiteralPath $summaryPath

Write-Host "Wallace arm twitch analysis: $LogPath"
Write-Host "Rows: $($orderedRows.Count) actor=$Actor side=$Side startLine=$startLine"
Write-Host "Spikes: $($spikeRows.Count)"
Write-Host "BrokenRows: $($brokenRows.Count)"
Write-Host "Rows CSV: $(ConvertTo-WorkspaceRelativePath $rowsCsv)"
Write-Host "Spikes CSV: $(ConvertTo-WorkspaceRelativePath $spikesCsv)"
Write-Host "Summary: $(ConvertTo-WorkspaceRelativePath $summaryPath)"
Write-Host ""
Get-Content -LiteralPath $summaryPath | Select-Object -First 80

if ($FailOnBroken -and $brokenRows.Count -gt 0) {
    Write-Host "FAIL: found $($brokenRows.Count) broken arm sanity rows."
    exit 1
}
if ($FailOnSpikes -and $spikeRows.Count -gt 0) {
    Write-Host "FAIL: found $($spikeRows.Count) arm twitch spike rows."
    exit 1
}

exit 0

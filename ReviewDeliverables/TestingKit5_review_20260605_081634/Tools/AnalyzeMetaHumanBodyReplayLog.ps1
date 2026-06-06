param(
    [string]$LogPath = "Saved\Logs\TestingKit3.log",
    [string]$Actor = "MP_LiveMetaHumanEmory",
    [string]$OutCsv = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $LogPath)) {
    throw "Log file not found: $LogPath"
}

$headLockPattern = "mp\.BodyFusion\.HeadLock actor=$([regex]::Escape($Actor)).*hmd=\((?<hmd>[-0-9.,]+)\).*solverEye=\((?<solverEye>[-0-9.,]+)\).*posedEye=\((?<posedEye>[-0-9.,]+)\).*posedHead=\((?<posedHead>[-0-9.,]+)\).*posedChest=\((?<posedChest>[-0-9.,]+)\).*posedPelvis=\((?<posedPelvis>[-0-9.,]+)\).*solverChestToPosedChest=(?<chestErr>[-0-9.]+)\).*ownerView\(chestDist=(?<chestDist>[-0-9.]+) chestForward=(?<chestForward>[-0-9.]+) chestUp=(?<chestUp>[-0-9.]+)\).*lean\(hmdPitch=(?<hmdPitch>[-0-9.]+) posedPelvisChest=(?<posedPelvisChest>[-0-9.]+) posedChestHead=(?<posedChestHead>[-0-9.]+) solverPelvisChest=(?<solverPelvisChest>[-0-9.]+) solverChestHead=(?<solverChestHead>[-0-9.]+)\)"
$debugPattern = "mp\.BodyFusion\.Debug actor=$([regex]::Escape($Actor)).*solveEye=\((?<solveEye>[-0-9.,]+)\) solveChest=\((?<solveChest>[-0-9.,]+)\) solvePelvis=\((?<solvePelvis>[-0-9.,]+)\)"
$wristPattern = "mp\.QuestWristSnapshot: actor=$([regex]::Escape($Actor)) left\(has=(?<leftHas>\d+) tracked=(?<leftTracked>\d+) wrist=V\((?<leftWrist>[^)]*)\)\) right\(has=(?<rightHas>\d+) tracked=(?<rightTracked>\d+) wrist=V\((?<rightWrist>[^)]*)\)\) hmdPose=(?<hmdPose>\d+) hmdWorld=V\((?<hmdWorld>[^)]*)\)"
$chainPattern = "mp\.MetaHumanFullArmChain: profile=(?<profile>\S+) actor=$([regex]::Escape($Actor)) side=(?<side>[LR]).*chainActive=(?<chainActive>\d+).*targetReachCm=(?<reach>[-0-9.]+).*handWorld=\((?<handWorld>[-0-9.,]+)\).*chainAge=(?<chainAge>[-0-9.]+)"

$rows = New-Object System.Collections.Generic.List[object]
$summary = [ordered]@{
    actor = $Actor
    logPath = (Resolve-Path -LiteralPath $LogPath).Path
    headLockRows = 0
    wristRows = 0
    trackedWristRows = 0
    activeFullArmRows = 0
    maxChestSolveToPoseCm = 0.0
    avgChestSolveToPoseCm = 0.0
    maxLeanErrorDeg = 0.0
    avgLeanErrorDeg = 0.0
}

$lastDebug = $null
$lineNumber = 0
Get-Content -LiteralPath $LogPath | ForEach-Object {
    $lineNumber++
    $line = $_

    if ($line -match $debugPattern) {
        $lastDebug = [pscustomobject]@{
            line = $lineNumber
            solveEye = $Matches.solveEye
            solveChest = $Matches.solveChest
            solvePelvis = $Matches.solvePelvis
        }
        return
    }

    if ($line -match $wristPattern) {
        $summary.wristRows++
        if ([int]$Matches.leftTracked -ne 0 -or [int]$Matches.rightTracked -ne 0) {
            $summary.trackedWristRows++
        }
        return
    }

    if ($line -match $chainPattern) {
        if ([int]$Matches.chainActive -ne 0) {
            $summary.activeFullArmRows++
        }
        return
    }

    if ($line -notmatch $headLockPattern) {
        return
    }

    $chestErr = [double]$Matches.chestErr
    $posedPelvisChest = [double]$Matches.posedPelvisChest
    $solverPelvisChest = [double]$Matches.solverPelvisChest
    $leanError = [math]::Abs($posedPelvisChest - $solverPelvisChest)
    $record = [pscustomobject]@{
        line = $lineNumber
        hmd = $Matches.hmd
        solveEye = if ($lastDebug) { $lastDebug.solveEye } else { "" }
        solveChest = if ($lastDebug) { $lastDebug.solveChest } else { "" }
        solvePelvis = if ($lastDebug) { $lastDebug.solvePelvis } else { "" }
        posedEye = $Matches.posedEye
        posedHead = $Matches.posedHead
        posedChest = $Matches.posedChest
        posedPelvis = $Matches.posedPelvis
        chestSolveToPoseCm = $chestErr
        chestDistCm = [double]$Matches.chestDist
        chestForwardCm = [double]$Matches.chestForward
        chestUpCm = [double]$Matches.chestUp
        hmdPitchDeg = [double]$Matches.hmdPitch
        posedPelvisChestDeg = $posedPelvisChest
        posedChestHeadDeg = [double]$Matches.posedChestHead
        solverPelvisChestDeg = $solverPelvisChest
        solverChestHeadDeg = [double]$Matches.solverChestHead
        pelvisChestLeanErrorDeg = $leanError
    }
    $rows.Add($record) | Out-Null
}

$summary.headLockRows = $rows.Count
if ($rows.Count -gt 0) {
    $summary.maxChestSolveToPoseCm = [math]::Round(($rows | Measure-Object chestSolveToPoseCm -Maximum).Maximum, 3)
    $summary.avgChestSolveToPoseCm = [math]::Round(($rows | Measure-Object chestSolveToPoseCm -Average).Average, 3)
    $summary.maxLeanErrorDeg = [math]::Round(($rows | Measure-Object pelvisChestLeanErrorDeg -Maximum).Maximum, 3)
    $summary.avgLeanErrorDeg = [math]::Round(($rows | Measure-Object pelvisChestLeanErrorDeg -Average).Average, 3)
}

if (-not [string]::IsNullOrWhiteSpace($OutCsv)) {
    $outDir = Split-Path -Parent $OutCsv
    if (-not [string]::IsNullOrWhiteSpace($outDir)) {
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    }
    $rows | Export-Csv -LiteralPath $OutCsv -NoTypeInformation
}

[pscustomobject]$summary | ConvertTo-Json -Depth 4

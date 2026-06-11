param(
    [Parameter(Mandatory = $true)]
    [string]$CsvPath,

    [double]$ExpectedChestToPelvisCm = 58.0,
    [double]$MaxAllowedProjectedChestErrorCm = 1.0,
    [double]$MinBaselineAvgChestErrorCm = 4.0,
    [double]$MinBaselineAvgLeanErrorDeg = 6.0,
    [string]$OutJson = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $CsvPath)) {
    throw "Replay CSV not found: $CsvPath"
}

$rows = @(Import-Csv -LiteralPath $CsvPath)
if ($rows.Count -eq 0) {
    throw "Replay CSV contains no rows: $CsvPath"
}

$requiredColumns = @(
    "line",
    "chestSolveToPoseCm",
    "pelvisChestLeanErrorDeg"
)

foreach ($column in $requiredColumns) {
    if (-not ($rows[0].PSObject.Properties.Name -contains $column)) {
        throw "Replay CSV is missing required column '$column': $CsvPath"
    }
}

$culture = [System.Globalization.CultureInfo]::InvariantCulture

function Convert-ReplayDouble {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Value,

        [Parameter(Mandatory = $true)]
        [string]$ColumnName
    )

    $text = [string]$Value
    $parsed = 0.0
    if (-not [double]::TryParse(
        $text,
        [System.Globalization.NumberStyles]::Float,
        $culture,
        [ref]$parsed)) {
        throw "Could not parse '$ColumnName' value '$text' as a number."
    }

    return $parsed
}

function Measure-ReplayValues {
    param(
        [Parameter(Mandatory = $true)]
        [double[]]$Values
    )

    if ($Values.Count -eq 0) {
        return [pscustomobject]@{
            min = 0.0
            max = 0.0
            avg = 0.0
        }
    }

    $measure = $Values | Measure-Object -Minimum -Maximum -Average
    return [pscustomobject]@{
        min = [math]::Round($measure.Minimum, 3)
        max = [math]::Round($measure.Maximum, 3)
        avg = [math]::Round($measure.Average, 3)
    }
}

if ($ExpectedChestToPelvisCm -le 0.0) {
    throw "ExpectedChestToPelvisCm must be positive."
}

$maxChestFollowDeltaCm = [math]::Min(
    [math]::Max($ExpectedChestToPelvisCm * 0.45, 14.0),
    32.0)

$baselineChestErrors = New-Object System.Collections.Generic.List[double]
$baselineLeanErrors = New-Object System.Collections.Generic.List[double]
$projectedChestErrors = New-Object System.Collections.Generic.List[double]
$rowsWithinClamp = 0
$rowsExceedingClamp = 0
$worstRows = @()

foreach ($row in $rows) {
    $chestError = Convert-ReplayDouble -Value $row.chestSolveToPoseCm -ColumnName "chestSolveToPoseCm"
    $leanError = Convert-ReplayDouble -Value $row.pelvisChestLeanErrorDeg -ColumnName "pelvisChestLeanErrorDeg"
    $projectedError = [math]::Max(0.0, $chestError - $maxChestFollowDeltaCm)

    $baselineChestErrors.Add($chestError) | Out-Null
    $baselineLeanErrors.Add($leanError) | Out-Null
    $projectedChestErrors.Add($projectedError) | Out-Null

    if ($projectedError -le $MaxAllowedProjectedChestErrorCm) {
        $rowsWithinClamp++
    } else {
        $rowsExceedingClamp++
        if ($worstRows.Count -lt 10) {
            $worstRows += [pscustomobject]@{
                line = $row.line
                chestSolveToPoseCm = [math]::Round($chestError, 3)
                projectedPostFixChestErrorCm = [math]::Round($projectedError, 3)
            }
        }
    }
}

$baselineChest = Measure-ReplayValues -Values $baselineChestErrors.ToArray()
$baselineLean = Measure-ReplayValues -Values $baselineLeanErrors.ToArray()
$projectedChest = Measure-ReplayValues -Values $projectedChestErrors.ToArray()

$baselineCapturedFailure = [bool](
    $baselineChest.avg -ge $MinBaselineAvgChestErrorCm -and
    $baselineLean.avg -ge $MinBaselineAvgLeanErrorDeg)

$projectedPass = [bool](
    $projectedChest.max -le $MaxAllowedProjectedChestErrorCm -and
    $rowsExceedingClamp -eq 0)

$result = [ordered]@{
    replayCsv = (Resolve-Path -LiteralPath $CsvPath).Path
    rowsTested = [int]$rows.Count
    expectedChestToPelvisCm = [math]::Round($ExpectedChestToPelvisCm, 3)
    maxChestFollowDeltaCm = [math]::Round($maxChestFollowDeltaCm, 3)
    baseline = [ordered]@{
        avgChestSolveToPoseCm = $baselineChest.avg
        maxChestSolveToPoseCm = $baselineChest.max
        avgPelvisChestLeanErrorDeg = $baselineLean.avg
        maxPelvisChestLeanErrorDeg = $baselineLean.max
        capturedBodyLockFailure = $baselineCapturedFailure
        minAvgChestErrorRequiredCm = $MinBaselineAvgChestErrorCm
        minAvgLeanErrorRequiredDeg = $MinBaselineAvgLeanErrorDeg
    }
    projectedAfterCurrentSpineTranslationPatch = [ordered]@{
        avgChestSolveToPoseCm = $projectedChest.avg
        maxChestSolveToPoseCm = $projectedChest.max
        rowsWithinClamp = $rowsWithinClamp
        rowsExceedingClamp = $rowsExceedingClamp
        maxAllowedProjectedChestErrorCm = $MaxAllowedProjectedChestErrorCm
        pass = $projectedPass
        worstRows = @($worstRows)
    }
    pass = [bool]($baselineCapturedFailure -and $projectedPass)
}

$json = [pscustomobject]$result | ConvertTo-Json -Depth 6

if (-not [string]::IsNullOrWhiteSpace($OutJson)) {
    $outDir = Split-Path -Parent $OutJson
    if (-not [string]::IsNullOrWhiteSpace($outDir)) {
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    }
    $json | Set-Content -LiteralPath $OutJson
}

$json

if (-not $result.pass) {
    exit 1
}

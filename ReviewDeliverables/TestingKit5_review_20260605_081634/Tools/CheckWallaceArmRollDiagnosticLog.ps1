param(
    [string]$LogPath = ".\Saved\Logs\TestingKit3.log",
    [int]$SinceLine = 0,
    [switch]$RequireActiveUpperArmRows
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $LogPath)) {
    throw "Log file not found: $LogPath"
}

function Get-Fields {
    param([string]$Line)

    $fields = @{}
    foreach ($match in [regex]::Matches($Line, '([A-Za-z0-9_]+)=(".*?"|[^ ]+)')) {
        $fields[$match.Groups[1].Value] = $match.Groups[2].Value.Trim('"')
    }
    return $fields
}

function Get-FloatField {
    param(
        [hashtable]$Fields,
        [string]$Name,
        [double]$Default = 0.0
    )

    if (-not $Fields.ContainsKey($Name)) {
        return $Default
    }

    $value = 0.0
    if ([double]::TryParse($Fields[$Name], [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$value)) {
        return $value
    }
    return $Default
}

$profileRows = Select-String -Path $LogPath -Pattern "Auto Quest profile applied:" |
    Where-Object { $_.LineNumber -gt $SinceLine }

if (-not $profileRows) {
    throw "No Auto Quest profile line found after line $SinceLine"
}

$profile = $profileRows | Select-Object -Last 1
$profileFields = Get-Fields $profile.Line

$requiredProfile = @{
    armRollDiagnostic = "1"
    armTwistBones = "1"
    upperArmRollDrive = "1"
}

$failures = @()
foreach ($key in $requiredProfile.Keys) {
    if (-not $profileFields.ContainsKey($key)) {
        $failures += "missing profile field $key"
    } elseif ($profileFields[$key] -ne $requiredProfile[$key]) {
        $failures += "$key=$($profileFields[$key]) expected $($requiredProfile[$key])"
    }
}

$upperArmBlend = Get-FloatField $profileFields "upperArmRollBlend"
if ([Math]::Abs($upperArmBlend - 0.18) -gt 0.01) {
    $failures += "upperArmRollBlend=$upperArmBlend expected 0.18"
}

$upperArmMax = Get-FloatField $profileFields "upperArmRollMax"
if ([Math]::Abs($upperArmMax - 24.0) -gt 0.1) {
    $failures += "upperArmRollMax=$upperArmMax expected 24.0"
}

$rollRows = Select-String -Path $LogPath -Pattern "mp\.QuestWristRollCompact: actor=MP_LiveMetaHumanWallace" |
    Where-Object { $_.LineNumber -gt $profile.LineNumber }

$rows = @()
foreach ($row in $rollRows) {
    $fields = Get-Fields $row.Line
    $rows += [pscustomobject]@{
        Line = $row.LineNumber
        Side = $fields["side"]
        Applied = [int](Get-FloatField $fields "applied")
        Tracked = [int](Get-FloatField $fields "tracked")
        UpperArmHelperScale = Get-FloatField $fields "upperArmHelperScale"
        UpperArmTargetDeg = Get-FloatField $fields "upperArmTargetDeg"
        UpperArmAppliedDeg = Get-FloatField $fields "upperArmAppliedDeg"
        UpperArmStepDeg = Get-FloatField $fields "upperArmStepDeg"
        UpperArmMaxStepDeg = Get-FloatField $fields "upperArmMaxStepDeg"
        UpperArmRateClamp = [int](Get-FloatField $fields "upperArmRateClamp")
        UpperArmTwist01Deg = Get-FloatField $fields "upperArmTwist01Deg"
        UpperArmTwist02Deg = Get-FloatField $fields "upperArmTwist02Deg"
    }
}

$activeRows = $rows | Where-Object {
    $_.Applied -eq 1 -and
    $_.Tracked -eq 1 -and
    $_.UpperArmHelperScale -gt 0.0 -and
    (
        [Math]::Abs($_.UpperArmAppliedDeg) -gt 0.05 -or
        [Math]::Abs($_.UpperArmTwist01Deg) -gt 0.05 -or
        [Math]::Abs($_.UpperArmTwist02Deg) -gt 0.05
    )
}

if ($RequireActiveUpperArmRows -and -not $activeRows) {
    $failures += "no active Wallace upper-arm roll rows after profile line $($profile.LineNumber)"
}

$maxApplied = 0.0
$maxStep = 0.0
$maxAllowedStep = 0.0
$rateClampRows = 0
if ($rows) {
    $maxApplied = ($rows | ForEach-Object { [Math]::Abs($_.UpperArmAppliedDeg) } | Measure-Object -Maximum).Maximum
    $maxStep = ($rows | ForEach-Object { [Math]::Abs($_.UpperArmStepDeg) } | Measure-Object -Maximum).Maximum
    $maxAllowedStep = ($rows | ForEach-Object { $_.UpperArmMaxStepDeg } | Measure-Object -Maximum).Maximum
    $rateClampRows = @($rows | Where-Object { $_.UpperArmRateClamp -ne 0 }).Count
}

Write-Host "Wallace arm-roll diagnostic log check"
Write-Host "LogPath: $LogPath"
Write-Host "SinceLine: $SinceLine"
Write-Host "ProfileLine: $($profile.LineNumber)"
Write-Host "Profile: armRollDiagnostic=$($profileFields['armRollDiagnostic']) armTwistBones=$($profileFields['armTwistBones']) upperArmRollDrive=$($profileFields['upperArmRollDrive']) upperArmRollBlend=$upperArmBlend upperArmRollMax=$upperArmMax"
Write-Host "WallaceRollRows: $($rows.Count)"
Write-Host "ActiveUpperArmRows: $(@($activeRows).Count)"
Write-Host ("UpperArmRanges: maxAppliedDeg={0:N1} maxStepDeg={1:N1} maxAllowedStepDeg={2:N1} rateClampRows={3}" -f $maxApplied, $maxStep, $maxAllowedStep, $rateClampRows)

if ($failures.Count -gt 0) {
    Write-Host "FAIL:"
    foreach ($failure in $failures) {
        Write-Host "  $failure"
    }
    exit 1
}

Write-Host "PASS: Auto Quest arm-roll diagnostic was armed"
if ($RequireActiveUpperArmRows) {
    Write-Host "PASS: Wallace upper-arm roll rows were active"
}

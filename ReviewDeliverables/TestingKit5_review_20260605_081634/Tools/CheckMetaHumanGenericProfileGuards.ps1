param(
    [string]$ProjectRoot = "."
)

$ErrorActionPreference = "Stop"

function Read-Text {
    param([string]$Path)

    $resolved = Join-Path $ProjectRoot $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Required file not found: $resolved"
    }
    return Get-Content -LiteralPath $resolved -Raw
}

function Add-Failure {
    param(
        [System.Collections.Generic.List[string]]$Failures,
        [string]$Message
    )
    $Failures.Add($Message) | Out-Null
}

$failures = New-Object System.Collections.Generic.List[string]

$profileSource = Read-Text "Source\MediaPipeDriver\MediaPipeMetaHumanProfile.cpp"
$runtimeCVars = Read-Text "Source\MediaPipeDriver\MediaPipeRuntimeCVars.cpp"
$prepareTool = Read-Text "Tools\PrepareMetaHumanVrPreviewProfile.ps1"
$captureTool = Read-Text "Tools\CaptureMetaHumanQuestVrEvidence.ps1"
$wallaceCaptureWrapper = Read-Text "Tools\CaptureWallaceQuestVrEvidence.ps1"

if ($profileSource -match "CVarWallace") {
    Add-Failure $failures "MediaPipeMetaHumanProfile.cpp still reads Wallace-only CVars in active profile resolution."
}

if ($runtimeCVars -notmatch "The active resolver ignores it; use mp\.MetaHumanArmSource") {
    Add-Failure $failures "mp.WallaceArmSource help text does not mark the alias as inactive."
}
if ($runtimeCVars -notmatch "The active resolver ignores it; use mp\.MetaHumanFullArmChainTrace") {
    Add-Failure $failures "mp.WallaceFullArmChainTrace help text does not mark the alias as inactive."
}
if ($runtimeCVars -notmatch "The active resolver ignores it; use mp\.MetaHumanFullArmChainTraceLogIntervalSeconds") {
    Add-Failure $failures "mp.WallaceFullArmChainTraceLogIntervalSeconds help text does not mark the alias as inactive."
}
if ($runtimeCVars -notmatch "The active resolver ignores it; use mp\.MetaHumanFullArmChainMaxAgeSeconds") {
    Add-Failure $failures "mp.WallaceFullArmChainMaxAgeSeconds help text does not mark the alias as inactive."
}

if ($prepareTool -notmatch 'mp\.MetaHumanActiveProfile \$ProfileId') {
    Add-Failure $failures "PrepareMetaHumanVrPreviewProfile.ps1 must expose mp.MetaHumanActiveProfile as the normal user-facing switch."
}
if ($prepareTool -notmatch 'Safe command to run before manually pressing VR Preview') {
    Add-Failure $failures "PrepareMetaHumanVrPreviewProfile.ps1 must label the normal workflow as a single safe command."
}
if ($prepareTool -match 'mp\.MetaHumanArmSource 1') {
    Add-Failure $failures "PrepareMetaHumanVrPreviewProfile.ps1 still forces mp.MetaHumanArmSource 1."
}
if ($prepareTool -match 'mp\.AutoQuestAvatar 1|mp\.AutoQuestWebcamHands 1') {
    Add-Failure $failures "PrepareMetaHumanVrPreviewProfile.ps1 should not expose Auto Quest setup CVars for normal profile switching."
}

foreach ($legacyCommand in @(
    'mp.WallaceArmSource ',
    'mp.WallaceFullArmChainTrace ',
    'mp.WallaceFullArmChainTraceLogIntervalSeconds ',
    'mp.WallaceFullArmChainMaxAgeSeconds '
)) {
    if ($captureTool.Contains($legacyCommand)) {
        Add-Failure $failures "CaptureMetaHumanQuestVrEvidence.ps1 still applies legacy command: $legacyCommand"
    }
}

if ($captureTool -match 'mp\.MetaHumanArmSource 1') {
    Add-Failure $failures "CaptureMetaHumanQuestVrEvidence.ps1 still forces mp.MetaHumanArmSource 1."
}
if ($captureTool -notmatch 'mp\.MetaHumanArmSource -1') {
    Add-Failure $failures "CaptureMetaHumanQuestVrEvidence.ps1 must use profile-driven mp.MetaHumanArmSource -1."
}

if ($wallaceCaptureWrapper -notmatch "CaptureMetaHumanQuestVrEvidence\.ps1") {
    Add-Failure $failures "CaptureWallaceQuestVrEvidence.ps1 is not archived as a wrapper to the generic capture tool."
}
if ($wallaceCaptureWrapper -match 'mp\.WallaceArmSource|mp\.WallaceFullArmChain') {
    Add-Failure $failures "CaptureWallaceQuestVrEvidence.ps1 wrapper still applies Wallace-only CVars."
}

if ($failures.Count -gt 0) {
    Write-Error ("MetaHuman generic profile guard failed:`n - " + ($failures -join "`n - "))
    exit 1
}

Write-Host "MetaHuman generic profile guard passed"
Write-Host "User-facing switch: mp.MetaHumanActiveProfile <Profile>"
Write-Host "Internal evidence path remains profile-driven: mp.MetaHumanArmSource -1, mp.MetaHumanFullArmChain*"
Write-Host "Archived aliases stay registered only for old logs/scripts: mp.WallaceArmSource, mp.WallaceFullArmChain*"

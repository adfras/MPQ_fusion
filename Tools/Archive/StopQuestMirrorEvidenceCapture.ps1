param(
    [string]$OutputRoot = "Saved/QuestScreenshots"
)

$ErrorActionPreference = "Stop"

$outputRootFull = [System.IO.Path]::GetFullPath($OutputRoot)
$activePath = Join-Path $outputRootFull "active_quest_mirror_capture.json"

if (-not (Test-Path -LiteralPath $activePath)) {
    Write-Host "No active Quest mirror capture record found at $activePath"
    exit 0
}

$active = Get-Content -LiteralPath $activePath -Raw | ConvertFrom-Json
if ([string]::IsNullOrWhiteSpace($active.stopFile)) {
    throw "Active capture record did not contain a stopFile: $activePath"
}

$stopFile = [System.IO.Path]::GetFullPath([string]$active.stopFile)
$stopDir = Split-Path -Parent $stopFile
if ($stopDir -and -not (Test-Path -LiteralPath $stopDir)) {
    New-Item -ItemType Directory -Force -Path $stopDir | Out-Null
}

"stop requested $(Get-Date -Format o)" | Set-Content -LiteralPath $stopFile
Write-Host "Stop requested for Quest mirror evidence capture."
Write-Host "Run: $($active.runName)"
Write-Host "Stop file: $stopFile"

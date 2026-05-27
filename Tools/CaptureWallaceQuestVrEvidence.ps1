param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RemainingArgs
)

$ErrorActionPreference = "Stop"

Write-Warning "CaptureWallaceQuestVrEvidence.ps1 is archived as a Wallace-named compatibility wrapper. Use CaptureMetaHumanQuestVrEvidence.ps1 -Profile Wallace for current testing."

& (Join-Path $PSScriptRoot "CaptureMetaHumanQuestVrEvidence.ps1") @RemainingArgs

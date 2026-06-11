<#
.SYNOPSIS
Retention cleanup for TestingKit5 transient capture outputs. DRY-RUN by default.

.DESCRIPTION
Produces a deletion manifest (path, size, age, family, reason) for:
  - Saved/CodexAgent/Diagnostics  : keep canonical dataset + doc-cited evidence (KeepGlobs),
                                    keep the newest $FamilyKeep timestamped runs per capture
                                    family, delete older runs.
  - Saved/CodexAgent/Screenshots  : keep evidence globs + anything newer than $RecentDays.
  - Saved/CodexAgent/BuildLogs    : keep newest $LogKeep build log pairs.
  - Saved/Logs                    : keep newest $LogKeep editor logs.
  - Saved/Crashes                 : delete crash dumps older than $CrashKeepDays.
Report-only (NEVER deleted by this script; require explicit user action):
  - Saved/QuestScreenshots, Saved/Videos, _MCPBench, ReviewDeliverables.

The canonical replay dataset (*20260609_170656*) is hard-coded as never-delete on top of all
other rules. Run without -Apply to review the manifest; run with -Apply to delete.

.EXAMPLE
  powershell -File Tools/CleanCodexAgentOutputs.ps1                # dry run, prints manifest
  powershell -File Tools/CleanCodexAgentOutputs.ps1 -Apply        # actually delete
#>
[CmdletBinding()]
param(
    [switch]$Apply,
    [int]$FamilyKeep = 2,
    [int]$LogKeep = 5,
    [int]$CrashKeepDays = 14,
    [int]$RecentDays = 2
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot

# Never deleted, regardless of any other rule.
$NeverDeleteGlobs = @(
    "*20260609_170656*"             # canonical avatar-locked sync calibration recording
)

# Evidence cited by active docs (Docs/*.md as of 2026-06-11) plus standing gate outputs.
$DiagnosticsKeepGlobs = @(
    "tracking_fusion_dataset_replay_avatar_output_tk3direct_final_20260609_210445*",
    "mpq_shadow_latency_stage2aPreArmSolveProof_20260607_203128*",
    "final_quality_plots_20260610",
    "kellan_replay_quality_plots_20260610",
    "kellan_live_pie_bone_measure_baseline_20260610_*",
    "kellan_live_pie_bone_measure_after_grounding_20260610_*",
    "live_pie_bone_measure_*",      # PIE gate evidence, small JSONs
    "bodyfusion_region_quality_*"   # region-quality evidence; pruned by family retention later if desired
)

$ScreenshotsKeepGlobs = @(
    "showcase_*", "final_all_avatars_*", "after_grounding_*", "baseline_fix_*"
)

function Matches-AnyGlob([string]$Name, [string[]]$Globs) {
    foreach ($g in $Globs) { if ($Name -like $g) { return $true } }
    return $false
}

function Get-FamilyKey([string]$Name) {
    # Family = the leading capture-type tokens, so label-unique tuning runs of the same capture
    # type group together (mpq_shadow_latency_stage2_<anything>_<ts>.json is ONE family).
    # Doc-cited items must be protected by KeepGlobs, not by family granularity.
    $base = [IO.Path]::GetFileNameWithoutExtension($Name).ToLowerInvariant()
    $tokens = $base -split '_' | Where-Object { $_ -notmatch '^\d+$' }
    $key = ($tokens | Select-Object -First 3) -join '_'
    if (-not $key) { $key = $base }
    return $key
}

function Get-RunStamp([System.IO.FileSystemInfo]$Item) {
    if ($Item.Name -match '(\d{8}_\d{6})') { return $Matches[1] }
    return $Item.LastWriteTime.ToString("yyyyMMdd_HHmmss")
}

function Get-ItemSizeBytes([System.IO.FileSystemInfo]$Item) {
    if ($Item.PSIsContainer) {
        return [int64]((Get-ChildItem $Item.FullName -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum)
    }
    return [int64]$Item.Length
}

$manifest = New-Object System.Collections.Generic.List[object]
function Add-Doomed([System.IO.FileSystemInfo]$Item, [string]$Reason) {
    $script:manifest.Add([PSCustomObject]@{
        Path   = $Item.FullName.Replace($ProjectRoot + [IO.Path]::DirectorySeparatorChar, "")
        MB     = [math]::Round((Get-ItemSizeBytes $Item) / 1MB, 1)
        Age    = [int]((Get-Date) - $Item.LastWriteTime).TotalDays
        Reason = $Reason
        Full   = $Item.FullName
    })
}

# --- Saved/CodexAgent/Diagnostics: keep-globs + per-family retention --------------------
$diag = Join-Path $ProjectRoot "Saved\CodexAgent\Diagnostics"
if (Test-Path $diag) {
    $items = Get-ChildItem $diag   # top-level files AND plot directories as units
    $candidates = @()
    foreach ($it in $items) {
        if (Matches-AnyGlob $it.Name $NeverDeleteGlobs) { continue }
        if (Matches-AnyGlob $it.Name $DiagnosticsKeepGlobs) { continue }
        if ($it.LastWriteTime -gt (Get-Date).AddDays(-$RecentDays)) { continue }
        $candidates += $it
    }
    $byFamily = $candidates | Group-Object { Get-FamilyKey $_.Name }
    foreach ($fam in $byFamily) {
        $runs = $fam.Group | Group-Object { Get-RunStamp $_ } | Sort-Object Name -Descending
        $stale = $runs | Select-Object -Skip $FamilyKeep
        foreach ($run in $stale) {
            foreach ($it in $run.Group) {
                Add-Doomed $it "Diagnostics family '$($fam.Name)': older than newest $FamilyKeep runs"
            }
        }
    }
}

# --- Saved/CodexAgent/Screenshots --------------------------------------------------------
$shots = Join-Path $ProjectRoot "Saved\CodexAgent\Screenshots"
if (Test-Path $shots) {
    foreach ($it in Get-ChildItem $shots -File) {
        if (Matches-AnyGlob $it.Name $NeverDeleteGlobs) { continue }
        if (Matches-AnyGlob $it.Name $ScreenshotsKeepGlobs) { continue }
        if ($it.LastWriteTime -gt (Get-Date).AddDays(-$RecentDays)) { continue }
        Add-Doomed $it "Screenshot not in evidence keep-set and older than $RecentDays days"
    }
}

# --- Saved/CodexAgent/BuildLogs and Saved/Logs: keep newest $LogKeep ----------------------
foreach ($logSpec in @(
    @{ Dir = "Saved\CodexAgent\BuildLogs"; Keep = ($LogKeep * 2) },   # .out/.err pairs
    @{ Dir = "Saved\Logs"; Keep = $LogKeep })) {
    $dir = Join-Path $ProjectRoot $logSpec.Dir
    if (Test-Path $dir) {
        $files = Get-ChildItem $dir -File | Sort-Object LastWriteTime -Descending
        foreach ($it in ($files | Select-Object -Skip $logSpec.Keep)) {
            Add-Doomed $it "$($logSpec.Dir): older than newest $($logSpec.Keep) files"
        }
    }
}

# --- Saved/Crashes ------------------------------------------------------------------------
$crashes = Join-Path $ProjectRoot "Saved\Crashes"
if (Test-Path $crashes) {
    foreach ($it in Get-ChildItem $crashes) {
        if ($it.LastWriteTime -lt (Get-Date).AddDays(-$CrashKeepDays)) {
            Add-Doomed $it "Crash dump older than $CrashKeepDays days"
        }
    }
}

# --- Output -------------------------------------------------------------------------------
$manifest | Sort-Object MB -Descending | Format-Table MB, Age, Reason, Path -AutoSize | Out-String -Width 300 | Write-Host
$totalMB = [math]::Round(($manifest | Measure-Object MB -Sum).Sum, 0)
Write-Host ("{0} items, {1:N0} MB would be deleted." -f $manifest.Count, $totalMB)

Write-Host "`nReport-only (require explicit user decision, never touched by this script):"
foreach ($ro in @("Saved\QuestScreenshots", "Saved\Videos", "_MCPBench", "ReviewDeliverables")) {
    $p = Join-Path $ProjectRoot $ro
    if (Test-Path $p) {
        $sz = (Get-ChildItem $p -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
        Write-Host ("  {0,10:N0} MB  {1}" -f ($sz / 1MB), $ro)
    }
}

if (-not $Apply) {
    Write-Host "`nDRY RUN ONLY. Re-run with -Apply to delete the items above."
    exit 0
}

Write-Host "`nApplying deletions..."
$deleted = 0
foreach ($entry in $manifest) {
    if (Matches-AnyGlob (Split-Path $entry.Full -Leaf) $NeverDeleteGlobs) { continue }  # belt and braces
    Remove-Item $entry.Full -Recurse -Force -ErrorAction Continue
    $deleted++
}
Write-Host ("Deleted {0} items ({1:N0} MB)." -f $deleted, $totalMB)

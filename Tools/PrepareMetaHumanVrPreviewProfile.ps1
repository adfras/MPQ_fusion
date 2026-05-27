param(
    [string]$Profile = "Wallace",
    [string]$BridgeUrl = "http://127.0.0.1:8765",
    [double]$TraceIntervalSeconds = 0.10,
    [double]$MaxAgeSeconds = 0.25,
    [switch]$WithDiagnostics,
    [switch]$NoDiagnostics,
    [switch]$PrintOnly
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Profile)) {
    throw "Profile is required. Examples: Wallace, Emory, Hudson, Kellan, Maria, Payton."
}

$Profile = $Profile.Trim()
if ($Profile -notmatch '^[A-Za-z0-9_.:-]+$') {
    throw "Profile '$Profile' contains unsupported characters. Use the profile id, for example Kellan or Maria."
}

function Format-InvariantNumber {
    param([double]$Value)
    return $Value.ToString("0.###", [System.Globalization.CultureInfo]::InvariantCulture)
}

function Get-MetaHumanVrPreviewCommands {
    param(
        [string]$ProfileId,
        [double]$TraceInterval,
        [double]$MaxAge,
        [bool]$IncludeDiagnostics
    )

    $commands = New-Object System.Collections.Generic.List[string]
    $commands.Add("mp.MetaHumanActiveProfile $ProfileId")

    if ($IncludeDiagnostics) {
        $commands.Add("mp.MetaHumanArmSource -1")
        $commands.Add("mp.MetaHumanFullArmChainTrace 1")
        $commands.Add("mp.MetaHumanFullArmChainTraceLogIntervalSeconds $(Format-InvariantNumber $TraceInterval)")
        $commands.Add("mp.MetaHumanFullArmChainMaxAgeSeconds $(Format-InvariantNumber $MaxAge)")
        $commands.Add("mp.MetaHumanArmSanity 1")
        $commands.Add("mp.MetaHumanArmSanityLogIntervalSeconds $(Format-InvariantNumber $TraceInterval)")
        $commands.Add("mp.AutoQuestMediaPipeStats 1")
        $commands.Add("mp.AutoQuestMediaPipeStatsHud 1")
        $commands.Add("mp.AutoQuestMediaPipeStatsIntervalSeconds 1.0")
    }

    return [string[]]$commands
}

function ConvertTo-PythonSingleQuotedLiteral {
    param([string]$Value)
    return "'" + ($Value -replace "\\", "\\\\" -replace "'", "\\'") + "'"
}

function Invoke-BridgeTool {
    param(
        [string]$Tool,
        [hashtable]$ToolArgs = @{}
    )

    $body = @{
        tool = $Tool
        args = $ToolArgs
    } | ConvertTo-Json -Depth 12

    Invoke-RestMethod -Method Post -Uri "$BridgeUrl/tool" -ContentType "application/json" -Body $body -TimeoutSec 30
}

function Invoke-UnrealPython {
    param([string]$Code)

    $result = Invoke-BridgeTool -Tool "run_unreal_python" -ToolArgs @{ code = $Code }
    if (-not $result.success) {
        throw "run_unreal_python failed: $($result | ConvertTo-Json -Depth 8)"
    }
    [string]$result.payload.output
}

function Invoke-UnrealConsoleCommands {
    param([string[]]$Commands)

    $commandsLiteral = ($Commands | ForEach-Object { ConvertTo-PythonSingleQuotedLiteral $_ }) -join ",`n    "
    $code = @"
import unreal

world = None
try:
    world = unreal.EditorLevelLibrary.get_game_world()
except Exception:
    world = None
if world is None:
    world = unreal.EditorLevelLibrary.get_editor_world()

cmds = [
    $commandsLiteral
]

for cmd in cmds:
    unreal.SystemLibrary.execute_console_command(world, cmd)
    print("CODEX_PROFILE_PREP_CMD " + cmd)

readbacks = [
    "mp.MetaHumanActiveProfile",
]

for name in readbacks:
    value = None
    for func_name in ("get_console_variable_string_value", "get_console_variable_float_value", "get_console_variable_int_value", "get_console_variable_bool_value"):
        try:
            func = getattr(unreal.SystemLibrary, func_name)
            value = func(name)
            break
        except Exception:
            value = None
    if value is None:
        print("CODEX_PROFILE_PREP_CVAR_ERR %s" % name)
    else:
        print("CODEX_PROFILE_PREP_CVAR %s=%s" % (name, value))
"@

    Invoke-UnrealPython -Code $code
}

$commands = Get-MetaHumanVrPreviewCommands `
    -ProfileId $Profile `
    -TraceInterval $TraceIntervalSeconds `
    -MaxAge $MaxAgeSeconds `
    -IncludeDiagnostics ($WithDiagnostics -and -not $NoDiagnostics)

Write-Host "MetaHuman VR Preview profile: $Profile"
Write-Host "Safe command to run before manually pressing VR Preview:"
$commands | ForEach-Object { Write-Host $_ }

if ($PrintOnly) {
    Write-Host ""
    Write-Host "PrintOnly was set. No bridge commands were applied."
    Write-Host "Next: run the command above, then press VR Preview manually."
    exit 0
}

Write-Host ""
Write-Host "Applying profile switch through $BridgeUrl. This does not start or press VR Preview."
$output = Invoke-UnrealConsoleCommands -Commands $commands
if (-not [string]::IsNullOrWhiteSpace($output)) {
    Write-Host $output
}

Write-Host "Next: press VR Preview manually and check the log for active profile '$Profile'."

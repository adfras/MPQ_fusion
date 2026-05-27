param(
    [string]$LogPath = "Saved/Logs/TestingKit3.log",
    [string]$Profile = "Wallace",
    [string]$Actor = "",
    [int]$SinceLine = 0,
    [switch]$AfterLastVrPreview,
    [int]$MinActiveRowsPerSide = 1,
    [double]$MaxActiveChainAgeSeconds = 0.25
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Profile)) {
    throw "Profile is required. Examples: Wallace, Emory, Hudson, Kellan, Maria, Payton."
}

$Profile = $Profile.Trim()

function Convert-ToNumber {
    param([string]$Value)

    $number = 0.0
    if ([double]::TryParse($Value, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$number)) {
        return $number
    }
    return $null
}

function Read-LogLines {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Log file not found: $Path"
    }

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $lines = New-Object System.Collections.Generic.List[string]
    $stream = [System.IO.File]::Open($resolved, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object System.IO.StreamReader($stream)
        try {
            while (($line = $reader.ReadLine()) -ne $null) {
                $lines.Add($line) | Out-Null
            }
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }

    return @($lines)
}

function Get-LatestVrPreviewStartLine {
    param([string[]]$Lines)

    $startLine = 0
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i] -match "Repeating last play command: VR Preview" -or
            $Lines[$i] -match "PlayLevel: Creating play world package" -or
            $Lines[$i] -match "Auto Quest profile applied:") {
            $startLine = $i + 1
        }
    }
    return $startLine
}

function ConvertFrom-MetaHumanLogLine {
    param(
        [string]$Line,
        [int]$LineNumber,
        [string]$Kind
    )

    $record = [ordered]@{
        Line = $LineNumber
        Kind = $Kind
        Text = $Line
    }

    foreach ($match in [regex]::Matches($Line, '([A-Za-z][A-Za-z0-9_]*)=(V\([^)]*\)|"[^"]*"|[^ ]+)')) {
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

    return [pscustomobject]$record
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

function Test-ProfileMatch {
    param(
        [object]$Record,
        [string]$ExpectedProfile
    )

    $value = [string](Get-PropertyValue -Object $Record -Name "profile")
    return [string]::Equals($value, $ExpectedProfile, [System.StringComparison]::OrdinalIgnoreCase)
}

function Test-ActorMatch {
    param(
        [object]$Record,
        [string]$ExpectedActor
    )

    if ([string]::IsNullOrWhiteSpace($ExpectedActor)) {
        return $true
    }

    $value = [string](Get-PropertyValue -Object $Record -Name "actor")
    return [string]::Equals($value, $ExpectedActor, [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-NumberStats {
    param(
        [object[]]$Rows,
        [string]$Field
    )

    $values = @($Rows | ForEach-Object { Get-PropertyValue -Object $_ -Name $Field } | Where-Object { $null -ne $_ })
    if ($values.Count -eq 0) {
        return "n/a"
    }

    $measure = $values | Measure-Object -Minimum -Maximum -Average
    return ("min={0:n2} avg={1:n2} max={2:n2}" -f $measure.Minimum, $measure.Average, $measure.Maximum)
}

$lines = Read-LogLines -Path $LogPath
$resolvedLogPath = (Resolve-Path -LiteralPath $LogPath).Path
$startLine = [math]::Max(0, $SinceLine)
if ($AfterLastVrPreview) {
    $startLine = [math]::Max($startLine, (Get-LatestVrPreviewStartLine -Lines $lines))
}

$profileRows = New-Object System.Collections.Generic.List[object]
$fullChainRows = New-Object System.Collections.Generic.List[object]

for ($i = $startLine; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]
    $lineNumber = $i + 1
    if ($line -match "mp\.MetaHumanProfile: resolved ") {
        $record = ConvertFrom-MetaHumanLogLine -Line $line -LineNumber $lineNumber -Kind "profile"
        if ((Test-ProfileMatch -Record $record -ExpectedProfile $Profile) -and (Test-ActorMatch -Record $record -ExpectedActor $Actor)) {
            $profileRows.Add($record) | Out-Null
        }
    }
    elseif ($line -match "mp\.MetaHumanFullArmChain:") {
        $record = ConvertFrom-MetaHumanLogLine -Line $line -LineNumber $lineNumber -Kind "fullChain"
        if ((Test-ProfileMatch -Record $record -ExpectedProfile $Profile) -and (Test-ActorMatch -Record $record -ExpectedActor $Actor)) {
            $fullChainRows.Add($record) | Out-Null
        }
    }
}

$failures = New-Object System.Collections.Generic.List[string]
$warnings = New-Object System.Collections.Generic.List[string]

$validProfileRows = @($profileRows | Where-Object {
    (Get-PropertyValue -Object $_ -Name "active") -eq 1 -and
    (Get-PropertyValue -Object $_ -Name "valid") -eq 1
})

if ($validProfileRows.Count -eq 0) {
    $failures.Add("No active valid mp.MetaHumanProfile resolution row found for profile '$Profile' after line $startLine.") | Out-Null
}

$invalidActiveProfileRows = @($profileRows | Where-Object {
    (Get-PropertyValue -Object $_ -Name "active") -eq 1 -and
    (Get-PropertyValue -Object $_ -Name "valid") -ne 1
})
if ($invalidActiveProfileRows.Count -gt 0) {
    $failures.Add("Found active profile resolution rows with valid!=1: $($invalidActiveProfileRows.Count)") | Out-Null
}

$activeFullChainRows = @($fullChainRows | Where-Object {
    (Get-PropertyValue -Object $_ -Name "chainActive") -eq 1
})

$goodActiveFullChainRows = @($activeFullChainRows | Where-Object {
    (Get-PropertyValue -Object $_ -Name "armSource") -eq "FullArmChain" -and
    (Get-PropertyValue -Object $_ -Name "shoulderValid") -eq 1 -and
    (Get-PropertyValue -Object $_ -Name "upperArmValid") -eq 1 -and
    (Get-PropertyValue -Object $_ -Name "lowerArmValid") -eq 1 -and
    (Get-PropertyValue -Object $_ -Name "wristOrPalmValid") -eq 1 -and
    (Get-PropertyValue -Object $_ -Name "mediaPipeArmUsed") -eq 0
})

$leftRows = @($goodActiveFullChainRows | Where-Object { (Get-PropertyValue -Object $_ -Name "side") -eq "L" })
$rightRows = @($goodActiveFullChainRows | Where-Object { (Get-PropertyValue -Object $_ -Name "side") -eq "R" })

if ($leftRows.Count -lt $MinActiveRowsPerSide) {
    $failures.Add("Expected at least $MinActiveRowsPerSide good active left full-chain rows; found $($leftRows.Count).") | Out-Null
}
if ($rightRows.Count -lt $MinActiveRowsPerSide) {
    $failures.Add("Expected at least $MinActiveRowsPerSide good active right full-chain rows; found $($rightRows.Count).") | Out-Null
}

$badActiveFullChainRows = @($activeFullChainRows | Where-Object {
    (Get-PropertyValue -Object $_ -Name "armSource") -ne "FullArmChain" -or
    (Get-PropertyValue -Object $_ -Name "shoulderValid") -ne 1 -or
    (Get-PropertyValue -Object $_ -Name "upperArmValid") -ne 1 -or
    (Get-PropertyValue -Object $_ -Name "lowerArmValid") -ne 1 -or
    (Get-PropertyValue -Object $_ -Name "wristOrPalmValid") -ne 1 -or
    (Get-PropertyValue -Object $_ -Name "mediaPipeArmUsed") -ne 0
})
if ($badActiveFullChainRows.Count -gt 0) {
    $failures.Add("Found active full-chain rows that do not prove full-chain authority with valid side joints and mediaPipeArmUsed=0: $($badActiveFullChainRows.Count)") | Out-Null
}

$tooOldRows = @($activeFullChainRows | Where-Object {
    $age = Get-PropertyValue -Object $_ -Name "chainAge"
    $null -ne $age -and [double]$age -gt $MaxActiveChainAgeSeconds
})
if ($tooOldRows.Count -gt 0) {
    $failures.Add("Found active full-chain rows with chainAge greater than $MaxActiveChainAgeSeconds seconds: $($tooOldRows.Count)") | Out-Null
}

if ($fullChainRows.Count -gt 0 -and $activeFullChainRows.Count -eq 0) {
    $warnings.Add("Full-chain rows were present for profile '$Profile', but none were active. Rows after VR shutdown can have chainActive=0 and are not headset proof.") | Out-Null
}

Write-Host "MetaHuman VR Preview log check"
Write-Host "Log: $resolvedLogPath"
Write-Host "Profile: $Profile"
if (-not [string]::IsNullOrWhiteSpace($Actor)) {
    Write-Host "Actor: $Actor"
}
Write-Host "Start line: $startLine"
Write-Host "Profile rows: $($profileRows.Count), active valid: $($validProfileRows.Count)"
Write-Host "Full-chain rows: total=$($fullChainRows.Count), active=$($activeFullChainRows.Count), goodActive=$($goodActiveFullChainRows.Count), L=$($leftRows.Count), R=$($rightRows.Count)"
Write-Host "Target reach: $(Get-NumberStats -Rows $goodActiveFullChainRows -Field 'targetReachCm')"
Write-Host "Elbow bend: $(Get-NumberStats -Rows $goodActiveFullChainRows -Field 'elbowBendDeg')"
Write-Host "Chain age: $(Get-NumberStats -Rows $goodActiveFullChainRows -Field 'chainAge')"

if ($validProfileRows.Count -gt 0) {
    $latestProfile = $validProfileRows[-1]
    Write-Host "Latest valid profile line: $($latestProfile.Line)"
}
if ($goodActiveFullChainRows.Count -gt 0) {
    $firstFullChain = $goodActiveFullChainRows[0]
    $lastFullChain = $goodActiveFullChainRows[-1]
    Write-Host "Good full-chain line range: $($firstFullChain.Line)-$($lastFullChain.Line)"
}

foreach ($warning in $warnings) {
    Write-Warning $warning
}

if ($failures.Count -gt 0) {
    Write-Host "FAIL"
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host "PASS"

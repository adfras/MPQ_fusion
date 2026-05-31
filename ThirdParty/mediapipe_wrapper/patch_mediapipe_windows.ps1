param(
  [Parameter(Mandatory=$true)][string]$MediaPipeDir
)

function Set-TextIfChanged {
  param(
    [Parameter(Mandatory=$true)][string]$Path,
    [Parameter(Mandatory=$true)][string]$Text
  )

  $Current = Get-Content -Raw -LiteralPath $Path
  if ($Current -ne $Text) {
    Set-Content -NoNewline -LiteralPath $Path -Value $Text
    Write-Host "Patched $Path"
  }
}

if (-not (Test-Path -LiteralPath $MediaPipeDir)) {
  throw "MediaPipe directory not found: $MediaPipeDir"
}

$GraphPath = Join-Path $MediaPipeDir "mediapipe/framework/api3/graph.h"
if (Test-Path -LiteralPath $GraphPath) {
  $Text = Get-Content -Raw -LiteralPath $GraphPath
  if ($Text -notmatch 'template <typename NodeT>\s*class SubgraphContext;') {
    $Text = $Text -replace 'class FunctionGraphBuilder;\s*', "class FunctionGraphBuilder;`r`ntemplate <typename NodeT>`r`nclass SubgraphContext;`r`n"
    Set-TextIfChanged -Path $GraphPath -Text $Text
  }
}

$LegacyPath = Join-Path $MediaPipeDir "mediapipe/framework/legacy_calculator_support.cc"
if (Test-Path -LiteralPath $LegacyPath) {
  $Text = Get-Content -Raw -LiteralPath $LegacyPath
  $Text = $Text -replace '(template <>\s*)thread_local CalculatorContext\*', '$1ABSL_CONST_INIT thread_local CalculatorContext*'
  $Text = $Text -replace '(template <>\s*)thread_local CalculatorContract\*', '$1ABSL_CONST_INIT thread_local CalculatorContract*'
  Set-TextIfChanged -Path $LegacyPath -Text $Text
}

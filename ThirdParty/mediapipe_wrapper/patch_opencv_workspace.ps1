param(
  [Parameter(Mandatory=$true)][string]$WorkspacePath,
  [Parameter(Mandatory=$true)][string]$OpenCvBuildDir
)

if (-not (Test-Path -LiteralPath $WorkspacePath)) {
  throw "WORKSPACE file not found: $WorkspacePath"
}
if (-not (Test-Path -LiteralPath $OpenCvBuildDir)) {
  throw "OpenCV build directory not found: $OpenCvBuildDir"
}

$OpenCvPath = (Resolve-Path -LiteralPath $OpenCvBuildDir).Path -replace '\\', '/'
$Text = Get-Content -Raw -LiteralPath $WorkspacePath
$Text = [regex]::Replace(
  $Text,
  '(?m)^\s*path\s*=\s*.*[A-Za-z]:[\\/]+opencv[\\/]+build.*$',
  '    path = "' + $OpenCvPath + '",')
Set-Content -NoNewline -LiteralPath $WorkspacePath -Value $Text

param(
  [Parameter(Mandatory=$true)][string]$Path
)

if (!(Test-Path $Path)) {
  exit 0
}

$c = Get-Content -Raw $Path
$orig = $c

$c = $c -replace 'template <int&\.\.\. DoNotSpecify, class\.\.\. F,', 'template <class... F,'
$c = $c -replace 'template <int&\.\.\. DoNotSpecify, class Visitor>', 'template <class Visitor>'
$c = $c -replace 'template <typename T, int&\.\.\. DoNotSpecify, typename F>', 'template <typename T, typename F>'
$c = $c -replace 'template <typename T, typename U, typename\.\.\. Rest, int&\.\.\. DoNotSpecify,\s*typename F>', 'template <typename T, typename U, typename... Rest, typename F>'

if ($c -ne $orig) {
  Set-Content -Path $Path -Value $c
}

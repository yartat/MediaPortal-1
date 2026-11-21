<#
.SYNOPSIS
  Verifies that the log calls in DirectShowFilters pass every value their format string asks for (SECURITY_AUDIT.md: CD-05).

.DESCRIPTION
  Fails (exit code 1) when a call of Log, LogDebug, LogDebugWithFlags, LogError or LogMsg with a literal format string has
  fewer arguments than the string has conversion specifiers (%d, %s, %*d ...). The missing value is read from the stack:
  garbage in the log file and, with %s, a possible crash.
  More arguments than specifiers are harmless and are not reported. Calls with a format that is not a literal are not checked.
  Not scanned: build output (build, vcpkg_installed, obj, bin), third-party code (thirdparty) and tests.
  The MSVC analyzer (/analyze, C6063/C6064) finds the same defect but needs the format parameter of Log marked with
  _Printf_format_string_; this script works on the text and needs nothing.

.EXAMPLE
  .\Check-LogFormats.ps1 -Path ..\DirectShowFilters
#>
param(
  [Parameter(Mandatory = $true)][string]$Path
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $Path).Path
$skipDirs = @('build', 'vcpkg_installed', 'obj', 'bin', 'thirdparty', 'tests')

$bs = [string][char]92
$call = [regex]('\b(Log|LogDebug|LogDebugWithFlags|LogError|LogMsg)\s*\(\s*L?"((?:[^"' + $bs + $bs + ']|' + $bs + '.)*)"\s*(,|\))')
$spec = [regex]('%%|%[-+ #0]*(' + $bs + '*|' + $bs + 'd+)?(?:' + $bs + '.(' + $bs + '*|' + $bs + 'd+))?(?:hh|h|ll|l|I64|I32|w|z|j|t|L)?[diouxXeEfFgGaAcCsSpn]')
$comments = [regex]'(?s)/\*.*?\*/|//[^\r\n]*'

# Arguments after the format string: commas at depth 0 until the closing parenthesis of the call
function Get-ArgumentCount([string]$text, [int]$start) {
  $depth = 1
  $count = 1
  $inString = $false
  $inChar = $false
  for ($i = $start; $i -lt $text.Length -and $depth -gt 0; $i++) {
    $c = $text[$i]
    if ($inString -or $inChar) {
      if ($c -eq [char]92) { $i++ }
      elseif ($inString -and $c -eq [char]34) { $inString = $false }
      elseif ($inChar -and $c -eq [char]39) { $inChar = $false }
    }
    elseif ($c -eq [char]34) { $inString = $true }
    elseif ($c -eq [char]39) { $inChar = $true }
    elseif ($c -eq '(' -or $c -eq '[' -or $c -eq '{') { $depth++ }
    elseif ($c -eq ')' -or $c -eq ']' -or $c -eq '}') { $depth-- }
    elseif ($c -eq ',' -and $depth -eq 1) { $count++ }
  }
  return $count
}

$problems = New-Object System.Collections.Generic.List[string]

foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File -Include *.cpp, *.c, *.h, *.hpp, *.inl) {
  $rel = $file.FullName.Substring($root.Length + 1)
  if ($rel.Split('\') | Where-Object { $skipDirs -contains $_ }) { continue }

  $raw = [System.IO.File]::ReadAllText($file.FullName)
  # comments become blank lines of the same count, so that line numbers stay right
  $text = $comments.Replace($raw, { param($m) "`n" * ($m.Value.Split("`n").Count - 1) })

  foreach ($m in $call.Matches($text)) {
    $needed = 0
    foreach ($s in $spec.Matches($m.Groups[2].Value)) {
      if ($s.Value -eq '%%') { continue }
      $needed++
      if ($s.Groups[1].Value -eq '*') { $needed++ }
      if ($s.Groups[2].Value -eq '*') { $needed++ }
    }
    $given = 0
    if ($m.Groups[3].Value -eq ',') {
      $given = (Get-ArgumentCount $text ($m.Index + $m.Length))
    }
    if ($needed -gt $given) {
      $line = $text.Substring(0, $m.Index).Split("`n").Count
      $problems.Add("${rel}:${line}: needs $needed, has ${given}: $($m.Groups[2].Value)")
    }
  }
}

if ($problems.Count -gt 0) {
  $problems | ForEach-Object { Write-Host "Log format: $_" }
  Write-Error "$($problems.Count) log calls with missing arguments found (see SECURITY_AUDIT.md, CD-05)"
  exit 1
}

Write-Host 'No log calls with missing arguments in DirectShowFilters.'

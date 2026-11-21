$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$patchScript = Join-Path $repoRoot ".github\workflows\patch-smart-extract-lang.ps1"
$tempName = "7z-smart-lang-" + [guid]::NewGuid().ToString("N")
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) $tempName
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Write-LangFixture {
  param(
    [string] $Dir,
    [bool] $WithInsertionPoint
  )

  New-Item -ItemType Directory -Force -Path $Dir | Out-Null
  $text = "100`nExisting item`n"
  if ($WithInsertionPoint) {
    $text += "2400`n2400 item`n"
  }

  foreach ($name in @("zh-cn.txt", "zh-tw.txt")) {
    [System.IO.File]::WriteAllText(
        (Join-Path $Dir $name), $text, $utf8)
  }
}

function Assert-LangPatch {
  param([string] $Dir)

  foreach ($name in @("zh-cn.txt", "zh-tw.txt")) {
    $path = Join-Path $Dir $name
    $text = [System.IO.File]::ReadAllText($path, $utf8)
    foreach ($id in 2331..2335) {
      $count = [regex]::Matches($text, "(?m)^$id\s*$").Count
      if ($count -ne 1) {
        throw "$name must contain id $id exactly once, got $count"
      }
    }
    if ([regex]::Matches($text, "(?m)^2400\s*$").Count -ne 1) {
      throw "$name must preserve insertion point 2400 exactly once"
    }
  }
}

try {
  $langDir = Join-Path $tempRoot "lang"
  Write-LangFixture -Dir $langDir -WithInsertionPoint $true

  & $patchScript -LangDir $langDir
  Assert-LangPatch -Dir $langDir

  $first = foreach ($name in @("zh-cn.txt", "zh-tw.txt")) {
    [System.IO.File]::ReadAllText(
        (Join-Path $langDir $name), $utf8)
  }
  & $patchScript -LangDir $langDir
  $second = foreach ($name in @("zh-cn.txt", "zh-tw.txt")) {
    [System.IO.File]::ReadAllText(
        (Join-Path $langDir $name), $utf8)
  }
  if (($first -join "`0") -ne ($second -join "`0")) {
    throw "Language patch is not idempotent."
  }
  Assert-LangPatch -Dir $langDir

  $invalidDir = Join-Path $tempRoot "invalid"
  Write-LangFixture -Dir $invalidDir -WithInsertionPoint $false
  $failed = $false
  try {
    & $patchScript -LangDir $invalidDir
  } catch {
    $failed = $true
  }
  if (-not $failed) {
    throw "Language patch must fail when insertion point 2400 is missing."
  }

  Write-Output "Smart extract language patch tests passed."
}
finally {
  if (Test-Path -LiteralPath $tempRoot) {
    $resolvedTempRoot = (Resolve-Path -LiteralPath $tempRoot).Path
    $systemTemp = [System.IO.Path]::GetTempPath().TrimEnd("\") + "\"
    $isUnderTemp = $resolvedTempRoot.StartsWith(
        $systemTemp, [System.StringComparison]::OrdinalIgnoreCase)
    $hasExpectedName = (Split-Path -Leaf $resolvedTempRoot) -like "7z-smart-lang-*"
    if (-not $isUnderTemp -or -not $hasExpectedName) {
      throw "Refusing to remove unexpected test directory: $resolvedTempRoot"
    }
    Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force
  }
}

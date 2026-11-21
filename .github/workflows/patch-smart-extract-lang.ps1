param(
  [Parameter(Mandatory = $true)]
  [string] $LangDir
)

$ErrorActionPreference = "Stop"

$fragmentDir = Join-Path $PSScriptRoot "smart-extract-lang"
$utf8 = New-Object System.Text.UTF8Encoding($false)

foreach ($fragmentFile in Get-ChildItem -LiteralPath $fragmentDir -File -Filter "*.txt") {
  $targetFile = Join-Path $LangDir $fragmentFile.Name
  if (-not (Test-Path -LiteralPath $targetFile)) {
    throw "Language file not found: $targetFile"
  }

  $targetText = [System.IO.File]::ReadAllText($targetFile, $utf8)
  $fragmentText = ([System.IO.File]::ReadAllText($fragmentFile.FullName, $utf8)).TrimEnd()

  $fragmentIds = @([regex]::Matches($fragmentText, '(?m)^(\d+)\s*$') |
      ForEach-Object { $_.Groups[1].Value })
  $allPresent = $true
  foreach ($id in $fragmentIds) {
    if ([regex]::Matches($targetText, "(?m)^$id\s*$").Count -ne 1) {
      $allPresent = $false
      break
    }
  }
  if ($allPresent) {
    continue
  }

  foreach ($id in $fragmentIds) {
    $entryPattern = "(?m)^$id\r?\n[^\r\n]*(?:\r?\n|$)"
    $targetText = [regex]::Replace($targetText, $entryPattern, '')
  }

  $newLine = if ($targetText.Contains("`r`n")) { "`r`n" } else { "`n" }
  $fragmentText = ($fragmentText -split '\r?\n') -join $newLine
  $replacement = $fragmentText + $newLine + "2400"

  $updatedText = [regex]::Replace($targetText, '(?m)^2400(?=\r?$)', $replacement, 1)
  if ($updatedText -eq $targetText) {
    throw "Language insertion point 2400 not found: $targetFile"
  }

  [System.IO.File]::WriteAllText($targetFile, $updatedText, $utf8)
}

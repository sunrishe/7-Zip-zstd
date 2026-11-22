param(
  [Parameter(Mandatory = $true)]
  [string] $InstallerPath
)

$ErrorActionPreference = "Stop"

$installer = (Resolve-Path -LiteralPath $InstallerPath).Path
$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if (-not $dumpbin) {
  throw "dumpbin.exe is not available."
}

$output = & $dumpbin.Source /imports $installer 2>&1
if ($LASTEXITCODE -ne 0) {
  throw "dumpbin failed for $installer`n$($output -join "`n")"
}

$text = $output -join "`n"
$forbidden = @(
  "Rstrtmgr.dll",
  "RmStartSession",
  "RmRegisterResources",
  "RmGetList",
  "RmShutdown",
  "RmRestart",
  "RmEndSession"
)

foreach ($name in $forbidden) {
  if ($text -match [regex]::Escape($name)) {
    throw "Installer contains static Restart Manager import: $name"
  }
}

Write-Output "Installer import check passed: $installer"

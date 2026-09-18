<#
.SYNOPSIS
  One-shot: stop SunshineService, drop in a local build (exes + assets), restart.

.DESCRIPTION
  Elevated helper for spotcobuild forks. Calls Install-SunshineDropIn.ps1, then
  restarts SunshineService. Leaves config\, credentials\, and scripts\ alone.

  If not already running as Administrator, re-launches itself with UAC elevation.

.PARAMETER BuildDir
  CMake build output directory containing sunshine.exe.
  Default: <repo>\build relative to this script (spotcobuild\..\build).

.PARAMETER InstallDir
  Installed Sunshine root. Default: C:\Program Files\Sunshine.

.PARAMETER SkipAssets
  Do not mirror build\assets (exe-only swap). Prefer omitting this when the Web
  UI hashed bundles changed.

.PARAMETER WhatIf
  Show planned actions without stopping the service or copying files.

.EXAMPLE
  # From an elevated or normal PowerShell (UAC prompt if needed):
  .\spotcobuild\Update-SunshineFromBuild.ps1

.EXAMPLE
  .\spotcobuild\Update-SunshineFromBuild.ps1 -BuildDir F:\dev\sunshine\build

.EXAMPLE
  .\spotcobuild\Update-SunshineFromBuild.ps1 -WhatIf
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $false)]
    [string] $BuildDir,

    [Parameter(Mandatory = $false)]
    [ValidateNotNullOrEmpty()]
    [string] $InstallDir = 'C:\Program Files\Sunshine',

    [Parameter(Mandatory = $false)]
    [switch] $SkipAssets
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
$dropIn = Join-Path $scriptDir 'Install-SunshineDropIn.ps1'

if (-not $BuildDir -or [string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot 'build'
}

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-IsAdmin)) {
    if ($WhatIfPreference) {
        Write-Host "WhatIf: would re-launch elevated (UAC)."
        Write-Host "WhatIf: BuildDir=$BuildDir InstallDir=$InstallDir SkipAssets=$SkipAssets"
        return
    }
    Write-Host "Not elevated — requesting UAC for service stop / Program Files write..."
    $argList = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', $MyInvocation.MyCommand.Path,
        '-BuildDir', $BuildDir,
        '-InstallDir', $InstallDir
    )
    if ($SkipAssets) { $argList += '-SkipAssets' }
    $p = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $argList -Wait -PassThru
    exit $p.ExitCode
}

if (-not (Test-Path -LiteralPath $dropIn -PathType Leaf)) {
    throw "Missing Install-SunshineDropIn.ps1 next to this script: $dropIn"
}
if (-not (Test-Path -LiteralPath (Join-Path $BuildDir 'sunshine.exe') -PathType Leaf)) {
    throw "BuildDir missing sunshine.exe: $BuildDir"
}

$before = $null
if (Test-Path -LiteralPath (Join-Path $InstallDir 'sunshine.exe')) {
    $before = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $InstallDir 'sunshine.exe')).ProductVersion
}

Write-Host "BuildDir:   $BuildDir"
Write-Host "InstallDir: $InstallDir"
Write-Host "IncludeAssets: $(-not $SkipAssets)"
if ($before) { Write-Host "Installed (before): $before" }

if ($WhatIfPreference) {
    Write-Host "WhatIf: would Stop-Service SunshineService"
    & $dropIn -BuildDir $BuildDir -InstallDir $InstallDir -IncludeAssets:(!$SkipAssets) -WhatIf
    Write-Host "WhatIf: would Start-Service SunshineService"
    return
}

Write-Host "Stopping SunshineService..."
Stop-Service -Name SunshineService -Force -ErrorAction Stop
Start-Sleep -Seconds 2
Get-Process -Name sunshine, sunshinesvc -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Write-Host ("Service status: " + (Get-Service SunshineService).Status)

$dropArgs = @{
    BuildDir   = $BuildDir
    InstallDir = $InstallDir
}
if (-not $SkipAssets) {
    $dropArgs['IncludeAssets'] = $true
}
& $dropIn @dropArgs

$after = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $InstallDir 'sunshine.exe')).ProductVersion
Write-Host "Installed (after):  $after"

Write-Host "Starting SunshineService..."
Start-Service -Name SunshineService -ErrorAction Stop
Start-Sleep -Seconds 2
$svc = Get-Service SunshineService
Write-Host ("Service status: " + $svc.Status)
if ($svc.Status -ne 'Running') {
    throw "SunshineService did not reach Running (status=$($svc.Status))"
}

Write-Host "Done. Config/credentials/scripts were not modified."
exit 0

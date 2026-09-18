<#
.SYNOPSIS
  Drop-in replace Sunshine install binaries from a local build (exes only).

.DESCRIPTION
  Copies sunshine.exe and tools\*.exe used at runtime, plus matching .pdb files
  when present, from a Sunshine build directory into an existing install.

  By default does NOT copy assets\. Pass -IncludeAssets to mirror build\assets
  into the install (needed when Web UI hashed bundles change). Still does NOT touch
  config\, scripts\, Uninstall.exe, or credentials. Stop the Sunshine service / exit the tray app before running.

  Note: MSYS2/MinGW RelWithDebInfo builds usually embed DWARF debug info inside
  the .exe (no separate .pdb). PDB copy is for MSVC/clang-cl builds that emit them.

.PARAMETER BuildDir
  Path to the CMake build output (contains sunshine.exe).

.PARAMETER InstallDir
  Path to the installed Sunshine root (default: C:\Program Files\Sunshine).

.PARAMETER WhatIf
  Show what would be copied without writing files.

.EXAMPLE
  .\Install-SunshineDropIn.ps1 -BuildDir F:\dev\sunshine\build

.EXAMPLE
  .\Install-SunshineDropIn.ps1 -BuildDir F:\dev\sunshine\build -InstallDir 'C:\Program Files\Sunshine'
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string] $BuildDir,

    [Parameter(Mandatory = $false)]
    [ValidateNotNullOrEmpty()]
    [string] $InstallDir = 'C:\Program Files\Sunshine',

    [Parameter(Mandatory = $false)]
    [switch] $IncludeAssets
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-ExistingDirectory {
    param([string] $Path, [string] $Label)
    $resolved = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Path).Path)
    if (-not (Test-Path -LiteralPath $resolved -PathType Container)) {
        throw "$Label does not exist or is not a directory: $Path"
    }
    return $resolved
}

$BuildDir = Resolve-ExistingDirectory -Path $BuildDir -Label 'BuildDir'
$InstallDir = Resolve-ExistingDirectory -Path $InstallDir -Label 'InstallDir'

$requiredFiles = @(
    'sunshine.exe',
    'tools\sunshinesvc.exe',
    'tools\audio-info.exe',
    'tools\dxgi-info.exe'
)

$missing = @()
foreach ($rel in $requiredFiles) {
    $src = Join-Path $BuildDir $rel
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) {
        $missing += $src
    }
}
if ($missing.Count -gt 0) {
    throw ("BuildDir is missing required outputs:`n  - " + ($missing -join "`n  - "))
}

# Warn if Sunshine looks like it is still running (do not stop it automatically).
$running = @(Get-Process -Name 'sunshine', 'sunshinesvc' -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    Write-Warning ("Sunshine appears to be running (PIDs: {0}). Stop the service/tray first or file copies may fail." -f (($running | ForEach-Object { $_.Id }) -join ', '))
}

$copies = New-Object System.Collections.Generic.List[object]
$optionalSkipped = New-Object System.Collections.Generic.List[string]

foreach ($rel in $requiredFiles) {
    $copies.Add([pscustomobject]@{
            Source      = Join-Path $BuildDir $rel
            Destination = Join-Path $InstallDir $rel
        }) | Out-Null

    # Sibling PDB next to each exe (MSVC-style). Optional — MinGW builds often have none.
    $pdbRel = [System.IO.Path]::ChangeExtension($rel, '.pdb')
    $pdbSrc = Join-Path $BuildDir $pdbRel
    if (Test-Path -LiteralPath $pdbSrc -PathType Leaf) {
        $copies.Add([pscustomobject]@{
                Source      = $pdbSrc
                Destination = Join-Path $InstallDir $pdbRel
            }) | Out-Null
    }
    else {
        $optionalSkipped.Add($pdbRel) | Out-Null
    }
}

Write-Host "BuildDir:   $BuildDir"
Write-Host "InstallDir: $InstallDir"
Write-Host ""

foreach ($item in $copies) {
    $destDir = Split-Path -Parent $item.Destination
    if ($PSCmdlet.ShouldProcess($item.Destination, "Copy $($item.Source)")) {
        if (-not (Test-Path -LiteralPath $destDir -PathType Container)) {
            New-Item -ItemType Directory -Path $destDir -Force | Out-Null
        }
        Copy-Item -LiteralPath $item.Source -Destination $item.Destination -Force
        Write-Host "Copied file  $($item.Destination)"
    }
}

Write-Host ""
if ($optionalSkipped.Count -gt 0) {
    Write-Host ("No PDB beside build exes (skipped): {0}" -f ($optionalSkipped -join ', '))
    Write-Host "MinGW RelWithDebInfo usually embeds DWARF in the .exe itself; WinDbg wants PDB."
}

if ($IncludeAssets) {
    $assetsSrc = Join-Path $BuildDir 'assets'
    $assetsDst = Join-Path $InstallDir 'assets'
    if (-not (Test-Path -LiteralPath $assetsSrc -PathType Container)) {
        throw "IncludeAssets requested but build assets missing: $assetsSrc"
    }
    if ($PSCmdlet.ShouldProcess($assetsDst, "Mirror assets from $assetsSrc")) {
        if (Test-Path -LiteralPath $assetsDst -PathType Container) {
            Remove-Item -LiteralPath $assetsDst -Recurse -Force
        }
        Copy-Item -LiteralPath $assetsSrc -Destination $assetsDst -Recurse -Force
        Write-Host "Mirrored assets  $assetsDst"
    }
    Write-Host "Done. Copied exes + assets. Left untouched: config\, scripts\, Uninstall.exe."
}
else {
    Write-Host "Done. Copied exes only (no assets). Left untouched: assets\, config\, scripts\, Uninstall.exe."
}
Write-Host "Restart the Sunshine service / tray app when you are ready."

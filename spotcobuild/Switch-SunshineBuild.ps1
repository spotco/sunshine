<#
.SYNOPSIS
  Switch Sunshine spotcobuild branches while swapping archived build trees.

.DESCRIPTION
  Archives the current repo build\ directory under Temp\builds\<current-branch>\
  (gitignored), checks out the target branch, then restores that branch's archived
  build\ if one exists. Avoids a full rebuild when intermediates were saved earlier.

.PARAMETER Branch
  Target branch name, e.g. v2026.906.222525-spotcobuild

.PARAMETER SkipSubmodules
  Do not run git submodule update after checkout.

.EXAMPLE
  .\spotcobuild\Switch-SunshineBuild.ps1 -Branch v2025.924.154138-spotcobuild
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string] $Branch,

    [switch] $SkipSubmodules
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location -LiteralPath $repoRoot

function Get-CurrentBranch {
    $name = (git branch --show-current).Trim()
    if (-not $name) { throw 'Detached HEAD; checkout a branch before switching builds.' }
    return $name
}

function Get-ArchivePath([string] $BranchName) {
    return Join-Path $repoRoot ("Temp\builds\" + $BranchName)
}

$current = Get-CurrentBranch
$buildDir = Join-Path $repoRoot 'build'
$archiveRoot = Join-Path $repoRoot 'Temp\builds'
New-Item -ItemType Directory -Force -Path $archiveRoot | Out-Null

Write-Host "Repo:    $repoRoot"
Write-Host "Current: $current"
Write-Host "Target:  $Branch"
Write-Host ""

if ($current -eq $Branch) {
    Write-Host "Already on $Branch."
    if (Test-Path -LiteralPath $buildDir) {
        Write-Host "build\ is present. Nothing to do."
        return
    }
    $existing = Get-ArchivePath $Branch
    if (Test-Path -LiteralPath $existing) {
        if ($PSCmdlet.ShouldProcess($buildDir, "Restore archive from $existing")) {
            Move-Item -LiteralPath $existing -Destination $buildDir
            Write-Host "Restored build\ from Temp\builds\$Branch"
        }
        return
    }
    Write-Warning "No build\ and no archive for $Branch. Run a full cmake/ninja build."
    return
}

# Archive current build tree if present
if (Test-Path -LiteralPath $buildDir) {
    $dest = Get-ArchivePath $current
    if (Test-Path -LiteralPath $dest) {
        Write-Host "Replacing existing archive: $dest"
        if ($PSCmdlet.ShouldProcess($dest, 'Remove old archive')) {
            Remove-Item -LiteralPath $dest -Recurse -Force
        }
    }
    if ($PSCmdlet.ShouldProcess($buildDir, "Archive to $dest")) {
        New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
        Move-Item -LiteralPath $buildDir -Destination $dest
        Write-Host "Archived build\ -> Temp\builds\$current"
    }
}
else {
    Write-Host "No current build\ to archive."
}

if ($PSCmdlet.ShouldProcess($Branch, 'git checkout')) {
    git checkout $Branch
    if ($LASTEXITCODE -ne 0) { throw "git checkout $Branch failed" }
}

if (-not $SkipSubmodules) {
    Write-Host "Updating submodules (can take a while)..."
    git submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "git submodule update returned $LASTEXITCODE — check before building."
    }
}

$restore = Get-ArchivePath $Branch
if (Test-Path -LiteralPath $restore) {
    if ($PSCmdlet.ShouldProcess($restore, 'Restore archived build\')) {
        if (Test-Path -LiteralPath $buildDir) {
            Remove-Item -LiteralPath $buildDir -Recurse -Force
        }
        Move-Item -LiteralPath $restore -Destination $buildDir
        Write-Host "Restored build\ from Temp\builds\$Branch"
        Write-Host "Incremental rebuild if needed: ninja -C build"
    }
}
else {
    Write-Warning "No archived build for $Branch under Temp\builds\."
    Write-Warning "Do a full configure+build, then switch again to auto-archive it."
}

Write-Host "Done. On branch: $(Get-CurrentBranch)"

<#
.SYNOPSIS
  Archive the current Sunshine build\ tree under Temp\builds\<branch>\.

.DESCRIPTION
  Moves (does not copy) repo build\ into the gitignored Temp\builds\<branch>\
  folder so you can free the working build\ slot or prepare for a branch switch.

.EXAMPLE
  .\spotcobuild\Archive-SunshineBuild.ps1
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location -LiteralPath $repoRoot

$branch = (git branch --show-current).Trim()
if (-not $branch) { throw 'Detached HEAD; checkout a branch first.' }

$buildDir = Join-Path $repoRoot 'build'
if (-not (Test-Path -LiteralPath $buildDir)) {
    throw "No build\ directory at $buildDir"
}

$dest = Join-Path $repoRoot ("Temp\builds\" + $branch)
New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
if (Test-Path -LiteralPath $dest) {
    if ($PSCmdlet.ShouldProcess($dest, 'Remove existing archive')) {
        Remove-Item -LiteralPath $dest -Recurse -Force
    }
}

if ($PSCmdlet.ShouldProcess($buildDir, "Move to $dest")) {
    Move-Item -LiteralPath $buildDir -Destination $dest
    Write-Host "Archived build\ -> Temp\builds\$branch"
}

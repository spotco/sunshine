<#
.SYNOPSIS
  Spotcobuild reproduction matrix harness (Windows host).

.DESCRIPTION
  Cycles codec / HDR / display / FPS / resolution / two-pass combinations and
  records session UUID references from diagnostics JSONL when available.

  This is intentionally best-effort and CI-optional. Full automation of
  Moonlight clients and GPU load is host-specific.
#>
param(
  [string]$SunshineHost = "https://localhost:47990",
  [string]$DiagnosticsDir = "$env:APPDATA\Sunshine\diagnostics",
  [switch]$WhatIf
)

$ErrorActionPreference = "Stop"

$matrix = @(
  @{ Codec = "h264"; Hdr = $false; Fps = 30; Width = 1920; Height = 1080; TwoPass = $false; Display = "physical" },
  @{ Codec = "hevc"; Hdr = $false; Fps = 60; Width = 1920; Height = 1080; TwoPass = $true;  Display = "physical" },
  @{ Codec = "hevc"; Hdr = $true;  Fps = 60; Width = 1920; Height = 1080; TwoPass = $false; Display = "physical" },
  @{ Codec = "av1";  Hdr = $false; Fps = 60; Width = 2560; Height = 1080; TwoPass = $false; Display = "physical" },
  @{ Codec = "hevc"; Hdr = $false; Fps = 60; Width = 1920; Height = 1080; TwoPass = $false; Display = "virtual" }
)

Write-Host "spotcobuild reproduction matrix ($($matrix.Count) cells)"
Write-Host "DiagnosticsDir=$DiagnosticsDir"

$results = @()
foreach ($cell in $matrix) {
  $name = "$($cell.Codec)_hdr$($cell.Hdr)_$($cell.Fps)_$($cell.Width)x$($cell.Height)_tp$($cell.TwoPass)_$($cell.Display)"
  Write-Host "==> $name"
  if ($WhatIf) {
    $results += [pscustomobject]@{ Cell = $name; Status = "skipped"; SessionId = "" }
    continue
  }

  # Manual/CI-optional: operator starts a Moonlight session matching the cell,
  # then we pick the newest JSONL session id from diagnostics.
  $sessionId = ""
  if (Test-Path $DiagnosticsDir) {
    $latest = Get-ChildItem $DiagnosticsDir -Filter "*.jsonl" -File |
      Where-Object { $_.Name -notlike "*.ring.jsonl" } |
      Sort-Object LastWriteTime -Descending |
      Select-Object -First 1
    if ($latest) {
      $sessionId = [IO.Path]::GetFileNameWithoutExtension($latest.Name)
    }
  }

  $results += [pscustomobject]@{
    Cell = $name
    Status = if ($sessionId) { "observed" } else { "pending_manual" }
    SessionId = $sessionId
    Codec = $cell.Codec
    Hdr = $cell.Hdr
    Fps = $cell.Fps
    Resolution = "$($cell.Width)x$($cell.Height)"
    TwoPass = $cell.TwoPass
    Display = $cell.Display
  }
}

$out = Join-Path $DiagnosticsDir "reproduction_matrix_results.json"
New-Item -ItemType Directory -Force -Path $DiagnosticsDir | Out-Null
$results | ConvertTo-Json -Depth 5 | Set-Content -Path $out -Encoding UTF8
Write-Host "Wrote $out"
$results | Format-Table -AutoSize

# Regenerates docs/screenshot.png (README + release notes): builds the screenshot tool, which
# opens the real lyric sequencer and lyrics panel on a lorem ipsum song against the test fake
# of the REAPER API, and converts its capture to PNG. Windows only; REAPER is not involved.
#   .\tools\screenshot.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

cmake -S $root -B "$root\build" -DLYRICS_BUILD_TESTS=ON | Out-Null
if ($LASTEXITCODE) { throw "cmake configure failed" }
cmake --build "$root\build" --config Release --target lyrics_screenshot
if ($LASTEXITCODE) { throw "build failed" }

$tmp = Join-Path ([IO.Path]::GetTempPath()) "lyrics-screenshot"
New-Item -ItemType Directory -Force $tmp | Out-Null
& "$root\build\Release\lyrics_screenshot.exe" "$tmp\shot.bmp" $tmp
if ($LASTEXITCODE) { throw "screenshot tool failed" }

Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force "$root\docs" | Out-Null
$img = [System.Drawing.Image]::FromFile("$tmp\shot.bmp")
try { $img.Save("$root\docs\screenshot.png", [System.Drawing.Imaging.ImageFormat]::Png) } finally { $img.Dispose() }
Write-Host "Wrote docs\screenshot.png"

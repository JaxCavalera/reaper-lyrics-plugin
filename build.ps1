param(
  [switch]$Install,
  [string]$Config = "Release"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

if (-not (Test-Path "$root\reaper-sdk\sdk\reaper_plugin.h") -or -not (Test-Path "$root\WDL\WDL\swell\swell.h")) {
  Write-Host "Fetching submodules (REAPER SDK, WDL)..."
  git -C $root submodule update --init --depth 1
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found - install Visual Studio Build Tools (Desktop C++ workload)." }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "No Visual Studio with the C++ toolset found." }
$ver = & $vswhere -latest -products * -property installationVersion
$major = [int]($ver.Split('.')[0])
$gen = switch ($major) { 17 { "Visual Studio 17 2022" } 18 { "Visual Studio 18 2026" } default { "Visual Studio 17 2022" } }

Write-Host "Using $vs ($gen)"
cmake -S $root -B "$root\build" -G $gen -A x64
if ($LASTEXITCODE) { throw "cmake configure failed" }
cmake --build "$root\build" --config $Config
if ($LASTEXITCODE) { throw "build failed" }

$dll = "$root\build\$Config\reaper_lyrics.dll"
Write-Host "Built: $dll"

if ($Install) {
  $dest = Join-Path $env:APPDATA "REAPER\UserPlugins"
  if (-not (Test-Path $dest)) { New-Item -ItemType Directory -Force $dest | Out-Null }
  if (Get-Process reaper -ErrorAction SilentlyContinue) { Write-Warning "REAPER is running - close it before installing, the DLL is locked." }
  Copy-Item $dll (Join-Path $dest "reaper_lyrics.dll") -Force
  Write-Host "Installed to $dest\reaper_lyrics.dll - restart REAPER."
}

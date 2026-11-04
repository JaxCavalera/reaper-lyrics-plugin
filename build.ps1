param(
  [switch]$Install,
  [string]$Config = "Release",
  # REAPER's resource path (Options > Show REAPER resource path). Default: the standard
  # install's %APPDATA%\REAPER. Pass it for a portable REAPER, e.g. -ResourcePath D:\REAPER
  [string]$ResourcePath = ""
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
$ver = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
$major = [int]($ver.Split('.')[0])
$gen = switch ($major) {
  16 { "Visual Studio 16 2019" }
  17 { "Visual Studio 17 2022" }
  18 { "Visual Studio 18 2026" }
  default { throw "Visual Studio $major is not known to this script - run cmake -G yourself (see README)." }
}

if (-not $ResourcePath) { $ResourcePath = Join-Path $env:APPDATA "REAPER" }

Write-Host "Using $vs ($gen)"
cmake -S $root -B "$root\build" -G $gen -A x64 "-DREAPER_RESOURCE_PATH=$ResourcePath"
if ($LASTEXITCODE) { throw "cmake configure failed" }
cmake --build "$root\build" --config $Config
if ($LASTEXITCODE) { throw "build failed" }

$dll = "$root\build\$Config\reaper_lyrics.dll"
Write-Host "Built: $dll"

# Portability check: the DLL must not need the Visual C++ runtime DLLs (CMake links it
# statically), or it won't load on PCs without a recent VC++ Redistributable.
$dumpbin = Get-ChildItem "$vs\VC\Tools\MSVC" -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
  Where-Object { $_.FullName -match 'Hostx64\\x64' } | Select-Object -First 1
if ($dumpbin) {
  $deps = & $dumpbin.FullName /dependents $dll | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim() }
  $crt = $deps | Where-Object { $_ -match '^(MSVCP|VCRUNTIME|api-ms-win-crt-|ucrtbase)' }
  if ($crt) { throw "reaper_lyrics.dll depends on the VC++ runtime ($($crt -join ', ')) - it must be linked statically." }
  Write-Host "Portable: depends only on $($deps -join ', ')"
}

if ($Install) {
  if (-not (Test-Path (Join-Path $ResourcePath "reaper.ini"))) {
    Write-Warning "No reaper.ini in $ResourcePath - is that REAPER's resource path? For a portable REAPER pass -ResourcePath <its folder>."
  }
  $dest = Join-Path $ResourcePath "UserPlugins"
  if (-not (Test-Path $dest)) { New-Item -ItemType Directory -Force $dest | Out-Null }
  if (Get-Process reaper -ErrorAction SilentlyContinue) { Write-Warning "REAPER is running - close it before installing, the DLL is locked." }
  Copy-Item $dll (Join-Path $dest "reaper_lyrics.dll") -Force
  Write-Host "Installed to $dest\reaper_lyrics.dll - restart REAPER."
}

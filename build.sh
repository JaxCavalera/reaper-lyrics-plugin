#!/usr/bin/env bash
# Build (and optionally install) reaper_lyrics.so on Linux.
#   ./build.sh            build only
#   ./build.sh --install  build and copy into ~/.config/REAPER/UserPlugins
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -f reaper-sdk/sdk/reaper_plugin.h ] || [ ! -f WDL/WDL/swell/swell.h ]; then
  echo "Fetching submodules (REAPER SDK, WDL)..."
  git submodule update --init --depth 1
fi

cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j"$(nproc)"
echo "Built: $(pwd)/build-linux/reaper_lyrics.so"

if [ "${1:-}" = "--install" ]; then
  dest="${HOME}/.config/REAPER/UserPlugins"
  mkdir -p "$dest"
  cp build-linux/reaper_lyrics.so "$dest/"
  echo "Installed to $dest/reaper_lyrics.so - restart REAPER."
fi

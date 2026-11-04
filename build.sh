#!/usr/bin/env bash
# Build (and optionally install) reaper_lyrics.so on Linux.
#   ./build.sh                  build only
#   ./build.sh --install        build and copy into REAPER's UserPlugins folder
#   ./build.sh --install DIR    ... where DIR is REAPER's resource path (portable REAPER etc.)
# The resource path can also come from $REAPER_RESOURCE_PATH; default ~/.config/REAPER.
set -euo pipefail
cd "$(dirname "$0")"

# Oldest glibc the plugin may ask for, so it loads on older distros too.
MAX_GLIBC="2.17"

if [ ! -f reaper-sdk/sdk/reaper_plugin.h ] || [ ! -f WDL/WDL/swell/swell.h ]; then
  echo "Fetching submodules (REAPER SDK, WDL)..."
  git submodule update --init --depth 1
fi

resource="${REAPER_RESOURCE_PATH:-$HOME/.config/REAPER}"
if [ "${1:-}" = "--install" ] && [ -n "${2:-}" ]; then resource="$2"; fi

cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release -DREAPER_RESOURCE_PATH="$resource"
cmake --build build-linux -j"$(nproc)"
so="build-linux/reaper_lyrics.so"
echo "Built: $(pwd)/$so"

# Portability check: the plugin's own code must not ask for a glibc newer than MAX_GLIBC
# (recent compilers quietly turn e.g. strtol into a glibc 2.38 symbol). libstdc++ comes from
# the system: the .so runs on this machine and on any with the same or a newer libstdc++.
if command -v objdump >/dev/null 2>&1; then
  newest="$(objdump -T "$so" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1)"
  if [ -n "$newest" ] && [ "$(printf '%s\n%s\n' "$newest" "$MAX_GLIBC" | sort -V | tail -1)" != "$MAX_GLIBC" ]; then
    echo "ERROR: $so needs glibc $newest; it should run on glibc $MAX_GLIBC. Symbols:" >&2
    objdump -T "$so" | grep "GLIBC_$newest" >&2
    exit 1
  fi
  exported="$(objdump -T "$so" | grep -v '\*UND\*' | grep -E ' D[FO] ' | awk '{print $NF}' | sort | tr '\n' ' ')"
  if [ "$exported" != "ReaperPluginEntry SWELL_dllMain " ]; then
    echo "ERROR: $so should export only ReaperPluginEntry and SWELL_dllMain, it exports: $exported" >&2; exit 1
  fi
  cxx="$(objdump -T "$so" | grep -o 'GLIBCXX_[0-9.]*' | sort -uV | tail -1)"
  echo "Needs glibc ${newest:-?} and libstdc++ ${cxx:-?} (or newer)"
fi

if [ "${1:-}" = "--install" ]; then
  [ -f "$resource/reaper.ini" ] || echo "WARNING: no reaper.ini in $resource - is that REAPER's resource path?" >&2
  dest="$resource/UserPlugins"
  mkdir -p "$dest"
  cp "$so" "$dest/"
  echo "Installed to $dest/reaper_lyrics.so - restart REAPER."
fi

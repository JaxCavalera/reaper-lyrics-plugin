# REAPER Lyrics — lyric tracks for the REAPER timeline

A native REAPER extension (`reaper_lyrics.dll` on Windows, `reaper_lyrics.so` on Linux) that
adds **lyric tracks**: paragraphs of lyrics that live on the timeline as coloured, draggable
blocks with a marker for every line.

## What you get

- **Insert lyric track** in the track-panel right-click menu (next to *Insert track from
  template…*), in the main **Track** menu, and as an action (`Lyrics: Insert lyric track`).
- Every **block** (paragraph) is an empty item on the lyric track: its name is the block label,
  its colour is the block colour, and the lyrics are rendered inside it on the timeline.
  Drag the item to reorder blocks, drag its edges to extend/contract the block — the lines
  inside scale with it.
- Every **line** gets a project marker (a *sub-marker*) in the block's colour. Drag a marker
  to move that line within its block; the gap to the next marker is the line's duration.
  Dragging the first marker moves the block's start.
- Labels follow **INTRO, VERSE, CHORUS, VERSE, CHORUS, … OUTRO** automatically until you
  rename a block. Click a label chip in the editor's gutter to rename it, pick a preset,
  or change its colour (the item and its markers recolour too).
- **Lyric editor** window: one blank line starts a new block. Edits are diffed against the
  existing blocks so timings survive typing. Drag a label chip up or down to slide a whole
  block (its timeline slot moves with it).
- **Track** toggle: the editor follows playback, keeping the current block near the top
  and highlighting the current line. Untick it to edit freely while REAPER plays.
- **Live typing**: with *Track* on and transport playing, press **Enter** to stamp the play
  position on the next line (single return) or the next block (double return). Blocks
  auto-size as you go.
- **Lyrics panel**: a floating, resizable window (position remembered) that shows the
  current line large with the lines around it, coloured by block.

Everything is stored in the project itself (items, markers, item ext-state), so the project
file carries the lyrics; only window positions go in `reaper_lyrics.ini` in REAPER's
resource folder.

The whole UI is drawn by the extension itself on the subset of Win32 that REAPER's SWELL
layer provides, so Windows and Linux run the same code. macOS is not supported.

## Build — Windows

Requirements:

- Visual Studio 2022/2026 Build Tools (or full VS) with the *Desktop development with C++*
  workload — gives you `cl.exe`, `rc.exe` and the Windows SDK.
- CMake 3.20 or newer.
- Git — the REAPER SDK and WDL are git submodules (`reaper-sdk/`, `WDL/`).

```powershell
git clone --recurse-submodules https://github.com/JaxCavalera/reaper-lyrics-plugin.git
# already cloned without them?  git submodule update --init --depth 1
```

From a *Developer PowerShell* (or any shell after running `vcvars64.bat`):

```powershell
cd reaper-lyrics-plugin
cmake -S . -B build -G "Visual Studio 18 2026" -A x64      # VS 2022: -G "Visual Studio 17 2022"
cmake --build build --config Release
```

Or run `.\build.ps1`, which locates the Build Tools with `vswhere`, configures, builds and
prints where the DLL ended up (`build\Release\reaper_lyrics.dll`).

### Install

Copy the DLL into REAPER's user plugin folder and restart REAPER:

```powershell
Copy-Item build\Release\reaper_lyrics.dll "$env:APPDATA\REAPER\UserPlugins\"
```

`.\build.ps1 -Install` does the copy for you; `cmake --build build --target install_to_reaper`
is the same thing. REAPER must be closed if it already loaded an older copy (the file is locked).

For a portable REAPER install the folder is `<REAPER dir>\UserPlugins` — whatever
*Options → Show REAPER resource path* opens, plus `UserPlugins`.

## Build — Linux

Requirements: `g++` (C++17), `cmake` ≥ 3.20, `make`, `git`. No GTK/X11 dev packages are
needed — REAPER supplies the SWELL implementation at runtime; the extension only compiles
against the SWELL headers in `WDL/` and links the small `swell-modstub`.

```bash
git clone --recurse-submodules https://github.com/JaxCavalera/reaper-lyrics-plugin.git
cd reaper-lyrics-plugin
./build.sh              # -> build-linux/reaper_lyrics.so
./build.sh --install    # also copies it to ~/.config/REAPER/UserPlugins/
```

Manual equivalent:

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j$(nproc)
mkdir -p ~/.config/REAPER/UserPlugins
cp build-linux/reaper_lyrics.so ~/.config/REAPER/UserPlugins/
```

Restart REAPER afterwards. (REAPER for Linux x86_64 only; build on the same architecture
you run REAPER on.)

## Use

1. Right-click the empty track-panel area → **Insert lyric track** (or Track menu / action
   list). A track named *Lyrics* appears with an INTRO block at the edit cursor and the lyric
   editor opens.
2. Type. Blank line = new block. Each block becomes an item; each line a marker.
3. Click a label chip to rename/recolour (double-click to rename inline); drag it to reorder.
4. Tick **Track**, press play, and hit Enter as you sing to lay lines and blocks down live.
5. **Lyrics panel** opens the playback display; drag it wherever you like.

Actions (Actions → Show action list, filter "Lyrics:"): *Insert lyric track*,
*Show/hide lyric editor*, *Show/hide lyrics panel*, *Toggle editor tracking*.

Editor keys: the usual — arrows, Home/End, PgUp/PgDn, Shift to select, Ctrl+A/C/X/V,
Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) undo/redo.

## Layout

```
src/main.cpp        plugin entry, actions, context-menu hook, timer
src/model.*         lyric document + two-way sync with items/markers
src/editor.*        editor window: toolbar, label gutter, text area
src/textedit.*      self-drawn text widget (selection, clipboard, undo, scrolling)
src/panel.*         playback lyrics panel
src/platform.*      the Win32-vs-SWELL differences (UTF-8 drawing, clipboard, colour picker)
src/util.*          utf-8, ini, colours, LCS diff
src/resource.rc     Windows dialog shells      src/resource_swell.cpp  the same for SWELL
reaper-sdk/         REAPER SDK headers (submodule)   WDL/   WDL: SWELL headers, win32_utf8 (submodule)
```

Prebuilt binaries for each release are on the GitHub Releases page.

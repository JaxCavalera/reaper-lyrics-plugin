# REAPER Lyrics — lyric tracks for the REAPER timeline

A native REAPER extension (`reaper_lyrics.dll` on Windows, `reaper_lyrics.so` on Linux) that
adds **lyric tracks**. Like a MIDI item, a lyric track holds a single **lyric item** on the
timeline; double-click it to open the **lyric sequencer**, where the song's lyric blocks and
lines are laid out in full colour and can be moved, stretched and reordered.

## What you get

- **Insert lyric track** in the track-panel right-click menu (next to *Insert track from
  template…*), in the main **Track** menu, and as an action (`Lyrics: Insert lyric track`).
  The track gets one lyric item with an empty INTRO block at the edit cursor, and the
  sequencer opens ready to type.
- **One lyric item per track.** It holds the whole lyric document (blocks, labels, colours,
  line timings) with times relative to the item, so moving the item moves the lyrics. It
  auto-fits: it always starts at the first block and never ends before the last one (drag its
  right edge further to leave space at the end). Nothing else is added to the main timeline —
  no project markers.
- **Double-click the lyric item** to open the sequencer (any other item double-click behaves
  as usual).
- **Lyric sequencer** window:
  - a timeline lane with a bar ruler, the **blocks** as solid coloured bars with their label,
    and each block's **lines** as coloured segments underneath;
  - drag a block to move it within the free space around it, or past a neighbour's middle to
    swap their order; drag a block's edges to extend/contract it (its lines scale with it).
    Dragging the right edge ripples: the next block snaps against it and every later block
    moves by the same amount, keeping the gaps between them — hold **Shift** to resize without
    moving anything else (the edge then stops at the next block);
    drag the boundary between two lines to retime them. Blocks never overlap;
  - click the ruler to move the edit cursor; scroll with the scrollbar under the lane or the
    wheel. Zoom with the **Zoom -** / **Zoom +** buttons (they keep the playhead or edit cursor
    in place), **Fit** to show the whole lyric item, or Ctrl+wheel / the wheel over the ruler
    to zoom around the mouse; the playhead is followed while playing with *Track* on;
  - the window is resizable from any edge or the grip in the bottom-right corner;
  - click a block or line to jump to it in the text; double-click a block or its label chip
    to rename it straight away; right-click (or click the grip on the right of a label chip)
    for label presets, colour, delete;
  - below the lane, the **lyric text**: one blank line starts a new block. Edits are diffed
    against the existing blocks so timings survive typing. Drag a label chip in the gutter
    up or down to move a whole block.
- Labels follow **INTRO, VERSE, CHORUS, VERSE, CHORUS, … OUTRO** automatically until you
  rename a block. A block starts in its label's default colour — **sky blue** INTRO,
  **light green** VERSE, **light orange** CHORUS, **grey** OUTRO — and renaming it never
  changes its colour; only picking a colour from the block menu does, and that colour stays.
- **Default size: 2 bars per line.** A new block gets 2 bars for each of its lines, evenly
  spaced, and keeps growing or shrinking by 2 bars as you add or remove lines — the blocks
  after it move along, keeping their gaps. Once you resize it or move one of its lines by
  hand (or stamp it live), it keeps the timing you gave it.
- An empty editor is still one block — there's no placeholder text, and clearing the text
  keeps the first block. Leave a blank line after a block and type to start the next one.
- **Track** toggle: the sequencer follows playback, keeping the current block near the top
  and highlighting the current line. Untick it to edit freely while REAPER plays.
- **Live typing**: with *Track* on and transport playing, press **Enter** to stamp the play
  position on the next line (single return) or the next block (double return). Blocks
  auto-size as you go.
- **Lyrics panel**: a floating, resizable window (position remembered) that shows the
  current line large with the lines around it, coloured by block.
- **Undo**: everything goes through REAPER's undo. Typing becomes one undo step per pause
  rather than one per keystroke.

The lyrics are stored in the project (in the lyric item's ext state), so the project file
carries them; only window positions go in `reaper_lyrics.ini` in REAPER's resource folder.
Projects made with earlier builds (an item per block plus a marker per line) are converted
to a single lyric item automatically, and their markers removed.

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

### Tests

The document model (text, the lyric item, sync, sequencer drags) has tests that run against a fake REAPER API:

```bash
cmake -S . -B build -DLYRICS_BUILD_TESTS=ON
cmake --build build --config Release --target model_test
build/Release/model_test      # Linux (single-config): build/model_test
```

## Use

1. Right-click the empty track-panel area → **Insert lyric track** (or Track menu / action
   list). A *Lyrics* track appears with its lyric item, and the lyric sequencer opens.
2. Type. Blank line = new block. Blocks and lines appear in the sequencer lane in colour.
3. Arrange in the lane: drag blocks, their edges, and the boundaries between lines.
4. Double-click the lyric item on the timeline any time to reopen the sequencer.
5. Tick **Track**, press play, and hit Enter as you sing to lay lines and blocks down live.
6. **Lyrics panel** opens the playback display; drag it wherever you like.

Actions (Actions → Show action list, filter "Lyrics:"): *Insert lyric track*,
*Show/hide lyric sequencer*, *Show/hide lyrics panel*, *Toggle editor tracking*.

Text keys: the usual — arrows, Home/End, PgUp/PgDn, Shift to select, Ctrl+A/C/X/V,
Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) undo/redo within the text.

## Layout

```
src/main.cpp        plugin entry, actions, context-menu hook, item double-click hook, timer
tests/model_test.cpp  model tests against a fake REAPER API
src/model.*         lyric document, its storage in the lyric item, sync with REAPER
src/editor.*        lyric sequencer: toolbar, timeline lane, label gutter, text area
src/textedit.*      self-drawn text widget (selection, clipboard, undo, scrolling)
src/panel.*         playback lyrics panel
src/platform.*      the Win32-vs-SWELL differences (UTF-8 drawing, clipboard, colour picker)
src/util.*          utf-8, ini, colours, LCS diff
src/resource.rc     Windows dialog shells      src/resource_swell.cpp  the same for SWELL
reaper-sdk/         REAPER SDK headers (submodule)   WDL/   WDL: SWELL headers, win32_utf8 (submodule)
```

Prebuilt binaries for each release are on the GitHub Releases page.

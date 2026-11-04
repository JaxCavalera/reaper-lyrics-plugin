// Model tests: runs the real LyricDoc (src/model.cpp) against an in-memory fake of the
// REAPER API (one project, tracks, items, markers). Build with -DLYRICS_BUILD_TESTS=ON.
#define REAPERAPI_IMPLEMENT
#include "api.h"
#include "model.h"
#include "trackstyle.h"
#include "util.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ------------------------------------------------------------------ fake REAPER

struct FakeTrack;
struct FakeTakeMarker { double srcpos; std::string name; int color; };
struct FakeTake { std::map<std::string, std::string> str; double startoffs = 0; std::vector<FakeTakeMarker> marks; };
struct FakeItem { double pos = 0, len = 0; int color = 0; bool loop = true, sel = false; FakeTrack* track = nullptr; std::map<std::string, std::string> str; std::unique_ptr<FakeTake> take; };
struct FakeTrack { std::map<std::string, std::string> str; std::map<std::string, double> num; std::vector<FakeItem*> items; GUID guid; };
struct FakeMarker { double pos; std::string name; int color; };

static std::vector<std::unique_ptr<FakeTrack>> g_tracks;
static std::vector<std::unique_ptr<FakeItem>> g_allItems;
static std::map<int, FakeMarker> g_markers;
static int g_state = 1, g_itemSerial = 0, g_undoPoints = 0;
static ReaProject* const kProj = (ReaProject*)(intptr_t)0x1234;
static double g_cursor = 0;

static FakeTrack* T(MediaTrack* t) { return (FakeTrack*)t; }
static FakeItem* I(MediaItem* i) { return (FakeItem*)i; }
static void Bump() { g_state++; }

static bool StrGetSet(std::map<std::string, std::string>& m, const char* k, char* buf, bool set)
{
  if (set) { m[k] = buf; Bump(); return true; }
  auto it = m.find(k);
  if (it == m.end()) { buf[0] = 0; return false; }
  strcpy(buf, it->second.c_str());
  return true;
}

static void InstallFakes()
{
#ifndef _WIN32
  // on Linux these come from SWELL, which REAPER provides at runtime
  GetTickCount = []() -> DWORD { static DWORD t = 1000; return t += 10; };
#endif
  EnumProjects = [](int, char*, int) { return kProj; };
  ValidatePtr2 = [](ReaProject*, void* p, const char*) {
    for (auto& t : g_tracks) if (t.get() == p) return true;
    for (auto& t : g_tracks) for (auto* i : t->items) if (i == p) return true;
    return false;
  };
  GetProjectStateChangeCount = [](ReaProject*) { return g_state; };
  PreventUIRefresh = [](int) {};
  UpdateArrange = []() {};
  UpdateTimeline = []() {};
  TrackList_AdjustWindows = [](bool) {};
  Undo_BeginBlock2 = [](ReaProject*) {};
  Undo_EndBlock2 = [](ReaProject*, const char*, int) { g_undoPoints++; Bump(); };
  Undo_OnStateChange_Item = [](ReaProject*, const char*, MediaItem*) { g_undoPoints++; Bump(); };
  CountTracks = [](ReaProject*) { return (int)g_tracks.size(); };
  GetTrack = [](ReaProject*, int i) { return i >= 0 && i < (int)g_tracks.size() ? (MediaTrack*)g_tracks[i].get() : nullptr; };
  InsertTrackAtIndex = [](int idx, bool) {
    auto t = std::make_unique<FakeTrack>();
    memset(&t->guid, 0, sizeof(GUID)); t->guid.Data1 = (unsigned)(g_tracks.size() + 1);
    g_tracks.insert(g_tracks.begin() + idx, std::move(t)); Bump();
  };
  CountSelectedTracks = [](ReaProject*) { return 0; };
  GetSelectedTrack = [](ReaProject*, int) { return (MediaTrack*)nullptr; };
  SetOnlyTrackSelected = [](MediaTrack*) {};
  GetSetMediaTrackInfo_String = [](MediaTrack* t, const char* k, char* buf, bool set) { return StrGetSet(T(t)->str, k, buf, set); };
  GetMediaTrackInfo_Value = [](MediaTrack* t, const char* k) -> double {
    if (!strcmp(k, "IP_TRACKNUMBER")) for (size_t i = 0; i < g_tracks.size(); i++) if (g_tracks[i].get() == T(t)) return (double)(i + 1);
    auto it = T(t)->num.find(k);
    return it == T(t)->num.end() ? 0.0 : it->second;
  };
  SetMediaTrackInfo_Value = [](MediaTrack* t, const char* k, double v) { T(t)->num[k] = v; return true; };
  ThemeLayout_GetLayout = [](const char* sec, int idx, char* out, int sz) {
    static const char* tcp[] = { "Tracking", "150%_Tracking", "Reduced", "Minimal", "Top Aligned" };
    if (strcmp(sec, "tcp") || idx < 0 || idx >= 5) return false;
    snprintf(out, sz, "%s", tcp[idx]);
    return true;
  };
  GetTrackGUID = [](MediaTrack* t) { return &T(t)->guid; };
  guidToString = [](const GUID* g, char* out) { sprintf(out, "{TRACK-%08X}", (unsigned)g->Data1); };
  AddMediaItemToTrack = [](MediaTrack* t) {
    g_allItems.push_back(std::make_unique<FakeItem>());
    FakeItem* i = g_allItems.back().get();
    char g[64]; sprintf(g, "{ITEM-%d}", ++g_itemSerial); i->str["GUID"] = g;
    i->track = T(t);
    T(t)->items.push_back(i); Bump();
    return (MediaItem*)i;
  };
  DeleteTrackMediaItem = [](MediaTrack* t, MediaItem* it) {
    auto& v = T(t)->items;
    for (size_t k = 0; k < v.size(); k++) if (v[k] == I(it)) { v.erase(v.begin() + k); I(it)->track = nullptr; Bump(); return true; }
    return false;
  };
  GetMediaItem_Track = [](MediaItem* it) { return (MediaTrack*)I(it)->track; };
  GetItemFromPoint = [](int, int, bool, MediaItem_Take**) { return (MediaItem*)nullptr; };
  CountTrackMediaItems = [](MediaTrack* t) { return (int)T(t)->items.size(); };
  GetTrackMediaItem = [](MediaTrack* t, int i) { return (MediaItem*)T(t)->items[i]; };
  GetSetMediaItemInfo_String = [](MediaItem* it, const char* k, char* buf, bool set) { return StrGetSet(I(it)->str, k, buf, set); };
  GetMediaItemInfo_Value = [](MediaItem* it, const char* k) -> double {
    if (!strcmp(k, "D_POSITION")) return I(it)->pos;
    if (!strcmp(k, "D_LENGTH")) return I(it)->len;
    if (!strcmp(k, "I_CUSTOMCOLOR")) return I(it)->color;
    if (!strcmp(k, "B_LOOPSRC")) return I(it)->loop ? 1 : 0;
    if (!strcmp(k, "B_UISEL")) return I(it)->sel ? 1 : 0;
    return 0;
  };
  SetMediaItemInfo_Value = [](MediaItem* it, const char* k, double v) {
    if (!strcmp(k, "D_POSITION")) I(it)->pos = v;
    else if (!strcmp(k, "D_LENGTH")) I(it)->len = v;
    else if (!strcmp(k, "I_CUSTOMCOLOR")) I(it)->color = (int)v;
    else if (!strcmp(k, "B_LOOPSRC")) I(it)->loop = v != 0;
    else if (!strcmp(k, "B_UISEL")) I(it)->sel = v != 0;
    Bump(); return true;
  };
  // an empty MIDI item: one take; QN = seconds * 2 (120 bpm)
  TimeMap2_timeToQN = [](ReaProject*, double t) { return t * 2.0; };
  CreateNewMIDIItemInProj = [](MediaTrack* t, double s, double e, const bool*) {
    MediaItem* it = AddMediaItemToTrack(t);
    I(it)->pos = s; I(it)->len = e - s;
    I(it)->take = std::make_unique<FakeTake>();
    I(it)->take->str["P_NAME"] = "untitled MIDI item";
    return it;
  };
  MIDI_SetItemExtents = [](MediaItem* it, double s, double e) {
    if (!I(it)->take) return false;
    I(it)->pos = s / 2.0; I(it)->len = (e - s) / 2.0;   // the source follows: offset stays 0
    Bump(); return true;
  };
  GetActiveTake = [](MediaItem* it) { return (MediaItem_Take*)I(it)->take.get(); };
  GetSetMediaItemTakeInfo_String = [](MediaItem_Take* tk, const char* k, char* buf, bool set) { return StrGetSet(((FakeTake*)tk)->str, k, buf, set); };
  GetMediaItemTakeInfo_Value = [](MediaItem_Take* tk, const char* k) -> double {
    if (!strcmp(k, "D_STARTOFFS")) return ((FakeTake*)tk)->startoffs;
    if (!strcmp(k, "D_PLAYRATE")) return 1.0;
    return 0;
  };
  GetNumTakeMarkers = [](MediaItem_Take* tk) { return (int)((FakeTake*)tk)->marks.size(); };
  GetTakeMarker = [](MediaItem_Take* tk, int i, char* name, int sz, int* col) -> double {
    auto& m = ((FakeTake*)tk)->marks;
    if (i < 0 || i >= (int)m.size()) return -1;
    snprintf(name, sz, "%s", m[i].name.c_str());
    if (col) *col = m[i].color;
    return m[i].srcpos;
  };
  SetTakeMarker = [](MediaItem_Take* tk, int i, const char* name, double* sp, int* col) {
    auto& m = ((FakeTake*)tk)->marks;
    if (i < 0) { m.push_back({ sp ? *sp : 0, name ? name : "", col ? *col : 0 }); i = (int)m.size() - 1; }
    else { if (name) m[i].name = name; if (sp) m[i].srcpos = *sp; if (col) m[i].color = *col; }
    std::stable_sort(m.begin(), m.end(), [](const FakeTakeMarker& a, const FakeTakeMarker& b) { return a.srcpos < b.srcpos; });
    Bump(); return i;
  };
  DeleteTakeMarker = [](MediaItem_Take* tk, int i) {
    auto& m = ((FakeTake*)tk)->marks;
    if (i < 0 || i >= (int)m.size()) return false;
    m.erase(m.begin() + i); Bump(); return true;
  };
  DeleteProjectMarker = [](ReaProject*, int id, bool) { bool had = g_markers.erase(id) > 0; Bump(); return had; };
  EnumProjectMarkers3 = [](ReaProject*, int idx, bool* isrgn, double* pos, double* end, const char** name, int* id, int* color) {
    if (idx < 0 || idx >= (int)g_markers.size()) return 0;
    auto it = g_markers.begin(); std::advance(it, idx);
    if (isrgn) *isrgn = false;
    if (pos) *pos = it->second.pos;
    if (end) *end = 0;
    if (name) *name = it->second.name.c_str();
    if (id) *id = it->first;
    if (color) *color = it->second.color;
    return idx + 1;
  };
  ColorToNative = [](int r, int g, int b) { return r | (g << 8) | (b << 16); };
  ColorFromNative = [](int c, int* r, int* g, int* b) { *r = c & 255; *g = (c >> 8) & 255; *b = (c >> 16) & 255; };
  GetPlayStateEx = [](ReaProject*) { return 0; };
  GetPlayPosition2Ex = [](ReaProject*) { return 0.0; };
  GetCursorPositionEx = [](ReaProject*) { return g_cursor; };
  TimeMap2_beatsToTime = [](ReaProject*, double, const int* m) { return m ? *m * 2.0 : 0.0; }; // 2 s bars
  TimeMap2_timeToBeats = [](ReaProject*, double t, int* m, int*, double*, int*) { if (m) *m = (int)(t / 2.0); return 0.0; };
}

// ------------------------------------------------------------------ checks

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) g_pass++; else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
static bool Near(double a, double b) { return std::fabs(a - b) < 1e-5; }

LyricDoc g_doc;
static FakeTrack* LyricTrack() { return T(g_doc.track); }
static FakeItem* LyricItem() { return LyricTrack()->items.empty() ? nullptr : LyricTrack()->items[0]; }
static std::string ItemStr(FakeItem* i, const char* k) { auto it = i->str.find(k); return it == i->str.end() ? "" : it->second; }
static void Sync() { if (g_doc.NeedsSync()) g_doc.SyncFromReaper(); }

// The lyric item's take markers are exactly the blocks: label + colour at each block start.
static bool MarksMatchBlocks()
{
  FakeItem* it = LyricItem();
  if (!it || !it->take) return false;
  auto& m = it->take->marks;
  auto& B = g_doc.blocks;
  if (m.size() != B.size()) return false;
  for (size_t i = 0; i < B.size(); i++)
    if (std::fabs(m[i].srcpos - (B[i].start - it->pos)) > 1e-6 || m[i].name != B[i].label || m[i].color != B[i].color) return false;
  return true;
}

// Nothing on the main timeline but the one lyric item: no project markers, one item (an
// empty MIDI item, no notes text), showing only the block labels as take markers.
static bool OnlyTheLyricItem()
{
  if (!g_markers.empty() || LyricTrack()->items.size() != 1) return false;
  FakeItem* it = LyricItem();
  return it->take && ItemStr(it, "P_NOTES").empty() && !ItemStr(it, "P_EXT:LYRD").empty() && MarksMatchBlocks();
}

// The item starts at the first block and doesn't end before the last.
static bool ItemFits()
{
  FakeItem* it = LyricItem();
  if (!it || g_doc.blocks.empty()) return false;
  return Near(it->pos, g_doc.blocks.front().start) && Near(it->pos + it->len, g_doc.blocks.back().end);   // exactly
}

static bool NoOverlaps()
{
  for (size_t k = 0; k + 1 < g_doc.blocks.size(); k++)
    if (g_doc.blocks[k].end > g_doc.blocks[k + 1].start + 1e-6 || g_doc.blocks[k].start >= g_doc.blocks[k + 1].start) return false;
  for (auto& b : g_doc.blocks)
  {
    if (b.lines.empty() || !Near(b.lines[0].pos, b.start)) return false;
    for (size_t i = 0; i < b.lines.size(); i++)
    {
      if (b.lines[i].pos < b.start - 1e-6 || b.lines[i].pos > b.end + 1e-6) return false;
      if (i && b.lines[i].pos < b.lines[i - 1].pos) return false;
    }
  }
  return true;
}

static bool NoPlaceholder()
{
  for (auto& b : g_doc.blocks) for (auto& l : b.lines) if (l.text.find("...") != std::string::npos) return false;
  return true;
}

int main()
{
  InstallFakes();
  g_cursor = 10.0;

  // --- new lyric track: one lyric item holding one empty INTRO block, nothing else on the timeline
  MediaTrack* t = LyricDoc::CreateLyricTrack(kProj);
  CHECK(t != nullptr);
  g_doc.Bind(kProj, t);
  CHECK(g_doc.blocks.empty());
  CHECK(LyricTrack()->items.empty());
  g_doc.AppendBlock();
  Sync();
  CHECK(g_doc.blocks.size() == 1 && g_doc.blocks[0].label == "INTRO");
  CHECK(g_doc.ToText() == "");
  CHECK(OnlyTheLyricItem());
  CHECK(Near(LyricItem()->pos, 10.0) && Near(LyricItem()->len, 4.0));    // edit cursor, 2 bars (one line)
  CHECK(LyricDoc::IsLyricItem((MediaItem*)LyricItem()));
  CHECK(NoPlaceholder());
  FakeItem* theItem = LyricItem();

  // --- empty text keeps the block; typing fills it; still one item
  g_doc.ApplyText("", nullptr); Sync();
  CHECK(g_doc.blocks.size() == 1);
  g_doc.ApplyText("hello there\nsecond line", nullptr); Sync();
  CHECK(g_doc.blocks.size() == 1 && g_doc.blocks[0].lines.size() == 2);
  CHECK(OnlyTheLyricItem() && LyricItem() == theItem);

  // --- typing is one coalesced undo point, not one per keystroke
  int undo0 = g_undoPoints;
  g_doc.ApplyText("hello there\nsecond line!", nullptr);
  g_doc.ApplyText("hello there\nsecond line!!", nullptr);
  CHECK(g_undoPoints == undo0);
  g_doc.FlushUndo(true);
  CHECK(g_undoPoints == undo0 + 1);

  // --- several blocks: labels, order, auto-fit item, no overlaps, no markers
  g_doc.ApplyText("a1\na2\n\nb1\n\nc1\nc2", nullptr); Sync();
  CHECK(g_doc.blocks.size() == 3);
  CHECK(g_doc.blocks[0].label == "INTRO" && g_doc.blocks[1].label == "VERSE" && g_doc.blocks[2].label == "OUTRO");
  CHECK(OnlyTheLyricItem() && LyricItem() == theItem);
  CHECK(ItemFits());
  CHECK(NoOverlaps());

  // --- the document round-trips through the item (as a project reload would)
  {
    std::vector<LyricBlock> before = g_doc.blocks;
    g_doc.Bind(kProj, t);
    CHECK(g_doc.blocks.size() == before.size());
    for (size_t i = 0; i < before.size() && i < g_doc.blocks.size(); i++)
    {
      CHECK(g_doc.blocks[i].label == before[i].label && g_doc.blocks[i].color == before[i].color);
      CHECK(Near(g_doc.blocks[i].start, before[i].start) && Near(g_doc.blocks[i].end, before[i].end));
      CHECK(g_doc.blocks[i].Text() == before[i].Text());
    }
  }

  // --- moving the item in REAPER moves the lyrics with it
  {
    double s0 = g_doc.blocks[1].start, l0 = g_doc.blocks[1].lines[0].pos;
    std::string doc0 = ItemStr(theItem, "P_EXT:LYRD");
    theItem->pos += 5.0; Bump(); Sync();
    CHECK(Near(g_doc.blocks[1].start, s0 + 5.0) && Near(g_doc.blocks[1].lines[0].pos, l0 + 5.0));
    CHECK(ItemStr(theItem, "P_EXT:LYRD") == doc0);   // stored relative: nothing rewritten
    CHECK(ItemFits());
  }
  // --- dragging the left edge: snaps back to the first block, lyrics stay put
  {
    double s0 = g_doc.blocks[0].start, end0 = theItem->pos + theItem->len;
    theItem->pos += 2.0; theItem->len -= 2.0; Bump(); Sync();
    CHECK(Near(g_doc.blocks[0].start, s0));
    CHECK(Near(theItem->pos, s0) && Near(theItem->pos + theItem->len, end0));
  }
  // --- right edge: always snaps back to the end of the last block (no empty space at the end)
  {
    double lastEnd = g_doc.blocks.back().end;
    theItem->len = (lastEnd - theItem->pos) - 3.0; Bump(); Sync();
    CHECK(Near(theItem->pos + theItem->len, lastEnd));
    theItem->len = (lastEnd - theItem->pos) + 6.0; Bump(); Sync();
    CHECK(Near(theItem->pos + theItem->len, lastEnd));
    CHECK(Near(g_doc.blocks.back().end, lastEnd));
    CHECK(ItemFits());
    // the item follows the blocks: a longer last block, or a gap between two blocks
    g_doc.DragBlockEdge((int)g_doc.blocks.size() - 1, false, lastEnd + 4.0); Sync();
    CHECK(Near(g_doc.blocks.back().end, lastEnd + 4.0) && ItemFits());
    double end1 = g_doc.blocks.back().end;
    g_doc.DragBlockTo(1, g_doc.blocks[1].start + 2.0); Sync();
    CHECK(ItemFits());
    g_doc.DragBlockEdge((int)g_doc.blocks.size() - 1, false, end1 - 2.0); Sync();
    CHECK(Near(g_doc.ItemEnd(), g_doc.blocks.back().end) && ItemFits());   // shorter last block: item shrinks too
    // a project saved with space after the last block (earlier builds) is trimmed when read
    theItem->len += 5.0; Bump();
    g_doc.Bind(kProj, g_doc.track);
    CHECK(ItemFits());
  }

  // --- undo/redo in REAPER (item state restored) is picked up
  {
    std::string doc0 = ItemStr(theItem, "P_EXT:LYRD");
    double pos0 = theItem->pos, len0 = theItem->len;
    g_doc.SetLabel(1, "BRIDGE", true); Sync();
    CHECK(g_doc.blocks[1].label == "BRIDGE");
    theItem->str["P_EXT:LYRD"] = doc0; theItem->pos = pos0; theItem->len = len0; Bump();   // "undo"
    Sync();
    CHECK(g_doc.blocks[1].label == "VERSE");
  }

  // --- sequencer drags never overlap a neighbour
  {
    double nextStart = g_doc.blocks[1].start;
    g_doc.DragBlockEdge(0, false, nextStart + 4.0);
    CHECK(Near(g_doc.blocks[0].end, nextStart));
    g_doc.DragBlockEdge(0, false, g_doc.blocks[0].start + 0.01);   // can't shrink below a quarter bar
    CHECK(Near(g_doc.blocks[0].Len(), g_doc.MinBlockLen()));
    g_doc.DragBlockTo(1, g_doc.blocks[0].start);                   // into the previous block: stops at its end
    CHECK(Near(g_doc.blocks[1].start, g_doc.blocks[0].end));
    g_doc.DragBlockTo(1, 1e6);                                     // past the next block: stops before it
    CHECK(Near(g_doc.blocks[1].end, g_doc.blocks[2].start));
    g_doc.DragBlockEdge(2, true, g_doc.blocks[1].start);           // left edge into the previous block
    CHECK(Near(g_doc.blocks[2].start, g_doc.blocks[1].end));
    LyricBlock& c = g_doc.blocks[2];
    g_doc.DragLineStart(2, 1, c.start - 10);                       // line start can't pass the line before
    CHECK(Near(g_doc.blocks[2].lines[1].pos, g_doc.blocks[2].lines[0].pos + g_doc.MinLineLen()));
    g_doc.DragLineStart(2, 1, c.end + 10);                         // ...or the block end
    CHECK(Near(g_doc.blocks[2].lines[1].pos, g_doc.blocks[2].end - g_doc.MinLineLen()));
    int u = g_undoPoints;
    g_doc.EndDrag("Lyrics: move line");
    CHECK(g_undoPoints == u + 1);
    Sync();
    CHECK(NoOverlaps());
    CHECK(ItemFits());
    CHECK(OnlyTheLyricItem());
  }

  // --- ripple resize (the sequencer default): the next block snaps to the new end and later
  //     blocks follow, keeping the gaps between them
  {
    auto& B = g_doc.blocks;
    g_doc.DragBlockEdge(2, true, B[2].start + 1.0);                // open a 1 s gap between blocks 1 and 2
    double gap12 = B[2].start - B[1].end;
    CHECK(Near(gap12, 1.0));
    CHECK(B[1].start - B[0].end > 1.0);                            // and there's a gap between 0 and 1
    double len1 = B[1].Len(), len2 = B[2].Len();
    std::vector<double> rel1;
    for (auto& l : B[1].lines) rel1.push_back(l.pos - B[1].start);

    g_doc.DragBlockEdge(0, false, B[0].end + 1.5, true);           // extend block 0
    CHECK(Near(B[1].start, B[0].end));                             // next block snapped against it
    CHECK(Near(B[2].start - B[1].end, gap12));                     // later gap kept
    CHECK(Near(B[1].Len(), len1) && Near(B[2].Len(), len2));       // lengths kept
    for (size_t k = 0; k < rel1.size() && k < B[1].lines.size(); k++) CHECK(Near(B[1].lines[k].pos - B[1].start, rel1[k]));

    double end2 = B[2].end;
    g_doc.DragBlockEdge(0, false, B[0].start + 0.01, true);        // shrink block 0: the rest follow back
    CHECK(Near(B[0].Len(), g_doc.MinBlockLen()));
    CHECK(Near(B[1].start, B[0].end));
    CHECK(Near(B[2].start - B[1].end, gap12));
    CHECK(B[2].end < end2);
    g_doc.EndDrag("Lyrics: resize block");
    Sync();
    CHECK(NoOverlaps() && ItemFits() && OnlyTheLyricItem());
  }

  // --- reorder: moves blocks, keeps them packed and non-overlapping
  {
    std::string first = g_doc.blocks[0].Text();
    g_doc.MoveBlock(0, 3); Sync();
    CHECK(g_doc.blocks.back().Text() == first);
    CHECK(g_doc.ToText() == "b1\n\nc1\nc2\n\na1\na2");
    CHECK(NoOverlaps() && ItemFits());
  }

  // --- deleting the only block keeps it (empty); deleting one of several removes it
  g_doc.DeleteBlock(1); Sync();
  CHECK(g_doc.blocks.size() == 2);
  g_doc.ApplyText("only", nullptr); Sync();
  g_doc.DeleteBlock(0); Sync();
  CHECK(g_doc.blocks.size() == 1 && g_doc.ToText() == "");
  CHECK(OnlyTheLyricItem());

  // --- text with quotes, backslashes, UTF-8 survives storage exactly
  {
    std::string txt = "say \"hi\" \\ there\nna\xc3\xafve \xe2\x99\xaa";
    g_doc.ApplyText(txt, nullptr); Sync();
    g_doc.Bind(kProj, t);
    CHECK(g_doc.ToText() == txt);
    std::vector<LyricBlock> v;
    CHECK(LyricDoc::Deserialize(LyricDoc::Serialize(g_doc.blocks, 3.25), 3.25, v) && v.size() == 1 && v[0].Text() == txt);
  }

  // --- deleting the lyric item in REAPER empties the doc; typing brings one back
  DeleteTrackMediaItem(t, (MediaItem*)theItem); Sync();
  CHECK(g_doc.blocks.empty());
  g_doc.ApplyText("new start", nullptr); Sync();
  CHECK(g_doc.blocks.size() == 1 && g_doc.blocks[0].label == "INTRO");
  CHECK(OnlyTheLyricItem());

  // --- a project from the previous build (an item per block + a marker per line) is converted
  {
    MediaTrack* t2 = LyricDoc::CreateLyricTrack(kProj);
    auto oldItem = [&](double pos, double len, const char* label, const char* textEsc, std::vector<std::pair<int, double>> marks) {
      FakeItem* i = I(AddMediaItemToTrack(t2));
      i->pos = pos; i->len = len; i->color = 0x1000000 | 255;
      i->str["P_NAME"] = label; i->str["P_EXT:LYRT"] = textEsc;
      std::string ids;
      for (auto& m : marks) { g_markers[m.first] = { m.second, "x", 0 }; if (!ids.empty()) ids += ","; ids += std::to_string(m.first); }
      i->str["P_EXT:LYRM"] = ids;
    };
    oldItem(40, 8, "VERSE", "v one\\nv two", { { 11, 40.0 }, { 12, 44.5 } });
    oldItem(20, 8, "INTRO", "intro line", { { 13, 20.0 } });
    Bump();
    g_doc.Bind(kProj, t2);
    CHECK(g_doc.blocks.size() == 2);
    CHECK(g_doc.ToText() == "intro line\n\nv one\nv two");
    CHECK(g_doc.blocks[1].label == "VERSE" && Near(g_doc.blocks[1].lines[1].pos, 44.5));
    CHECK(OnlyTheLyricItem());           // old items replaced by the one lyric item, markers gone
    CHECK(ItemFits() && Near(LyricItem()->pos, 20.0));
  }

  // --- default size: 2 bars per line; default-sized blocks grow/shrink as lines are typed and
  //     carry the later blocks along (gaps kept); a block sized by hand stays put
  {
    const double slot = 4.0;   // 2 bars of 2 s
    MediaTrack* t3 = LyricDoc::CreateLyricTrack(kProj);
    g_cursor = 100.0;
    g_doc.Bind(kProj, t3);
    g_doc.AppendBlock(); Sync();
    auto& B = g_doc.blocks;
    CHECK(B.size() == 1 && Near(B[0].Len(), slot));
    g_doc.ApplyText("one\ntwo\nthree", nullptr); Sync();
    CHECK(Near(B[0].Len(), 3 * slot));
    CHECK(Near(B[0].lines[1].pos, B[0].start + slot) && Near(B[0].lines[2].pos, B[0].start + 2 * slot));
    g_doc.ApplyText("one\ntwo\nthree\n\nfour\n\nfive\nsix", nullptr); Sync();
    CHECK(B.size() == 3);
    CHECK(Near(B[1].start, B[0].end) && Near(B[1].Len(), slot));
    CHECK(Near(B[2].start, B[1].end) && Near(B[2].Len(), 2 * slot));
    CHECK(ItemFits() && Near(LyricItem()->pos + LyricItem()->len, B[2].end));

    // open a gap before the last block, then grow the first: later blocks move, gap kept
    g_doc.DragBlockTo(2, B[2].start + 3.0); g_doc.EndDrag("move");
    double gap = B[2].start - B[1].end;
    CHECK(Near(gap, 3.0));
    g_doc.ApplyText("one\ntwo\nthree\nthree-and-a-half\n\nfour\n\nfive\nsix", nullptr); Sync();
    CHECK(Near(B[0].Len(), 4 * slot));
    CHECK(Near(B[1].start, B[0].end));
    CHECK(Near(B[2].start - B[1].end, gap));
    // shrinking pulls them back
    g_doc.ApplyText("one\n\nfour\n\nfive\nsix", nullptr); Sync();
    CHECK(Near(B[0].Len(), slot) && Near(B[1].start, B[0].end) && Near(B[2].start - B[1].end, gap));
    CHECK(NoOverlaps() && ItemFits());

    // a new block typed between two touching blocks gets 2 bars per line and pushes the rest
    g_doc.DragBlockTo(2, B[1].end); g_doc.EndDrag("move");   // close the gap: blocks 1 and 2 touch
    double s2 = B[2].start;
    g_doc.ApplyText("one\n\nfour\n\nnew a\nnew b\n\nfive\nsix", nullptr); Sync();
    CHECK(B.size() == 4 && B[2].Text() == "new a\nnew b");
    CHECK(Near(B[2].start, B[1].end) && Near(B[2].Len(), 2 * slot));
    CHECK(Near(B[3].start, s2 + 2 * slot));
    CHECK(NoOverlaps() && ItemFits());

    // sized by hand: no longer follows its line count
    g_doc.DragBlockEdge(1, false, B[1].end + 2.0, false); g_doc.EndDrag("resize");
    double len1 = B[1].Len(), s3 = B[2].start;
    g_doc.ApplyText("one\n\nfour\nfour b\n\nnew a\nnew b\n\nfive\nsix", nullptr); Sync();
    CHECK(Near(B[1].Len(), len1) && B[1].lines.size() == 2);
    CHECK(Near(B[2].start, s3));
    // the default-sized flag survives storage
    g_doc.Bind(kProj, t3);
    CHECK(B[0].autoLen && !B[1].autoLen && B[2].autoLen);
    CHECK(OnlyTheLyricItem());
  }

  // --- label colours: defaults by label; renaming never recolours; a set colour sticks
  {
    auto rgb = [](int r, int g, int b) { return ColorToNative(r, g, b) | 0x1000000; };
    CHECK(LyricDoc::DefaultColorForLabel("INTRO") == rgb(110, 182, 236));    // sky blue
    CHECK(LyricDoc::DefaultColorForLabel("VERSE") == rgb(128, 200, 128));    // light green
    CHECK(LyricDoc::DefaultColorForLabel("CHORUS") == rgb(246, 176, 98));    // light orange
    CHECK(LyricDoc::DefaultColorForLabel("OUTRO") == rgb(141, 141, 141));    // grey

    MediaTrack* t4 = LyricDoc::CreateLyricTrack(kProj);
    g_cursor = 300.0;
    g_doc.Bind(kProj, t4);
    g_doc.AppendBlock(); Sync();
    auto& B = g_doc.blocks;
    g_doc.ApplyText("a\n\nb\n\nc\n\nd", nullptr); Sync();
    CHECK(B.size() == 4 && B[1].label == "VERSE" && B[2].label == "CHORUS" && B[3].label == "OUTRO");
    for (auto& b : B) CHECK(!b.customColor && b.color == LyricDoc::DefaultColorForLabel(b.label));

    // rename VERSE -> "Verse 1" and -> CHORUS: stays light green
    int green = B[1].color;
    g_doc.SetLabel(1, "Verse 1", true); Sync();
    CHECK(B[1].label == "Verse 1" && B[1].color == green);
    g_doc.SetLabel(1, "CHORUS", true); Sync();
    CHECK(B[1].label == "CHORUS" && B[1].color == green);
    // typing OUTRO's name by hand keeps it grey
    int grey = B[3].color;
    g_doc.SetLabel(3, "BRIDGE", true); Sync();
    CHECK(B[3].color == grey);
    // ...but picking a preset label switches to that preset's colour, even over a picked colour
    g_doc.ApplyLabelPreset(3, "CHORUS"); Sync();
    CHECK(B[3].label == "CHORUS" && B[3].customLabel && B[3].color == LyricDoc::DefaultColorForLabel("CHORUS"));
    g_doc.SetColor(3, rgb(10, 20, 30), true); Sync();
    g_doc.ApplyLabelPreset(3, "OUTRO"); Sync();
    CHECK(B[3].label == "OUTRO" && B[3].color == grey && !B[3].customColor);
    g_doc.ApplyLabelPreset(3, "BRIDGE"); Sync();
    int bridge = LyricDoc::DefaultColorForLabel("BRIDGE");
    CHECK(B[3].color == bridge);
    // the Automatic preset: automatic label and its colour (VERSE = light green)
    g_doc.ApplyLabelPreset(1, ""); Sync();
    CHECK(B[1].label == "VERSE" && !B[1].customLabel && B[1].color == green);
    // a preset colour survives later typing and storage
    g_doc.ApplyText("a\n\nb\n\nx\n\nc\n\nd", nullptr); Sync();
    CHECK(B.size() == 5 && B[1].color == green && B[4].label == "BRIDGE" && B[4].color == bridge);
    g_doc.Bind(kProj, t4);
    CHECK(B[4].label == "BRIDGE" && B[4].color == bridge);
    // a user-picked colour survives a rename and storage
    int pink = rgb(230, 120, 170);
    g_doc.SetColor(2, pink, true); Sync();
    g_doc.SetLabel(2, "HOOK", true); Sync();
    CHECK(B[2].label == "HOOK" && B[2].color == pink);
    g_doc.Bind(kProj, t4);
    CHECK(B[2].color == pink && B[1].color == green && B[4].color == bridge);
  }

  // --- on the timeline the lyric item is a normal (empty MIDI) item showing only the block
  //     labels, as take markers at each block start in the block colour; no lyric text
  {
    MediaTrack* t5 = LyricDoc::CreateLyricTrack(kProj);
    g_cursor = 500.0;
    g_doc.Bind(kProj, t5);
    g_doc.AppendBlock(); Sync();
    FakeItem* li = LyricItem();
    CHECK(li->take && !li->loop && li->take->str["P_NAME"].empty());     // no take name, no looping
    CHECK(li->take->marks.size() == 1 && li->take->marks[0].name == "INTRO" && Near(li->take->marks[0].srcpos, 0));
    g_doc.ApplyText("hello\nworld\n\nsecond", nullptr); Sync();
    CHECK(MarksMatchBlocks() && li->take->marks[1].name == "VERSE");
    CHECK(ItemStr(li, "P_NOTES").empty());                                // no lyrics text on the item
    g_doc.SetLabel(1, "HOOK", true); Sync();
    CHECK(MarksMatchBlocks() && li->take->marks[1].name == "HOOK");
    g_doc.SetColor(1, ColorToNative(1, 2, 3) | 0x1000000, true); Sync();
    CHECK(MarksMatchBlocks() && li->take->marks[1].color == (ColorToNative(1, 2, 3) | 0x1000000));
    g_doc.DragBlockTo(1, g_doc.blocks[1].start + 3.0); Sync();
    CHECK(MarksMatchBlocks());                                            // moved block: marker moved
    // item covers every block, so every marker is inside it
    CHECK(Near(li->pos, g_doc.blocks.front().start) && li->pos + li->len >= g_doc.blocks.back().end - 1e-6);
    // the user moves the item in REAPER: markers travel with it
    li->pos += 10.0; Bump(); Sync();
    CHECK(Near(g_doc.blocks[0].start, li->pos) && MarksMatchBlocks());
    // nothing changed: markers are not rewritten
    int st = g_state; g_doc.Bind(kProj, t5);
    CHECK(g_state == st && MarksMatchBlocks());

    // a lyric item from an earlier build (empty item, no take) is swapped for one with a take
    std::string doc = ItemStr(li, "P_EXT:LYRD");
    double p0 = li->pos, l0 = li->len;
    std::string text0 = g_doc.ToText();
    DeleteTrackMediaItem(t5, (MediaItem*)li);
    FakeItem* old = I(AddMediaItemToTrack(t5));
    old->pos = p0; old->len = l0; old->sel = true;
    old->str["P_EXT:LYRD"] = doc; old->str["P_NOTES"] = "INTRO: hello / world";
    g_doc.Bind(kProj, t5);
    li = LyricItem();
    CHECK(li != old && li->take && li->sel && Near(li->pos, p0) && Near(li->len, l0));
    CHECK(ItemStr(li, "P_EXT:LYRD") == doc && g_doc.ToText() == text0);
    CHECK(OnlyTheLyricItem());
  }

  // --- lyric track look: theme layout, hidden in mixer, fixed height, colour, no recording
  {
    std::vector<std::string> lay = TrackStyle::TcpLayouts();
    CHECK(lay.size() == 4 && lay[2] == "Minimal");                              // hi-DPI variant skipped
    CHECK(TrackStyle::PickLayout(TrackStyle::kAuto, lay) == "Minimal");         // automatic
    CHECK(TrackStyle::PickLayout("", lay) == "");                               // theme default
    CHECK(TrackStyle::PickLayout("Reduced", lay) == "Reduced");
    CHECK(TrackStyle::PickLayout(TrackStyle::kAuto, { "A", "B", "C" }) == "");  // no Minimal: default

    // a new lyric track is styled straight away
    FakeTrack* nt = T(LyricDoc::CreateLyricTrack(kProj));
    CHECK(nt->str["P_TCP_LAYOUT"] == "Minimal");
    CHECK(nt->num.count("B_SHOWINMIXER") && nt->num["B_SHOWINMIXER"] == 0);
    CHECK(nt->num["I_HEIGHTOVERRIDE"] == TrackStyle::Height() && nt->num["B_HEIGHTLOCK"] == 1);
    CHECK(nt->num["I_RECARM"] == 0 && nt->num["I_RECMON"] == 0 && nt->num["I_RECINPUT"] == -1);
    CHECK((int)nt->num["I_CUSTOMCOLOR"] == TrackStyle::DefaultColor());
    CHECK(TrackStyle::IsStyled((MediaTrack*)nt));

    // an older lyric track (no style yet, user colour + icon) is styled once, keeping both
    g_tracks.push_back(std::make_unique<FakeTrack>());
    FakeTrack* old = g_tracks.back().get();
    old->str["P_EXT:LYR"] = "1"; old->str["P_ICON"] = "mine.png"; old->num["I_CUSTOMCOLOR"] = 0x1000000 | 123;
    CHECK(TrackStyle::StyleNewTracks(kProj) == 1);
    CHECK(old->str["P_TCP_LAYOUT"] == "Minimal" && old->num["B_SHOWINMIXER"] == 0 && old->num["B_HEIGHTLOCK"] == 1);
    CHECK(old->str["P_ICON"] == "mine.png" && (int)old->num["I_CUSTOMCOLOR"] == (0x1000000 | 123));
    // ...and then left alone: a height the user changes later is not reset
    old->num["I_HEIGHTOVERRIDE"] = 120;
    CHECK(TrackStyle::StyleNewTracks(kProj) == 0 && old->num["I_HEIGHTOVERRIDE"] == 120);

    // a layout change reaches every lyric track, and ordinary tracks are never touched
    g_tracks.push_back(std::make_unique<FakeTrack>());
    FakeTrack* audio = g_tracks.back().get();
    TrackStyle::SetLayoutSetting("Reduced", false);
    TrackStyle::ApplyAll(kProj, true);
    CHECK(nt->str["P_TCP_LAYOUT"] == "Reduced" && old->str["P_TCP_LAYOUT"] == "Reduced");
    CHECK(old->num["I_HEIGHTOVERRIDE"] == 120);                                  // layout only
    TrackStyle::StyleNewTracks(kProj);
    TrackStyle::ApplyAll(kProj, false);
    CHECK(audio->str.empty() && audio->num.empty());
  }

  // --- reordering (dragging a label) keeps every block's label and colour
  {
    MediaTrack* t5 = LyricDoc::CreateLyricTrack(kProj);
    g_cursor = 500.0;
    g_doc.Bind(kProj, t5);
    g_doc.AppendBlock(); Sync();
    auto& B = g_doc.blocks;
    g_doc.ApplyText("a\n\nb\n\nc\n\nd", nullptr); Sync();
    std::vector<std::string> labels; std::vector<int> colors;
    for (auto& b : B) { labels.push_back(b.label); colors.push_back(b.color); }
    CHECK(labels.size() == 4 && labels[1] == "VERSE" && labels[2] == "CHORUS");
    g_doc.MoveBlock(2, 1); Sync();   // CHORUS above VERSE
    CHECK(B[1].Text() == "c" && B[1].label == labels[2] && B[1].color == colors[2]);
    CHECK(B[2].Text() == "b" && B[2].label == labels[1] && B[2].color == colors[1]);
    g_doc.MoveBlock(0, 4); Sync();   // INTRO to the end
    CHECK(B[3].Text() == "a" && B[3].label == "INTRO" && B[3].color == colors[0]);
    CHECK(B[0].Text() == "c" && B[0].label == "CHORUS" && B[2].Text() == "d" && B[2].label == "OUTRO");
    CHECK(NoOverlaps() && ItemFits());

    // the editor drag: drop index from the ghost, and the other labels shunting to make room
    // (three blocks of 1, 2 and 1 lines at 20 px a line, a blank line after each)
    std::vector<std::pair<int, int>> spans = { { 0, 20 }, { 40, 80 }, { 100, 120 } };
    std::vector<int> heights = { 40, 60, 40 };
    CHECK(DragInsertIndex(spans, 0, 10) == 0);     // not past anything: stays
    CHECK(DragInsertIndex(spans, 0, 70) == 2);     // past the middle of block 1: after it
    CHECK(DragInsertIndex(spans, 0, 115) == 3);    // to the end
    CHECK(DragInsertIndex(spans, 2, 50) == 1);     // last one up, above block 1
    int slot = -1;
    auto tg = DragShuntTargets(heights, 0, 2, 0, &slot);
    CHECK(tg[1] == 0 && slot == 60 && tg[2] == 100);   // block 1 slides up into the gap
    tg = DragShuntTargets(heights, 2, 1, 0, &slot);
    CHECK(tg[0] == 0 && slot == 40 && tg[1] == 80);    // block 1 slides down to make room
    tg = DragShuntTargets(heights, 1, 1, 0, &slot);
    CHECK(tg[0] == 0 && slot == 40 && tg[2] == 100);   // no move: everything at home
  }

  // --- copy / paste: block data only when pasted on a clean line
  {
    // where a paste lands
    std::vector<std::string> L = { "a1", "a2", "", "b1", "", "" };
    std::string t = "x\ny"; int fp = -1;
    CHECK(!PrepareBlockPaste(L, 0, t, &fp) && t == "x\ny");          // inside a block: text only
    CHECK(!PrepareBlockPaste(L, 3, t, &fp));
    t = "\nx\n"; CHECK(PrepareBlockPaste(L, 5, t, &fp) && t == "x" && fp == 2);       // after the last block
    t = "x"; CHECK(PrepareBlockPaste(L, 2, t, &fp) && t == "\nx\n" && fp == 1);       // the one blank line between two blocks
    t = "x"; CHECK(PrepareBlockPaste(L, 4, t, &fp) && t == "\nx" && fp == 2);

    // which blocks a selection copies from
    std::vector<std::string> lines = { "one", "two", "", "three", "four", "", "five" };
    std::vector<std::pair<int, int>> spans = { { 0, 1 }, { 3, 4 }, { 6, 6 } };
    std::vector<std::string> texts = { "one\ntwo", "three\nfour", "five" };
    auto c1 = CopiedBlocks(lines, spans, texts, 3, 0, 4, 4);
    CHECK(c1.size() == 1 && c1[0].block == 1 && c1[0].whole);
    auto c2 = CopiedBlocks(lines, spans, texts, 1, 0, 3, 5);       // "two" .. "three"
    CHECK(c2.size() == 2 && c2[0].block == 0 && !c2[0].whole && c2[1].block == 1 && !c2[1].whole);
    CHECK(CopiedBlocks(lines, spans, texts, 1, 3, 3, 0).empty());   // only line breaks
    auto c4 = CopiedBlocks(lines, spans, texts, 0, 0, 6, 4);
    CHECK(c4.size() == 3 && c4[0].whole && c4[1].whole && c4[2].whole);

    // the model side
    MediaTrack* t6 = LyricDoc::CreateLyricTrack(kProj);
    g_cursor = 700.0;
    g_doc.Bind(kProj, t6);
    g_doc.AppendBlock(); Sync();
    auto& B = g_doc.blocks;
    g_doc.ApplyText("a\n\nb1\nb2\n\nc", nullptr); Sync();
    int pink = ColorToNative(230, 120, 170) | 0x1000000;
    g_doc.SetLabel(1, "HOOK", true); g_doc.SetColor(1, pink, true); Sync();
    g_doc.DragBlockEdge(1, false, B[1].end + 1.5, true); g_doc.EndDrag("resize");
    g_doc.DragLineStart(1, 1, B[1].start + 1.0); g_doc.EndDrag("line");
    LyricBlock hook = B[1];

    // a whole block pasted after the last one: same label, colour, length and line timing
    PastedBlock w = PastedBlock::From(hook, true); w.para = 3;
    std::vector<PastedBlock> pv = { w };
    g_doc.ApplyText("a\n\nb1\nb2\n\nc\n\nb1\nb2", nullptr, &pv); Sync();
    CHECK(B.size() == 4);
    CHECK(Near(B[1].start, hook.start) && Near(B[1].end, hook.end) && B[1].label == "HOOK");   // original untouched
    CHECK(B[3].label == "HOOK" && B[3].color == pink && B[3].customLabel && B[3].customColor);
    CHECK(Near(B[3].Len(), hook.Len()) && Near(B[3].lines[1].pos - B[3].start, hook.lines[1].pos - hook.start));
    CHECK(Near(B[3].start, B[2].end));
    CHECK(NoOverlaps() && ItemFits());

    // pasted before the block it was copied from: the original keeps its own timing
    double s1 = B[1].start;
    w.para = 0; pv = { w };
    g_doc.ApplyText("b1\nb2\n\na\n\nb1\nb2\n\nc\n\nb1\nb2", nullptr, &pv); Sync();
    CHECK(B.size() == 5 && B[0].label == "HOOK" && B[0].color == pink && B[2].label == "HOOK" && Near(B[2].start, s1));
    CHECK(NoOverlaps() && ItemFits());

    // part of a block: label and colour, default size (2 bars per line)
    PastedBlock part = PastedBlock::From(hook, false); part.para = 5;
    pv = { part };
    g_doc.ApplyText(g_doc.ToText() + "\n\nb2", nullptr, &pv); Sync();
    CHECK(B.size() == 6 && B[5].label == "HOOK" && B[5].color == pink && B[5].autoLen && Near(B[5].Len(), g_doc.LineSlot()));

    // plain text (pasted inside a block, or text from elsewhere): no block data
    g_doc.ApplyText(g_doc.ToText() + "\n\nb1\nb2", nullptr); Sync();
    CHECK(B.size() == 7 && B[6].label != "HOOK" && !B[6].customLabel && !B[6].customColor);
    int hooks = 0; for (auto& b : B) if (b.label == "HOOK") hooks++;
    CHECK(hooks == 4);

    // pasted into an empty lyric track: takes the place of its empty first block
    MediaTrack* t7 = LyricDoc::CreateLyricTrack(kProj);
    g_cursor = 900.0;
    g_doc.Bind(kProj, t7);
    g_doc.AppendBlock(); Sync();
    double s0 = B[0].start;
    w.para = 0; pv = { w };
    g_doc.ApplyText("b1\nb2", nullptr, &pv); Sync();
    CHECK(B.size() == 1 && B[0].label == "HOOK" && Near(B[0].start, s0) && Near(B[0].Len(), hook.Len()));
  }

  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}

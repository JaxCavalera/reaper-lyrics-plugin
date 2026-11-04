// Model tests: runs the real LyricDoc (src/model.cpp) against an in-memory fake of the
// REAPER API (one project, tracks, items, markers). Build with -DLYRICS_BUILD_TESTS=ON.
#define REAPERAPI_IMPLEMENT
#include "api.h"
#include "model.h"
#include "util.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ------------------------------------------------------------------ fake REAPER

struct FakeItem { double pos = 0, len = 0; int color = 0; std::map<std::string, std::string> str; };
struct FakeTrack { std::map<std::string, std::string> str; std::vector<FakeItem*> items; GUID guid; };
struct FakeMarker { double pos; std::string name; int color; };

static std::vector<std::unique_ptr<FakeTrack>> g_tracks;
static std::vector<std::unique_ptr<FakeItem>> g_allItems;
static std::map<int, FakeMarker> g_markers;
static int g_state = 1, g_itemSerial = 0;
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
  Undo_EndBlock2 = [](ReaProject*, const char*, int) {};
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
    return 0;
  };
  SetMediaTrackInfo_Value = [](MediaTrack*, const char*, double) { return true; };
  GetTrackGUID = [](MediaTrack* t) { return &T(t)->guid; };
  guidToString = [](const GUID* g, char* out) { sprintf(out, "{TRACK-%08X}", (unsigned)g->Data1); };
  AddMediaItemToTrack = [](MediaTrack* t) {
    g_allItems.push_back(std::make_unique<FakeItem>());
    FakeItem* i = g_allItems.back().get();
    char g[64]; sprintf(g, "{ITEM-%d}", ++g_itemSerial); i->str["GUID"] = g;
    T(t)->items.push_back(i); Bump();
    return (MediaItem*)i;
  };
  DeleteTrackMediaItem = [](MediaTrack* t, MediaItem* it) {
    auto& v = T(t)->items;
    for (size_t k = 0; k < v.size(); k++) if (v[k] == I(it)) { v.erase(v.begin() + k); Bump(); return true; }
    return false;
  };
  CountTrackMediaItems = [](MediaTrack* t) { return (int)T(t)->items.size(); };
  GetTrackMediaItem = [](MediaTrack* t, int i) { return (MediaItem*)T(t)->items[i]; };
  GetSetMediaItemInfo_String = [](MediaItem* it, const char* k, char* buf, bool set) { return StrGetSet(I(it)->str, k, buf, set); };
  GetMediaItemInfo_Value = [](MediaItem* it, const char* k) -> double {
    if (!strcmp(k, "D_POSITION")) return I(it)->pos;
    if (!strcmp(k, "D_LENGTH")) return I(it)->len;
    if (!strcmp(k, "I_CUSTOMCOLOR")) return I(it)->color;
    return 0;
  };
  SetMediaItemInfo_Value = [](MediaItem* it, const char* k, double v) {
    if (!strcmp(k, "D_POSITION")) I(it)->pos = v;
    else if (!strcmp(k, "D_LENGTH")) I(it)->len = v;
    else if (!strcmp(k, "I_CUSTOMCOLOR")) I(it)->color = (int)v;
    Bump(); return true;
  };
  AddProjectMarker2 = [](ReaProject*, bool, double pos, double, const char* name, int, int color) {
    int id = 1; while (g_markers.count(id)) id++;
    g_markers[id] = { pos, name, color }; Bump(); return id;
  };
  SetProjectMarker4 = [](ReaProject*, int id, bool, double pos, double, const char* name, int color, int) {
    if (!g_markers.count(id)) return false;
    g_markers[id] = { pos, name, color }; Bump(); return true;
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
}

// ------------------------------------------------------------------ checks

LyricDoc g_doc;
static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) g_pass++; else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static FakeTrack* LyricTrack() { return T(g_doc.track); }
static std::string ItemStr(FakeItem* i, const char* k) { auto it = i->str.find(k); return it == i->str.end() ? "" : it->second; }

// No "..." placeholder anywhere: item notes, lyric ext state, marker names.
static bool NoPlaceholder()
{
  for (auto* i : LyricTrack()->items)
    if (ItemStr(i, "P_NOTES").find("...") != std::string::npos || ItemStr(i, "P_EXT:LYRT").find("...") != std::string::npos) return false;
  for (auto& m : g_markers) if (m.second.name.find("...") != std::string::npos) return false;
  return true;
}

// Timeline items show only their label: notes (which REAPER draws inside an empty item) stay empty.
static bool ItemsShowOnlyLabel()
{
  for (auto* i : LyricTrack()->items) if (!ItemStr(i, "P_NOTES").empty()) return false;
  return true;
}

// Sorted by position, every item ends at or before the next one starts.
static bool NoOverlaps()
{
  std::vector<FakeItem*> v = LyricTrack()->items;
  std::sort(v.begin(), v.end(), [](FakeItem* a, FakeItem* b) { return a->pos < b->pos; });
  for (size_t k = 0; k + 1 < v.size(); k++) if (v[k]->pos + v[k]->len > v[k + 1]->pos + 1e-6) return false;
  return true;
}

static void Sync() { if (g_doc.NeedsSync()) g_doc.SyncFromReaper(); }

int main()
{
  InstallFakes();
  g_cursor = 10.0;

  // --- a new lyric track starts with one empty INTRO block, no placeholder text
  MediaTrack* t = LyricDoc::CreateLyricTrack(kProj);
  CHECK(t != nullptr);
  g_doc.Bind(kProj, t);
  CHECK(g_doc.blocks.empty());
  g_doc.AppendBlock();
  Sync();
  CHECK(g_doc.blocks.size() == 1);
  CHECK(g_doc.blocks[0].label == "INTRO");
  CHECK(g_doc.ToText() == "");
  CHECK(LyricTrack()->items.size() == 1);
  CHECK(g_markers.size() == 1);
  CHECK(NoPlaceholder());
  std::string firstGuid = g_doc.blocks[0].guid;

  // --- empty editor text keeps the single block (it is not deleted, no placeholder)
  g_doc.ApplyText("", nullptr); Sync();
  CHECK(g_doc.blocks.size() == 1 && g_doc.blocks[0].guid == firstGuid);

  // --- typing fills the first block (same item, not a new one)
  g_doc.ApplyText("hello there\nsecond line", nullptr); Sync();
  CHECK(g_doc.blocks.size() == 1 && g_doc.blocks[0].guid == firstGuid);
  CHECK(g_doc.ToText() == "hello there\nsecond line");
  CHECK(g_markers.size() == 2);
  CHECK(ItemsShowOnlyLabel());
  CHECK(ItemStr(LyricTrack()->items[0], "P_NAME") == "INTRO");

  // --- clearing all text keeps the first block, now empty; extra line marker goes away
  g_doc.ApplyText("", nullptr); Sync();
  CHECK(g_doc.blocks.size() == 1 && g_doc.blocks[0].guid == firstGuid);
  CHECK(g_doc.ToText() == "");
  CHECK(g_markers.size() == 1);
  CHECK(NoPlaceholder());

  // --- several blocks, then clear everything: back to one block (the first)
  g_doc.ApplyText("a1\na2\n\nb1\n\nc1\nc2", nullptr); Sync();
  CHECK(g_doc.blocks.size() == 3);
  CHECK(g_doc.blocks[0].guid == firstGuid);
  CHECK(g_doc.blocks[0].label == "INTRO" && g_doc.blocks[1].label == "VERSE" && g_doc.blocks[2].label == "OUTRO");
  CHECK(NoOverlaps());
  CHECK(ItemsShowOnlyLabel());
  CHECK(NoPlaceholder());

  // --- dragging an item's right edge over the next block in REAPER gets trimmed back
  {
    FakeItem* a = I(g_doc.blocks[0].item);
    FakeItem* b = I(g_doc.blocks[1].item);
    a->len = (b->pos - a->pos) + 3.0; Bump();
    Sync();
    CHECK(NoOverlaps());
    CHECK(std::fabs((a->pos + a->len) - b->pos) < 1e-6);
  }
  // --- moving an item so it starts inside the previous block trims the previous block
  {
    FakeItem* b = I(g_doc.blocks[1].item);
    FakeItem* c = I(g_doc.blocks[2].item);
    c->pos = b->pos + 1.0; Bump();
    Sync();
    CHECK(NoOverlaps());
    CHECK(g_doc.blocks.size() == 3);
  }
  // --- dropping an item exactly on another block's start: pushed after, still no overlap
  {
    FakeItem* a = I(g_doc.blocks[0].item);
    FakeItem* c = I(g_doc.blocks[2].item);
    c->pos = a->pos; Bump();
    Sync();
    CHECK(NoOverlaps());
    for (auto& blk : g_doc.blocks) CHECK(blk.Len() > 0.1);
  }
  // --- markers stay inside their blocks after the trims
  for (auto& blk : g_doc.blocks)
    for (auto& l : blk.lines) CHECK(l.pos >= blk.start - 1e-6 && l.pos < blk.end + 1e-6);

  // --- lyrics typed into item notes in REAPER (or an older project) move into ext state
  {
    FakeItem* a = I(g_doc.blocks[0].item);
    a->str["P_NOTES"] = "from notes\nline two"; Bump();
    Sync();
    CHECK(g_doc.blocks[0].Text() == "from notes\nline two");
    CHECK(ItemsShowOnlyLabel());
    CHECK(ItemStr(a, "P_EXT:LYRT") == "from notes\\nline two");
  }
  // --- quotes/backslashes survive the ext-state round trip
  {
    std::string txt = g_doc.ToText();
    std::string first = "say \"hi\" \\ there";
    g_doc.ApplyText(first + txt.substr(txt.find('\n')), nullptr); Sync();
    g_doc.Bind(kProj, t);   // re-read everything from the fake project
    CHECK(g_doc.blocks[0].lines[0].text == first);
  }

  g_doc.ApplyText("", nullptr); Sync();
  CHECK(g_doc.blocks.size() == 1 && g_doc.blocks[0].guid == firstGuid);
  CHECK(LyricTrack()->items.size() == 1);
  CHECK(NoPlaceholder());

  // --- a lyric track whose items were all deleted: typing creates the first block
  DeleteTrackMediaItem(t, (MediaItem*)LyricTrack()->items[0]); Sync();
  CHECK(g_doc.blocks.empty());
  g_doc.ApplyText("new start", nullptr); Sync();
  CHECK(g_doc.blocks.size() == 1 && g_doc.blocks[0].label == "INTRO");

  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}

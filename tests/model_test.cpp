// Model tests: runs the real LyricDoc (src/model.cpp) against an in-memory fake of the
// REAPER API (one project, tracks, items, markers). Build with -DLYRICS_BUILD_TESTS=ON.
#define REAPERAPI_IMPLEMENT
#include "api.h"
#include "model.h"
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
struct FakeItem { double pos = 0, len = 0; int color = 0; FakeTrack* track = nullptr; std::map<std::string, std::string> str; };
struct FakeTrack { std::map<std::string, std::string> str; std::vector<FakeItem*> items; GUID guid; };
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
    return 0;
  };
  SetMediaTrackInfo_Value = [](MediaTrack*, const char*, double) { return true; };
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
    return 0;
  };
  SetMediaItemInfo_Value = [](MediaItem* it, const char* k, double v) {
    if (!strcmp(k, "D_POSITION")) I(it)->pos = v;
    else if (!strcmp(k, "D_LENGTH")) I(it)->len = v;
    else if (!strcmp(k, "I_CUSTOMCOLOR")) I(it)->color = (int)v;
    Bump(); return true;
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

// Nothing on the main timeline but the one lyric item: no project markers, one item, no notes text.
static bool OnlyTheLyricItem()
{
  if (!g_markers.empty() || LyricTrack()->items.size() != 1) return false;
  FakeItem* it = LyricItem();
  return ItemStr(it, "P_NOTES").empty() && !ItemStr(it, "P_EXT:LYRD").empty() && ItemStr(it, "P_NAME") == "Lyrics";
}

// The item starts at the first block and doesn't end before the last.
static bool ItemFits()
{
  FakeItem* it = LyricItem();
  if (!it || g_doc.blocks.empty()) return false;
  return Near(it->pos, g_doc.blocks.front().start) && it->pos + it->len >= g_doc.blocks.back().end - 1e-6;
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
  // --- right edge: shorter than the last block snaps back, longer is kept as empty space
  {
    double lastEnd = g_doc.blocks.back().end;
    theItem->len = (lastEnd - theItem->pos) - 3.0; Bump(); Sync();
    CHECK(Near(theItem->pos + theItem->len, lastEnd));
    theItem->len = (lastEnd - theItem->pos) + 6.0; Bump(); Sync();
    CHECK(Near(theItem->pos + theItem->len, lastEnd + 6.0));
    CHECK(Near(g_doc.blocks.back().end, lastEnd));
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

  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}

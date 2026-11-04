// In-memory fake of the REAPER API (one project, tracks, items with an empty MIDI take,
// markers, a 2-second bar at 120 bpm) for the model tests and the screenshot tool.
#pragma once
#include "api.h"
#include <algorithm>
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


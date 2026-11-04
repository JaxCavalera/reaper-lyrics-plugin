#include "model.h"
#include "trackstyle.h"
#include "util.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

static const double kEps = 0.0005;
static const char* kDocKey = "P_EXT:LYRD";
static const char* kItemName = "Lyrics";

std::string LyricBlock::Text() const
{
  std::string s;
  for (size_t i = 0; i < lines.size(); i++) { if (i) s += "\n"; s += lines[i].text; }
  return s;
}

// ------------------------------------------------------------------ labels / colours

std::string LyricDoc::AutoLabel(int idx, int count)
{
  if (idx == 0) return "INTRO";
  if (count >= 3 && idx == count - 1) return "OUTRO";
  return (idx % 2 == 1) ? "VERSE" : "CHORUS";
}

static const char* s_presets[] = { "INTRO", "VERSE", "PRE-CHORUS", "CHORUS", "BRIDGE", "SOLO", "OUTRO" };

const char** LyricDoc::PresetLabels(int* count)
{
  *count = (int)(sizeof(s_presets) / sizeof(s_presets[0]));
  return s_presets;
}

int LyricDoc::DefaultColorForLabel(const std::string& labelIn)
{
  std::string l = labelIn;
  for (auto& c : l) c = (char)toupper((unsigned char)c);
  int r = 74, g = 144, b = 217;
  if (l.find("INTRO") != std::string::npos) { r = 110; g = 182; b = 236; }        // sky blue
  else if (l.find("PRE") != std::string::npos) { r = 217; g = 195; b = 74; }
  else if (l.find("CHORUS") != std::string::npos) { r = 246; g = 176; b = 98; }  // light orange
  else if (l.find("VERSE") != std::string::npos) { r = 128; g = 200; b = 128; }   // light green
  else if (l.find("BRIDGE") != std::string::npos) { r = 166; g = 107; b = 190; }
  else if (l.find("SOLO") != std::string::npos) { r = 210; g = 80; b = 90; }
  else if (l.find("OUTRO") != std::string::npos) { r = 141; g = 141; b = 141; }   // grey
  return ColorToNative(r, g, b) | 0x1000000;
}

// ------------------------------------------------------------------ ext-state helpers

// Ext-state values live on one line of the project file: escape \ " CR LF.
static std::string EscapeExt(const std::string& s)
{
  std::string o;
  for (char c : s)
  {
    if (c == '\\') o += "\\\\";
    else if (c == '\n') o += "\\n";
    else if (c == '\r') {}
    else if (c == '"') o += "\\q";
    else o += c;
  }
  return o;
}

static std::string UnescapeExt(const std::string& s)
{
  std::string o;
  for (size_t i = 0; i < s.size(); i++)
  {
    if (s[i] != '\\' || i + 1 >= s.size()) { o += s[i]; continue; }
    char c = s[++i];
    o += c == 'n' ? '\n' : c == 'q' ? '"' : c;
  }
  return o;
}

// item string reads need a big buffer; lyric documents are small but not tiny
static std::string ItemString(MediaItem* it, const char* key)
{
  static std::vector<char> buf(1 << 20);
  buf[0] = 0;
  if (!GetSetMediaItemInfo_String(it, key, buf.data(), false)) return std::string();
  buf[buf.size() - 1] = 0;
  return std::string(buf.data());
}

static void SetItemString(MediaItem* it, const char* key, const std::string& v)
{
  std::vector<char> b(v.begin(), v.end()); b.push_back(0);
  GetSetMediaItemInfo_String(it, key, b.data(), true);
}

// What the lyric item shows on the timeline: it has no take, so REAPER draws its notes.
// One line, block by block: "INTRO: line / line  |  VERSE: line / line".
std::string LyricDoc::ItemNotes(const std::vector<LyricBlock>& blocks)
{
  std::string out;
  for (auto& b : blocks)
  {
    std::string lines;
    for (auto& l : b.lines)
    {
      std::string t = Trim(l.text);
      if (t.empty()) continue;
      if (!lines.empty()) lines += " / ";
      lines += t;
    }
    if (lines.empty()) continue;
    if (!out.empty()) out += "  |  ";
    out += b.label.empty() ? lines : b.label + ": " + lines;
  }
  return out.empty() ? std::string(kItemName) : out;
}

// Times are stored as integer microseconds relative to the item start: no locale-dependent
// decimal separators in the project file.
static std::string Micros(double t) { return FormatStr("%lld", (long long)std::llround(t * 1e6)); }

static bool ParseMicros(const char*& p, double* out)
{
  while (*p == ' ') p++;
  char* end = nullptr;
  long long v = strtoll(p, &end, 10);
  if (end == p) return false;
  p = end;
  *out = (double)v / 1e6;
  return true;
}

static std::string OneLine(const std::string& s)
{
  std::string o;
  for (char c : s) if (c != '\n' && c != '\r') o += c;
  return o;
}

std::string LyricDoc::Serialize(const std::vector<LyricBlock>& blocks, double origin)
{
  std::string s = "LYRD1\n";
  for (auto& b : blocks)
  {
    s += "B " + Micros(b.start - origin) + " " + Micros(b.end - origin) + " " + FormatStr("%d", b.color) + " ";
    s += b.customLabel ? 'L' : '-';
    s += b.customColor ? 'C' : '-';
    s += b.autoLen ? 'A' : '-';
    s += " " + OneLine(b.label) + "\n";
    for (auto& l : b.lines) s += "L " + Micros(l.pos - origin) + " " + OneLine(l.text) + "\n";
  }
  return EscapeExt(s);
}

bool LyricDoc::Deserialize(const std::string& stored, double origin, std::vector<LyricBlock>& out)
{
  out.clear();
  std::vector<std::string> rows = SplitLines(UnescapeExt(stored));
  if (rows.empty() || rows[0] != "LYRD1") return false;
  for (size_t r = 1; r < rows.size(); r++)
  {
    const std::string& row = rows[r];
    if (row.size() < 2 || row[1] != ' ') continue;
    const char* p = row.c_str() + 2;
    if (row[0] == 'B')
    {
      LyricBlock b;
      double s, e;
      if (!ParseMicros(p, &s) || !ParseMicros(p, &e)) continue;
      while (*p == ' ') p++;
      char* ce = nullptr;
      b.color = (int)strtol(p, &ce, 10);
      p = ce;
      while (*p == ' ') p++;
      if (p[0] && p[1]) { b.customLabel = p[0] == 'L'; b.customColor = p[1] == 'C'; p += 2; }
      if (*p && *p != ' ') { b.autoLen = *p == 'A'; p++; }
      if (*p == ' ') p++;
      b.label = p;
      b.start = origin + s; b.end = origin + e;
      out.push_back(b);
    }
    else if (row[0] == 'L' && !out.empty())
    {
      LyricLine l;
      double t;
      if (!ParseMicros(p, &t)) continue;
      if (*p == ' ') p++;
      l.text = p;
      l.pos = origin + t;
      out.back().lines.push_back(l);
    }
  }
  for (auto& b : out)
  {
    if (b.lines.empty()) { LyricLine l; l.pos = b.start; b.lines.push_back(l); }
    if (b.end <= b.start) b.end = b.start + 1;
  }
  std::stable_sort(out.begin(), out.end(), [](const LyricBlock& a, const LyricBlock& b) { return a.start < b.start; });
  return true;
}

// ------------------------------------------------------------------ tracks / items

bool LyricDoc::IsLyricTrack(MediaTrack* t)
{
  char buf[64] = "";
  if (!t || !GetSetMediaTrackInfo_String(t, "P_EXT:LYR", buf, false)) return false;
  return buf[0] == '1';
}

bool LyricDoc::IsLyricItem(MediaItem* it)
{
  if (!it) return false;
  MediaTrack* t = GetMediaItem_Track(it);
  return t && IsLyricTrack(t) && !ItemString(it, kDocKey).empty();
}

void LyricDoc::FindLyricTracks(ReaProject* p, std::vector<MediaTrack*>& out)
{
  out.clear();
  int n = CountTracks(p);
  for (int i = 0; i < n; i++)
  {
    MediaTrack* t = GetTrack(p, i);
    if (t && IsLyricTrack(t)) out.push_back(t);
  }
}

std::string LyricDoc::TrackName(MediaTrack* t)
{
  char buf[512] = "";
  GetSetMediaTrackInfo_String(t, "P_NAME", buf, false);
  int idx = (int)GetMediaTrackInfo_Value(t, "IP_TRACKNUMBER");
  return FormatStr("%d: %s", idx, buf[0] ? buf : "Lyrics");
}

MediaTrack* LyricDoc::CreateLyricTrack(ReaProject* p)
{
  Undo_BeginBlock2(p);
  int idx = CountTracks(p);
  int nsel = CountSelectedTracks(p);
  if (nsel > 0)
  {
    MediaTrack* last = GetSelectedTrack(p, nsel - 1);
    idx = (int)GetMediaTrackInfo_Value(last, "IP_TRACKNUMBER"); // 1-based -> insert after
  }
  InsertTrackAtIndex(idx, true);
  TrackList_AdjustWindows(false);
  MediaTrack* t = GetTrack(p, idx);
  if (t)
  {
    char name[] = "Lyrics";
    GetSetMediaTrackInfo_String(t, "P_NAME", name, true);
    char one[] = "1";
    GetSetMediaTrackInfo_String(t, "P_EXT:LYR", one, true);
    TrackStyle::Apply(t, true, TrackStyle::IconFile());   // layout, no mixer, height, icon, colour
    SetOnlyTrackSelected(t);
  }
  Undo_EndBlock2(p, "Insert lyric track", -1);
  return t;
}

MediaItem* LyricDoc::FindLyricItem() const
{
  if (item && ValidatePtr2(proj, item, "MediaItem*") && GetMediaItem_Track(item) == track && !ItemString(item, kDocKey).empty())
    return item;
  MediaItem* best = nullptr; double bestPos = 0;
  int n = CountTrackMediaItems(track);
  for (int i = 0; i < n; i++)
  {
    MediaItem* it = GetTrackMediaItem(track, i);
    if (ItemString(it, kDocKey).empty()) continue;
    double p = GetMediaItemInfo_Value(it, "D_POSITION");
    if (!best || p < bestPos) { best = it; bestPos = p; }
  }
  return best;
}

bool LyricDoc::Bind(ReaProject* p, MediaTrack* t)
{
  Unbind();
  if (!p || !t) return false;
  proj = p; track = t;
  SyncFromReaper();
  return true;
}

void LyricDoc::Unbind()
{
  if (IsBound() && ValidatePtr2(proj, track, "MediaTrack*")) FlushUndo(true);
  proj = nullptr; track = nullptr; item = nullptr; blocks.clear(); lastStateCount = -1;
  m_written.clear(); m_itemPos = m_itemLen = m_tail = 0; m_undoPending = false;
}

bool LyricDoc::ValidateTrack()
{
  if (!IsBound()) return false;
  if (EnumProjects(-1, nullptr, 0) != proj || !ValidatePtr2(proj, track, "MediaTrack*") || !IsLyricTrack(track))
  {
    m_undoPending = false;   // the project/track is gone: nothing to add an undo point to
    Unbind();
    return false;
  }
  return true;
}

bool LyricDoc::NeedsSync() const
{
  return IsBound() && GetProjectStateChangeCount(proj) != lastStateCount;
}

// ------------------------------------------------------------------ time helpers

double LyricDoc::BarLen() const
{
  int m0 = 0, m1 = 1;
  double a = TimeMap2_beatsToTime(proj, 0, &m0), b = TimeMap2_beatsToTime(proj, 0, &m1);
  double d = b - a;
  return d > 0.1 ? d : 2.0;
}


bool LyricDoc::IsPlaying() const { return proj && (GetPlayStateEx(proj) & 1); }

double LyricDoc::PlayOrEditPos() const
{
  if (!proj) return 0;
  return IsPlaying() ? GetPlayPosition2Ex(proj) : GetCursorPositionEx(proj);
}

double LyricDoc::ItemStart() const { return blocks.empty() ? m_itemPos : blocks.front().start; }
double LyricDoc::ItemEnd() const
{
  return blocks.empty() ? m_itemPos + m_itemLen : blocks.back().end + m_tail;
}

int LyricDoc::FindBlockAt(double t, int* lineOut) const
{
  int found = -1;
  for (int i = 0; i < (int)blocks.size(); i++)
  {
    const LyricBlock& b = blocks[i];
    if (t >= b.start - kEps && t < b.end) { found = i; break; }
  }
  if (lineOut)
  {
    *lineOut = -1;
    if (found >= 0)
    {
      const LyricBlock& b = blocks[found];
      for (int i = 0; i < (int)b.lines.size(); i++)
        if (t >= b.lines[i].pos - kEps) *lineOut = i;
    }
  }
  return found;
}

// ------------------------------------------------------------------ geometry helpers

void LyricDoc::NormaliseLines(LyricBlock& b)
{
  if (b.end <= b.start + kEps) b.end = b.start + BarLen();
  if (b.lines.empty()) { LyricLine l; l.pos = b.start; b.lines.push_back(l); }
  double last = b.start;
  for (size_t i = 0; i < b.lines.size(); i++)
  {
    double& p = b.lines[i].pos;
    if (i == 0) p = b.start;
    if (p < last) p = last;
    if (p > b.end - kEps) p = b.end - kEps;
    last = p;
  }
}

void LyricDoc::ScaleLines(LyricBlock& b, double oldStart, double oldLen)
{
  double scale = oldLen > kEps ? b.Len() / oldLen : 1.0;
  for (auto& l : b.lines) l.pos = b.start + (l.pos - oldStart) * scale;
  NormaliseLines(b);
}

void LyricDoc::RelabelAuto()
{
  int n = (int)blocks.size();
  for (int i = 0; i < n; i++)
  {
    LyricBlock& b = blocks[i];
    if (!b.customLabel) b.label = AutoLabel(i, n);
    if (!b.customColor) b.color = DefaultColorForLabel(b.label);
  }
}

// A block never extends past the start of the block after it: an overlapping end is trimmed
// to the next start (lines scale with it). If that would leave less than a quarter bar, the
// next block is pushed later instead (and the check carries on down the list).
void LyricDoc::ShiftFrom(size_t first, double d)
{
  for (size_t j = first; j < blocks.size(); j++)
  {
    LyricBlock& n = blocks[j];
    n.start += d; n.end += d;
    for (auto& l : n.lines) l.pos += d;
  }
}

void LyricDoc::RelayoutAuto(LyricBlock& b)
{
  if (b.lines.empty()) { LyricLine l; b.lines.push_back(l); }
  double slot = LineSlot();
  b.end = b.start + slot * (double)b.lines.size();
  for (size_t i = 0; i < b.lines.size(); i++) b.lines[i].pos = b.start + slot * (double)i;
}

void LyricDoc::FixOverlaps()
{
  double minLen = MinBlockLen();
  for (size_t i = 0; i + 1 < blocks.size(); i++)
  {
    LyricBlock& a = blocks[i];
    LyricBlock& n = blocks[i + 1];
    if (n.start < a.start + minLen)
    {
      double d = a.start + minLen - n.start;
      n.start += d; n.end += d;
      for (auto& l : n.lines) l.pos += d;
    }
    if (a.end > n.start + kEps)
    {
      double os = a.start, ol = a.Len();
      a.end = n.start;
      ScaleLines(a, os, ol);
    }
  }
}

// ------------------------------------------------------------------ model -> REAPER

void LyricDoc::Write()
{
  if (!IsBound() || blocks.empty()) return;
  for (auto& b : blocks) NormaliseLines(b);
  bool created = false;
  if (!item || !ValidatePtr2(proj, item, "MediaItem*") || GetMediaItem_Track(item) != track)
  {
    item = AddMediaItemToTrack(track);
    m_itemPos = blocks.front().start; m_itemLen = 0; m_tail = 0;
    m_written.clear();
    created = true;
  }
  // auto-fit: starts at the first block and ends at the last, plus any space the user added at the end
  double pos = blocks.front().start;
  double end = blocks.back().end + m_tail;
  std::string doc = Serialize(blocks, pos);
  PreventUIRefresh(1);
  if (created) SetItemString(item, "P_NAME", kItemName);
  if (doc != m_written || created)
  {
    SetItemString(item, kDocKey, doc);
    SetItemString(item, "P_NOTES", ItemNotes(blocks));
  }
  if (created || std::fabs(GetMediaItemInfo_Value(item, "D_POSITION") - pos) > 1e-9) SetMediaItemInfo_Value(item, "D_POSITION", pos);
  if (created || std::fabs(GetMediaItemInfo_Value(item, "D_LENGTH") - (end - pos)) > 1e-9) SetMediaItemInfo_Value(item, "D_LENGTH", end - pos);
  PreventUIRefresh(-1);
  m_written = doc; m_itemPos = pos; m_itemLen = end - pos;
  UpdateArrange();
  lastStateCount = GetProjectStateChangeCount(proj);
}

void LyricDoc::UndoPoint(const char* name)
{
  m_undoPending = false;
  if (item && ValidatePtr2(proj, item, "MediaItem*")) Undo_OnStateChange_Item(proj, name, item);
  lastStateCount = GetProjectStateChangeCount(proj);
}

void LyricDoc::FlushUndo(bool force)
{
  if (!m_undoPending || !IsBound()) return;
  if (force || GetTickCount() - m_lastTextEdit > 1200) UndoPoint("Lyrics: edit text");
}

// ------------------------------------------------------------------ REAPER -> model

static std::vector<std::string> NotesToLines(const std::string& notes)
{
  std::vector<std::string> out;
  for (auto& l : SplitLines(notes))
    if (!Trim(l).empty()) out.push_back(l);
  return out;
}

// Projects from before the sequencer stored one item per block (P_EXT:LYRT/LYRM) plus a
// project marker per line. Convert them into the single lyric item and delete the markers.
bool LyricDoc::MigrateOldItems()
{
  std::vector<MediaItem*> old;
  int n = CountTrackMediaItems(track);
  for (int i = 0; i < n; i++)
  {
    MediaItem* it = GetTrackMediaItem(track, i);
    if (!ItemString(it, kDocKey).empty()) return false;   // already has a lyric item
    if (!ItemString(it, "P_EXT:LYRM").empty() || !ItemString(it, "P_EXT:LYRT").empty()) old.push_back(it);
  }
  if (old.empty()) return false;

  std::map<int, double> markerPos;
  int idx = 0; bool isrgn; double pos, rgnend; const char* name; int id, color;
  while ((idx = EnumProjectMarkers3(proj, idx, &isrgn, &pos, &rgnend, &name, &id, &color)))
    if (!isrgn) markerPos[id] = pos;

  std::vector<LyricBlock> got;
  std::set<int> deadMarkers;
  for (MediaItem* it : old)
  {
    LyricBlock b;
    b.label = ItemString(it, "P_NAME");
    b.start = GetMediaItemInfo_Value(it, "D_POSITION");
    b.end = b.start + GetMediaItemInfo_Value(it, "D_LENGTH");
    b.color = (int)GetMediaItemInfo_Value(it, "I_CUSTOMCOLOR");
    std::string flags = ItemString(it, "P_EXT:LYRF");
    b.customLabel = flags.find('L') != std::string::npos;
    b.customColor = flags.find('C') != std::string::npos;
    std::string notes = ItemString(it, "P_NOTES");
    std::vector<std::string> lines = NotesToLines(Trim(notes).empty() ? UnescapeExt(ItemString(it, "P_EXT:LYRT")) : notes);
    std::vector<int> ids;
    std::string m = ItemString(it, "P_EXT:LYRM");
    for (const char* p = m.c_str(); *p;)
    {
      char* e = nullptr; long v = strtol(p, &e, 10);
      if (e == p) { p++; continue; }
      ids.push_back((int)v); deadMarkers.insert((int)v); p = e;
    }
    for (size_t i = 0; i < lines.size(); i++)
    {
      LyricLine l; l.text = lines[i] == "..." ? "" : lines[i];
      l.pos = (i < ids.size() && markerPos.count(ids[i])) ? markerPos[ids[i]] : -1;
      b.lines.push_back(l);
    }
    if (b.lines.empty()) { LyricLine l; l.pos = b.start; b.lines.push_back(l); }
    for (size_t i = 0; i < b.lines.size(); i++)   // lines without a marker: spread evenly
      if (b.lines[i].pos < 0) b.lines[i].pos = b.start + (b.end - b.start) * (double)i / (double)b.lines.size();
    if (b.color == 0) b.color = DefaultColorForLabel(b.label);
    got.push_back(b);
  }
  std::stable_sort(got.begin(), got.end(), [](const LyricBlock& a, const LyricBlock& b) { return a.start < b.start; });
  // an empty line can only stand alone in the editor: drop empty blocks between others
  if (got.size() > 1)
    got.erase(std::remove_if(got.begin(), got.end(), [](const LyricBlock& b) { return Trim(b.Text()).empty(); }), got.end());

  Undo_BeginBlock2(proj);
  PreventUIRefresh(1);
  for (int mid : deadMarkers) DeleteProjectMarker(proj, mid, false);
  for (MediaItem* it : old) DeleteTrackMediaItem(track, it);
  item = nullptr; m_itemLen = 0; m_tail = 0;
  blocks = got;
  if (!blocks.empty())
  {
    m_itemPos = blocks.front().start;
    FixOverlaps();
    Write();
  }
  PreventUIRefresh(-1);
  Undo_EndBlock2(proj, "Lyrics: convert to lyric item", -1);
  UpdateTimeline();
  return true;
}

bool LyricDoc::SyncFromReaper()
{
  if (!ValidateTrack()) return true;
  std::string oldText = ToText();
  size_t oldCount = blocks.size();
  bool migrated = MigrateOldItems();

  MediaItem* it = FindLyricItem();
  if (!it)
  {
    item = nullptr; blocks.clear(); m_written.clear(); m_undoPending = false;
    lastStateCount = GetProjectStateChangeCount(proj);
    return migrated || oldCount != 0;
  }
  double pos = GetMediaItemInfo_Value(it, "D_POSITION");
  double len = GetMediaItemInfo_Value(it, "D_LENGTH");
  std::string doc = ItemString(it, kDocKey);
  bool needWrite = false;

  if (it != item || doc != m_written)
  {
    // another item, or the document changed under us (undo/redo, project reload)
    std::vector<LyricBlock> nb;
    Deserialize(doc, pos, nb);
    item = it; blocks = nb; m_written = doc; m_itemPos = pos; m_itemLen = len;
    for (auto& b : blocks) if (!b.customColor) b.color = DefaultColorForLabel(b.label);   // current defaults
    m_tail = blocks.empty() ? 0 : std::max(0.0, pos + len - blocks.back().end);
    m_undoPending = false;
    if (blocks.empty())
    {
      LyricBlock b; b.start = pos; b.end = pos + (len > kEps ? len : DefaultBlockLen());
      LyricLine l; l.pos = b.start; b.lines.push_back(l);
      blocks.push_back(b);
      RelabelAuto();
      m_tail = 0;
      needWrite = true;
    }
    else if (std::fabs(pos - blocks.front().start) > kEps) needWrite = true;
    // lyric items from earlier builds have no notes yet (undo/redo restores them with the item)
    std::string notes = ItemNotes(blocks);
    if (ItemString(it, "P_NOTES") != notes) SetItemString(it, "P_NOTES", notes);
  }
  else
  {
    double oldEnd = m_itemPos + m_itemLen;
    bool moved = std::fabs(pos - m_itemPos) > kEps;
    bool sameLen = std::fabs(len - m_itemLen) <= kEps;
    bool sameEnd = std::fabs(pos + len - oldEnd) <= kEps;
    if (moved && sameLen)
    {
      // the item was moved: the lyrics move with it (stored relative to the item start)
      double d = pos - m_itemPos;
      for (auto& b : blocks) { b.start += d; b.end += d; for (auto& l : b.lines) l.pos += d; }
      m_itemPos = pos;
    }
    else if (moved && sameEnd)
    {
      needWrite = true;   // left edge dragged: it always sits on the first block, snap it back
    }
    else if (moved)
    {
      double d = pos - m_itemPos;
      for (auto& b : blocks) { b.start += d; b.end += d; for (auto& l : b.lines) l.pos += d; }
      m_itemPos = pos; m_itemLen = len;
      if (!blocks.empty()) m_tail = std::max(0.0, pos + len - blocks.back().end);
      needWrite = true;
    }
    else if (!sameLen)
    {
      m_itemLen = len;    // right edge: longer keeps the extra space, shorter stops at the last block
      if (!blocks.empty()) m_tail = std::max(0.0, pos + len - blocks.back().end);
      if (!blocks.empty() && pos + len < blocks.back().end - kEps) needWrite = true;
    }
  }
  if (needWrite) Write();
  lastStateCount = GetProjectStateChangeCount(proj);
  return migrated || ToText() != oldText || blocks.size() != oldCount;
}

// ------------------------------------------------------------------ text -> model

std::string LyricDoc::ToText() const
{
  std::string s;
  for (size_t i = 0; i < blocks.size(); i++)
  {
    if (i) s += "\n\n";
    s += blocks[i].Text();
  }
  return s;
}

static std::vector<std::vector<std::string>> ParseParagraphs(const std::string& text)
{
  std::vector<std::vector<std::string>> out;
  std::vector<std::string> cur;
  for (auto& raw : SplitLines(text))
  {
    if (Trim(raw).empty()) { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
    else cur.push_back(raw);
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

static bool UsePending(LiveContext* live, const LyricBlock& b, const LyricBlock* next)
{
  if (!live || !live->valid) return false;
  if (GetTickCount() - live->stamp > 20000) { live->valid = false; return false; }
  if (live->pos < b.start - kEps) return false;
  if (next && live->pos >= next->start - kEps) return false;
  return true;
}

void LyricDoc::DiffLines(LyricBlock& b, const std::vector<std::string>& newLines, LiveContext* live, const LyricBlock* next)
{
  std::vector<std::string> oldTexts;
  for (auto& l : b.lines) oldTexts.push_back(l.text);
  auto matches = LcsMatch(oldTexts, newLines);

  std::vector<LyricLine> result(newLines.size());
  for (auto& m : matches) result[m.second] = b.lines[m.first];
  // pair unmatched runs by index (an edited line keeps its timing)
  size_t oi = 0, ni = 0;
  std::vector<std::pair<int, int>> bounds = matches;
  bounds.push_back({ (int)b.lines.size(), (int)newLines.size() });
  for (auto& m : bounds)
  {
    size_t oe = m.first, ne = m.second;
    while (oi < oe && ni < ne) { result[ni] = b.lines[oi]; result[ni].text = newLines[ni]; oi++; ni++; }
    while (ni < ne) { result[ni].text = newLines[ni]; result[ni].pos = -1; ni++; }
    oi = oe + 1; ni = ne + 1;
  }
  // brand new lines: pending live position for the first one, interpolation for the rest
  bool pendingUsed = false;
  for (size_t i = 0; i < result.size(); i++)
  {
    if (result[i].pos >= 0) continue;
    if (!pendingUsed && i > 0 && UsePending(live, b, next))
    {
      result[i].pos = live->pos;
      live->valid = false; pendingUsed = true;
      b.autoLen = false;   // timed live: no longer default-sized
      if (b.end < live->pos + BarLen()) b.end = live->pos + BarLen();
      if (next && b.end > next->start) b.end = next->start;
      continue;
    }
    size_t j = i; while (j < result.size() && result[j].pos < 0) j++;
    double p0 = i == 0 ? b.start : result[i - 1].pos;
    double p1 = j < result.size() ? result[j].pos : b.end;
    int cnt = (int)(j - i);
    for (size_t k = i; k < j; k++) result[k].pos = p0 + (p1 - p0) * (double)(k - i + 1) / (cnt + 1);
    i = j - 1;
  }
  b.lines = result;
  NormaliseLines(b);
}

PastedBlock PastedBlock::From(const LyricBlock& b, bool whole)
{
  PastedBlock p;
  p.label = b.label; p.color = b.color; p.whole = whole; p.autoLen = b.autoLen; p.len = b.Len();
  for (auto& l : b.lines) p.offsets.push_back(l.pos - b.start);
  return p;
}

void LyricDoc::ApplyText(const std::string& utf8, LiveContext* live, const std::vector<PastedBlock>* pasted)
{
  if (!ValidateTrack()) return;
  auto paras = ParseParagraphs(utf8);
  // an empty editor is one empty block, never zero blocks: clearing the text keeps the
  // first block (and its timing) instead of deleting it
  if (paras.empty()) paras.push_back({ std::string() });
  std::vector<std::string> oldTexts, newTexts;
  for (auto& b : blocks) oldTexts.push_back(b.Text());
  for (auto& p : paras) newTexts.push_back(JoinLines(p));
  if (oldTexts == newTexts) return;

  std::vector<LyricBlock> old = blocks;

  struct Slot { LyricBlock blk; bool isNew = false; bool exists = false; std::vector<std::string> lines; const PastedBlock* paste = nullptr; };
  std::vector<Slot> slots(paras.size());
  // pasted paragraphs are always new blocks: keep them out of the matching, so a pasted copy
  // never takes over the timing of the block it was copied from
  if (pasted)
    for (auto& p : *pasted)
      if (p.para >= 0 && p.para < (int)slots.size()) { slots[p.para].paste = &p; slots[p.para].isNew = true; }
  std::vector<int> keep;   // paragraph indices that are matched against the old blocks
  std::vector<std::string> keepTexts;
  for (size_t i = 0; i < paras.size(); i++) if (!slots[i].paste) { keep.push_back((int)i); keepTexts.push_back(newTexts[i]); }
  auto matches = LcsMatch(oldTexts, keepTexts);
  for (auto& m : matches) { slots[keep[m.second]].blk = old[m.first]; slots[keep[m.second]].exists = true; }
  std::vector<std::pair<int, int>> bounds = matches;
  bounds.push_back({ (int)old.size(), (int)keep.size() });
  int deleted = 0;
  size_t oi = 0, ni = 0;
  for (auto& m : bounds)
  {
    size_t oe = m.first, ne = m.second;
    while (oi < oe && ni < ne) { slots[keep[ni]].blk = old[oi]; slots[keep[ni]].exists = true; oi++; ni++; }
    while (oi < oe) { deleted++; oi++; }
    while (ni < ne) { slots[keep[ni]].isNew = true; ni++; }
    oi = oe + 1; ni = ne + 1;
  }
  for (size_t i = 0; i < slots.size(); i++) slots[i].lines = paras[i];

  bool structural = deleted > 0;
  for (auto& s : slots) if (s.isNew) structural = true;
  if (structural) FlushUndo(true);   // typing so far gets its own undo point first

  std::vector<LyricBlock> result;
  for (size_t i = 0; i < slots.size(); i++)
  {
    Slot& s = slots[i];
    const LyricBlock* next = nullptr;
    for (size_t j = i + 1; j < slots.size(); j++) if (slots[j].exists) { next = &slots[j].blk; break; }
    LyricBlock* prev = result.empty() ? nullptr : &result.back();

    if (s.isNew)
    {
      LyricBlock nb;
      double len = LineSlot() * (double)std::max<size_t>(1, s.lines.size());
      const PastedBlock* pb = s.paste;
      if (pb)
      {
        // pasted on a clean line: same label and colour (kept for good); a whole block also
        // keeps its length and line timing. Placed like any new block.
        nb.label = pb->label; nb.customLabel = true;
        nb.color = pb->color; nb.customColor = true;
        bool keepTiming = pb->whole && pb->len > kEps && pb->offsets.size() == s.lines.size();
        if (keepTiming) len = pb->len;
        if (prev) nb.start = prev->end;
        else if (old.size() == 1 && old[0].Text().empty()) nb.start = old[0].start;   // replaces the empty first block
        else if (next) nb.start = std::max(0.0, next->start - len);
        else nb.start = GetCursorPositionEx(proj);
        if (keepTiming)
        {
          nb.autoLen = pb->autoLen;
          nb.end = nb.start + pb->len;
          for (size_t k = 0; k < s.lines.size(); k++) { LyricLine l; l.text = s.lines[k]; l.pos = nb.start + pb->offsets[k]; nb.lines.push_back(l); }
          NormaliseLines(nb);
        }
        else
        {
          nb.autoLen = true;
          nb.end = nb.start + len;
          LyricBlock empty = nb;
          DiffLines(empty, s.lines, nullptr, next);
          nb.lines = empty.lines;
        }
        result.push_back(nb);
        continue;
      }
      if (live && live->valid && (GetTickCount() - live->stamp < 20000) && (!prev || live->pos >= prev->start) && (!next || live->pos < next->start))
      {
        // live typing: the block starts at the stamped play position
        nb.start = live->pos;
        live->valid = false;
        if (prev) { prev->end = nb.start; prev->autoLen = false; NormaliseLines(*prev); }
        nb.end = nb.start + len;
        if (next && nb.end > next->start) nb.end = next->start;
      }
      else
      {
        // default size, 2 bars per line, straight after the previous block; blocks after it
        // are pushed along below if it doesn't fit
        nb.autoLen = true;
        if (prev) nb.start = prev->end;
        else if (next) nb.start = std::max(0.0, next->start - len);
        else nb.start = GetCursorPositionEx(proj);
        nb.end = nb.start + len;
      }
      if (nb.end <= nb.start + kEps) nb.end = nb.start + len;
      LyricBlock empty = nb;
      DiffLines(empty, s.lines, nullptr, next);
      nb.lines = empty.lines;
      result.push_back(nb);
    }
    else
    {
      LyricBlock b = s.blk;
      if (b.Text() != JoinLines(s.lines)) DiffLines(b, s.lines, live, next);
      result.push_back(b);
    }
  }
  blocks = result;
  // default-sized blocks follow their line count; the blocks after one that grew or shrank
  // move by the same amount (gaps kept)
  for (size_t i = 0; i < blocks.size(); i++)
  {
    if (!blocks[i].autoLen) continue;
    double oldEnd = blocks[i].end;
    RelayoutAuto(blocks[i]);
    double d = blocks[i].end - oldEnd;
    if (std::fabs(d) > kEps) ShiftFrom(i + 1, d);
  }
  // a block that doesn't fit before the next one pushes it (and everything after) along
  for (size_t i = 0; i + 1 < blocks.size(); i++)
    if (blocks[i + 1].start < blocks[i].end - kEps) ShiftFrom(i + 1, blocks[i].end - blocks[i + 1].start);
  RelabelAuto();
  FixOverlaps();
  Write();
  if (structural) UndoPoint("Lyrics: edit blocks");
  else { m_undoPending = true; m_lastTextEdit = GetTickCount(); }
}

// ------------------------------------------------------------------ structural ops

void LyricDoc::MoveBlock(int from, int to)
{
  if (!ValidateTrack()) return;
  int n = (int)blocks.size();
  if (from < 0 || from >= n || to < 0 || to > n) return;
  if (to > from) to--;
  if (from == to) return;
  FlushUndo(true);
  LyricBlock moving = blocks[from];
  blocks.erase(blocks.begin() + from);
  blocks.insert(blocks.begin() + to, moving);
  // the blocks between the two slots keep their lengths and are packed from the earliest start
  int lo = std::min(from, to), hi = std::max(from, to);
  double t = 1e18;
  for (int i = lo; i <= hi; i++) t = std::min(t, blocks[i].start);
  for (int i = lo; i <= hi; i++)
  {
    LyricBlock& b = blocks[i];
    double len = b.Len(), d = t - b.start;
    b.start += d; b.end = b.start + len;
    for (auto& l : b.lines) l.pos += d;
    t = b.end;
  }
  // labels travel with their blocks: reordering never renames or recolours anything. A block
  // whose automatic label would change in its new place keeps the label it has now.
  for (int i = 0; i < n; i++)
    if (!blocks[i].customLabel && AutoLabel(i, n) != blocks[i].label) { blocks[i].customLabel = true; blocks[i].customColor = true; }
  RelabelAuto();
  FixOverlaps();
  Write();
  UndoPoint("Lyrics: move block");
}

void LyricDoc::SetLabel(int i, const std::string& label, bool custom)
{
  if (!ValidateTrack() || i < 0 || i >= (int)blocks.size()) return;
  FlushUndo(true);
  blocks[i].customLabel = custom;
  blocks[i].label = custom ? label : AutoLabel(i, (int)blocks.size());
  // renaming never recolours: a renamed block keeps the colour it has now, for good
  // (only the colour menu changes it)
  if (custom) blocks[i].customColor = true;
  Write();
  UndoPoint("Lyrics: rename block");
}

void LyricDoc::ApplyLabelPreset(int i, const std::string& label)
{
  if (!ValidateTrack() || i < 0 || i >= (int)blocks.size()) return;
  FlushUndo(true);
  // a preset brings its own colour (empty label = the automatic one); the colour then
  // follows that label until the user renames the block or picks a colour
  blocks[i].customLabel = !label.empty();
  blocks[i].label = label.empty() ? AutoLabel(i, (int)blocks.size()) : label;
  blocks[i].customColor = false;
  blocks[i].color = DefaultColorForLabel(blocks[i].label);
  Write();
  UndoPoint("Lyrics: label preset");
}

void LyricDoc::SetColor(int i, int nativeColor, bool custom)
{
  if (!ValidateTrack() || i < 0 || i >= (int)blocks.size()) return;
  FlushUndo(true);
  blocks[i].customColor = custom;
  blocks[i].color = custom ? nativeColor : DefaultColorForLabel(blocks[i].label);
  Write();
  UndoPoint("Lyrics: block colour");
}

void LyricDoc::DeleteBlock(int i)
{
  if (!ValidateTrack() || i < 0 || i >= (int)blocks.size()) return;
  FlushUndo(true);
  if (blocks.size() == 1)
  {
    // the first and only block stays (empty): a lyric item always has one block
    LyricLine l; l.pos = blocks[0].start;
    blocks[0].lines.assign(1, l);
  }
  else blocks.erase(blocks.begin() + i);
  RelabelAuto();
  Write();
  UndoPoint("Lyrics: delete block");
}

void LyricDoc::AppendBlock(double atPos)
{
  if (!ValidateTrack() || !blocks.empty()) return;   // only the first block is created empty
  LyricBlock nb;
  nb.start = atPos >= 0 ? atPos : GetCursorPositionEx(proj);
  nb.end = nb.start + LineSlot();
  nb.autoLen = true;
  LyricLine l; l.pos = nb.start; nb.lines.push_back(l);
  blocks.push_back(nb);
  RelabelAuto();
  Undo_BeginBlock2(proj);
  Write();
  Undo_EndBlock2(proj, "Lyrics: add lyric item", -1);
  lastStateCount = GetProjectStateChangeCount(proj);
}

// ------------------------------------------------------------------ sequencer drags

void LyricDoc::DragBlockTo(int i, double newStart)
{
  if (!ValidateTrack() || i < 0 || i >= (int)blocks.size()) return;
  LyricBlock& b = blocks[i];
  double len = b.Len();
  double lo = i > 0 ? blocks[i - 1].end : 0.0;
  double hi = i + 1 < (int)blocks.size() ? blocks[i + 1].start - len : 1e12;
  if (hi < lo) return;
  double s = std::min(std::max(newStart, lo), hi);
  double d = s - b.start;
  if (std::fabs(d) < 1e-9) return;
  b.start += d; b.end += d;
  for (auto& l : b.lines) l.pos += d;
  Write();
}

void LyricDoc::DragBlockEdge(int i, bool left, double t, bool ripple)
{
  if (!ValidateTrack() || i < 0 || i >= (int)blocks.size()) return;
  LyricBlock& b = blocks[i];
  double os = b.start, ol = b.Len();
  b.autoLen = false;   // sized by hand from now on
  if (!left && ripple)
  {
    b.end = std::max(t, b.start + MinBlockLen());
    ScaleLines(b, os, ol);
    // snap the next block against this one and carry the rest along, gaps intact
    if (i + 1 < (int)blocks.size()) ShiftFrom(i + 1, b.end - blocks[i + 1].start);
    Write();
    return;
  }
  if (left)
  {
    double lo = i > 0 ? blocks[i - 1].end : 0.0, hi = b.end - MinBlockLen();
    if (hi < lo) return;
    b.start = std::min(std::max(t, lo), hi);
  }
  else
  {
    double lo = b.start + MinBlockLen(), hi = i + 1 < (int)blocks.size() ? blocks[i + 1].start : 1e12;
    if (hi < lo) return;
    b.end = std::min(std::max(t, lo), hi);
  }
  if (std::fabs(b.start - os) < 1e-9 && std::fabs(b.Len() - ol) < 1e-9) return;
  ScaleLines(b, os, ol);
  Write();
}

void LyricDoc::DragLineStart(int bi, int li, double t)
{
  if (!ValidateTrack() || bi < 0 || bi >= (int)blocks.size()) return;
  LyricBlock& b = blocks[bi];
  if (li <= 0 || li >= (int)b.lines.size()) return;
  double lo = b.lines[li - 1].pos + MinLineLen();
  double hi = (li + 1 < (int)b.lines.size() ? b.lines[li + 1].pos : b.end) - MinLineLen();
  if (hi < lo) return;
  double p = std::min(std::max(t, lo), hi);
  if (std::fabs(p - b.lines[li].pos) < 1e-9) return;
  b.lines[li].pos = p;
  b.autoLen = false;
  Write();
}

void LyricDoc::EndDrag(const char* undoName)
{
  if (!ValidateTrack()) return;
  UndoPoint(undoName);
}

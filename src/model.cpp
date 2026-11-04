#include "model.h"
#include "util.h"
#include <algorithm>
#include <set>
#include <cmath>

static const double kEps = 0.0005;

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
  if (l.find("INTRO") != std::string::npos) { r = 108; g = 142; b = 191; }
  else if (l.find("PRE") != std::string::npos) { r = 217; g = 195; b = 74; }
  else if (l.find("CHORUS") != std::string::npos) { r = 232; g = 146; b = 58; }
  else if (l.find("VERSE") != std::string::npos) { r = 79; g = 157; b = 105; }
  else if (l.find("BRIDGE") != std::string::npos) { r = 166; g = 107; b = 190; }
  else if (l.find("SOLO") != std::string::npos) { r = 210; g = 80; b = 90; }
  else if (l.find("OUTRO") != std::string::npos) { r = 141; g = 141; b = 141; }
  return ColorToNative(r, g, b) | 0x1000000;
}

// ------------------------------------------------------------------ tracks

bool LyricDoc::IsLyricTrack(MediaTrack* t)
{
  char buf[64] = "";
  if (!GetSetMediaTrackInfo_String(t, "P_EXT:LYR", buf, false)) return false;
  return buf[0] == '1';
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
    SetMediaTrackInfo_Value(t, "I_HEIGHTOVERRIDE", 64);
    SetOnlyTrackSelected(t);
  }
  Undo_EndBlock2(p, "Insert lyric track", -1);
  return t;
}

bool LyricDoc::Bind(ReaProject* p, MediaTrack* t)
{
  Unbind();
  if (!p || !t) return false;
  proj = p; track = t;
  char g[128];
  guidToString(GetTrackGUID(t), g);
  trackGuid = g;
  lastStateCount = -1;
  SyncFromReaper();
  return true;
}

void LyricDoc::Unbind()
{
  proj = nullptr; track = nullptr; trackGuid.clear(); blocks.clear(); lastStateCount = -1;
}

bool LyricDoc::ValidateTrack()
{
  if (!IsBound()) return false;
  if (EnumProjects(-1, nullptr, 0) != proj || !ValidatePtr2(proj, track, "MediaTrack*") || !IsLyricTrack(track))
  {
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

double LyricDoc::DefaultBlockLen() const { return BarLen() * 8; }

bool LyricDoc::IsPlaying() const { return proj && (GetPlayStateEx(proj) & 1); }

double LyricDoc::PlayOrEditPos() const
{
  if (!proj) return 0;
  return IsPlaying() ? GetPlayPosition2Ex(proj) : GetCursorPositionEx(proj);
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

// ------------------------------------------------------------------ REAPER read

void LyricDoc::ReadMarkers(std::map<int, MarkerInfo>& out) const
{
  out.clear();
  int idx = 0;
  bool isrgn; double pos, rgnend; const char* name; int id, color;
  while ((idx = EnumProjectMarkers3(proj, idx, &isrgn, &pos, &rgnend, &name, &id, &color)))
  {
    if (isrgn) continue;
    out[id] = { pos, name ? name : "", color };
  }
}

static std::vector<std::string> NotesToLines(const std::string& notes)
{
  std::vector<std::string> out;
  for (auto& l : SplitLines(notes))
    if (!Trim(l).empty()) out.push_back(l);
  return out;
}

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

bool LyricDoc::ReadBlockFromItem(MediaItem* item, LyricBlock& b, const std::map<int, MarkerInfo>& markers, std::set<int>* usedIds)
{
  char buf[65536];
  b.item = item;
  buf[0] = 0; GetSetMediaItemInfo_String(item, "GUID", buf, false); b.guid = buf;
  buf[0] = 0; GetSetMediaItemInfo_String(item, "P_NAME", buf, false); b.label = buf;
  // Lyrics live in ext state so the item draws only its label. Text found in the item notes
  // (typed there in REAPER, or a project from an older build) wins and is moved across.
  buf[0] = 0; GetSetMediaItemInfo_String(item, "P_NOTES", buf, false);
  std::string text = buf;
  b.notesPending = !Trim(text).empty();
  if (!b.notesPending)
  {
    buf[0] = 0; GetSetMediaItemInfo_String(item, "P_EXT:LYRT", buf, false);
    text = UnescapeExt(buf);
  }
  std::vector<std::string> lines = NotesToLines(text);
  if (lines.empty()) lines.push_back(""); // a block always has at least one line (empty = no lyrics yet)
  b.start = GetMediaItemInfo_Value(item, "D_POSITION");
  b.end = b.start + GetMediaItemInfo_Value(item, "D_LENGTH");
  b.color = (int)GetMediaItemInfo_Value(item, "I_CUSTOMCOLOR");
  buf[0] = 0; GetSetMediaItemInfo_String(item, "P_EXT:LYRF", buf, false);
  b.customLabel = strchr(buf, 'L') != nullptr;
  b.customColor = strchr(buf, 'C') != nullptr;
  buf[0] = 0; GetSetMediaItemInfo_String(item, "P_EXT:LYRM", buf, false);
  std::vector<int> ids;
  for (auto& s : SplitLines(std::string(buf)))
    for (size_t p = 0; p < s.size();)
    {
      size_t q = s.find(',', p);
      std::string tok = s.substr(p, q == std::string::npos ? std::string::npos : q - p);
      if (!tok.empty()) ids.push_back(atoi(tok.c_str()));
      if (q == std::string::npos) break;
      p = q + 1;
    }
  b.lines.clear();
  for (size_t i = 0; i < lines.size(); i++)
  {
    LyricLine l;
    l.text = lines[i];
    l.markerId = i < ids.size() ? ids[i] : -1;
    if (l.markerId >= 0 && usedIds)
    {
      if (usedIds->count(l.markerId)) l.markerId = -1; else usedIds->insert(l.markerId);
    }
    auto it = l.markerId >= 0 ? markers.find(l.markerId) : markers.end();
    if (it != markers.end()) l.pos = it->second.pos;
    else { l.markerId = -1; l.pos = -1; } // -1 = needs interpolation
    b.lines.push_back(l);
  }
  // interpolate missing
  for (size_t i = 0; i < b.lines.size(); i++)
  {
    if (b.lines[i].pos >= 0) continue;
    size_t j = i; while (j < b.lines.size() && b.lines[j].pos < 0) j++;
    double p0 = i == 0 ? b.start : b.lines[i - 1].pos;
    double p1 = j < b.lines.size() ? b.lines[j].pos : b.end;
    int cnt = (int)(j - i);
    for (size_t k = i; k < j; k++) b.lines[k].pos = p0 + (p1 - p0) * (double)(k - i + 1) / (cnt + 1);
    i = j;
  }
  if (!b.lines.empty()) b.lines[0].pos = b.start;
  return true;
}

// ------------------------------------------------------------------ REAPER write

void LyricDoc::NormaliseLines(LyricBlock& b)
{
  if (b.end <= b.start + kEps) b.end = b.start + BarLen();
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

void LyricDoc::WriteExtState(LyricBlock& b)
{
  std::string ids;
  for (size_t i = 0; i < b.lines.size(); i++) { if (i) ids += ","; ids += FormatStr("%d", b.lines[i].markerId); }
  std::vector<char> buf(ids.begin(), ids.end()); buf.push_back(0);
  GetSetMediaItemInfo_String(b.item, "P_EXT:LYRM", buf.data(), true);
  char flags[8] = ""; int n = 0;
  if (b.customLabel) flags[n++] = 'L';
  if (b.customColor) flags[n++] = 'C';
  flags[n] = 0;
  GetSetMediaItemInfo_String(b.item, "P_EXT:LYRF", flags, true);
}

void LyricDoc::WriteBlock(LyricBlock& b)
{
  if (!b.item) return;
  NormaliseLines(b);
  SetMediaItemInfo_Value(b.item, "D_POSITION", b.start);
  SetMediaItemInfo_Value(b.item, "D_LENGTH", b.end - b.start);
  SetMediaItemInfo_Value(b.item, "I_CUSTOMCOLOR", b.color);
  std::vector<char> name(b.label.begin(), b.label.end()); name.push_back(0);
  GetSetMediaItemInfo_String(b.item, "P_NAME", name.data(), true);
  std::string text = EscapeExt(b.Text());
  std::vector<char> ext(text.begin(), text.end()); ext.push_back(0);
  GetSetMediaItemInfo_String(b.item, "P_EXT:LYRT", ext.data(), true);
  char none[1] = "";
  GetSetMediaItemInfo_String(b.item, "P_NOTES", none, true);   // REAPER would draw notes inside the item
  b.notesPending = false;
  for (size_t i = 0; i < b.lines.size(); i++)
  {
    LyricLine& l = b.lines[i];
    std::string mname = i == 0 ? b.label : l.text;
    if (mname.empty()) mname = " ";
    if (l.markerId < 0) l.markerId = AddProjectMarker2(proj, false, l.pos, 0, mname.c_str(), -1, b.color);
    else SetProjectMarker4(proj, l.markerId, false, l.pos, 0, mname.c_str(), b.color, 0);
  }
  WriteExtState(b);
}

void LyricDoc::DeleteBlockMarkers(LyricBlock& b)
{
  for (auto& l : b.lines) if (l.markerId >= 0) { DeleteProjectMarker(proj, l.markerId, false); l.markerId = -1; }
}

void LyricDoc::DeleteBlockFromReaper(LyricBlock& b)
{
  DeleteBlockMarkers(b);
  if (b.item && ValidatePtr2(proj, b.item, "MediaItem*")) DeleteTrackMediaItem(track, b.item);
  b.item = nullptr;
}

void LyricDoc::FinishWrite()
{
  UpdateArrange();
  UpdateTimeline();
  lastStateCount = GetProjectStateChangeCount(proj);
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
void LyricDoc::FixOverlaps()
{
  double minLen = BarLen() / 4;
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

bool LyricDoc::BlocksDiffer(const LyricBlock& a, const LyricBlock& b) const
{
  if (a.label != b.label || a.color != b.color || a.customLabel != b.customLabel || a.customColor != b.customColor) return true;
  if (fabs(a.start - b.start) > kEps || fabs(a.end - b.end) > kEps) return true;
  if (a.lines.size() != b.lines.size()) return true;
  for (size_t i = 0; i < a.lines.size(); i++)
    if (a.lines[i].text != b.lines[i].text || fabs(a.lines[i].pos - b.lines[i].pos) > kEps || a.lines[i].markerId != b.lines[i].markerId) return true;
  return false;
}

// ------------------------------------------------------------------ sync REAPER -> model

bool LyricDoc::SyncFromReaper()
{
  if (!ValidateTrack()) return true;
  std::string oldText = ToText();
  std::vector<LyricBlock> old = blocks;
  std::map<std::string, const LyricBlock*> oldByGuid;
  for (auto& b : old) oldByGuid[b.guid] = &b;

  std::map<int, MarkerInfo> markers;
  ReadMarkers(markers);

  std::set<int> usedIds;
  std::vector<LyricBlock> fresh;
  int n = CountTrackMediaItems(track);
  for (int i = 0; i < n; i++)
  {
    MediaItem* it = GetTrackMediaItem(track, i);
    LyricBlock b;
    ReadBlockFromItem(it, b, markers, &usedIds);
    fresh.push_back(b);
  }
  // Only a sole block can be shown empty in the editor (a blank line separates blocks).
  // An empty item drawn/pasted in REAPER next to other blocks gets "..." so it shows (and
  // survives) in the editor. The editor itself never creates such a block.
  if (fresh.size() > 1)
    for (auto& b : fresh) if (b.Text().empty() && !b.lines.empty()) b.lines[0].text = "...";

  bool wrote = false;
  PreventUIRefresh(1);

  // blocks that vanished from REAPER: drop their markers
  for (auto& ob : old)
  {
    bool present = false;
    for (auto& fb : fresh) if (fb.guid == ob.guid) { present = true; break; }
    if (!present)
    {
      for (auto& l : ob.lines)
        if (l.markerId >= 0 && markers.count(l.markerId) && !usedIds.count(l.markerId)) { DeleteProjectMarker(proj, l.markerId, false); wrote = true; }
    }
  }

  for (auto& b : fresh)
  {
    auto it = oldByGuid.find(b.guid);
    bool needWrite = b.notesPending;
    if (it != oldByGuid.end())
    {
      const LyricBlock& o = *it->second;
      bool geomChanged = fabs(o.start - b.start) > kEps || fabs(o.end - b.end) > kEps;
      if (geomChanged)
      {
        // the item was moved/resized in REAPER: carry the lines along proportionally
        for (size_t i = 0; i < b.lines.size() && i < o.lines.size(); i++)
          if (b.lines[i].markerId == o.lines[i].markerId) b.lines[i].pos = o.lines[i].pos;
        ScaleLines(b, o.start, o.Len());
        needWrite = true;
      }
      else
      {
        // line 0 marker dragged -> move block start, keep end
        if (!b.lines.empty() && !o.lines.empty() && b.lines[0].markerId == o.lines[0].markerId)
        {
          auto m = markers.find(b.lines[0].markerId);
          if (m != markers.end() && fabs(m->second.pos - o.start) > kEps && m->second.pos < b.end - kEps)
          {
            b.start = m->second.pos;
            needWrite = true;
          }
        }
        // other markers dragged: keep inside block, reorder lines by time
        std::vector<LyricLine> sorted = b.lines;
        bool clamped = false;
        for (size_t i = 1; i < sorted.size(); i++)
        {
          double p = sorted[i].pos;
          double c = p; if (c < b.start) c = b.start; if (c > b.end - kEps) c = b.end - kEps;
          if (fabs(c - p) > kEps) { sorted[i].pos = c; clamped = true; }
        }
        std::stable_sort(sorted.begin() + (sorted.empty() ? 0 : 1), sorted.end(),
          [](const LyricLine& x, const LyricLine& y) { return x.pos < y.pos; });
        bool reordered = false;
        for (size_t i = 0; i < sorted.size(); i++) if (sorted[i].markerId != b.lines[i].markerId) reordered = true;
        b.lines = sorted;
        if (clamped || reordered) needWrite = true;
      }
      // label / colour edited in REAPER
      if (b.label != o.label) { b.customLabel = (b.label != AutoLabel((int)(&b - &fresh[0]), (int)fresh.size())); needWrite = true; }
      if (b.color != o.color && b.color != 0) { b.customColor = true; needWrite = true; }
      if (b.color == 0) { b.color = o.color; needWrite = true; }
      // marker colour/name drift
      for (size_t i = 0; i < b.lines.size(); i++)
      {
        auto m = markers.find(b.lines[i].markerId);
        if (m != markers.end() && m->second.color != b.color) needWrite = true;
      }
      // text edited through item notes: lines matched by LCS keep their markers
      if (b.Text() != o.Text())
      {
        std::vector<std::string> oldL, newL;
        for (auto& l : o.lines) oldL.push_back(l.text);
        for (auto& l : b.lines) newL.push_back(l.text);
        // b.lines came from the item notes with marker ids by index; rebuild from o by text match
        LyricBlock tmp = o; tmp.start = b.start; tmp.end = b.end; tmp.label = b.label; tmp.color = b.color;
        tmp.customLabel = b.customLabel; tmp.customColor = b.customColor; tmp.item = b.item; tmp.guid = b.guid;
        DiffLines(tmp, newL, nullptr, nullptr);
        b = tmp;
        needWrite = true;
      }
    }
    else
    {
      // new item on the lyric track (pasted / split / drawn by the user)
      if (b.color == 0) b.color = DefaultColorForLabel(b.label);
      for (auto& l : b.lines) l.markerId = -1; // never share markers with other blocks
      needWrite = true;
    }
    for (auto& l : b.lines) if (l.markerId < 0) needWrite = true;
    if (needWrite) { WriteBlock(b); wrote = true; }
  }

  std::stable_sort(fresh.begin(), fresh.end(), [](const LyricBlock& a, const LyricBlock& b) { return a.start < b.start; });
  blocks = fresh;
  // auto labels follow the order
  std::vector<LyricBlock> before = blocks;
  RelabelAuto();
  FixOverlaps();   // an item dragged/stretched over its neighbour gets trimmed back
  for (size_t i = 0; i < blocks.size(); i++)
    if (BlocksDiffer(blocks[i], before[i])) { WriteBlock(blocks[i]); wrote = true; }

  PreventUIRefresh(-1);
  if (wrote) FinishWrite();
  else lastStateCount = GetProjectStateChangeCount(proj);
  return ToText() != oldText;
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
  std::vector<bool> newAssigned(newLines.size(), false), oldUsed(b.lines.size(), false);
  for (auto& m : matches)
  {
    result[m.second] = b.lines[m.first];
    newAssigned[m.second] = true; oldUsed[m.first] = true;
  }
  // pair unmatched runs by index (edited lines keep their marker)
  size_t oi = 0, ni = 0;
  std::vector<std::pair<int, int>> bounds = matches;
  bounds.push_back({ (int)b.lines.size(), (int)newLines.size() });
  for (auto& m : bounds)
  {
    size_t oe = m.first, ne = m.second;
    while (oi < oe && ni < ne)
    {
      result[ni] = b.lines[oi]; result[ni].text = newLines[ni]; result[ni].pos = -1; // position re-interpolated below? keep old
      result[ni].pos = b.lines[oi].pos;
      newAssigned[ni] = true; oldUsed[oi] = true;
      oi++; ni++;
    }
    while (oi < oe) { if (!oldUsed[oi] && b.lines[oi].markerId >= 0) DeleteProjectMarker(proj, b.lines[oi].markerId, false); oi++; }
    while (ni < ne) { result[ni].text = newLines[ni]; result[ni].pos = -1; result[ni].markerId = -1; ni++; }
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

void LyricDoc::ApplyText(const std::string& utf8, LiveContext* live)
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
  auto matches = LcsMatch(oldTexts, newTexts);

  struct Slot { LyricBlock blk; bool isNew = false; bool exists = false; std::vector<std::string> lines; };
  std::vector<Slot> slots(paras.size());
  std::vector<bool> oldUsed(old.size(), false);
  for (auto& m : matches) { slots[m.second].blk = old[m.first]; slots[m.second].exists = true; oldUsed[m.first] = true; }
  std::vector<std::pair<int, int>> bounds = matches;
  bounds.push_back({ (int)old.size(), (int)paras.size() });
  std::vector<LyricBlock*> toDelete;
  size_t oi = 0, ni = 0;
  for (auto& m : bounds)
  {
    size_t oe = m.first, ne = m.second;
    while (oi < oe && ni < ne) { slots[ni].blk = old[oi]; slots[ni].exists = true; oldUsed[oi] = true; oi++; ni++; }
    while (oi < oe) { if (!oldUsed[oi]) toDelete.push_back(&old[oi]); oi++; }
    while (ni < ne) { slots[ni].isNew = true; ni++; }
    oi = oe + 1; ni = ne + 1;
  }
  for (size_t i = 0; i < slots.size(); i++) slots[i].lines = paras[i];

  bool structural = !toDelete.empty();
  for (auto& s : slots) if (s.isNew) structural = true;

  PreventUIRefresh(1);
  if (structural) Undo_BeginBlock2(proj);

  for (auto* d : toDelete) DeleteBlockFromReaper(*d);

  // place new blocks and diff lines, in order
  std::vector<LyricBlock> result;
  for (size_t i = 0; i < slots.size(); i++)
  {
    Slot& s = slots[i];
    // next kept block (for bounds)
    const LyricBlock* next = nullptr;
    for (size_t j = i + 1; j < slots.size(); j++) if (slots[j].exists) { next = &slots[j].blk; break; }
    LyricBlock* prev = result.empty() ? nullptr : &result.back();

    if (s.isNew)
    {
      LyricBlock nb;
      double def = DefaultBlockLen();
      if (live && live->valid && (GetTickCount() - live->stamp < 20000) && (!prev || live->pos >= prev->start) && (!next || live->pos < next->start))
      {
        nb.start = live->pos;
        live->valid = false;
        if (prev) { prev->end = nb.start; NormaliseLines(*prev); }
        nb.end = nb.start + def;
        if (next && nb.end > next->start) nb.end = next->start;
      }
      else if (prev && next && next->start > prev->end + 0.5) { nb.start = prev->end; nb.end = next->start; }
      else if (prev && next)
      {
        double os = prev->start, ol = prev->Len();
        nb.start = prev->start + ol / 2; nb.end = prev->end;
        prev->end = nb.start; ScaleLines(*prev, os, ol);
      }
      else if (prev) { nb.start = prev->end; nb.end = nb.start + def; }
      else if (next) { nb.start = next->start - def; if (nb.start < 0) nb.start = 0; nb.end = next->start; }
      else { nb.start = GetCursorPositionEx(proj); nb.end = nb.start + def; }
      if (nb.end <= nb.start + kEps) nb.end = nb.start + def;
      nb.item = AddMediaItemToTrack(track);
      char g[128] = ""; GetSetMediaItemInfo_String(nb.item, "GUID", g, false); nb.guid = g;
      for (auto& t : s.lines) { LyricLine l; l.text = t; l.pos = -1; nb.lines.push_back(l); }
      // interpolate all lines
      LyricBlock empty = nb; empty.lines.clear();
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
  RelabelAuto();
  FixOverlaps();

  // write what changed
  std::map<std::string, const LyricBlock*> oldByGuid;
  for (auto& b : old) oldByGuid[b.guid] = &b;
  for (auto& b : blocks)
  {
    auto it = oldByGuid.find(b.guid);
    if (it == oldByGuid.end() || BlocksDiffer(b, *it->second)) WriteBlock(b);
  }
  if (structural) Undo_EndBlock2(proj, "Lyrics: edit blocks", -1);
  PreventUIRefresh(-1);
  FinishWrite();
}

// ------------------------------------------------------------------ structural ops

void LyricDoc::MoveBlock(int from, int to)
{
  if (!ValidateTrack()) return;
  int n = (int)blocks.size();
  if (from < 0 || from >= n || to < 0 || to > n) return;
  if (to > from) to--;
  if (from == to) return;
  LyricBlock moving = blocks[from];
  blocks.erase(blocks.begin() + from);
  blocks.insert(blocks.begin() + to, moving);
  int lo = std::min(from, to), hi = std::max(from, to);
  double t = 1e18;
  for (int i = lo; i <= hi; i++) t = std::min(t, blocks[i].start);
  Undo_BeginBlock2(proj);
  PreventUIRefresh(1);
  for (int i = lo; i <= hi; i++)
  {
    LyricBlock& b = blocks[i];
    double len = b.Len(), d = t - b.start;
    b.start += d; b.end = b.start + len;
    for (auto& l : b.lines) l.pos += d;
    t = b.end;
  }
  RelabelAuto();
  FixOverlaps();
  for (auto& b : blocks) WriteBlock(b);
  PreventUIRefresh(-1);
  Undo_EndBlock2(proj, "Lyrics: move block", -1);
  FinishWrite();
}

void LyricDoc::SetLabel(int i, const std::string& label, bool custom)
{
  if (!ValidateTrack() || i < 0 || i >= (int)blocks.size()) return;
  Undo_BeginBlock2(proj);
  blocks[i].customLabel = custom;
  blocks[i].label = custom ? label : AutoLabel(i, (int)blocks.size());
  if (!blocks[i].customColor) blocks[i].color = DefaultColorForLabel(blocks[i].label);
  WriteBlock(blocks[i]);
  Undo_EndBlock2(proj, "Lyrics: rename block", -1);
  FinishWrite();
}

void LyricDoc::SetColor(int i, int nativeColor, bool custom)
{
  if (!ValidateTrack() || i < 0 || i >= (int)blocks.size()) return;
  Undo_BeginBlock2(proj);
  blocks[i].customColor = custom;
  blocks[i].color = custom ? nativeColor : DefaultColorForLabel(blocks[i].label);
  WriteBlock(blocks[i]);
  Undo_EndBlock2(proj, "Lyrics: block colour", -1);
  FinishWrite();
}

void LyricDoc::DeleteBlock(int i)
{
  if (!ValidateTrack() || i < 0 || i >= (int)blocks.size()) return;
  Undo_BeginBlock2(proj);
  PreventUIRefresh(1);
  DeleteBlockFromReaper(blocks[i]);
  blocks.erase(blocks.begin() + i);
  std::vector<LyricBlock> before = blocks;
  RelabelAuto();
  for (size_t k = 0; k < blocks.size(); k++) if (BlocksDiffer(blocks[k], before[k])) WriteBlock(blocks[k]);
  PreventUIRefresh(-1);
  Undo_EndBlock2(proj, "Lyrics: delete block", -1);
  FinishWrite();
}

void LyricDoc::AppendBlock(double atPos)
{
  if (!ValidateTrack()) return;
  Undo_BeginBlock2(proj);
  PreventUIRefresh(1);
  LyricBlock nb;
  double def = DefaultBlockLen();
  if (atPos >= 0) nb.start = atPos;
  else nb.start = blocks.empty() ? GetCursorPositionEx(proj) : blocks.back().end;
  nb.end = nb.start + def;
  nb.item = AddMediaItemToTrack(track);
  char g[128] = ""; GetSetMediaItemInfo_String(nb.item, "GUID", g, false); nb.guid = g;
  LyricLine l; l.text = ""; l.pos = nb.start; nb.lines.push_back(l);
  blocks.push_back(nb);
  std::stable_sort(blocks.begin(), blocks.end(), [](const LyricBlock& a, const LyricBlock& b) { return a.start < b.start; });
  RelabelAuto();
  FixOverlaps();
  for (auto& b : blocks) WriteBlock(b);
  PreventUIRefresh(-1);
  Undo_EndBlock2(proj, "Lyrics: add block", -1);
  FinishWrite();
}

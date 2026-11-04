#include "trackstyle.h"
#include "model.h"
#include "util.h"
#include <cctype>
#include <cstdio>
#include <cstring>

namespace TrackStyle
{
const char* const kAuto = "*";
static std::string s_layout = kAuto;
static const char* kStyledKey = "P_EXT:LYRSTYLE";

void LoadSettings() { s_layout = IniGetStr("track", "tcp_layout", kAuto); }
std::string LayoutSetting() { return s_layout; }
void SetLayoutSetting(const std::string& s, bool save) { s_layout = s; if (save) IniSetStr("track", "tcp_layout", s.c_str()); }

std::vector<std::string> TcpLayouts()
{
  std::vector<std::string> out;
  if (!ThemeLayout_GetLayout) return out;
  for (int i = 0; i < 512; i++)
  {
    char buf[512] = "";
    if (!ThemeLayout_GetLayout("tcp", i, buf, sizeof(buf))) break;
    std::string n = buf;
    if (n.empty()) continue;
    size_t pct = n.find("%_");   // "150%_A" etc. are the theme's hi-DPI variants of "A"
    if (pct != std::string::npos && pct > 0 && isdigit((unsigned char)n[0])) continue;
    bool dup = false;
    for (auto& o : out) if (o == n) dup = true;
    if (!dup) out.push_back(n);
  }
  return out;
}

std::string PickLayout(const std::string& setting, const std::vector<std::string>& layouts)
{
  if (setting != kAuto) return setting;
  for (auto& n : layouts)
  {
    std::string l = n;
    for (auto& c : l) c = (char)tolower((unsigned char)c);
    if (l.find("minimal") != std::string::npos) return n;
  }
  return "";
}

std::string ResolvedLayout() { return PickLayout(s_layout, TcpLayouts()); }

int Height() { return 48; }
int DefaultColor() { return ColorToNative(150, 120, 200) | 0x1000000; }   // soft violet

bool IsStyled(MediaTrack* t)
{
  char buf[64] = "";
  return GetSetMediaTrackInfo_String(t, kStyledKey, buf, false) && buf[0] == '1';
}

std::string IconFile()
{
  if (!GetResourcePath) return "";
  std::string p = GetResourcePath();
#ifdef _WIN32
  p += "\\Data\\track_icons\\speech.png";
#else
  p += "/Data/track_icons/speech.png";
#endif
  FILE* f = fopen(p.c_str(), "rb");
  if (!f) return "";
  fclose(f);
  return p;
}

static void SetStr(MediaTrack* t, const char* key, const std::string& v)
{
  std::string b = v;
  GetSetMediaTrackInfo_String(t, key, &b[0], true);
}

void Apply(MediaTrack* t, bool full, const std::string& iconFile)
{
  if (!t) return;
  SetStr(t, "P_TCP_LAYOUT", ResolvedLayout());
  if (!full) return;
  SetMediaTrackInfo_Value(t, "B_SHOWINMIXER", 0);        // no audio: nothing to mix
  SetMediaTrackInfo_Value(t, "I_HEIGHTOVERRIDE", Height());
  SetMediaTrackInfo_Value(t, "B_HEIGHTLOCK", 1);         // after the height (API requirement)
  SetMediaTrackInfo_Value(t, "I_RECARM", 0);
  SetMediaTrackInfo_Value(t, "I_RECMON", 0);
  SetMediaTrackInfo_Value(t, "I_RECINPUT", -1);
  char icon[4096] = "";
  GetSetMediaTrackInfo_String(t, "P_ICON", icon, false);
  if (!icon[0] && !iconFile.empty()) SetStr(t, "P_ICON", iconFile);   // keep one the user chose
  int col = (int)GetMediaTrackInfo_Value(t, "I_CUSTOMCOLOR");
  if (!(col & 0x1000000)) SetMediaTrackInfo_Value(t, "I_CUSTOMCOLOR", DefaultColor());
  SetStr(t, kStyledKey, "1");
}

int StyleNewTracks(ReaProject* p)
{
  if (!p) return 0;
  std::vector<MediaTrack*> tracks;
  LyricDoc::FindLyricTracks(p, tracks);
  std::vector<MediaTrack*> todo;
  for (auto* t : tracks) if (!IsStyled(t)) todo.push_back(t);
  if (todo.empty()) return 0;
  std::string icon = IconFile();
  PreventUIRefresh(1);
  for (auto* t : todo) Apply(t, true, icon);
  PreventUIRefresh(-1);
  TrackList_AdjustWindows(false);
  UpdateArrange();
  return (int)todo.size();
}

void ApplyAll(ReaProject* p, bool layoutOnly)
{
  if (!p) return;
  std::vector<MediaTrack*> tracks;
  LyricDoc::FindLyricTracks(p, tracks);
  if (tracks.empty()) return;
  std::string icon = layoutOnly ? "" : IconFile();
  Undo_BeginBlock2(p);
  PreventUIRefresh(1);
  for (auto* t : tracks) Apply(t, !layoutOnly, icon);
  PreventUIRefresh(-1);
  TrackList_AdjustWindows(false);
  UpdateArrange();
  Undo_EndBlock2(p, layoutOnly ? "Lyrics: lyric track layout" : "Lyrics: reset lyric track style", -1);
}
} // namespace TrackStyle

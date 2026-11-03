// reaper_lyrics - lyric tracks for REAPER (Windows + Linux)
#define REAPERAPI_IMPLEMENT
#include "plugin.h"
#include "editor.h"
#include "panel.h"
#include <cstring>
#include <cctype>

HINSTANCE g_hInst = nullptr;
HWND g_mainHwnd = nullptr;
LyricDoc g_doc;

static int g_cmdInsert = 0, g_cmdEditor = 0, g_cmdPanel = 0, g_cmdTracking = 0;
static gaccel_register_t g_accel[4];

static void InsertLyricTrack()
{
  ReaProject* p = EnumProjects(-1, nullptr, 0);
  if (!p) return;
  MediaTrack* t = LyricDoc::CreateLyricTrack(p);
  if (!t) return;
  LyricEditor::Get().BindTrack(t);
  g_doc.AppendBlock();
  LyricEditor::Get().Show(true);
  LyricEditor::Get().RefreshText();
}

static bool HookCommand(int cmd, int flag)
{
  (void)flag;
  if (!cmd) return false;
  if (cmd == g_cmdInsert) { InsertLyricTrack(); return true; }
  if (cmd == g_cmdEditor) { LyricEditor::Get().Toggle(); return true; }
  if (cmd == g_cmdPanel) { LyricsPanel::Get().Toggle(); return true; }
  if (cmd == g_cmdTracking) { LyricEditor::Get().SetTracking(!LyricEditor::Get().Tracking()); return true; }
  return false;
}

static int ToggleAction(int cmd)
{
  if (cmd == g_cmdEditor) return LyricEditor::Get().IsVisible() ? 1 : 0;
  if (cmd == g_cmdPanel) return LyricsPanel::Get().IsVisible() ? 1 : 0;
  if (cmd == g_cmdTracking) return LyricEditor::Get().Tracking() ? 1 : 0;
  return -1;
}

// Adds "Insert lyric track" right after "Insert track from template" in the
// track-panel context menu and the main Track menu.
static void MenuHook(const char* menuid, void* menu, int flag)
{
  if (flag != 0 || !menu) return;
  bool tcp = !strcmp(menuid, "Track control panel context");
  bool trackMenu = !strcmp(menuid, "Main track");
  if (!tcp && !trackMenu) return;
  HMENU hm = (HMENU)menu;
  int n = GetMenuItemCount(hm), at = -1;
  for (int i = 0; i < n; i++)
  {
    std::string s = MenuItemText(hm, i);
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    if (s.find("template") != std::string::npos) { at = i + 1; break; }
  }
  if (at < 0) at = 0;
  InsertMenu(hm, at, MF_BYPOSITION | MF_STRING, g_cmdInsert, "Insert lyric track");
}

static void TimerFn()
{
  LyricEditor::Get().OnTimer();
  LyricsPanel::Get().OnTimer();
}

static void RegisterAction(reaper_plugin_info_t* rec, int slot, const char* idstr, const char* desc, int* cmdOut)
{
  int id = rec->Register("command_id", (void*)idstr);
  *cmdOut = id;
  g_accel[slot].accel.fVirt = 0; g_accel[slot].accel.key = 0; g_accel[slot].accel.cmd = (WORD)id;
  g_accel[slot].desc = desc;
  rec->Register("gaccel", &g_accel[slot]);
}

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t* rec)
{
  if (!rec)
  {
    if (plugin_register)
    {
      plugin_register("-timer", (void*)TimerFn);
      plugin_register("-hookcommand", (void*)HookCommand);
      plugin_register("-hookcustommenu", (void*)MenuHook);
      plugin_register("-toggleaction", (void*)ToggleAction);
    }
    return 0;
  }
  if (rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc) return 0;
  if (REAPERAPI_LoadAPI(rec->GetFunc) != 0) return 0;

  g_hInst = hInstance;
  g_mainHwnd = rec->hwnd_main;

  RegisterAction(rec, 0, "LYRICS_INSERT_TRACK", "Lyrics: Insert lyric track", &g_cmdInsert);
  RegisterAction(rec, 1, "LYRICS_TOGGLE_EDITOR", "Lyrics: Show/hide lyric editor", &g_cmdEditor);
  RegisterAction(rec, 2, "LYRICS_TOGGLE_PANEL", "Lyrics: Show/hide lyrics panel", &g_cmdPanel);
  RegisterAction(rec, 3, "LYRICS_TOGGLE_TRACKING", "Lyrics: Toggle editor tracking (follow playback)", &g_cmdTracking);

  rec->Register("hookcommand", (void*)HookCommand);
  rec->Register("toggleaction", (void*)ToggleAction);
  rec->Register("hookcustommenu", (void*)MenuHook);
  rec->Register("timer", (void*)TimerFn);
  return 1;
}

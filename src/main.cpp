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

static bool MenuHasCommand(HMENU hm, int cmd)
{
  int n = GetMenuItemCount(hm);
  for (int i = 0; i < n; i++)
    if ((int)GetMenuItemID(hm, i) == cmd) return true;
  return false;
}

// Adds "Insert lyric track" right after "Insert track from template" in the
// track-panel context menus (empty area and on a track) and the main Track menu.
// Handles both flag 0 (menu built) and flag 1 (about to show): context menus may be
// built lazily or rebuilt, so insert whenever our item is missing.
static void MenuHook(const char* menuid, void* menu, int flag)
{
  if ((flag != 0 && flag != 1) || !menu || !menuid || !g_cmdInsert) return;
  if (strcmp(menuid, "Empty TCP context") && strcmp(menuid, "Track control panel context") &&
      strcmp(menuid, "Main track")) return;
  HMENU hm = (HMENU)menu;
  if (MenuHasCommand(hm, g_cmdInsert)) return;
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

// REAPER routes keystrokes through its accelerator processing first. Without this hook,
// keys aimed at the editor are treated as dialog mnemonics / REAPER shortcuts and typed
// characters never arrive. While the editor has focus we deliver keys to it ourselves,
// except F-keys and Ctrl shortcuts the editor doesn't use, which go to REAPER (Ctrl+S etc).
static int TranslateAccel(MSG* msg, accelerator_register_t*)
{
  HWND ed = LyricEditor::Get().Hwnd();
  if (!ed || !msg->hwnd || (msg->hwnd != ed && !IsChild(ed, msg->hwnd))) return 0;
  UINT m = msg->message;
  if (m == WM_SYSKEYDOWN || m == WM_SYSKEYUP) return 0;          // Alt combos: REAPER's business
  if (m == WM_KEYDOWN)
  {
    int vk = (int)msg->wParam;
    if (vk >= VK_F1 && vk <= VK_F24) return -666;
    if (CtrlDown() && vk >= 'A' && vk <= 'Z' && !strchr("ACXVZY", vk)) return -666;
  }
#ifdef _WIN32
  if (m == WM_KEYDOWN) TranslateMessage(msg);                     // generates the WM_CHAR we insert
  DispatchMessage(msg);
  return 1;
#else
  return -1;                                                      // SWELL delivers characters in WM_KEYDOWN
#endif
}

static accelerator_register_t g_accelHook = { TranslateAccel, true, nullptr };

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
      plugin_register("-accelerator", (void*)&g_accelHook);
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
  rec->Register("accelerator", &g_accelHook);
  return 1;
}

// reaper_lyrics - lyric tracks for REAPER (Windows + Linux)
#define REAPERAPI_IMPLEMENT
#include "plugin.h"
#include "editor.h"
#include "panel.h"
#include "trackstyle.h"
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
  g_doc.AppendBlock();                 // the lyric item + its first block (empty, no text)
  LyricEditor::Get().OpenForTrack(t);
  LyricEditor::Get().StartNewBlock();
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

// ------------------------------------------------------------------ double-click a lyric item
// REAPER has no per-item double-click hook, so the arrange view's window procedure is
// subclassed: a double-click on a lyric item opens the sequencer and is not passed on;
// everything else goes to REAPER untouched.

static HWND g_arrange = nullptr;
static WNDPROC g_arrangeOldProc = nullptr;
static DWORD g_lastDownTick = 0;
static POINT g_lastDownPt = { 0, 0 };
static bool g_swallowUp = false;

static DWORD DoubleClickMs()
{
#ifdef _WIN32
  return GetDoubleClickTime();
#else
  return 400;
#endif
}

static LRESULT CALLBACK ArrangeProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK)
  {
    POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    bool dbl = msg == WM_LBUTTONDBLCLK;
    DWORD now = GetTickCount();
    if (!dbl) dbl = now - g_lastDownTick <= DoubleClickMs() && abs(pt.x - g_lastDownPt.x) <= 4 && abs(pt.y - g_lastDownPt.y) <= 4;
    g_lastDownTick = dbl ? 0 : now;
    g_lastDownPt = pt;
    if (dbl && !ShiftDown() && !CtrlDown())
    {
      POINT sp = pt; ClientToScreen(h, &sp);
      MediaItem* it = GetItemFromPoint(sp.x, sp.y, true, nullptr);
      if (it && LyricDoc::IsLyricItem(it))
      {
        g_swallowUp = true;
        LyricEditor::Get().OpenForTrack(GetMediaItem_Track(it));
        return 0;
      }
    }
  }
  else if (msg == WM_LBUTTONUP && g_swallowUp) { g_swallowUp = false; return 0; }
#ifdef _WIN32
  return CallWindowProcW(g_arrangeOldProc, h, msg, wp, lp);
#else
  return CallWindowProc(g_arrangeOldProc, h, msg, wp, lp);
#endif
}

static void HookArrange()
{
  if (g_arrange || !g_mainHwnd) return;
  HWND a = GetDlgItem(g_mainHwnd, 1000);   // REAPER's arrange view ("trackview")
  if (!a) return;
  g_arrange = a;
#ifdef _WIN32
  g_arrangeOldProc = (WNDPROC)SetWindowLongPtrW(a, GWLP_WNDPROC, (LONG_PTR)ArrangeProc);
#else
  g_arrangeOldProc = (WNDPROC)SetWindowLong(a, GWL_WNDPROC, (LONG_PTR)ArrangeProc);
#endif
  if (!g_arrangeOldProc) g_arrange = nullptr;
}

static void UnhookArrange()
{
  if (!g_arrange || !g_arrangeOldProc) return;
#ifdef _WIN32
  if ((WNDPROC)GetWindowLongPtrW(g_arrange, GWLP_WNDPROC) == ArrangeProc)
    SetWindowLongPtrW(g_arrange, GWLP_WNDPROC, (LONG_PTR)g_arrangeOldProc);
#else
  if ((WNDPROC)GetWindowLong(g_arrange, GWL_WNDPROC) == ArrangeProc)
    SetWindowLong(g_arrange, GWL_WNDPROC, (LONG_PTR)g_arrangeOldProc);
#endif
  g_arrange = nullptr; g_arrangeOldProc = nullptr;
}

static void TimerFn()
{
  HookArrange();   // the arrange view may not exist yet when the plugin loads
  static DWORD lastStyle = 0;
  DWORD now = GetTickCount();
  if (now - lastStyle >= 1000)   // lyric tracks from older projects get the lyric track look
  {
    lastStyle = now;
    TrackStyle::StyleNewTracks(EnumProjects(-1, nullptr, 0));
  }
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
    UnhookArrange();
    return 0;
  }
  if (rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc) return 0;
  if (REAPERAPI_LoadAPI(rec->GetFunc) != 0) return 0;

  g_hInst = hInstance;
  g_mainHwnd = rec->hwnd_main;

  RegisterAction(rec, 0, "LYRICS_INSERT_TRACK", "Lyrics: Insert lyric track", &g_cmdInsert);
  RegisterAction(rec, 1, "LYRICS_TOGGLE_EDITOR", "Lyrics: Show/hide lyric sequencer", &g_cmdEditor);
  RegisterAction(rec, 2, "LYRICS_TOGGLE_PANEL", "Lyrics: Show/hide lyrics panel", &g_cmdPanel);
  RegisterAction(rec, 3, "LYRICS_TOGGLE_TRACKING", "Lyrics: Toggle editor tracking (follow playback)", &g_cmdTracking);

  TrackStyle::LoadSettings();
  rec->Register("hookcommand", (void*)HookCommand);
  rec->Register("toggleaction", (void*)ToggleAction);
  rec->Register("hookcustommenu", (void*)MenuHook);
  rec->Register("timer", (void*)TimerFn);
  rec->Register("accelerator", &g_accelHook);
  return 1;
}

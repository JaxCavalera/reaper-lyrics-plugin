// reaper_lyrics - lyric tracks for REAPER (Windows + Linux)
#define REAPERAPI_IMPLEMENT
#include "plugin.h"
#include "editor.h"
#include "panel.h"
#include "trackstyle.h"
#include "util.h"
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
// characters never arrive. Only while the lyric text has been clicked into (or a label is
// being renamed) do we deliver keys to the editor ourselves, and even then F-keys and Ctrl
// shortcuts the editor doesn't use go to REAPER (Ctrl+S etc). Otherwise the sequencer
// merely having focus changes nothing: every key goes to REAPER (space = play, ...).
static int TranslateAccel(MSG* msg, accelerator_register_t*)
{
  HWND ed = LyricEditor::Get().Hwnd();
  if (!ed || !msg->hwnd || (msg->hwnd != ed && !IsChild(ed, msg->hwnd))) return 0;
  UINT m = msg->message;
  KeyRoute r = RouteEditorKey(LyricEditor::Get().WantsKeys(), m, (int)msg->wParam, CtrlDown());
  if (r == KEY_DEFAULT) return 0;
  if (r == KEY_TO_REAPER) return -666;                             // to REAPER's main window
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

// ------------------------------------------------------------------ open windows, per project
// Saving a project records whether the lyric sequencer and the lyrics panel were open (and
// which lyric track the sequencer showed); loading it opens or closes them to match. Projects
// saved without that line leave the windows as they are. Undo states don't carry it.

static bool g_restorePending = false;
static WindowState g_restore;

static std::string TrackGuidStr(MediaTrack* t)
{
  char buf[64] = "";
  if (t) guidToString(GetTrackGUID(t), buf);
  return buf;
}

static MediaTrack* TrackByGuid(ReaProject* p, const std::string& guid)
{
  if (!p || guid.empty()) return nullptr;
  int n = CountTracks(p);
  for (int i = 0; i < n; i++) { MediaTrack* t = GetTrack(p, i); if (t && TrackGuidStr(t) == guid) return t; }
  return nullptr;
}

static void BeginLoadState(bool isUndo, project_config_extension_t*)
{
  if (!isUndo) g_restorePending = false;
}

static bool ProcessStateLine(const char* line, ProjectStateContext*, bool isUndo, project_config_extension_t*)
{
  WindowState s;
  if (!ParseWindowState(line, &s)) return false;
  if (!isUndo) { g_restore = s; g_restorePending = true; }
  return true;
}

static void SaveState(ProjectStateContext* ctx, bool isUndo, project_config_extension_t*)
{
  if (isUndo) return;   // opening a window is not an undoable change
  WindowState s;
  s.sequencer = LyricEditor::Get().IsVisible();
  s.panel = LyricsPanel::Get().IsVisible();
  if (g_doc.IsBound()) s.trackGuid = TrackGuidStr(g_doc.track);
  ctx->AddLine("%s", FormatWindowState(s).c_str());
}

static project_config_extension_t g_projConfig = { ProcessStateLine, SaveState, BeginLoadState, nullptr };

// after the project has finished loading (the timer runs once the load is done)
static void RestoreWindows()
{
  if (!g_restorePending) return;
  g_restorePending = false;
  ReaProject* p = EnumProjects(-1, nullptr, 0);
  MediaTrack* t = g_restore.sequencer ? TrackByGuid(p, g_restore.trackGuid) : nullptr;
  if (t) LyricEditor::Get().OpenForTrack(t);
  else LyricEditor::Get().Show(g_restore.sequencer);
  LyricsPanel::Get().Show(g_restore.panel);
}

static void TimerFn()
{
  RestoreWindows();
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
      plugin_register("-projectconfig", (void*)&g_projConfig);
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
  rec->Register("projectconfig", &g_projConfig);   // which lyric windows are open, per project
  return 1;
}

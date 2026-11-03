#include "platform.h"
#include "util.h"
#include <vector>

#ifdef _WIN32
static std::wstring ToWide(const char* s, int len)
{
  if (len <= 0) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s, len, nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s, len, &w[0], n);
  return w;
}
static std::string ToUtf8(const wchar_t* w, int len)
{
  if (len <= 0) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, nullptr, nullptr);
  return s;
}
#endif

const char* UiFontFace()
{
#ifdef _WIN32
  return "Segoe UI";
#else
  return "Sans";
#endif
}

HFONT MakeFont(int px, bool bold)
{
  return CreateFont(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
#ifdef _WIN32
    CLEARTYPE_QUALITY,
#else
    DEFAULT_QUALITY,
#endif
    DEFAULT_PITCH, UiFontFace());
}

int DrawTextU8(HDC dc, const std::string& s, RECT* r, UINT flags)
{
#ifdef _WIN32
  std::wstring w = ToWide(s.c_str(), (int)s.size());
  return DrawTextW(dc, w.c_str(), (int)w.size(), r, flags);
#else
  return DrawText(dc, s.c_str(), (int)s.size(), r, flags);
#endif
}

int TextWidthU8(HDC dc, const char* s, int len)
{
  if (len <= 0) return 0;
  RECT r = { 0, 0, 0, 0 };
#ifdef _WIN32
  std::wstring w = ToWide(s, len);
  DrawTextW(dc, w.c_str(), (int)w.size(), &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
#else
  DrawText(dc, s, len, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
#endif
  return r.right - r.left;
}

int FontLineHeight(HDC dc)
{
  TEXTMETRIC tm;
  GetTextMetrics(dc, &tm);
  return tm.tmHeight + 3;
}

// ---------------------------------------------------------------- clipboard

std::string ClipboardGetText(HWND owner)
{
  std::string out;
  if (!OpenClipboard(owner)) return out;
#ifdef _WIN32
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  if (h) { const wchar_t* w = (const wchar_t*)GlobalLock(h); if (w) { out = ToUtf8(w, (int)wcslen(w)); GlobalUnlock(h); } }
#else
  HANDLE h = GetClipboardData(CF_TEXT);
  if (h) { const char* p = (const char*)GlobalLock(h); if (p) { out = p; GlobalUnlock(h); } }
#endif
  CloseClipboard();
  return out;
}

void ClipboardSetText(HWND owner, const std::string& s)
{
  if (!OpenClipboard(owner)) return;
  EmptyClipboard();
#ifdef _WIN32
  std::wstring w = ToWide(s.c_str(), (int)s.size());
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
  if (h) { wchar_t* p = (wchar_t*)GlobalLock(h); memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
#else
  HANDLE h = GlobalAlloc(0, (int)s.size() + 1);
  if (h) { char* p = (char*)GlobalLock(h); memcpy(p, s.c_str(), s.size() + 1); GlobalUnlock(h); SetClipboardData(CF_TEXT, h); }
#endif
  CloseClipboard();
}

// ---------------------------------------------------------------- colour picker

bool PickColor(HWND owner, COLORREF* inout)
{
  static COLORREF custom[16] = { 0 };
#ifdef _WIN32
  CHOOSECOLORW cc = { sizeof(cc) };
  cc.hwndOwner = owner; cc.lpCustColors = custom; cc.rgbResult = *inout;
  cc.Flags = CC_RGBINIT | CC_FULLOPEN;
  if (!ChooseColorW(&cc)) return false;
  *inout = cc.rgbResult;
  return true;
#else
  return SWELL_ChooseColor(owner, inout, 16, custom);
#endif
}

// ---------------------------------------------------------------- keyboard

bool ShiftDown() { return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0; }
bool CtrlDown() { return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0; }

// ---------------------------------------------------------------- menus

std::string MenuItemText(HMENU m, int pos)
{
  char buf[256] = "";
#ifdef _WIN32
  GetMenuStringA(m, pos, buf, sizeof(buf), MF_BYPOSITION);
#else
  MENUITEMINFO mi = { sizeof(mi) };
  mi.fMask = MIIM_TYPE; mi.dwTypeData = buf; mi.cch = sizeof(buf);
  GetMenuItemInfo(m, pos, TRUE, &mi);
#endif
  return buf;
}

// ---------------------------------------------------------------- windows

extern HINSTANCE g_hInst;

HWND CreateDlg(int resid, HWND parent, DLGPROC proc, LPARAM param)
{
#ifdef _WIN32
  return CreateDialogParamW(g_hInst, MAKEINTRESOURCEW(resid), parent, proc, param);
#else
  return CreateDialogParam(g_hInst, MAKEINTRESOURCE(resid), parent, proc, param);
#endif
}

void SetUserData(HWND h, void* p)
{
#ifdef _WIN32
  SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)p);
#else
  SetWindowLong(h, GWL_USERDATA, (LONG_PTR)p);
#endif
}

void* GetUserData(HWND h)
{
#ifdef _WIN32
  return (void*)GetWindowLongPtr(h, GWLP_USERDATA);
#else
  return (void*)GetWindowLong(h, GWL_USERDATA);
#endif
}

void SaveWindowRect(const char* section, HWND h)
{
#ifdef _WIN32
  if (IsIconic(h)) return;
#endif
  RECT r; GetWindowRect(h, &r);
  if (r.right - r.left < 50 || r.bottom - r.top < 50) return;
  IniSetInt(section, "x", r.left);
  IniSetInt(section, "y", r.top);
  IniSetInt(section, "w", r.right - r.left);
  IniSetInt(section, "h", r.bottom - r.top);
}

bool LoadWindowRect(const char* section, RECT* out)
{
  int w = IniGetInt(section, "w", 0), h = IniGetInt(section, "h", 0);
  if (w < 50 || h < 50) return false;
  int x = IniGetInt(section, "x", 100), y = IniGetInt(section, "y", 100);
  RECT r = { x, y, x + w, y + h };
#ifdef _WIN32
  if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) { x = 100; y = 100; }
#else
  RECT vp; SWELL_GetViewPort(&vp, &r, true);
  if (x < vp.left - w + 50 || x > vp.right - 50 || y < vp.top - 20 || y > vp.bottom - 50) { x = vp.left + 100; y = vp.top + 100; }
#endif
  out->left = x; out->top = y; out->right = x + w; out->bottom = y + h;
  return true;
}

void ApplyWindowRect(HWND h, const RECT& r)
{
  SetWindowPos(h, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void ShowWindowNoActivate(HWND h)
{
  ShowWindow(h, SW_SHOWNA);
}

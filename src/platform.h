// The thin layer between the UI and the OS: plain Win32 on Windows, SWELL on Linux.
// Everything here takes UTF-8.
#pragma once

#ifdef _WIN32
  #include <windows.h>
  #include <windowsx.h>
  #include <commdlg.h>
  #include <shellapi.h>
  #include "../WDL/WDL/win32_utf8.h"
  #define PLAT_SCROLL_SIGN 1
#else
  #include "../WDL/WDL/swell/swell.h"
  #include <string.h>
  // swell-types.h defines min/max as macros; they break <algorithm>
  #ifdef min
    #undef min
  #endif
  #ifdef max
    #undef max
  #endif
  #ifndef GET_Y_LPARAM
    #define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
  #endif
#endif
#include "../WDL/WDL/wdltypes.h"   // WDL_DLGRET
#include <string>

// drawing
int DrawTextU8(HDC dc, const std::string& s, RECT* r, UINT flags);
int TextWidthU8(HDC dc, const char* s, int len);
int FontLineHeight(HDC dc);
HFONT MakeFont(int px, bool bold);
const char* UiFontFace();

// clipboard
std::string ClipboardGetText(HWND owner);
void ClipboardSetText(HWND owner, const std::string& s);

// colour picker (modal)
bool PickColor(HWND owner, COLORREF* inout);

// keyboard: are Shift / Ctrl down right now
bool ShiftDown();
bool CtrlDown();

// menus
std::string MenuItemText(HMENU m, int pos);

// windows
HWND CreateDlg(int resid, HWND parent, DLGPROC proc, LPARAM param);
void SetUserData(HWND h, void* p);
void* GetUserData(HWND h);
void SaveWindowRect(const char* section, HWND h);
bool LoadWindowRect(const char* section, RECT* out);
void ApplyWindowRect(HWND h, const RECT& r);
void ShowWindowNoActivate(HWND h);

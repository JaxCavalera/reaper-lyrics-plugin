#pragma once
#include "plugin.h"

class LyricsPanel
{
public:
  static LyricsPanel& Get();
  void Show(bool show);
  void Toggle() { Show(!IsVisible()); }
  bool IsVisible() const;
  void OnTimer();

private:
  LyricsPanel() {}
  void Create();
  void Paint(HDC dc, RECT rc);
  INT_PTR Handle(HWND h, UINT msg, WPARAM wp, LPARAM lp);
  static WDL_DLGRET DlgProc(HWND, UINT, WPARAM, LPARAM);

  HWND m_hwnd = nullptr;
  int m_curBlock = -2, m_curLine = -2;
  double m_lastPos = -1;
};

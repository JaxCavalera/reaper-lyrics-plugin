#pragma once
#include "plugin.h"
#include <string>

// The panel's line rows (previous, current, next, next-but-one) for a client area w x h:
// font sizes and real row heights, rows placed so nothing overlaps. Draws the texts given
// (empty = row left blank) and reports where each row went.
struct PanelRowLayout { int big = 0, med = 0, hBig = 0, hMed = 0, gap = 0; int tops[4] = { 0, 0, 0, 0 }; int RowH(int i) const { return i == 1 ? hBig : hMed; } };
PanelRowLayout LayoutPanelRows(HDC dc, int w, int h);
void DrawPanelRows(HDC dc, int w, const PanelRowLayout& L, const std::string text[4], const COLORREF col[4]);

class LyricsPanel
{
public:
  static LyricsPanel& Get();
  void Show(bool show);
  void Toggle() { Show(!IsVisible()); }
  bool IsVisible() const;
  void OnTimer();
  HWND Hwnd() const { return m_hwnd; }

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

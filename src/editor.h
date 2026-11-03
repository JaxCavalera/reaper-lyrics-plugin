#pragma once
#include "plugin.h"
#include "textedit.h"
#include <string>
#include <vector>

class LyricEditor
{
public:
  static LyricEditor& Get();

  void Show(bool show);
  void Toggle() { Show(!IsVisible()); }
  bool IsVisible() const;
  void BindTrack(MediaTrack* t);
  void OnTimer();          // called from the plugin timer (UI thread)
  void RefreshText();      // model -> text widget, caret preserving
  bool Tracking() const { return m_tracking; }
  void SetTracking(bool on);

private:
  struct Layout { int first = 0, last = 0; };          // text-line indices of a block
  struct Button { RECT r; std::string text; int id; bool toggle; };

  LyricEditor();
  void Create();
  void LayoutControls();
  void RebuildLayout();
  void OnEditChanged();
  void OnBeforeNewline();
  void CaretInfo(int* block, int* line, int* col);
  void UpdateStatus();
  void AutoScroll();
  void Paint(HDC dc);
  void PaintToolbar(HDC dc);
  void PaintGutter(HDC dc);
  void PaintTextBackground(HDC dc);
  int BlockAtY(int y);
  int DropIndexAtY(int y);
  int ButtonAt(int x, int y);
  void OnButton(int id);
  void ShowBlockMenu(int block, POINT screenPt);
  void ShowTrackMenu(POINT screenPt);
  void BeginRename(int block);
  void CommitRename(bool apply);
  void Invalidate() { if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE); }
  INT_PTR Handle(HWND h, UINT msg, WPARAM wp, LPARAM lp);
  static WDL_DLGRET DlgProc(HWND, UINT, WPARAM, LPARAM);

  HWND m_hwnd = nullptr;
  TextEdit m_text;
  TextEdit m_rename;
  bool m_renaming = false; int m_renameBlock = -1;
  HFONT m_font = nullptr, m_fontUi = nullptr, m_fontChip = nullptr;
  std::vector<Layout> m_layout;
  std::vector<Button> m_buttons;
  std::string m_status, m_trackName;
  RECT m_toolbar = { 0, 0, 0, 0 }, m_gutter = { 0, 0, 0, 0 };
  bool m_tracking = true;
  int m_curBlock = -1, m_curLine = -1, m_scrolledBlock = -1;
  int m_dragBlock = -1, m_dropIndex = -1; POINT m_dragStart = { 0, 0 }; bool m_dragging = false;
  bool m_textCapture = false;
  unsigned long m_lastTrackScan = 0;
  LiveContext m_live;
};

// A small self-drawn multi-line text widget. It lives inside a parent window's client
// area (no HWND of its own) so it behaves identically on Win32 and SWELL.
#pragma once
#include "platform.h"
#include <string>
#include <vector>
#include <functional>

struct TEPos { int line = 0, col = 0; };   // col = byte offset into the UTF-8 line
inline bool operator<(const TEPos& a, const TEPos& b) { return a.line < b.line || (a.line == b.line && a.col < b.col); }
inline bool operator==(const TEPos& a, const TEPos& b) { return a.line == b.line && a.col == b.col; }

class TextEdit
{
public:
  explicit TextEdit(bool singleLine = false);

  void SetParent(HWND h) { m_parent = h; }
  void SetRect(const RECT& r);
  const RECT& Rect() const { return m_rc; }
  void SetFont(HFONT f);
  void SetReadOnly(bool ro) { m_readOnly = ro; }

  void SetText(const std::string& utf8);
  std::string GetText() const;
  const std::vector<std::string>& Lines() const { return m_lines; }

  int LineH() const { return m_lineH; }
  int TextTop() const { return m_rc.top + m_padY; }
  int LineY(int line) const { return TextTop() + (line - m_firstLine) * m_lineH; }
  int FirstVisibleLine() const { return m_firstLine; }
  int VisibleLines() const;
  void ScrollToLine(int line);
  void ScrollBy(int lines);
  void EnsureCaretVisible();

  TEPos Caret() const { return m_caret; }
  void SetCaret(TEPos p, bool keepAnchor = false);
  void SelectAll();
  bool HasSelection() const { return !(m_caret == m_anchor); }

  void Paint(HDC dc);
  bool OnMouse(UINT msg, WPARAM wp, int x, int y);    // true if consumed
  bool OnKey(UINT msg, WPARAM wp, LPARAM lp);          // true if consumed
  void OnWheel(int delta);
  bool HitTest(int x, int y) const;                     // inside the widget rect
  void SetFocus(bool f);
  bool Focused() const { return m_focus; }
  void Tick();                                           // caret blink; call ~every 30ms

  std::function<void()> onChange;          // text changed
  std::function<void()> onBeforeNewline;   // Return is about to insert a line break
  std::function<void()> onScroll;
  std::function<void(HDC)> paintBackground; // called after background fill, before text
  std::function<void()> onEscape;
  std::function<void()> onEnterSingle;     // single-line mode: Return pressed

  COLORREF colBg = RGB(30, 30, 30), colFg = RGB(222, 222, 222), colSel = RGB(60, 90, 140), colCaret = RGB(255, 255, 255);
  int m_padX = 10, m_padY = 6;

private:
  void InsertText(const std::string& s);
  void InsertCodepoint(unsigned cp);
  void DeleteSelection();
  void Backspace();
  void DeleteForward();
  void Newline();
  void MoveCaret(int dLine, int dCol, bool select);
  void MoveLineEdge(bool home, bool select);
  void MoveDocEdge(bool home, bool select);
  void Page(int dir, bool select);
  bool HandleVK(int vk, bool shift, bool ctrl);
  std::string SelectedText() const;
  void SelRange(TEPos* a, TEPos* b) const;
  int ColX(HDC dc, int line, int col) const;
  int ColFromX(HDC dc, int line, int x) const;
  TEPos PosFromPoint(int x, int y) const;
  void PushUndo(int kind);
  void Undo();
  void Redo();
  void Changed();
  void ClampScroll();
  RECT ScrollbarRect() const;
  RECT ThumbRect() const;

  struct Snap { std::vector<std::string> lines; TEPos caret, anchor; };

  HWND m_parent = nullptr;
  RECT m_rc = { 0, 0, 0, 0 };
  HFONT m_font = nullptr;
  int m_lineH = 20;
  bool m_single = false, m_readOnly = false, m_focus = false;
  std::vector<std::string> m_lines;
  TEPos m_caret, m_anchor;
  int m_firstLine = 0;
  int m_preferredX = -1;
  bool m_drag = false, m_thumbDrag = false; int m_thumbDragOff = 0;
  bool m_caretOn = true; unsigned long m_blinkTick = 0;
  std::vector<Snap> m_undo, m_redo; int m_lastUndoKind = -1; unsigned long m_lastEditTick = 0;
#ifdef _WIN32
  unsigned m_pendingHighSurrogate = 0;
#endif
};

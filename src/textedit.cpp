#include "textedit.h"
#include "util.h"
#include <algorithm>

static const int kScrollW = 10;

TextEdit::TextEdit(bool singleLine) : m_single(singleLine)
{
  m_lines.push_back("");
  if (singleLine) { m_padX = 6; m_padY = 2; }
}

void TextEdit::SetRect(const RECT& r) { m_rc = r; ClampScroll(); }

void TextEdit::SetFont(HFONT f)
{
  m_font = f;
  if (m_parent)
  {
    HDC dc = GetDC(m_parent);
    HGDIOBJ old = SelectObject(dc, f);
    m_lineH = FontLineHeight(dc);
    SelectObject(dc, old);
    ReleaseDC(m_parent, dc);
  }
}

void TextEdit::SetText(const std::string& utf8)
{
  m_lines = SplitLines(utf8);
  if (m_single && m_lines.size() > 1) m_lines.resize(1);
  if (m_lines.empty()) m_lines.push_back("");
  m_caret = m_anchor = TEPos();
  m_undo.clear(); m_redo.clear(); m_lastUndoKind = -1;
  ClampScroll();
}

std::string TextEdit::GetText() const { return JoinLines(m_lines); }

int TextEdit::VisibleLines() const
{
  int h = m_rc.bottom - m_rc.top - m_padY * 2;
  return std::max(1, h / std::max(1, m_lineH));
}

void TextEdit::ClampScroll()
{
  int maxFirst = std::max(0, (int)m_lines.size() - VisibleLines());
  if (m_firstLine > maxFirst) m_firstLine = maxFirst;
  if (m_firstLine < 0) m_firstLine = 0;
}

void TextEdit::ScrollToLine(int line)
{
  m_firstLine = line;
  ClampScroll();
  if (onScroll) onScroll();
}

void TextEdit::ScrollBy(int lines) { ScrollToLine(m_firstLine + lines); }

void TextEdit::EnsureCaretVisible()
{
  int vis = VisibleLines();
  if (m_caret.line < m_firstLine) ScrollToLine(m_caret.line);
  else if (m_caret.line >= m_firstLine + vis) ScrollToLine(m_caret.line - vis + 1);
}

void TextEdit::SetCaret(TEPos p, bool keepAnchor)
{
  if (p.line < 0) p.line = 0;
  if (p.line >= (int)m_lines.size()) p.line = (int)m_lines.size() - 1;
  if (p.col < 0) p.col = 0;
  if (p.col > (int)m_lines[p.line].size()) p.col = (int)m_lines[p.line].size();
  m_caret = p;
  if (!keepAnchor) m_anchor = p;
  m_caretOn = true; m_blinkTick = GetTickCount();
}

void TextEdit::SelectAll()
{
  m_anchor = TEPos();
  m_caret = { (int)m_lines.size() - 1, (int)m_lines.back().size() };
}

void TextEdit::StartParagraphAtEnd()
{
  // whitespace-only change: block structure is unaffected until the user types
  while (m_lines.size() > 1 && Trim(m_lines.back()).empty()) m_lines.pop_back();
  if (m_lines.size() == 1 && Trim(m_lines[0]).empty()) m_lines[0].clear();
  else { m_lines.push_back(""); m_lines.push_back(""); }
  SetCaret({ (int)m_lines.size() - 1, 0 });
  ClampScroll();
  EnsureCaretVisible();
}

void TextEdit::SelRange(TEPos* a, TEPos* b) const
{
  if (m_caret < m_anchor) { *a = m_caret; *b = m_anchor; } else { *a = m_anchor; *b = m_caret; }
}

std::string TextEdit::SelectedText() const
{
  TEPos a, b; SelRange(&a, &b);
  if (a == b) return "";
  if (a.line == b.line) return m_lines[a.line].substr(a.col, b.col - a.col);
  std::string s = m_lines[a.line].substr(a.col);
  for (int l = a.line + 1; l < b.line; l++) { s += "\n"; s += m_lines[l]; }
  s += "\n"; s += m_lines[b.line].substr(0, b.col);
  return s;
}

// ---------------------------------------------------------------- editing

void TextEdit::PushUndo(int kind)
{
  unsigned long now = GetTickCount();
  if (kind == m_lastUndoKind && kind == 1 && now - m_lastEditTick < 700 && !m_undo.empty()) { m_lastEditTick = now; return; }
  m_undo.push_back({ m_lines, m_caret, m_anchor });
  if (m_undo.size() > 300) m_undo.erase(m_undo.begin());
  m_redo.clear();
  m_lastUndoKind = kind; m_lastEditTick = now;
}

void TextEdit::Undo()
{
  if (m_undo.empty()) return;
  m_redo.push_back({ m_lines, m_caret, m_anchor });
  Snap s = m_undo.back(); m_undo.pop_back();
  m_lines = s.lines; m_caret = s.caret; m_anchor = s.anchor;
  m_lastUndoKind = -1;
  Changed();
}

void TextEdit::Redo()
{
  if (m_redo.empty()) return;
  m_undo.push_back({ m_lines, m_caret, m_anchor });
  Snap s = m_redo.back(); m_redo.pop_back();
  m_lines = s.lines; m_caret = s.caret; m_anchor = s.anchor;
  m_lastUndoKind = -1;
  Changed();
}

void TextEdit::Changed()
{
  ClampScroll();
  EnsureCaretVisible();
  m_caretOn = true; m_blinkTick = GetTickCount();
  m_preferredX = -1;
  if (onChange) onChange();
}

void TextEdit::DeleteSelection()
{
  TEPos a, b; SelRange(&a, &b);
  if (a == b) return;
  std::string merged = m_lines[a.line].substr(0, a.col) + m_lines[b.line].substr(b.col);
  m_lines.erase(m_lines.begin() + a.line, m_lines.begin() + b.line + 1);
  m_lines.insert(m_lines.begin() + a.line, merged);
  m_caret = m_anchor = a;
}

void TextEdit::InsertText(const std::string& raw)
{
  if (m_readOnly) return;
  std::string s;
  for (char c : raw) if (c != '\r') s += c;
  if (m_single) { std::string t; for (char c : s) if (c != '\n') t += c; s = t; }
  PushUndo(s.size() == 1 ? 1 : 2);
  DeleteSelection();
  std::vector<std::string> parts = SplitLines(s);
  std::string tail = m_lines[m_caret.line].substr(m_caret.col);
  m_lines[m_caret.line] = m_lines[m_caret.line].substr(0, m_caret.col) + parts[0];
  for (size_t i = 1; i < parts.size(); i++) m_lines.insert(m_lines.begin() + m_caret.line + i, parts[i]);
  int lastLine = m_caret.line + (int)parts.size() - 1;
  int col = (int)m_lines[lastLine].size();
  m_lines[lastLine] += tail;
  m_caret = m_anchor = { lastLine, col };
  Changed();
}

void TextEdit::CopySelection()
{
  std::string s = SelectedText();
  ClipboardSetText(m_parent, s);
  TEPos a, b; SelRange(&a, &b);
  if (onCopy && HasSelection()) onCopy(s, a, b);
}

void TextEdit::CutSelection()
{
  if (!HasSelection()) return;
  CopySelection();
  if (m_readOnly) return;
  PushUndo(3); DeleteSelection(); Changed();
}

bool TextEdit::CanPaste() const
{
  return !m_readOnly && !ClipboardGetText(m_parent).empty();
}

void TextEdit::ContextClick(int x, int y)
{
  TEPos p = PosFromPoint(x, y);
  TEPos a, b; SelRange(&a, &b);
  bool inside = HasSelection() && !(p < a) && !(b < p);
  if (!inside) { m_caret = m_anchor = p; m_preferredX = -1; }
}

void TextEdit::PasteClipboard()
{
  if (m_readOnly) return;
  std::string s = ClipboardGetText(m_parent);
  if (onPaste) onPaste(s);
  if (!s.empty()) InsertText(s);
}

void TextEdit::InsertCodepoint(unsigned cp)
{
  std::string s; Utf8Append(s, cp);
  InsertText(s);
}

void TextEdit::Newline()
{
  if (m_readOnly || m_single) return;
  if (onBeforeNewline) onBeforeNewline();
  InsertText("\n");
}

void TextEdit::Backspace()
{
  if (m_readOnly) return;
  if (HasSelection()) { PushUndo(3); DeleteSelection(); Changed(); return; }
  if (m_caret.col > 0)
  {
    PushUndo(3);
    int p = Utf8Prev(m_lines[m_caret.line], m_caret.col);
    m_lines[m_caret.line].erase(p, m_caret.col - p);
    m_caret.col = p; m_anchor = m_caret;
    Changed();
  }
  else if (m_caret.line > 0)
  {
    PushUndo(3);
    int col = (int)m_lines[m_caret.line - 1].size();
    m_lines[m_caret.line - 1] += m_lines[m_caret.line];
    m_lines.erase(m_lines.begin() + m_caret.line);
    m_caret = m_anchor = { m_caret.line - 1, col };
    Changed();
  }
}

void TextEdit::DeleteForward()
{
  if (m_readOnly) return;
  if (HasSelection()) { PushUndo(3); DeleteSelection(); Changed(); return; }
  std::string& L = m_lines[m_caret.line];
  if (m_caret.col < (int)L.size())
  {
    PushUndo(3);
    int n = Utf8Next(L, m_caret.col);
    L.erase(m_caret.col, n - m_caret.col);
    Changed();
  }
  else if (m_caret.line + 1 < (int)m_lines.size())
  {
    PushUndo(3);
    L += m_lines[m_caret.line + 1];
    m_lines.erase(m_lines.begin() + m_caret.line + 1);
    Changed();
  }
}

// ---------------------------------------------------------------- navigation

void TextEdit::MoveCaret(int dLine, int dCol, bool select)
{
  TEPos p = m_caret;
  if (dCol)
  {
    if (!select && HasSelection())
    {
      TEPos a, b; SelRange(&a, &b);
      p = dCol < 0 ? a : b;
    }
    else if (dCol < 0)
    {
      if (p.col > 0) p.col = Utf8Prev(m_lines[p.line], p.col);
      else if (p.line > 0) { p.line--; p.col = (int)m_lines[p.line].size(); }
    }
    else
    {
      if (p.col < (int)m_lines[p.line].size()) p.col = Utf8Next(m_lines[p.line], p.col);
      else if (p.line + 1 < (int)m_lines.size()) { p.line++; p.col = 0; }
    }
    m_preferredX = -1;
  }
  if (dLine)
  {
    HDC dc = GetDC(m_parent); HGDIOBJ old = SelectObject(dc, m_font);
    if (m_preferredX < 0) m_preferredX = ColX(dc, p.line, p.col);
    p.line = std::max(0, std::min((int)m_lines.size() - 1, p.line + dLine));
    p.col = ColFromX(dc, p.line, m_preferredX);
    SelectObject(dc, old); ReleaseDC(m_parent, dc);
  }
  SetCaret(p, select);
  EnsureCaretVisible();
}

void TextEdit::MoveLineEdge(bool home, bool select)
{
  TEPos p = m_caret;
  p.col = home ? 0 : (int)m_lines[p.line].size();
  m_preferredX = -1;
  SetCaret(p, select);
  EnsureCaretVisible();
}

void TextEdit::MoveDocEdge(bool home, bool select)
{
  TEPos p = home ? TEPos() : TEPos{ (int)m_lines.size() - 1, (int)m_lines.back().size() };
  m_preferredX = -1;
  SetCaret(p, select);
  EnsureCaretVisible();
}

void TextEdit::Page(int dir, bool select)
{
  int n = VisibleLines() - 1;
  if (n < 1) n = 1;
  MoveCaret(dir * n, 0, select);
  ScrollBy(dir * n);
}

// ---------------------------------------------------------------- measuring

int TextEdit::ColX(HDC dc, int line, int col) const
{
  const std::string& L = m_lines[line];
  return TextWidthU8(dc, L.c_str(), std::min(col, (int)L.size()));
}

int TextEdit::ColFromX(HDC dc, int line, int x) const
{
  const std::string& L = m_lines[line];
  int prev = 0, i = 0, prevW = 0;
  while (i < (int)L.size())
  {
    int n = Utf8Next(L, i);
    int w = TextWidthU8(dc, L.c_str(), n);
    if (x < (prevW + w) / 2) return prev;
    prev = n; prevW = w; i = n;
  }
  return (int)L.size();
}

TEPos TextEdit::PosFromPoint(int x, int y) const
{
  TEPos p;
  p.line = m_firstLine + (y - TextTop()) / std::max(1, m_lineH);
  if (y < TextTop()) p.line = m_firstLine - 1;
  p.line = std::max(0, std::min((int)m_lines.size() - 1, p.line));
  HDC dc = GetDC(m_parent); HGDIOBJ old = SelectObject(dc, m_font);
  p.col = ColFromX(dc, p.line, x - m_rc.left - m_padX);
  SelectObject(dc, old); ReleaseDC(m_parent, dc);
  return p;
}

bool TextEdit::HitTest(int x, int y) const
{
  return x >= m_rc.left && x < m_rc.right && y >= m_rc.top && y < m_rc.bottom;
}

RECT TextEdit::ScrollbarRect() const
{
  RECT r = { m_rc.right - kScrollW, m_rc.top, m_rc.right, m_rc.bottom };
  return r;
}

RECT TextEdit::ThumbRect() const
{
  RECT sb = ScrollbarRect();
  int total = (int)m_lines.size(), vis = VisibleLines();
  int h = sb.bottom - sb.top;
  if (total <= vis) return { sb.left, sb.top, sb.right, sb.top };
  int th = std::max(24, h * vis / total);
  int ty = sb.top + (int)((long long)(h - th) * m_firstLine / (total - vis));
  return { sb.left + 2, ty, sb.right - 2, ty + th };
}

// ---------------------------------------------------------------- painting

void TextEdit::Paint(HDC dc)
{
  HBRUSH bb = CreateSolidBrush(colBg);
  FillRect(dc, &m_rc, bb); DeleteObject(bb);
  if (paintBackground) paintBackground(dc);
  HGDIOBJ oldFont = SelectObject(dc, m_font);
  SetBkMode(dc, TRANSPARENT);
  TEPos a, b; SelRange(&a, &b);
  int vis = VisibleLines();
  int x0 = m_rc.left + m_padX;
  int rightEdge = m_rc.right - (m_single ? 0 : kScrollW);
  for (int l = m_firstLine; l < (int)m_lines.size() && l < m_firstLine + vis; l++)
  {
    int y = LineY(l);
    const std::string& L = m_lines[l];
    if (!(a == b) && l >= a.line && l <= b.line)
    {
      int sx0 = l == a.line ? ColX(dc, l, a.col) : 0;
      int sx1 = l == b.line ? ColX(dc, l, b.col) : ColX(dc, l, (int)L.size()) + 8;
      RECT sr = { x0 + sx0, y, std::min(rightEdge, x0 + sx1), y + m_lineH };
      HBRUSH sb = CreateSolidBrush(colSel); FillRect(dc, &sr, sb); DeleteObject(sb);
    }
    SetTextColor(dc, colFg);
    RECT tr = { x0, y, rightEdge, y + m_lineH };
    DrawTextU8(dc, L, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  }
  if (m_focus && m_caretOn && m_caret.line >= m_firstLine && m_caret.line < m_firstLine + vis)
  {
    int cx = x0 + ColX(dc, m_caret.line, m_caret.col);
    RECT cr = { cx, LineY(m_caret.line) + 1, cx + 2, LineY(m_caret.line) + m_lineH - 1 };
    HBRUSH cb = CreateSolidBrush(colCaret); FillRect(dc, &cr, cb); DeleteObject(cb);
  }
  if (!m_single)
  {
    RECT sb = ScrollbarRect();
    HBRUSH tb = CreateSolidBrush(Blend(colBg, RGB(0, 0, 0), 0.3)); FillRect(dc, &sb, tb); DeleteObject(tb);
    RECT th = ThumbRect();
    if (th.bottom > th.top)
    {
      HBRUSH hb = CreateSolidBrush(m_thumbDrag ? RGB(150, 150, 150) : RGB(95, 95, 95)); FillRect(dc, &th, hb); DeleteObject(hb);
    }
  }
  SelectObject(dc, oldFont);
}

// ---------------------------------------------------------------- input

void TextEdit::SetFocus(bool f) { m_focus = f; m_caretOn = true; m_blinkTick = GetTickCount(); }

void TextEdit::Tick()
{
  if (!m_focus) return;
  unsigned long now = GetTickCount();
  if (now - m_blinkTick > 500)
  {
    m_blinkTick = now; m_caretOn = !m_caretOn;
    if (m_parent) InvalidateRect(m_parent, &m_rc, FALSE);
  }
}

void TextEdit::OnWheel(int delta)
{
  if (m_single) return;
  ScrollBy(delta > 0 ? -3 : 3);
}

bool TextEdit::OnMouse(UINT msg, WPARAM wp, int x, int y)
{
  if (msg == WM_LBUTTONDOWN)
  {
    if (!HitTest(x, y)) return false;
    RECT sb = ScrollbarRect();
    if (!m_single && x >= sb.left)
    {
      RECT th = ThumbRect();
      if (y >= th.top && y < th.bottom) { m_thumbDrag = true; m_thumbDragOff = y - th.top; }
      else ScrollBy(y < th.top ? -VisibleLines() : VisibleLines());
      return true;
    }
    TEPos p = PosFromPoint(x, y);
    SetCaret(p, ShiftDown());
    m_preferredX = -1;
    m_drag = true;
    return true;
  }
  if (msg == WM_LBUTTONDBLCLK)
  {
    if (!HitTest(x, y)) return false;
    // select word
    TEPos p = PosFromPoint(x, y);
    const std::string& L = m_lines[p.line];
    int a = p.col, b = p.col;
    auto isWord = [](unsigned char c) { return c > ' ' && c != ',' && c != '.' && c != ';'; };
    while (a > 0 && isWord(L[a - 1])) a--;
    while (b < (int)L.size() && isWord(L[b])) b++;
    m_anchor = { p.line, a }; m_caret = { p.line, b };
    return true;
  }
  if (msg == WM_MOUSEMOVE)
  {
    if (m_thumbDrag)
    {
      RECT sb = ScrollbarRect(); RECT th = ThumbRect();
      int total = (int)m_lines.size(), vis = VisibleLines();
      int trackH = (sb.bottom - sb.top) - (th.bottom - th.top);
      if (trackH > 0 && total > vis)
      {
        int ty = y - m_thumbDragOff - sb.top;
        ScrollToLine((int)((long long)ty * (total - vis) / trackH));
      }
      return true;
    }
    if (m_drag)
    {
      TEPos p = PosFromPoint(x, y);
      SetCaret(p, true);
      if (y < m_rc.top) ScrollBy(-1); else if (y > m_rc.bottom) ScrollBy(1);
      return true;
    }
    return false;
  }
  if (msg == WM_LBUTTONUP)
  {
    bool was = m_drag || m_thumbDrag;
    m_drag = false; m_thumbDrag = false;
    return was;
  }
  return false;
}

bool TextEdit::HandleVK(int vk, bool shift, bool ctrl)
{
  switch (vk)
  {
  case VK_LEFT:  MoveCaret(0, -1, shift); return true;
  case VK_RIGHT: MoveCaret(0, 1, shift); return true;
  case VK_UP:    if (m_single) return true; MoveCaret(-1, 0, shift); return true;
  case VK_DOWN:  if (m_single) return true; MoveCaret(1, 0, shift); return true;
  case VK_HOME:  if (ctrl) MoveDocEdge(true, shift); else MoveLineEdge(true, shift); return true;
  case VK_END:   if (ctrl) MoveDocEdge(false, shift); else MoveLineEdge(false, shift); return true;
  case VK_PRIOR: Page(-1, shift); return true;
  case VK_NEXT:  Page(1, shift); return true;
  case VK_BACK:  Backspace(); return true;
  case VK_DELETE: if (shift) CutSelection(); else DeleteForward(); return true;
  case VK_INSERT: if (ctrl) CopySelection(); else if (shift) PasteClipboard(); return true;
  case VK_RETURN:
    if (m_single) { if (onEnterSingle) onEnterSingle(); }
    else Newline();
    return true;
  case VK_ESCAPE: if (onEscape) onEscape(); return true;
  case VK_TAB: return true;
  case 'A': if (ctrl) { SelectAll(); return true; } break;
  case 'C': if (ctrl) { CopySelection(); return true; } break;
  case 'X': if (ctrl) { CutSelection(); return true; } break;
  case 'V': if (ctrl) { PasteClipboard(); return true; } break;
  case 'Z': if (ctrl) { if (shift) Redo(); else Undo(); return true; } break;
  case 'Y': if (ctrl) { Redo(); return true; } break;
  }
  return false;
}

bool TextEdit::OnKey(UINT msg, WPARAM wp, LPARAM lp)
{
#ifdef _WIN32
  (void)lp;
  bool shift = ShiftDown(), ctrl = CtrlDown();
  if (msg == WM_CHAR)
  {
    unsigned c = (unsigned)wp;
    if (c < 32 || c == 127 || ctrl) return c < 32 || c == 127; // swallow control chars (handled as VKs)
    if (c >= 0xD800 && c <= 0xDBFF) { m_pendingHighSurrogate = c; return true; }
    if (c >= 0xDC00 && c <= 0xDFFF && m_pendingHighSurrogate)
    {
      c = 0x10000 + ((m_pendingHighSurrogate - 0xD800) << 10) + (c - 0xDC00);
      m_pendingHighSurrogate = 0;
    }
    InsertCodepoint(c);
    return true;
  }
  if (msg != WM_KEYDOWN) return false;
  int vk = (int)wp;
  if (vk >= 'A' && vk <= 'Z' && !ctrl) return false;          // let WM_CHAR insert it
  if (vk >= '0' && vk <= '9' && !ctrl) return false;
  return HandleVK(vk, shift, ctrl);
#else
  if (msg != WM_KEYDOWN) return false;
  bool shift = (lp & FSHIFT) != 0, ctrl = (lp & FCONTROL) != 0;
  if (lp & FVIRTKEY)
  {
    int vk = (int)wp;
    if (!ctrl)
    {
      if (vk >= 'A' && vk <= 'Z') { InsertCodepoint(shift ? vk : vk + 32); return true; }
      if (vk >= '0' && vk <= '9') { InsertCodepoint(vk); return true; }
      if (vk == VK_SPACE) { InsertCodepoint(' '); return true; }
    }
    return HandleVK(vk, shift, ctrl);
  }
  if (ctrl) return false;
  unsigned cp = (unsigned)wp;
  if (cp == ' ' ) { InsertCodepoint(' '); return true; }
  if (cp < 32 || cp == 127) return HandleVK((int)cp, shift, ctrl);
  InsertCodepoint(cp);
  return true;
#endif
}

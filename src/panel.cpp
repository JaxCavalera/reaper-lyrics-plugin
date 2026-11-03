#include "panel.h"
#include "util.h"
#include "resource.h"
#include <algorithm>

LyricsPanel& LyricsPanel::Get() { static LyricsPanel p; return p; }
bool LyricsPanel::IsVisible() const { return m_hwnd && IsWindowVisible(m_hwnd); }

WDL_DLGRET LyricsPanel::DlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  if (msg == WM_INITDIALOG) { SetUserData(h, (void*)lp); ((LyricsPanel*)lp)->m_hwnd = h; }
  LyricsPanel* p = (LyricsPanel*)GetUserData(h);
  if (!p) return 0;
  return p->Handle(h, msg, wp, lp);
}

void LyricsPanel::Create()
{
  if (m_hwnd) return;
  CreateDlg(IDD_PANEL, g_mainHwnd, DlgProc, (LPARAM)this);
  if (!m_hwnd) return;
  RECT r;
  if (!LoadWindowRect("panel", &r)) { r.left = 300; r.top = 80; r.right = 1100; r.bottom = 380; }
  ApplyWindowRect(m_hwnd, r);
}

void LyricsPanel::Show(bool show)
{
  if (show) { Create(); if (m_hwnd) { ShowWindowNoActivate(m_hwnd); m_curBlock = -2; } }
  else if (m_hwnd) { SaveWindowRect("panel", m_hwnd); ShowWindow(m_hwnd, SW_HIDE); }
}

void LyricsPanel::OnTimer()
{
  if (!IsVisible()) return;
  int line = -1, b = -1;
  double t = 0;
  if (g_doc.IsBound()) { t = g_doc.PlayOrEditPos(); b = g_doc.FindBlockAt(t, &line); }
  static unsigned long lastRepaint = 0;
  bool changed = b != m_curBlock || line != m_curLine;
  if (changed || GetTickCount() - lastRepaint > 250)
  {
    m_curBlock = b; m_curLine = line; m_lastPos = t; lastRepaint = GetTickCount();
    InvalidateRect(m_hwnd, nullptr, FALSE);
  }
}

static int FitFontSize(HDC dc, const std::string& text, int wantPx, int maxW, bool bold)
{
  int px = wantPx;
  while (px > 14)
  {
    HFONT f = MakeFont(px, bold); HGDIOBJ o = SelectObject(dc, f);
    int w = TextWidthU8(dc, text.c_str(), (int)text.size());
    SelectObject(dc, o); DeleteObject(f);
    if (w <= maxW) break;
    px = (int)(px * 0.9);
  }
  return px;
}

static void DrawLine(HDC dc, const std::string& text, int y, int w, int px, COLORREF col, bool bold)
{
  if (text.empty()) return;
  px = FitFontSize(dc, text, px, w - 40, bold);
  HFONT f = MakeFont(px, bold); HGDIOBJ o = SelectObject(dc, f);
  SetTextColor(dc, col);
  RECT r = { 20, y, w - 20, y + px + 8 };
  DrawTextU8(dc, text, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
  SelectObject(dc, o); DeleteObject(f);
}

void LyricsPanel::Paint(HDC dc, RECT rc)
{
  int w = rc.right, h = rc.bottom;
  const LyricBlock* blk = (m_curBlock >= 0 && m_curBlock < (int)g_doc.blocks.size()) ? &g_doc.blocks[m_curBlock] : nullptr;
  const LyricBlock* upcoming = nullptr;
  if (!blk && g_doc.IsBound())
    for (auto& b : g_doc.blocks) if (b.start > m_lastPos) { upcoming = &b; break; }

  COLORREF accent = blk ? NativeToColorref(blk->color) : (upcoming ? NativeToColorref(upcoming->color) : RGB(90, 90, 90));
  COLORREF bg = Blend(RGB(18, 18, 18), accent, blk ? 0.22 : 0.08);
  HBRUSH bb = CreateSolidBrush(bg); FillRect(dc, &rc, bb); DeleteObject(bb);
  SetBkMode(dc, TRANSPARENT);

  std::string label = blk ? blk->label : (upcoming ? "next: " + upcoming->label : (g_doc.IsBound() ? "" : "no lyric track"));
  if (!label.empty())
  {
    HFONT f = MakeFont(15, true); HGDIOBJ o = SelectObject(dc, f);
    int tw = TextWidthU8(dc, label.c_str(), (int)label.size());
    RECT chip = { 12, 10, 12 + tw + 20, 10 + 26 };
    HBRUSH cb = CreateSolidBrush(accent); HGDIOBJ ob = SelectObject(dc, cb); HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, chip.left, chip.top, chip.right, chip.bottom, 8, 8);
    SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(cb);
    SetTextColor(dc, IsLightColor(accent) ? RGB(20, 20, 20) : RGB(250, 250, 250));
    DrawTextU8(dc, label, &chip, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, o); DeleteObject(f);
  }
  if (g_doc.IsBound())
  {
    char tb[64] = ""; format_timestr_pos(m_lastPos, tb, sizeof(tb), -1);
    HFONT f = MakeFont(13, false); HGDIOBJ o = SelectObject(dc, f);
    SetTextColor(dc, RGB(150, 150, 150));
    RECT tr = { w - 160, 12, w - 12, 32 };
    DrawTextU8(dc, tb, &tr, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, o); DeleteObject(f);
  }

  int big = std::max(20, std::min(84, h / 5));
  int med = std::max(14, (int)(big * 0.5));
  int cy = (int)(h * 0.47);
  if (blk)
  {
    int li = std::max(0, m_curLine);
    std::string curL = li < (int)blk->lines.size() ? blk->lines[li].text : "";
    std::string prevL = li > 0 ? blk->lines[li - 1].text : "";
    std::string next1 = li + 1 < (int)blk->lines.size() ? blk->lines[li + 1].text : "";
    std::string next2 = li + 2 < (int)blk->lines.size() ? blk->lines[li + 2].text : "";
    if (next1.empty() && m_curBlock + 1 < (int)g_doc.blocks.size() && !g_doc.blocks[m_curBlock + 1].lines.empty())
      next1 = "\xe2\x86\x92 " + g_doc.blocks[m_curBlock + 1].lines[0].text;
    DrawLine(dc, prevL, cy - big / 2 - med - 14, w, med, RGB(120, 120, 120), false);
    DrawLine(dc, curL, cy - big / 2 - 4, w, big, RGB(255, 255, 255), true);
    DrawLine(dc, next1, cy + big / 2 + 10, w, med, RGB(170, 170, 170), false);
    DrawLine(dc, next2, cy + big / 2 + 10 + med + 10, w, med, RGB(120, 120, 120), false);
  }
  else if (upcoming && !upcoming->lines.empty())
    DrawLine(dc, upcoming->lines[0].text, cy - big / 2 - 4, w, big, RGB(130, 130, 130), true);
  else if (g_doc.IsBound())
    DrawLine(dc, "\xe2\x80\x94", cy - big / 2 - 4, w, big, RGB(90, 90, 90), true);
}

INT_PTR LyricsPanel::Handle(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
#ifdef _WIN32
  case WM_ERASEBKGND: SetWindowLongPtr(h, DWLP_MSGRESULT, 1); return 1;
  case WM_NCHITTEST:
  {
    // drag the panel by its body as well as the caption
    POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(h, &pt);
    RECT rc; GetClientRect(h, &rc);
    if (PtInRect(&rc, pt)) { SetWindowLongPtr(h, DWLP_MSGRESULT, HTCAPTION); return 1; }
    return 0;
  }
  case WM_NCLBUTTONDBLCLK: SetWindowLongPtr(h, DWLP_MSGRESULT, 0); return 1;
#else
  case WM_ERASEBKGND: return 1;
#endif
  case WM_PAINT:
  {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT rc; GetClientRect(h, &rc);
#ifdef _WIN32
    HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, std::max(1L, rc.right), std::max(1L, rc.bottom));
    HGDIOBJ old = SelectObject(mem, bmp);
    Paint(mem, rc);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
#else
    Paint(dc, rc);
#endif
    EndPaint(h, &ps);
    return 1;
  }
#ifdef _WIN32
  case WM_SIZE: InvalidateRect(h, nullptr, FALSE); return 0;
  case WM_EXITSIZEMOVE: SaveWindowRect("panel", h); return 0;
#else
  case WM_SIZE: InvalidateRect(h, nullptr, FALSE); SaveWindowRect("panel", h); return 0;
  case WM_MOVE: SaveWindowRect("panel", h); return 0;
#endif
  case WM_CLOSE: Show(false); return 1;
  case WM_DESTROY: m_hwnd = nullptr; return 0;
  }
  return 0;
}

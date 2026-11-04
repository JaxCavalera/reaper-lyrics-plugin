#include "editor.h"
#include "panel.h"
#include "util.h"
#include "resource.h"
#include <algorithm>
#include <cmath>

static const int kToolbarH = 38;
static const int kGutterW = 150;
// sequencer lane: ruler, block row, line row
static const int kRulerH = 20, kBlockRowH = 40, kLineRowH = 54, kLanePad = 4, kScrollH = 12;
static const int kLaneH = kRulerH + kLanePad + kBlockRowH + kLanePad + kLineRowH + kLanePad + kScrollH + 2;
static const int kGripSize = 16;
static const COLORREF kLaneBg = RGB(20, 20, 20);
static const COLORREF kRulerBg = RGB(34, 34, 34);
static const COLORREF kItemSpanBg = RGB(28, 30, 34);
static const COLORREF kBg = RGB(30, 30, 30);
static const COLORREF kToolbarBg = RGB(38, 38, 38);
static const COLORREF kGutterBg = RGB(24, 24, 24);
static const COLORREF kText = RGB(222, 222, 222);
static const COLORREF kDim = RGB(128, 128, 128);
static const COLORREF kButton = RGB(58, 58, 58);
static const COLORREF kAccent = RGB(74, 144, 217);

enum { BTN_TRACKSEL = 1, BTN_TRACK, BTN_PANEL, BTN_ZOOM_OUT, BTN_ZOOM_IN, BTN_ZOOM_FIT };
enum { MENU_RENAME = 1, MENU_DELETE, MENU_GOTO, MENU_LABEL_PRESET = 10, MENU_LABEL_AUTO = 30, MENU_COLOR_PRESET = 40, MENU_COLOR_CUSTOM = 60, MENU_COLOR_AUTO, MENU_TRACK = 100 };

struct ColorPreset { const char* name; COLORREF c; };
static const ColorPreset kColors[] = {
  { "Sky blue", RGB(110, 182, 236) }, { "Light green", RGB(128, 200, 128) }, { "Light orange", RGB(246, 176, 98) },
  { "Blue", RGB(74, 144, 217) }, { "Green", RGB(79, 157, 105) }, { "Orange", RGB(232, 146, 58) },
  { "Purple", RGB(166, 107, 190) }, { "Red", RGB(210, 80, 90) }, { "Yellow", RGB(217, 195, 74) },
  { "Teal", RGB(64, 180, 180) }, { "Pink", RGB(230, 120, 170) }, { "Grey", RGB(141, 141, 141) },
};
static const int kNumColors = (int)(sizeof(kColors) / sizeof(kColors[0]));

static void AddMenu(HMENU m, UINT flags, UINT_PTR id, const char* text)
{
  InsertMenu(m, GetMenuItemCount(m), MF_BYPOSITION | flags, id, text);
}

static void FillRectColor(HDC dc, const RECT& r, COLORREF c)
{
  HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b);
}

static void RoundRectColor(HDC dc, const RECT& r, COLORREF c, int radius)
{
  HBRUSH b = CreateSolidBrush(c);
  HGDIOBJ ob = SelectObject(dc, b); HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
  RoundRect(dc, r.left, r.top, r.right + 1, r.bottom + 1, radius, radius);
  SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(b);
}

LyricEditor& LyricEditor::Get() { static LyricEditor e; return e; }

LyricEditor::LyricEditor() : m_text(false), m_rename(true) {}

bool LyricEditor::IsVisible() const { return m_hwnd && IsWindowVisible(m_hwnd); }

void LyricEditor::SetTracking(bool on)
{
  m_tracking = on;
  IniSetInt("editor", "track", on ? 1 : 0);
  m_scrolledBlock = -1;
  Invalidate();
}

// ------------------------------------------------------------------ creation

WDL_DLGRET LyricEditor::DlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  if (msg == WM_INITDIALOG) { SetUserData(h, (void*)lp); ((LyricEditor*)lp)->m_hwnd = h; }
  LyricEditor* ed = (LyricEditor*)GetUserData(h);
  if (!ed) return 0;
  return ed->Handle(h, msg, wp, lp);
}

void LyricEditor::Create()
{
  if (m_hwnd) return;
  m_tracking = IniGetInt("editor", "track", 1) != 0;
  CreateDlg(IDD_EDITOR, g_mainHwnd, DlgProc, (LPARAM)this);
  if (!m_hwnd) return;
  m_font = MakeFont(17, false);
  m_fontUi = MakeFont(13, false);
  m_fontChip = MakeFont(12, true);
  m_fontLane = MakeFont(12, false);
  m_text.SetParent(m_hwnd); m_text.SetFont(m_font);
  m_text.colBg = kBg; m_text.colFg = kText;
  m_text.onChange = [this] { OnEditChanged(); };
  m_text.onBeforeNewline = [this] { OnBeforeNewline(); };
  m_text.onScroll = [this] { Invalidate(); };
  m_text.paintBackground = [this](HDC dc) { PaintTextBackground(dc); };
  m_rename.SetParent(m_hwnd); m_rename.SetFont(m_fontChip);
  m_rename.colBg = RGB(250, 250, 250); m_rename.colFg = RGB(20, 20, 20); m_rename.colCaret = RGB(20, 20, 20); m_rename.colSel = RGB(170, 200, 240);
  m_rename.onEnterSingle = [this] { CommitRename(true); };
  m_rename.onEscape = [this] { CommitRename(false); };
  m_text.SetFocus(true);

  RECT r;
  if (!LoadWindowRect("sequencer", &r)) { r.left = 160; r.top = 120; r.right = 1160; r.bottom = 820; }
  ApplyWindowRect(m_hwnd, r);
  LayoutControls();
  RefreshText();
}

void LyricEditor::LayoutControls()
{
  if (!m_hwnd) return;
  RECT rc; GetClientRect(m_hwnd, &rc);
  int w = rc.right, h = rc.bottom;
  m_toolbar = { 0, 0, w, kToolbarH };
  m_lane = { 0, kToolbarH, w, kToolbarH + kLaneH };
  int top = (int)m_lane.bottom;
  m_gutter = { 0, top, kGutterW, h };
  RECT tr = { kGutterW, top, w, h };
  m_text.SetRect(tr);
  m_buttons.clear();
  m_buttons.push_back({ { 8, 7, 228, kToolbarH - 7 }, "", BTN_TRACKSEL, false });
  m_buttons.push_back({ { 236, 7, 312, kToolbarH - 7 }, "Track", BTN_TRACK, true });
  m_buttons.push_back({ { 320, 7, 424, kToolbarH - 7 }, "Lyrics panel", BTN_PANEL, true });
  m_buttons.push_back({ { 440, 7, 508, kToolbarH - 7 }, "Zoom -", BTN_ZOOM_OUT, false });
  m_buttons.push_back({ { 512, 7, 580, kToolbarH - 7 }, "Zoom +", BTN_ZOOM_IN, false });
  m_buttons.push_back({ { 584, 7, 628, kToolbarH - 7 }, "Fit", BTN_ZOOM_FIT, false });
  if (m_renaming) CommitRename(true);
  Invalidate();
}

void LyricEditor::Show(bool show)
{
  if (show)
  {
    Create();
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    SetFocus(m_hwnd);
    m_lastTrackScan = 0;
    Invalidate();
  }
  else if (m_hwnd)
  {
    SaveWindowRect("sequencer", m_hwnd);
    ShowWindow(m_hwnd, SW_HIDE);
  }
}

void LyricEditor::BindTrack(MediaTrack* t)
{
  ReaProject* p = EnumProjects(-1, nullptr, 0);
  g_doc.Bind(p, t);
  m_curBlock = m_curLine = -1; m_scrolledBlock = -1;
  m_trackName = t ? LyricDoc::TrackName(t) : "";
  m_needFit = true;
  if (m_hwnd) { RefreshText(); UpdateStatus(); }
}

void LyricEditor::OpenForTrack(MediaTrack* t)
{
  if (!t) return;
  if (g_doc.track != t || !g_doc.IsBound()) BindTrack(t);
  m_needFit = true;
  Show(true);
  RefreshText();
}

// ------------------------------------------------------------------ text <-> layout

void LyricEditor::RebuildLayout()
{
  m_layout.clear();
  const auto& lines = m_text.Lines();
  bool open = false; Layout cur;
  for (int i = 0; i < (int)lines.size(); i++)
  {
    bool blank = Trim(lines[i]).empty();
    if (blank) { if (open) { m_layout.push_back(cur); open = false; } }
    else { if (!open) { cur.first = i; open = true; } cur.last = i; }
  }
  if (open) m_layout.push_back(cur);
  // the sole block with no lyrics yet still gets its chip on the first line
  if (m_layout.empty() && g_doc.IsBound() && g_doc.blocks.size() == 1) m_layout.push_back(Layout());
}

static std::string CanonicalText(const std::vector<std::string>& lines)
{
  std::string out; bool open = false;
  for (auto& l : lines)
  {
    if (Trim(l).empty()) { open = false; continue; }
    if (!out.empty()) out += open ? "\n" : "\n\n";
    out += l; open = true;
  }
  return out;
}

void LyricEditor::CaretInfo(int* block, int* line, int* col)
{
  TEPos c = m_text.Caret();
  *block = -1; *line = 0; *col = c.col;
  for (size_t b = 0; b < m_layout.size(); b++)
  {
    if (c.line < m_layout[b].first) { *block = (int)b; *line = 0; return; }
    if (c.line <= m_layout[b].last) { *block = (int)b; *line = c.line - m_layout[b].first; return; }
  }
  *block = (int)m_layout.size();
}

void LyricEditor::RefreshText()
{
  if (!m_hwnd) return;
  RebuildLayout();
  std::string current = CanonicalText(m_text.Lines());
  std::string want = g_doc.IsBound() ? g_doc.ToText() : "";
  m_text.SetReadOnly(!g_doc.IsBound());
  if (current == want) { Invalidate(); return; }

  int cb, cl, cc; CaretInfo(&cb, &cl, &cc);
  int first = m_text.FirstVisibleLine();
  m_text.SetText(want);
  RebuildLayout();
  TEPos np;
  if (cb >= 0 && cb < (int)m_layout.size())
  {
    np.line = std::min(m_layout[cb].first + cl, m_layout[cb].last);
    np.col = cc;
  }
  else { np.line = (int)m_text.Lines().size() - 1; np.col = (int)m_text.Lines().back().size(); }
  m_text.SetCaret(np);
  if (!m_tracking) m_text.ScrollToLine(first);
  m_scrolledBlock = -1;
  UpdateStatus();
  Invalidate();
}

void LyricEditor::OnEditChanged()
{
  RebuildLayout();
  if (g_doc.IsBound())
  {
    g_doc.ApplyText(CanonicalText(m_text.Lines()), m_tracking ? &m_live : nullptr);
    if (g_doc.blocks.size() != m_layout.size()) RefreshText();
  }
  UpdateStatus();
  Invalidate();
}

// A track with no blocks gets its first block straight away (empty, no placeholder text).
// Otherwise the caret goes to a fresh paragraph and the block appears with its first typed
// line - an empty block between others can't be shown, since a blank line separates blocks.
// While playing with Track on, the play position is stamped so the block starts there.
void LyricEditor::StartNewBlock()
{
  if (!m_hwnd || m_renaming || !g_doc.IsBound()) return;
  if (g_doc.blocks.empty()) { g_doc.AppendBlock(); m_needFit = true; RefreshText(); }
  else if (!(g_doc.blocks.size() == 1 && g_doc.blocks[0].Text().empty())) OnBeforeNewline();
  m_text.StartParagraphAtEnd();
  m_text.SetFocus(true);
  SetFocus(m_hwnd);
  RebuildLayout();
  UpdateStatus();
  Invalidate();
}

void LyricEditor::OnBeforeNewline()
{
  if (m_tracking && g_doc.IsBound() && g_doc.IsPlaying())
  {
    m_live.valid = true;
    m_live.pos = GetPlayPosition2Ex(g_doc.proj);
    m_live.stamp = GetTickCount();
  }
}

// ------------------------------------------------------------------ timer / tracking

void LyricEditor::OnTimer()
{
  if (g_doc.IsBound() && !g_doc.ValidateTrack())
  {
    m_trackName.clear();
    if (m_hwnd) { RefreshText(); UpdateStatus(); }
  }
  unsigned long now = GetTickCount();
  if (now - m_lastTrackScan >= 1000)
  {
    m_lastTrackScan = now;
    ReaProject* p = EnumProjects(-1, nullptr, 0);
    std::vector<MediaTrack*> tracks;
    if (p) LyricDoc::FindLyricTracks(p, tracks);
    if (!g_doc.IsBound() && !tracks.empty()) BindTrack(tracks[0]);
    else if (g_doc.IsBound()) { std::string n = LyricDoc::TrackName(g_doc.track); if (n != m_trackName) { m_trackName = n; Invalidate(); } }
  }
  if (g_doc.IsBound() && g_doc.NeedsSync())
  {
    bool textChanged = g_doc.SyncFromReaper();
    if (m_hwnd && (textChanged || g_doc.blocks.size() != m_layout.size())) RefreshText();
    Invalidate();
  }
  if (!m_hwnd) return;

  int line = -1, b = -1;
  if (g_doc.IsBound()) b = g_doc.FindBlockAt(g_doc.PlayOrEditPos(), &line);
  if (b != m_curBlock || line != m_curLine)
  {
    m_curBlock = b; m_curLine = line;
    UpdateStatus();
    Invalidate();
  }
  if (m_tracking && b >= 0 && b != m_scrolledBlock && IsVisible()) { AutoScroll(); m_scrolledBlock = b; }
  if (m_laneDrag == HIT_NONE) g_doc.FlushUndo(false);   // typing pauses become one undo point
  if (g_doc.IsBound() && IsVisible())
  {
    double t = g_doc.PlayOrEditPos();
    if (m_tracking && g_doc.IsPlaying()) FollowPlayhead();
    if (std::fabs(t - m_lastPlayDrawn) > 1e-4) { m_lastPlayDrawn = t; InvalidateRect(m_hwnd, &m_lane, FALSE); }
  }
  m_text.Tick();
  m_rename.Tick();
  static unsigned long lastStatus = 0;
  if (now - lastStatus > 200) { lastStatus = now; UpdateStatus(); InvalidateRect(m_hwnd, &m_toolbar, FALSE); }
}

void LyricEditor::AutoScroll()
{
  if (m_curBlock < 0 || m_curBlock >= (int)m_layout.size()) return;
  int target = std::max(0, m_layout[m_curBlock].first - 1);
  if (target != m_text.FirstVisibleLine()) m_text.ScrollToLine(target);
}

void LyricEditor::UpdateStatus()
{
  std::string s;
  if (!g_doc.IsBound()) s = "No lyric track - Track menu > Insert lyric track";
  else
  {
    char tb[64] = ""; format_timestr_pos(g_doc.PlayOrEditPos(), tb, sizeof(tb), -1);
    if (m_curBlock >= 0 && m_curBlock < (int)g_doc.blocks.size())
    {
      const LyricBlock& b = g_doc.blocks[m_curBlock];
      s = FormatStr("%s  -  line %d/%d  -  %s", b.label.c_str(), m_curLine + 1, (int)b.lines.size(), tb);
    }
    else s = FormatStr("%d blocks  -  %s", (int)g_doc.blocks.size(), tb);
    if (m_tracking && g_doc.IsPlaying()) s += "   [live: Enter stamps the play position]";
  }
  if (s != m_status) { m_status = s; if (m_hwnd) InvalidateRect(m_hwnd, &m_toolbar, FALSE); }
}

// ------------------------------------------------------------------ painting

void LyricEditor::Paint(HDC dc)
{
  m_text.Paint(dc);
  PaintGutter(dc);
  PaintLane(dc);
  PaintToolbar(dc);
  if (m_renaming) m_rename.Paint(dc);
  PaintGrip(dc);
}

void LyricEditor::PaintToolbar(HDC dc)
{
  FillRectColor(dc, m_toolbar, kToolbarBg);
  HGDIOBJ oldFont = SelectObject(dc, m_fontUi);
  SetBkMode(dc, TRANSPARENT);
  for (auto& b : m_buttons)
  {
    bool on = (b.id == BTN_TRACK && m_tracking) || (b.id == BTN_PANEL && LyricsPanel::Get().IsVisible());
    RoundRectColor(dc, b.r, on ? kAccent : kButton, 6);
    SetTextColor(dc, RGB(240, 240, 240));
    std::string text = b.text;
    if (b.id == BTN_TRACKSEL) text = g_doc.IsBound() ? m_trackName + "  \xe2\x96\xbe" : "(no lyric track)  \xe2\x96\xbe";
    if (b.toggle) text = (on ? "\xe2\x98\x91 " : "\xe2\x98\x90 ") + text;
    RECT tr = b.r; tr.left += 6; tr.right -= 6;
    DrawTextU8(dc, text, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  }
  SetTextColor(dc, kDim);
  RECT sr = { 640, m_toolbar.top, m_toolbar.right - 8, m_toolbar.bottom };
  DrawTextU8(dc, m_status, &sr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  SelectObject(dc, oldFont);
}

void LyricEditor::PaintTextBackground(HDC dc)
{
  const RECT& rc = m_text.Rect();
  int lh = m_text.LineH();
  int first = m_text.FirstVisibleLine(), last = first + m_text.VisibleLines();
  for (size_t i = 0; i < m_layout.size() && i < g_doc.blocks.size(); i++)
  {
    const Layout& L = m_layout[i];
    if (L.last < first || L.first >= last) continue;
    COLORREF c = NativeToColorref(g_doc.blocks[i].color);
    bool cur = (int)i == m_curBlock;
    RECT r = { rc.left, std::max((int)rc.top, m_text.LineY(L.first)), rc.right - 10, std::min((int)rc.bottom, m_text.LineY(L.last) + lh) };
    if (r.bottom > r.top) FillRectColor(dc, r, Blend(kBg, c, cur ? 0.16 : 0.07));
    if (cur && m_curLine >= 0)
    {
      int line = L.first + m_curLine;
      if (line >= first && line < last)
      {
        RECT lr = { rc.left, m_text.LineY(line), rc.right - 10, m_text.LineY(line) + lh };
        FillRectColor(dc, lr, Blend(kBg, c, 0.32));
      }
    }
  }
  if (g_doc.IsBound() && m_text.Lines().size() == 1 && m_text.Lines()[0].empty())
  {
    // hint only - not part of the text; drawn over the block tint
    HGDIOBJ of = SelectObject(dc, m_fontUi);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(110, 110, 110));
    RECT hr = { rc.left + m_text.m_padX + 4, m_text.TextTop(), rc.right - 14, m_text.TextTop() + lh };
    DrawTextU8(dc, "Type lyrics here. A blank line starts a new block.", &hr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, of);
  }
}

void LyricEditor::PaintGutter(HDC dc)
{
  FillRectColor(dc, m_gutter, kGutterBg);
  SetBkMode(dc, TRANSPARENT);
  int lh = m_text.LineH(), w = m_gutter.right;
  int first = m_text.FirstVisibleLine(), lastVis = first + m_text.VisibleLines();
  HGDIOBJ oldFont = SelectObject(dc, m_fontChip);
  for (size_t i = 0; i < m_layout.size(); i++)
  {
    const Layout& L = m_layout[i];
    if (L.last < first || L.first >= lastVis) continue;
    int y1 = m_text.LineY(L.first), y2 = m_text.LineY(L.last) + lh;
    bool cur = (int)i == m_curBlock;
    COLORREF c = i < g_doc.blocks.size() ? NativeToColorref(g_doc.blocks[i].color) : RGB(90, 90, 90);
    std::string label = i < g_doc.blocks.size() ? g_doc.blocks[i].label : "";
    RECT bar = { w - 10, std::max((int)m_gutter.top, y1), w - 5, std::min((int)m_gutter.bottom, y2) };
    if (bar.bottom > bar.top) FillRectColor(dc, bar, cur ? c : Blend(c, kGutterBg, 0.45));
    if (L.first >= first)
    {
      RECT chip = { 6, y1 + 1, w - 16, y1 + lh - 1 };
      RoundRectColor(dc, chip, c, 6);
      if (cur)
      {
        HPEN p = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HGDIOBJ op = SelectObject(dc, p); HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, chip.left - 1, chip.top - 1, chip.right + 2, chip.bottom + 2, 7, 7);
        SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(p);
      }
      SetTextColor(dc, IsLightColor(c) ? RGB(20, 20, 20) : RGB(250, 250, 250));
      RECT tr = { chip.left + 8, chip.top, chip.right - 18, chip.bottom };
      DrawTextU8(dc, label, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
      SetTextColor(dc, IsLightColor(c) ? RGB(60, 60, 60) : RGB(210, 210, 210));
      RECT gr = { chip.right - 16, chip.top, chip.right - 2, chip.bottom };
      DrawTextU8(dc, "\xe2\x89\xa1", &gr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (cur && m_curLine >= 0)
    {
      int line = L.first + m_curLine;
      if (line >= first && line < lastVis)
      {
        int ly = m_text.LineY(line);
        POINT tri[3] = { { w - 22, ly + lh / 2 - 5 }, { w - 22, ly + lh / 2 + 5 }, { w - 13, ly + lh / 2 } };
        HBRUSH tb = CreateSolidBrush(RGB(255, 255, 255));
        HGDIOBJ ob = SelectObject(dc, tb); HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, tri, 3);
        SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(tb);
      }
    }
  }
  if (m_dragging && m_dropIndex >= 0 && !m_layout.empty())
  {
    int y = m_dropIndex < (int)m_layout.size() ? m_text.LineY(m_layout[m_dropIndex].first) - 1 : m_text.LineY(m_layout.back().last) + lh + 1;
    if (y >= m_gutter.top && y <= m_gutter.bottom)
    {
      RECT line = { 2, y - 1, w - 2, y + 2 };
      FillRectColor(dc, line, RGB(255, 255, 255));
    }
  }
  if (!g_doc.IsBound())
  {
    SelectObject(dc, m_fontUi);
    SetTextColor(dc, kDim);
    RECT tr = { 8, m_gutter.top + 10, w - 8, m_gutter.top + 200 };
    DrawTextU8(dc, "No lyric track.\n\nRight-click the track panel and choose Insert lyric track.", &tr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
  }
  SelectObject(dc, oldFont);
}

// ------------------------------------------------------------------ sequencer lane

static void FrameRectColor(HDC dc, const RECT& r, COLORREF c, int w)
{
  RECT t = { r.left, r.top, r.right, r.top + w }, b = { r.left, r.bottom - w, r.right, r.bottom };
  RECT l = { r.left, r.top, r.left + w, r.bottom }, rr = { r.right - w, r.top, r.right, r.bottom };
  FillRectColor(dc, t, c); FillRectColor(dc, b, c); FillRectColor(dc, l, c); FillRectColor(dc, rr, c);
}

static int LaneBlockTop(const RECT& lane) { return (int)lane.top + kRulerH + kLanePad; }
static int LaneLineTop(const RECT& lane) { return LaneBlockTop(lane) + kBlockRowH + kLanePad; }

double LyricEditor::XToTime(int x) const { return m_viewStart + (double)(x - m_lane.left) / m_pxPerSec; }

int LyricEditor::TimeToX(double t) const
{
  double x = (double)m_lane.left + (t - m_viewStart) * m_pxPerSec;
  if (x < -100000) x = -100000;
  if (x > 100000) x = 100000;
  return (int)std::floor(x + 0.5);
}

void LyricEditor::FitView()
{
  int w = (int)(m_lane.right - m_lane.left);
  if (w < 80) return;   // not laid out yet: try again on the next paint
  double a, b;
  if (g_doc.IsBound() && !g_doc.blocks.empty()) { a = g_doc.ItemStart(); b = g_doc.ItemEnd(); }
  else { a = g_doc.IsBound() ? GetCursorPositionEx(g_doc.proj) : 0.0; b = a + 30; }
  double span = std::max(b - a, 4.0);
  m_pxPerSec = std::max(2.0, (w - 48) / span);
  m_viewStart = a - 24.0 / m_pxPerSec;
  m_needFit = false;
}

// zoom buttons keep the playhead (while playing) or the edit cursor in place when it is in
// view, otherwise the middle of the lane
int LyricEditor::ZoomAnchorX() const
{
  if (g_doc.IsBound())
  {
    int x = TimeToX(g_doc.PlayOrEditPos());
    if (x >= m_lane.left && x < m_lane.right) return x;
  }
  return (int)(m_lane.left + m_lane.right) / 2;
}

void LyricEditor::ZoomAt(int x, double factor)
{
  double t = XToTime(x);
  m_pxPerSec = std::min(4000.0, std::max(1.0, m_pxPerSec * factor));
  m_viewStart = t - (double)(x - m_lane.left) / m_pxPerSec;
  ClampView();
  Invalidate();
}

// keep the playhead in view while playing with Track on (page forward like REAPER does)
void LyricEditor::FollowPlayhead()
{
  double t = GetPlayPosition2Ex(g_doc.proj);
  int x = TimeToX(t), w = (int)(m_lane.right - m_lane.left);
  if (x < m_lane.left + 10 || x > m_lane.right - 40)
  {
    m_viewStart = t - (w * 0.1) / m_pxPerSec;
    Invalidate();
  }
}

void LyricEditor::ClampView()
{
  double minStart = -24.0 / m_pxPerSec;   // a little margin before project start
  if (m_viewStart < minStart) m_viewStart = minStart;
}

// Scrollable range: project start to a bit past the lyric item (or the cursor / playhead),
// always including the current view.
void LyricEditor::LaneRange(double* lo, double* hi) const
{
  double span = (double)(m_lane.right - m_lane.left) / m_pxPerSec;
  double a = -24.0 / m_pxPerSec, b = 60.0;
  if (g_doc.IsBound())
  {
    if (!g_doc.blocks.empty()) b = std::max(b, g_doc.ItemEnd() + 30.0);
    b = std::max(b, GetCursorPositionEx(g_doc.proj) + 10.0);
    if (g_doc.IsPlaying()) b = std::max(b, GetPlayPosition2Ex(g_doc.proj) + 10.0);
  }
  *lo = std::min(a, m_viewStart);
  *hi = std::max(b, m_viewStart + span);
}

RECT LyricEditor::ScrollbarRect() const
{
  RECT r = { m_lane.left, m_lane.bottom - 2 - kScrollH, m_lane.right, m_lane.bottom - 2 };
  return r;
}

RECT LyricEditor::ThumbRect(double lo, double hi) const
{
  RECT sb = ScrollbarRect();
  int w = (int)(sb.right - sb.left);
  double total = std::max(1e-6, hi - lo);
  double span = (double)(m_lane.right - m_lane.left) / m_pxPerSec;
  int tw = std::max(24, (int)(w * std::min(1.0, span / total)));
  int tx = (int)sb.left + (int)((w - tw) * std::min(1.0, std::max(0.0, (m_viewStart - lo) / std::max(1e-6, total - span))));
  RECT t = { tx, sb.top + 2, tx + tw, sb.bottom - 2 };
  return t;
}

void LyricEditor::PaintScrollbar(HDC dc)
{
  RECT sb = ScrollbarRect();
  FillRectColor(dc, sb, RGB(26, 26, 26));
  double lo, hi;
  if (m_laneDrag == HIT_SCROLL) { lo = m_scrollLo; hi = m_scrollHi; } else LaneRange(&lo, &hi);
  RECT t = ThumbRect(lo, hi);
  RoundRectColor(dc, t, m_laneDrag == HIT_SCROLL ? RGB(130, 130, 130) : RGB(88, 88, 88), 6);
}

// bottom-right resize grip (the window is resizable from any edge; this makes it obvious)
void LyricEditor::PaintGrip(HDC dc)
{
  RECT rc; GetClientRect(m_hwnd, &rc);
  for (int i = 0; i < 3; i++)
    for (int j = 0; j <= i; j++)
    {
      int x = rc.right - 4 - (i - j) * 4, y = rc.bottom - 4 - j * 4;
      RECT d = { x - 2, y - 2, x, y };
      FillRectColor(dc, d, RGB(120, 120, 120));
    }
}

void LyricEditor::PaintLane(HDC dc)
{
  if (m_needFit) FitView();
  const RECT& r = m_lane;
  if (r.right <= r.left) return;
  FillRectColor(dc, r, kLaneBg);
  RECT ruler = { r.left, r.top, r.right, r.top + kRulerH };
  FillRectColor(dc, ruler, kRulerBg);
  int by0 = LaneBlockTop(r), by1 = by0 + kBlockRowH, ly0 = LaneLineTop(r), ly1 = ly0 + kLineRowH;
  HGDIOBJ oldFont = SelectObject(dc, m_fontLane);
  SetBkMode(dc, TRANSPARENT);
  RECT border = { r.left, r.bottom - 2, r.right, r.bottom };
  FillRectColor(dc, border, RGB(52, 52, 52));
  PaintScrollbar(dc);
  if (!g_doc.IsBound())
  {
    SetTextColor(dc, kDim);
    RECT tr = { r.left + 12, by0, r.right - 12, ly1 };
    DrawTextU8(dc, "No lyric track", &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    return;
  }
  ReaProject* proj = g_doc.proj;

  // the lyric item's extent
  if (!g_doc.blocks.empty())
  {
    RECT sp = { std::max((int)r.left, TimeToX(g_doc.ItemStart())), by0 - 2, std::min((int)r.right, TimeToX(g_doc.ItemEnd())), ly1 + 2 };
    if (sp.right > sp.left) FillRectColor(dc, sp, kItemSpanBg);
  }

  // bars: ruler ticks + numbers, faint grid lines through the rows
  {
    int meas = 0;
    TimeMap2_timeToBeats(proj, std::max(0.0, m_viewStart), &meas, nullptr, nullptr, nullptr);
    double barPx = g_doc.BarLen() * m_pxPerSec;
    int step = 1;
    while (step * barPx < 48 && step < 4096) step *= 2;
    meas -= meas % step;
    SetTextColor(dc, RGB(150, 150, 150));
    for (int m = std::max(0, meas), guard = 0; guard < 2000; m += step, guard++)
    {
      int mm = m;
      int x = TimeToX(TimeMap2_beatsToTime(proj, 0, &mm));
      if (x > r.right) break;
      if (x < r.left) continue;
      RECT tick = { x, ruler.top + 9, x + 1, ruler.bottom }; FillRectColor(dc, tick, RGB(105, 105, 105));
      RECT grid = { x, by0 - 2, x + 1, ly1 + 2 }; FillRectColor(dc, grid, RGB(38, 38, 40));
      RECT tr = { x + 3, ruler.top, x + 60, ruler.bottom };
      DrawTextU8(dc, FormatStr("%d", m + 1), &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
  }

  int cb = -1, cl = 0, cc = 0;
  CaretInfo(&cb, &cl, &cc);
  const auto& blocks = g_doc.blocks;
  if (blocks.empty())
  {
    SetTextColor(dc, kDim);
    RECT tr = { r.left + 12, by0, r.right - 12, by1 };
    DrawTextU8(dc, "Type lyrics below to create the lyric item.", &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  for (size_t i = 0; i < blocks.size(); i++)
  {
    const LyricBlock& b = blocks[i];
    int x0 = TimeToX(b.start), x1 = TimeToX(b.end);
    if (x1 < r.left || x0 > r.right) continue;
    COLORREF c = NativeToColorref(b.color);
    bool light = IsLightColor(c);
    RECT br = { std::max(x0, (int)r.left), by0, std::min(x1 - 1, (int)r.right), by1 };
    if (br.right > br.left) RoundRectColor(dc, br, c, 6);
    if ((int)i == cb && br.right > br.left) FrameRectColor(dc, br, RGB(255, 255, 255), 2);
    SetTextColor(dc, light ? RGB(20, 20, 20) : RGB(250, 250, 250));
    SelectObject(dc, m_fontChip);
    RECT tr = { br.left + 7, by0, br.right - 4, by1 };
    if (tr.right > tr.left + 6) DrawTextU8(dc, b.label, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, m_fontLane);

    for (size_t li = 0; li < b.lines.size(); li++)
    {
      double ls = b.lines[li].pos, le = li + 1 < b.lines.size() ? b.lines[li + 1].pos : b.end;
      int lx0 = TimeToX(ls), lx1 = TimeToX(le);
      if (lx1 < r.left || lx0 > r.right) continue;
      bool now = (int)i == m_curBlock && (int)li == m_curLine;
      COLORREF lc = (li % 2) ? Blend(c, RGB(0, 0, 0), 0.2) : c;
      if (now) lc = Blend(c, RGB(255, 255, 255), 0.4);
      RECT lr = { std::max(lx0, (int)r.left), ly0, std::min(lx1 - 1, (int)r.right), ly1 };
      if (lr.right <= lr.left) continue;
      FillRectColor(dc, lr, lc);
      if ((int)i == cb && (int)li == cl) { RECT u = { lr.left, ly1 - 3, lr.right, ly1 }; FillRectColor(dc, u, RGB(255, 255, 255)); }
      if (li > 0 && lx0 >= r.left) { RECT hnd = { lx0, ly0, lx0 + 2, ly1 }; FillRectColor(dc, hnd, Blend(c, RGB(255, 255, 255), 0.7)); }
      SetTextColor(dc, IsLightColor(lc) ? RGB(20, 20, 20) : RGB(245, 245, 245));
      RECT ttr = { lr.left + 5, ly0 + 3, lr.right - 3, ly1 - 4 };
      if (ttr.right > ttr.left + 6 && !b.lines[li].text.empty())
        DrawTextU8(dc, b.lines[li].text, &ttr, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    }
  }

  // reorder drop marker while dragging a block past its neighbours
  if (m_laneDrag == HIT_BLOCK && m_laneDrop >= 0 && !blocks.empty())
  {
    double t = m_laneDrop < (int)blocks.size() ? blocks[m_laneDrop].start : blocks.back().end;
    int x = TimeToX(t);
    RECT mk = { x - 2, by0 - 3, x + 2, ly1 + 3 };
    FillRectColor(dc, mk, RGB(255, 255, 255));
  }

  // edit cursor and playhead
  int ex = TimeToX(GetCursorPositionEx(proj));
  if (ex >= r.left && ex < r.right) { RECT e = { ex, r.top, ex + 1, ly1 + 2 }; FillRectColor(dc, e, RGB(175, 175, 175)); }
  if (g_doc.IsPlaying())
  {
    int px = TimeToX(GetPlayPosition2Ex(proj));
    if (px >= r.left && px < r.right)
    {
      RECT p = { px - 1, r.top, px + 1, ly1 + 2 }; FillRectColor(dc, p, RGB(255, 255, 255));
      POINT tri[3] = { { px - 6, (int)r.top }, { px + 6, (int)r.top }, { px, (int)r.top + 8 } };
      HBRUSH tb = CreateSolidBrush(RGB(255, 255, 255));
      HGDIOBJ ob = SelectObject(dc, tb); HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
      Polygon(dc, tri, 3);
      SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(tb);
    }
  }
  SelectObject(dc, oldFont);
}

LyricEditor::LaneHitKind LyricEditor::LaneHit(int x, int y, int* block, int* line) const
{
  *block = -1; *line = -1;
  if (x < m_lane.left || x >= m_lane.right || y < m_lane.top || y >= m_lane.bottom) return HIT_NONE;
  if (y < m_lane.top + kRulerH) return HIT_RULER;
  RECT sb = ScrollbarRect();
  if (y >= sb.top && y < sb.bottom) return HIT_SCROLL;
  if (!g_doc.IsBound()) return HIT_EMPTY;
  int by0 = LaneBlockTop(m_lane), by1 = by0 + kBlockRowH, ly0 = LaneLineTop(m_lane), ly1 = ly0 + kLineRowH;
  bool inBlocks = y >= by0 - 2 && y < by1 + 2, inLines = y >= ly0 - 2 && y < ly1 + 2;
  if (!inBlocks && !inLines) return HIT_EMPTY;
  const auto& blocks = g_doc.blocks;
  for (int i = 0; i < (int)blocks.size(); i++)
  {
    const LyricBlock& b = blocks[i];
    int x0 = TimeToX(b.start), x1 = TimeToX(b.end);
    if (x >= x1 - 5 && x <= x1 + 1) { *block = i; return HIT_BLOCK_RIGHT; }
    if (x >= x0 - 1 && x <= x0 + 5 && x < x1) { *block = i; return HIT_BLOCK_LEFT; }
    if (x < x0 || x >= x1) continue;
    *block = i;
    if (inBlocks) return HIT_BLOCK;
    for (int li = 1; li < (int)b.lines.size(); li++)
      if (std::abs(x - TimeToX(b.lines[li].pos)) <= 4) { *line = li; return HIT_LINE_EDGE; }
    for (int li = (int)b.lines.size() - 1; li >= 0; li--)
      if (x >= TimeToX(b.lines[li].pos)) { *line = li; return HIT_LINE; }
    *line = 0;
    return HIT_LINE;
  }
  return HIT_EMPTY;
}

// Dropping a block with the pointer past a neighbour's middle reorders the blocks;
// otherwise it just moves within the free space around it.
int LyricEditor::ReorderTarget(int block, double t) const
{
  const auto& blocks = g_doc.blocks;
  int k = 0;
  for (int i = 0; i < (int)blocks.size(); i++)
    if (i != block && (blocks[i].start + blocks[i].end) / 2 < t) k++;
  if (k == block) return -1;
  return k > block ? k + 1 : k;
}

void LyricEditor::FocusBlockInText(int block, int line)
{
  if (block < 0 || block >= (int)m_layout.size()) return;
  int ln = std::min(m_layout[block].first + std::max(0, line), m_layout[block].last);
  m_text.SetCaret({ ln, 0 });
  m_text.EnsureCaretVisible();
  m_text.SetFocus(true);
  Invalidate();
}

bool LyricEditor::OnLaneMouse(UINT msg, WPARAM wp, int x, int y)
{
  (void)wp;
  int b = -1, l = -1;
  switch (msg)
  {
  case WM_LBUTTONDOWN:
  {
    LaneHitKind k = LaneHit(x, y, &b, &l);
    if (k == HIT_NONE) return false;
    SetFocus(m_hwnd);
    if (m_renaming) CommitRename(true);
    if (k == HIT_SCROLL)
    {
      LaneRange(&m_scrollLo, &m_scrollHi);
      RECT th = ThumbRect(m_scrollLo, m_scrollHi);
      double span = (double)(m_lane.right - m_lane.left) / m_pxPerSec;
      if (x < th.left) m_viewStart -= span * 0.9;          // page left/right on the trough
      else if (x >= th.right) m_viewStart += span * 0.9;
      else { m_laneDrag = HIT_SCROLL; m_scrollGrab = x - (int)th.left; SetCapture(m_hwnd); }
      ClampView();
      Invalidate();
      return true;
    }
    if (!g_doc.IsBound()) return true;
    g_doc.FlushUndo(true);
    m_laneDown = { x, y }; m_laneMoved = false; m_laneBlock = b; m_laneLine = l; m_laneDrop = -1;
    if (k == HIT_RULER || k == HIT_EMPTY)
    {
      m_laneDrag = HIT_RULER;
      SetEditCurPos2(g_doc.proj, std::max(0.0, XToTime(x)), false, true);
    }
    else
    {
      m_laneDrag = k;
      if (k == HIT_BLOCK) m_laneGrab = XToTime(x) - g_doc.blocks[b].start;
    }
    SetCapture(m_hwnd);
    Invalidate();
    return true;
  }
  case WM_MOUSEMOVE:
  {
    if (m_laneDrag == HIT_NONE) return false;
    if (m_laneDrag == HIT_SCROLL)
    {
      RECT sb = ScrollbarRect(), th = ThumbRect(m_scrollLo, m_scrollHi);
      int track = (int)(sb.right - sb.left) - (int)(th.right - th.left);
      double span = (double)(m_lane.right - m_lane.left) / m_pxPerSec;
      double frac = track > 0 ? (double)(x - m_scrollGrab - sb.left) / track : 0.0;
      frac = std::min(1.0, std::max(0.0, frac));
      m_viewStart = m_scrollLo + frac * std::max(0.0, (m_scrollHi - m_scrollLo) - span);
      ClampView();
      Invalidate();
      return true;
    }
    if (!m_laneMoved && std::abs(x - m_laneDown.x) < 3) return true;
    m_laneMoved = true;
    double t = XToTime(x);
    switch (m_laneDrag)
    {
    case HIT_RULER: SetEditCurPos2(g_doc.proj, std::max(0.0, t), false, true); break;
    case HIT_BLOCK: g_doc.DragBlockTo(m_laneBlock, t - m_laneGrab); m_laneDrop = ReorderTarget(m_laneBlock, t); break;
    case HIT_BLOCK_LEFT: g_doc.DragBlockEdge(m_laneBlock, true, t); break;
    case HIT_BLOCK_RIGHT: g_doc.DragBlockEdge(m_laneBlock, false, t, !ShiftDown()); break;   // Shift: no ripple
    case HIT_LINE_EDGE: g_doc.DragLineStart(m_laneBlock, m_laneLine, t); break;
    default: break;
    }
    Invalidate();
    return true;
  }
  case WM_LBUTTONUP:
  {
    if (m_laneDrag == HIT_NONE) return false;
    LaneHitKind k = m_laneDrag;
    m_laneDrag = HIT_NONE;
    if (GetCapture() == m_hwnd) ReleaseCapture();
    int drop = m_laneDrop; m_laneDrop = -1;
    if (k == HIT_RULER || k == HIT_SCROLL) {}
    else if (!m_laneMoved) FocusBlockInText(m_laneBlock, k == HIT_LINE || k == HIT_LINE_EDGE ? m_laneLine : 0);
    else if (k == HIT_BLOCK && drop >= 0) { g_doc.MoveBlock(m_laneBlock, drop); RefreshText(); }
    else if (k == HIT_BLOCK) g_doc.EndDrag("Lyrics: move block");
    else if (k == HIT_LINE_EDGE) g_doc.EndDrag("Lyrics: move line");
    else if (k == HIT_BLOCK_LEFT || k == HIT_BLOCK_RIGHT) g_doc.EndDrag("Lyrics: resize block");
    Invalidate();
    return true;
  }
  case WM_LBUTTONDBLCLK:
  {
    LaneHitKind k = LaneHit(x, y, &b, &l);
    if (k == HIT_NONE) return false;
    if (k == HIT_SCROLL) return true;
    if (k == HIT_BLOCK && b >= 0) { FocusBlockInText(b, 0); BeginRename(b); }
    else if (k == HIT_LINE && b >= 0) FocusBlockInText(b, l);
    return true;
  }
  case WM_RBUTTONUP:
  {
    LaneHitKind k = LaneHit(x, y, &b, &l);
    if (k == HIT_NONE) return false;
    if (b >= 0) { POINT pt = { x, y }; ClientToScreen(m_hwnd, &pt); ShowBlockMenu(b, pt); }
    return true;
  }
  }
  return false;
}

// ------------------------------------------------------------------ hit testing

int LyricEditor::BlockAtY(int y)
{
  int lh = m_text.LineH(), first = m_text.FirstVisibleLine();
  for (size_t i = 0; i < m_layout.size(); i++)
  {
    if (m_layout[i].first < first) continue;
    int y1 = m_text.LineY(m_layout[i].first);
    if (y >= y1 && y < y1 + lh) return (int)i;
  }
  return -1;
}

int LyricEditor::DropIndexAtY(int y)
{
  int lh = m_text.LineH(), best = 0, bestD = 1 << 30;
  for (size_t i = 0; i <= m_layout.size(); i++)
  {
    int by;
    if (i < m_layout.size()) by = m_text.LineY(m_layout[i].first);
    else if (!m_layout.empty()) by = m_text.LineY(m_layout.back().last) + lh;
    else by = 0;
    int d = abs(by - y);
    if (d < bestD) { bestD = d; best = (int)i; }
  }
  return best;
}

int LyricEditor::ButtonAt(int x, int y)
{
  for (auto& b : m_buttons) if (x >= b.r.left && x < b.r.right && y >= b.r.top && y < b.r.bottom) return b.id;
  return 0;
}

void LyricEditor::OnButton(int id)
{
  if (id == BTN_TRACK) SetTracking(!m_tracking);
  else if (id == BTN_PANEL) { LyricsPanel::Get().Toggle(); Invalidate(); }
  else if (id == BTN_ZOOM_IN || id == BTN_ZOOM_OUT) ZoomAt(ZoomAnchorX(), id == BTN_ZOOM_IN ? 1.5 : 1.0 / 1.5);
  else if (id == BTN_ZOOM_FIT) { FitView(); Invalidate(); }
  else if (id == BTN_TRACKSEL)
  {
    POINT pt = { m_buttons[0].r.left, m_buttons[0].r.bottom };
    ClientToScreen(m_hwnd, &pt);
    ShowTrackMenu(pt);
  }
}

// ------------------------------------------------------------------ menus

void LyricEditor::ShowTrackMenu(POINT pt)
{
  ReaProject* p = EnumProjects(-1, nullptr, 0);
  std::vector<MediaTrack*> tracks;
  if (p) LyricDoc::FindLyricTracks(p, tracks);
  HMENU m = CreatePopupMenu();
  for (size_t i = 0; i < tracks.size(); i++)
    AddMenu(m, MF_STRING | (tracks[i] == g_doc.track ? MF_CHECKED : 0), MENU_TRACK + i, LyricDoc::TrackName(tracks[i]).c_str());
  if (tracks.empty()) AddMenu(m, MF_STRING | MF_GRAYED, 0, "(no lyric tracks in this project)");
  AddMenu(m, MF_SEPARATOR, 0, nullptr);
  AddMenu(m, MF_STRING, MENU_TRACK - 1, "Insert new lyric track");
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
  DestroyMenu(m);
  if (cmd >= MENU_TRACK && cmd - MENU_TRACK < (int)tracks.size()) BindTrack(tracks[cmd - MENU_TRACK]);
  else if (cmd == MENU_TRACK - 1 && p)
  {
    MediaTrack* t = LyricDoc::CreateLyricTrack(p);
    if (t) { BindTrack(t); g_doc.AppendBlock(); m_needFit = true; RefreshText(); StartNewBlock(); }
  }
  Invalidate();
}

void LyricEditor::ShowBlockMenu(int block, POINT pt)
{
  if (block < 0 || block >= (int)g_doc.blocks.size()) return;
  HMENU m = CreatePopupMenu();
  HMENU labels = CreatePopupMenu();
  int np; const char** presets = LyricDoc::PresetLabels(&np);
  for (int i = 0; i < np; i++) AddMenu(labels, MF_STRING | (g_doc.blocks[block].label == presets[i] ? MF_CHECKED : 0), MENU_LABEL_PRESET + i, presets[i]);
  AddMenu(labels, MF_SEPARATOR, 0, nullptr);
  AddMenu(labels, MF_STRING | (g_doc.blocks[block].customLabel ? 0 : MF_CHECKED), MENU_LABEL_AUTO, "Automatic (INTRO / VERSE / CHORUS ... OUTRO)");
  HMENU colors = CreatePopupMenu();
  for (int i = 0; i < kNumColors; i++) AddMenu(colors, MF_STRING, MENU_COLOR_PRESET + i, kColors[i].name);
  AddMenu(colors, MF_SEPARATOR, 0, nullptr);
  AddMenu(colors, MF_STRING, MENU_COLOR_CUSTOM, "Custom...");
  AddMenu(colors, MF_STRING | (g_doc.blocks[block].customColor ? 0 : MF_CHECKED), MENU_COLOR_AUTO, "Automatic (by label)");

  AddMenu(m, MF_STRING, MENU_RENAME, "Rename label...");
  AddMenu(m, MF_POPUP, (UINT_PTR)labels, "Label preset");
  AddMenu(m, MF_POPUP, (UINT_PTR)colors, "Colour");
  AddMenu(m, MF_SEPARATOR, 0, nullptr);
  AddMenu(m, MF_STRING, MENU_GOTO, "Move edit cursor to block");
  AddMenu(m, MF_SEPARATOR, 0, nullptr);
  AddMenu(m, MF_STRING, MENU_DELETE, "Delete block");
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
  DestroyMenu(m);
  if (!cmd) return;
  if (cmd == MENU_RENAME) BeginRename(block);
  else if (cmd == MENU_DELETE) { g_doc.DeleteBlock(block); RefreshText(); }
  else if (cmd == MENU_GOTO) SetEditCurPos2(g_doc.proj, g_doc.blocks[block].start, true, false);
  else if (cmd >= MENU_LABEL_PRESET && cmd < MENU_LABEL_PRESET + np) g_doc.SetLabel(block, presets[cmd - MENU_LABEL_PRESET], true);
  else if (cmd == MENU_LABEL_AUTO) g_doc.SetLabel(block, "", false);
  else if (cmd >= MENU_COLOR_PRESET && cmd < MENU_COLOR_PRESET + kNumColors) g_doc.SetColor(block, ColorrefToNative(kColors[cmd - MENU_COLOR_PRESET].c), true);
  else if (cmd == MENU_COLOR_AUTO) g_doc.SetColor(block, 0, false);
  else if (cmd == MENU_COLOR_CUSTOM)
  {
    COLORREF c = NativeToColorref(g_doc.blocks[block].color);
    if (PickColor(m_hwnd, &c)) g_doc.SetColor(block, ColorrefToNative(c), true);
  }
  Invalidate();
}

// ------------------------------------------------------------------ rename

DWORD LyricEditor::DoubleClickMs()
{
#ifdef _WIN32
  return GetDoubleClickTime();
#else
  return 400;
#endif
}

// the grip drawn at the right of a block's label chip (see PaintGutter)
bool LyricEditor::OnChipGrip(int block, int x, int y) const
{
  if (block < 0 || block >= (int)m_layout.size()) return false;
  int chipRight = (int)m_gutter.right - 16, y1 = m_text.LineY(m_layout[block].first);
  return x >= chipRight - 18 && x < chipRight && y >= y1 && y < y1 + m_text.LineH();
}

void LyricEditor::BeginRename(int block)
{
  if (block < 0 || block >= (int)m_layout.size() || block >= (int)g_doc.blocks.size()) return;
  CommitRename(false);
  int lh = m_text.LineH();
  int y1 = m_text.LineY(m_layout[block].first);
  RECT r = { 6, y1, kGutterW - 22, y1 + lh };
  m_rename.SetRect(r);
  m_rename.SetText(g_doc.blocks[block].label);
  m_rename.SelectAll();
  m_rename.SetFocus(true);
  m_text.SetFocus(false);
  m_renaming = true; m_renameBlock = block;
  Invalidate();
}

void LyricEditor::CommitRename(bool apply)
{
  if (!m_renaming) return;
  m_renaming = false;
  if (apply && m_renameBlock >= 0)
  {
    std::string label = Trim(m_rename.GetText());
    if (!label.empty()) g_doc.SetLabel(m_renameBlock, label, true);
  }
  m_renameBlock = -1;
  m_rename.SetFocus(false);
  m_text.SetFocus(true);
  Invalidate();
}

// ------------------------------------------------------------------ window proc

INT_PTR LyricEditor::Handle(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
  case WM_INITDIALOG: return 0;
  case WM_SIZE:
    LayoutControls();
#ifndef _WIN32
    SaveWindowRect("sequencer", h);
#endif
    return 0;
#ifdef _WIN32
  case WM_GETMINMAXINFO: { MINMAXINFO* mi = (MINMAXINFO*)lp; mi->ptMinTrackSize.x = 640; mi->ptMinTrackSize.y = 420; return 0; }
  case WM_ERASEBKGND: SetWindowLongPtr(h, DWLP_MSGRESULT, 1); return 1;
  case WM_NCHITTEST:
  {
    // the bottom-right grip resizes the window
    POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }; ScreenToClient(h, &pt);
    RECT rc; GetClientRect(h, &rc);
    if (pt.x >= rc.right - kGripSize && pt.y >= rc.bottom - kGripSize && pt.x < rc.right && pt.y < rc.bottom)
    { SetWindowLongPtr(h, DWLP_MSGRESULT, HTBOTTOMRIGHT); return 1; }
    return 0;
  }
  // the dialog manager otherwise treats typed characters as mnemonics and drops them
  case WM_GETDLGCODE: SetWindowLongPtr(h, DWLP_MSGRESULT, DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_WANTTAB); return 1;
#else
  case WM_ERASEBKGND: return 1;
#endif
  case WM_PAINT:
  {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
#ifdef _WIN32
    RECT rc; GetClientRect(h, &rc);
    HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, std::max(1L, rc.right), std::max(1L, rc.bottom));
    HGDIOBJ old = SelectObject(mem, bmp);
    Paint(mem);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
#else
    Paint(dc);
#endif
    EndPaint(h, &ps);
    return 1;
  }
  case WM_SETCURSOR:
  {
#ifdef _WIN32
    if (LOWORD(lp) != HTCLIENT) return 0;   // borders/caption: default cursors (resize arrows)
#endif
    POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
    int hb, hl;
    LaneHitKind lk = m_laneDrag != HIT_NONE ? m_laneDrag : LaneHit(pt.x, pt.y, &hb, &hl);
    if (lk == HIT_BLOCK_LEFT || lk == HIT_BLOCK_RIGHT || lk == HIT_LINE_EDGE) SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
    else if (lk == HIT_BLOCK) SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
    else if (lk != HIT_NONE) SetCursor(LoadCursor(nullptr, IDC_ARROW));
    else if (m_text.HitTest(pt.x, pt.y) && pt.x < m_text.Rect().right - 10) SetCursor(LoadCursor(nullptr, IDC_IBEAM));
    else if (m_dragging) SetCursor(LoadCursor(nullptr, IDC_SIZENS));
    else SetCursor(LoadCursor(nullptr, IDC_ARROW));
#ifdef _WIN32
    SetWindowLongPtr(h, DWLP_MSGRESULT, 1);
#endif
    return 1;
  }
  case WM_LBUTTONDOWN:
  {
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    if (OnLaneMouse(msg, wp, x, y)) return 1;
    SetFocus(h);
    if (m_renaming)
    {
      if (m_rename.HitTest(x, y)) { m_rename.OnMouse(msg, wp, x, y); Invalidate(); return 1; }
      CommitRename(true);
    }
    if (m_text.HitTest(x, y))
    {
      m_text.SetFocus(true);
      if (m_text.OnMouse(msg, wp, x, y)) { m_textCapture = true; SetCapture(h); }
      Invalidate();
      return 1;
    }
    if (y < m_toolbar.bottom) { int id = ButtonAt(x, y); if (id) OnButton(id); return 1; }
    if (x < m_gutter.right)
    {
      int b = BlockAtY(y);
      // second click on the same label chip: rename straight away (timed here as well, so it
      // works whether or not the window gets WM_LBUTTONDBLCLK)
      DWORD now = GetTickCount();
      if (b >= 0 && b == m_chipClickBlock && now - m_chipClickTick <= DoubleClickMs()
          && abs(x - m_chipClickPt.x) <= 6 && abs(y - m_chipClickPt.y) <= 6)
      {
        m_chipClickBlock = -1; m_dragBlock = -1;
        BeginRename(b);
        return 1;
      }
      m_chipClickBlock = b; m_chipClickTick = now; m_chipClickPt = { x, y };
      m_dragBlock = b; m_dragging = false; m_dragStart = { x, y };
      if (m_dragBlock >= 0) SetCapture(h);
      return 1;
    }
    return 0;
  }
  case WM_LBUTTONDBLCLK:
  {
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    if (OnLaneMouse(msg, wp, x, y)) return 1;
    if (m_text.HitTest(x, y)) { m_text.OnMouse(msg, wp, x, y); Invalidate(); return 1; }
    if (x < m_gutter.right && y >= m_gutter.top)
    {
      int b = BlockAtY(y);
      if (b >= 0) { m_dragBlock = -1; m_chipClickBlock = -1; if (GetCapture() == h) ReleaseCapture(); BeginRename(b); }
      return 1;
    }
    return 0;
  }
  case WM_MOUSEMOVE:
  {
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    if (m_laneDrag != HIT_NONE) { OnLaneMouse(msg, wp, x, y); return 1; }
    if (m_textCapture) { if (m_text.OnMouse(msg, wp, x, y)) Invalidate(); return 1; }
    if (m_dragBlock >= 0 && GetCapture() == h)
    {
      if (!m_dragging && abs(y - m_dragStart.y) > 4) m_dragging = true;
      if (m_dragging) { m_dropIndex = DropIndexAtY(y); Invalidate(); }
      return 1;
    }
    return 0;
  }
  case WM_LBUTTONUP:
  {
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    if (m_laneDrag != HIT_NONE) { OnLaneMouse(msg, wp, x, y); return 1; }
    if (GetCapture() == h) ReleaseCapture();
    if (m_textCapture) { m_textCapture = false; m_text.OnMouse(msg, wp, x, y); Invalidate(); return 1; }
    int block = m_dragBlock; m_dragBlock = -1;
    if (m_dragging)
    {
      m_dragging = false;
      int to = m_dropIndex; m_dropIndex = -1;
      if (block >= 0 && to >= 0 && to != block && to != block + 1) { g_doc.MoveBlock(block, to); RefreshText(); }
      Invalidate();
      return 1;
    }
    if (block >= 0)
    {
      // plain click: the menu from the grip (the lines at the right of the chip), otherwise
      // select the block. Never a menu on the label itself, so a double-click can rename it.
      if (OnChipGrip(block, x, y)) { m_chipClickBlock = -1; POINT pt = { x, y }; ClientToScreen(h, &pt); ShowBlockMenu(block, pt); }
      else FocusBlockInText(block, 0);
      return 1;
    }
    return 0;
  }
  case WM_RBUTTONUP:
  {
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    if (OnLaneMouse(msg, wp, x, y)) return 1;
    if (x < m_gutter.right && y >= m_gutter.top)
    {
      int b = BlockAtY(y);
      if (b >= 0) { POINT pt = { x, y }; ClientToScreen(h, &pt); ShowBlockMenu(b, pt); }
      return 1;
    }
    return 0;
  }
  case WM_MOUSEWHEEL:
  {
    int delta = (short)HIWORD(wp);
    POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };   // screen coordinates
    ScreenToClient(h, &pt);
    if (pt.x >= m_lane.left && pt.x < m_lane.right && pt.y >= m_lane.top && pt.y < m_lane.bottom)
    {
      // lane: wheel scrolls in time; Ctrl+wheel (or the wheel over the ruler) zooms around the pointer
      if (CtrlDown() || pt.y < m_lane.top + kRulerH) ZoomAt(pt.x, std::pow(1.25, delta / 120.0));
      else { m_viewStart -= (delta / 120.0) * 80.0 / m_pxPerSec; ClampView(); Invalidate(); }
      return 1;
    }
    m_text.OnWheel(delta);
    Invalidate();
    return 1;
  }
  case WM_KEYDOWN:
  case WM_CHAR:
  {
    bool handled = m_renaming ? m_rename.OnKey(msg, wp, lp) : m_text.OnKey(msg, wp, lp);
    if (handled) { Invalidate(); return 1; }
    return 0;
  }
  case WM_SETFOCUS: if (!m_renaming) m_text.SetFocus(true); Invalidate(); return 0;
  case WM_KILLFOCUS: m_text.SetFocus(false); if (m_renaming) CommitRename(true); Invalidate(); return 0;
  case WM_CLOSE: Show(false); return 1;
#ifdef _WIN32
  case WM_EXITSIZEMOVE: SaveWindowRect("sequencer", h); return 0;
#else
  case WM_MOVE: SaveWindowRect("sequencer", h); return 0;
#endif
  case WM_DESTROY:
    m_hwnd = nullptr;
    if (m_font) { DeleteObject(m_font); m_font = nullptr; }
    if (m_fontUi) { DeleteObject(m_fontUi); m_fontUi = nullptr; }
    if (m_fontChip) { DeleteObject(m_fontChip); m_fontChip = nullptr; }
    if (m_fontLane) { DeleteObject(m_fontLane); m_fontLane = nullptr; }
    return 0;
  }
  return 0;
}

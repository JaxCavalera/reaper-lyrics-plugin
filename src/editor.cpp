#include "editor.h"
#include "panel.h"
#include "util.h"
#include "resource.h"
#include <algorithm>

static const int kToolbarH = 38;
static const int kGutterW = 150;
static const COLORREF kBg = RGB(30, 30, 30);
static const COLORREF kToolbarBg = RGB(38, 38, 38);
static const COLORREF kGutterBg = RGB(24, 24, 24);
static const COLORREF kText = RGB(222, 222, 222);
static const COLORREF kDim = RGB(128, 128, 128);
static const COLORREF kButton = RGB(58, 58, 58);
static const COLORREF kAccent = RGB(74, 144, 217);

enum { BTN_TRACKSEL = 1, BTN_TRACK, BTN_PANEL, BTN_ADD };
enum { MENU_RENAME = 1, MENU_DELETE, MENU_GOTO, MENU_LABEL_PRESET = 10, MENU_LABEL_AUTO = 30, MENU_COLOR_PRESET = 40, MENU_COLOR_CUSTOM = 60, MENU_COLOR_AUTO, MENU_TRACK = 100 };

struct ColorPreset { const char* name; COLORREF c; };
static const ColorPreset kColors[] = {
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
  if (!LoadWindowRect("editor", &r)) { r.left = 200; r.top = 150; r.right = 900; r.bottom = 750; }
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
  m_gutter = { 0, kToolbarH, kGutterW, h };
  RECT tr = { kGutterW, kToolbarH, w, h };
  m_text.SetRect(tr);
  m_buttons.clear();
  m_buttons.push_back({ { 8, 7, 228, kToolbarH - 7 }, "", BTN_TRACKSEL, false });
  m_buttons.push_back({ { 236, 7, 312, kToolbarH - 7 }, "Track", BTN_TRACK, true });
  m_buttons.push_back({ { 320, 7, 424, kToolbarH - 7 }, "Lyrics panel", BTN_PANEL, true });
  m_buttons.push_back({ { 432, 7, 508, kToolbarH - 7 }, "+ Block", BTN_ADD, false });
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
    SaveWindowRect("editor", m_hwnd);
    ShowWindow(m_hwnd, SW_HIDE);
  }
}

void LyricEditor::BindTrack(MediaTrack* t)
{
  ReaProject* p = EnumProjects(-1, nullptr, 0);
  g_doc.Bind(p, t);
  m_curBlock = m_curLine = -1; m_scrolledBlock = -1;
  m_trackName = t ? LyricDoc::TrackName(t) : "";
  if (m_hwnd) { RefreshText(); UpdateStatus(); }
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
  PaintToolbar(dc);
  if (m_renaming) m_rename.Paint(dc);
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
  RECT sr = { 520, m_toolbar.top, m_toolbar.right - 8, m_toolbar.bottom };
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
  else if (id == BTN_ADD) { if (g_doc.IsBound()) { g_doc.AppendBlock(); RefreshText(); } }
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
    if (t) { BindTrack(t); g_doc.AppendBlock(); RefreshText(); }
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
    SaveWindowRect("editor", h);
#endif
    return 0;
#ifdef _WIN32
  case WM_GETMINMAXINFO: { MINMAXINFO* mi = (MINMAXINFO*)lp; mi->ptMinTrackSize.x = 560; mi->ptMinTrackSize.y = 240; return 0; }
  case WM_ERASEBKGND: SetWindowLongPtr(h, DWLP_MSGRESULT, 1); return 1;
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
    POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
    if (m_text.HitTest(pt.x, pt.y) && pt.x < m_text.Rect().right - 10) SetCursor(LoadCursor(nullptr, IDC_IBEAM));
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
      m_dragBlock = BlockAtY(y); m_dragging = false; m_dragStart = { x, y };
      if (m_dragBlock >= 0) SetCapture(h);
      return 1;
    }
    return 0;
  }
  case WM_LBUTTONDBLCLK:
  {
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    if (m_text.HitTest(x, y)) { m_text.OnMouse(msg, wp, x, y); Invalidate(); return 1; }
    if (x < m_gutter.right && y >= m_gutter.top)
    {
      int b = BlockAtY(y);
      if (b >= 0) { m_dragBlock = -1; if (GetCapture() == h) ReleaseCapture(); BeginRename(b); }
      return 1;
    }
    return 0;
  }
  case WM_MOUSEMOVE:
  {
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
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
      POINT pt = { x, y }; ClientToScreen(h, &pt);
      ShowBlockMenu(block, pt);
      return 1;
    }
    return 0;
  }
  case WM_RBUTTONUP:
  {
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
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
  case WM_EXITSIZEMOVE: SaveWindowRect("editor", h); return 0;
#else
  case WM_MOVE: SaveWindowRect("editor", h); return 0;
#endif
  case WM_DESTROY:
    m_hwnd = nullptr;
    if (m_font) { DeleteObject(m_font); m_font = nullptr; }
    if (m_fontUi) { DeleteObject(m_fontUi); m_fontUi = nullptr; }
    if (m_fontChip) { DeleteObject(m_fontChip); m_fontChip = nullptr; }
    return 0;
  }
  return 0;
}

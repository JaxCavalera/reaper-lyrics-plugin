// Screenshot tool (Windows): opens the real lyric sequencer and lyrics panel windows on a
// lorem ipsum song, against the in-memory fake of the REAPER API used by the tests, and saves
// both windows stacked into one picture (panel above, sequencer below) as a 32-bit BMP.
//
//   lyrics_screenshot <out.bmp> <scratch dir for settings>
//
// tools/screenshot.ps1 builds it, runs it and writes docs/screenshot.png.
#define REAPERAPI_IMPLEMENT
#include "api.h"
#include "model.h"
#include "editor.h"
#include "panel.h"
#include "util.h"
#include "../tests/fake_reaper.h"
#include <cmath>
#include <string>
#include <vector>

HINSTANCE g_hInst = nullptr;
HWND g_mainHwnd = nullptr;
LyricDoc g_doc;

static std::string g_resPath;

static void InstallMoreFakes()
{
  plugin_register = [](const char*, void*) { return 0; };
  ShowConsoleMsg = [](const char* s) { printf("%s", s); };
  GetMainHwnd = []() { return (HWND)nullptr; };
  GetResourcePath = []() { return g_resPath.c_str(); };   // reaper_lyrics.ini goes there, not into REAPER's
  SetEditCurPos2 = [](ReaProject*, double t, bool, bool) { g_cursor = t; };
  // measures.beats.hundredths, 2-second 4/4 bars (120 bpm) like the rest of the fake
  format_timestr_pos = [](double t, char* buf, int sz, int) {
    int bar = (int)std::floor(t / 2.0);
    double beats = (t - bar * 2.0) / 0.5;
    int beat = (int)std::floor(beats);
    snprintf(buf, sz, "%d.%d.%02d", bar + 1, beat + 1, (int)((beats - beat) * 100));
  };
}

// a song with lorem ipsum lyrics: intro, verse, chorus (with a soft line between its halves),
// verse, chorus, bridge, outro
static std::string SongText()
{
  std::string soft = SoftBlank();
  return
    "Lorem ipsum dolor sit amet\n"
    "Consectetur adipiscing elit\n"
    "\n"
    "Sed do eiusmod tempor incididunt\n"
    "Ut labore et dolore magna aliqua\n"
    "Ut enim ad minim veniam\n"
    "Quis nostrud exercitation ullamco\n"
    "\n"
    "Duis aute irure dolor\n"
    "In reprehenderit in voluptate\n"
    "Velit esse cillum dolore\n" + soft + "\n"
    "Excepteur sint occaecat\n"
    "Cupidatat non proident\n"
    "Sunt in culpa qui officia\n"
    "\n"
    "Nemo enim ipsam voluptatem\n"
    "Quia voluptas sit aspernatur\n"
    "Aut odit aut fugit, sed quia\n"
    "Consequuntur magni dolores\n"
    "\n"
    "Duis aute irure dolor\n"
    "In reprehenderit in voluptate\n"
    "Velit esse cillum dolore\n" + soft + "\n"
    "Excepteur sint occaecat\n"
    "Cupidatat non proident\n"
    "Sunt in culpa qui officia\n"
    "\n"
    "Aaaah, neque porro quisquam\n"
    "\n"
    "Ut enim ad minima veniam\n"
    "Quis autem vel eum iure\n"
    "Nam libero tempore\n"
    "Temporibus autem quibusdam!";
}

static HBITMAP Capture(HWND h, int* w, int* hgt)
{
  RECT r; GetWindowRect(h, &r);
  *w = r.right - r.left; *hgt = r.bottom - r.top;
  HDC screen = GetDC(nullptr);
  HDC dc = CreateCompatibleDC(screen);
  HBITMAP bmp = CreateCompatibleBitmap(screen, *w, *hgt);
  HGDIOBJ old = SelectObject(dc, bmp);
  PrintWindow(h, dc, 2 /* PW_RENDERFULLCONTENT */);
  SelectObject(dc, old); DeleteDC(dc); ReleaseDC(nullptr, screen);
  return bmp;
}

static void Pump(int ms)
{
  DWORD end = GetTickCount() + ms;
  while ((int)(end - GetTickCount()) > 0)
  {
    MSG m;
    while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessage(&m); }
    LyricEditor::Get().OnTimer();
    LyricsPanel::Get().OnTimer();
    Sleep(30);
  }
}

int main(int argc, char** argv)
{
  if (argc < 3) { printf("usage: lyrics_screenshot <out.bmp> <scratch dir>\n"); return 2; }
  g_resPath = argv[2];
  InstallFakes();
  InstallMoreFakes();
  g_hInst = GetModuleHandle(nullptr);

  // the song: blocks laid out from bar 17, default 2 bars per line
  g_cursor = 32.0;
  MediaTrack* t = LyricDoc::CreateLyricTrack(kProj);
  g_doc.Bind(kProj, t);
  g_doc.AppendBlock();
  g_doc.ApplyText(SongText(), nullptr);
  if (g_doc.blocks.size() != 7) { printf("expected 7 blocks, got %d\n", (int)g_doc.blocks.size()); return 1; }
  g_doc.ApplyLabelPreset(5, "BRIDGE");
  // edit cursor on the second line of the first chorus (what the panel shows; with Track on
  // the text scrolls there, so the song shows from the end of the first verse)
  g_cursor = g_doc.blocks[2].lines[1].pos + 0.4;

  // each window is captured at the top of the screen in turn (a window can't be taller than
  // the screen, so they can't sit one above the other)
  RECT work; SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
  const int W = 1070, panelH = 330, seqH = std::min(980, (int)(work.bottom - work.top));
  LyricEditor::Get().OpenForTrack(t);
  LyricsPanel::Get().Show(true);
  HWND seq = LyricEditor::Get().Hwnd(), pan = LyricsPanel::Get().Hwnd();
  if (!seq || !pan) { printf("windows not created\n"); return 1; }
  int w1, h1, w2, h2;
  SetWindowPos(pan, HWND_TOP, work.left, work.top, W, panelH, SWP_NOACTIVATE);
  Pump(1200);
  HBITMAP b1 = Capture(pan, &w1, &h1);
  SetWindowPos(seq, HWND_TOP, work.left, work.top, W, seqH, SWP_NOACTIVATE);
  Pump(1500);
  HBITMAP b2 = Capture(seq, &w2, &h2);
  int w = std::max(w1, w2), h = h1 + h2;
  HDC screen = GetDC(nullptr);
  HDC dc = CreateCompatibleDC(screen), src = CreateCompatibleDC(screen);
  BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP out = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ o = SelectObject(dc, out);
  RECT all = { 0, 0, w, h }; FillRect(dc, &all, (HBRUSH)GetStockObject(BLACK_BRUSH));
  HGDIOBJ os = SelectObject(src, b1); BitBlt(dc, 0, 0, w1, h1, src, 0, 0, SRCCOPY);
  SelectObject(src, b2); BitBlt(dc, 0, h1, w2, h2, src, 0, 0, SRCCOPY);
  SelectObject(src, os);
  GdiFlush();
  BITMAPFILEHEADER fh = {}; fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader);
  fh.bfSize = fh.bfOffBits + (DWORD)w * h * 4;
  FILE* f = fopen(argv[1], "wb");
  if (!f) { printf("can't write %s\n", argv[1]); return 1; }
  fwrite(&fh, sizeof(fh), 1, f); fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f); fwrite(bits, 4, (size_t)w * h, f); fclose(f);
  SelectObject(dc, o); DeleteObject(out); DeleteObject(b1); DeleteObject(b2); DeleteDC(dc); DeleteDC(src); ReleaseDC(nullptr, screen);

  LyricsPanel::Get().Show(false); LyricEditor::Get().Show(false);
  printf("wrote %s (%dx%d)\n", argv[1], w, h);
  return 0;
}

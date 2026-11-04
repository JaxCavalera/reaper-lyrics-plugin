#include "util.h"
#include "api.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

std::string FormatStr(const char* fmt, ...)
{
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  return buf;
}

std::vector<std::string> SplitLines(const std::string& s)
{
  std::vector<std::string> out;
  std::string cur;
  for (char c : s)
  {
    if (c == '\r') continue;
    if (c == '\n') { out.push_back(cur); cur.clear(); }
    else cur += c;
  }
  out.push_back(cur);
  return out;
}

std::string JoinLines(const std::vector<std::string>& v, const char* sep)
{
  std::string s;
  for (size_t i = 0; i < v.size(); i++) { if (i) s += sep; s += v[i]; }
  return s;
}

std::string Trim(const std::string& s)
{
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------- utf-8

int Utf8Next(const std::string& s, int i)
{
  int n = (int)s.size();
  if (i >= n) return n;
  i++;
  while (i < n && (((unsigned char)s[i]) & 0xC0) == 0x80) i++;
  return i;
}

int Utf8Prev(const std::string& s, int i)
{
  if (i <= 0) return 0;
  i--;
  while (i > 0 && (((unsigned char)s[i]) & 0xC0) == 0x80) i--;
  return i;
}

void Utf8Append(std::string& s, unsigned cp)
{
  if (cp < 0x80) s += (char)cp;
  else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
  else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

// ---------------------------------------------------------------- ini

std::string IniPath()
{
  std::string p = GetResourcePath ? GetResourcePath() : ".";
#ifdef _WIN32
  return p + "\\reaper_lyrics.ini";
#else
  return p + "/reaper_lyrics.ini";
#endif
}

int IniGetInt(const char* section, const char* key, int def)
{
  return (int)GetPrivateProfileInt(section, key, def, IniPath().c_str());
}

void IniSetInt(const char* section, const char* key, int v)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", v);
  WritePrivateProfileString(section, key, buf, IniPath().c_str());
}

std::string IniGetStr(const char* section, const char* key, const char* def)
{
  char buf[1024] = "";
  GetPrivateProfileString(section, key, def, buf, sizeof(buf), IniPath().c_str());
  return buf;
}

void IniSetStr(const char* section, const char* key, const char* v)
{
  WritePrivateProfileString(section, key, v, IniPath().c_str());
}

// ---------------------------------------------------------------- colours

COLORREF NativeToColorref(int native)
{
  int r = 0, g = 0, b = 0;
  if (native & 0x1000000) ColorFromNative(native & 0xffffff, &r, &g, &b);
  else return RGB(90, 90, 90);
  return RGB(r, g, b);
}

int ColorrefToNative(COLORREF c)
{
  return ColorToNative(GetRValue(c), GetGValue(c), GetBValue(c)) | 0x1000000;
}

bool IsLightColor(COLORREF c)
{
  double l = 0.299 * GetRValue(c) + 0.587 * GetGValue(c) + 0.114 * GetBValue(c);
  return l > 150;
}

COLORREF Blend(COLORREF a, COLORREF b, double t)
{
  auto mix = [&](int x, int y) { return (int)(x + (y - x) * t); };
  return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

// ---------------------------------------------------------------- LCS

std::vector<std::pair<int, int>> LcsMatch(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
  int n = (int)a.size(), m = (int)b.size();
  std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
  for (int i = n - 1; i >= 0; i--)
    for (int j = m - 1; j >= 0; j--)
      dp[i][j] = (a[i] == b[j]) ? dp[i + 1][j + 1] + 1 : (dp[i + 1][j] > dp[i][j + 1] ? dp[i + 1][j] : dp[i][j + 1]);
  std::vector<std::pair<int, int>> out;
  int i = 0, j = 0;
  while (i < n && j < m)
  {
    if (a[i] == b[j]) { out.push_back({ i, j }); i++; j++; }
    else if (dp[i + 1][j] >= dp[i][j + 1]) i++;
    else j++;
  }
  return out;
}

// ---------------------------------------------------------------- lyric text

std::string CanonicalText(const std::vector<std::string>& lines)
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

int CountParagraphs(const std::vector<std::string>& lines, int upTo)
{
  int n = 0; bool open = false;
  for (int i = 0; i < upTo && i < (int)lines.size(); i++)
  {
    if (Trim(lines[i]).empty()) open = false;
    else if (!open) { n++; open = true; }
  }
  return n;
}

std::string StripCR(const std::string& s)
{
  std::string o;
  for (char c : s) if (c != '\r') o += c;
  return o;
}

std::vector<CopiedBlock> CopiedBlocks(const std::vector<std::string>& lines, const std::vector<std::pair<int, int>>& spans,
                                      const std::vector<std::string>& blockTexts, int aLine, int aCol, int bLine, int bCol)
{
  std::vector<CopiedBlock> out;
  for (size_t i = 0; i < spans.size() && i < blockTexts.size(); i++)
  {
    int first = spans[i].first, last = spans[i].second;
    if (last < aLine || first > bLine) continue;
    std::vector<std::string> part;
    for (int ln = std::max(first, aLine); ln <= std::min(last, bLine) && ln < (int)lines.size(); ln++)
    {
      const std::string& s = lines[ln];
      int c0 = ln == aLine ? std::min(aCol, (int)s.size()) : 0;
      int c1 = ln == bLine ? std::min(bCol, (int)s.size()) : (int)s.size();
      part.push_back(c1 > c0 ? s.substr(c0, c1 - c0) : std::string());
    }
    std::string copied = CanonicalText(part);
    if (copied.empty()) continue;
    out.push_back({ (int)i, copied == blockTexts[i] });
  }
  return out;
}

bool PrepareBlockPaste(const std::vector<std::string>& lines, int caretLine, std::string& text, int* firstPara)
{
  if (caretLine < 0 || caretLine >= (int)lines.size() || !Trim(lines[caretLine]).empty()) return false;   // inside a block
  size_t s = 0, e = text.size();
  while (s < e && text[s] == '\n') s++;
  while (e > s && text[e - 1] == '\n') e--;
  if (s >= e) return false;
  text = text.substr(s, e - s);
  // keep the pasted paragraphs apart from the blocks around the clean line
  if (caretLine > 0 && !Trim(lines[caretLine - 1]).empty()) text = "\n" + text;
  if (caretLine + 1 < (int)lines.size() && !Trim(lines[caretLine + 1]).empty()) text += "\n";
  *firstPara = CountParagraphs(lines, caretLine);
  return true;
}

long long ParseInt(const char* p, const char** end)
{
  const char* s = p;
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' || *s == '\v' || *s == '\f') s++;
  bool neg = false;
  if (*s == '+' || *s == '-') { neg = *s == '-'; s++; }
  if (*s < '0' || *s > '9') { if (end) *end = p; return 0; }   // no number: nothing consumed
  unsigned long long v = 0;
  while (*s >= '0' && *s <= '9') { v = v * 10 + (unsigned)(*s - '0'); s++; }
  if (end) *end = s;
  return neg ? -(long long)v : (long long)v;
}

double WheelZoomFactor(int wheelDelta)
{
  double n = wheelDelta / 120.0;
  bool down = n < 0;
  if (down) n = -n;
  double f = 1.0;
  while (n >= 1.0) { f *= 1.25; n -= 1.0; }
  f *= 1.0 + 0.25 * n;   // part of a notch (touchpads): close to 1.25^n, and monotonic
  return down ? 1.0 / f : f;
}

KeyRoute RouteEditorKey(bool wantsKeys, unsigned msg, int vk, bool ctrl)
{
  if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) return KEY_DEFAULT;   // Alt combos: REAPER's business
  bool key = msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR;
  if (!wantsKeys) return key ? KEY_TO_REAPER : KEY_DEFAULT;          // sequencer focused, text not
  if (msg == WM_KEYDOWN)
  {
    if (vk >= VK_F1 && vk <= VK_F24) return KEY_TO_REAPER;
    if (ctrl && vk >= 'A' && vk <= 'Z' && !strchr("ACXVZY", vk)) return KEY_TO_REAPER;
  }
  return KEY_TO_EDITOR;
}

int DragInsertIndex(const std::vector<std::pair<int, int>>& spans, int dragged, int ghostMid)
{
  int n = (int)spans.size();
  if (dragged < 0 || dragged >= n) return -1;
  int above = 0;   // other blocks whose middle is above the ghost
  for (int j = 0; j < n; j++)
    if (j != dragged && ghostMid > (spans[j].first + spans[j].second) / 2) above++;
  return above <= dragged ? above : above + 1;
}

std::vector<int> DragShuntTargets(const std::vector<int>& heights, int dragged, int to, int y0, int* slotY)
{
  int n = (int)heights.size();
  std::vector<int> out(n, y0);
  if (dragged < 0 || dragged >= n) return out;
  if (to < 0) to = dragged;
  std::vector<int> order;
  for (int j = 0; j < n; j++) if (j != dragged) order.push_back(j);
  int at = to > dragged ? to - 1 : to;
  order.insert(order.begin() + std::max(0, std::min(at, (int)order.size())), dragged);
  int y = y0;
  for (int j : order)
  {
    out[j] = y;
    if (j == dragged && slotY) *slotY = y;
    y += heights[j];
  }
  return out;
}

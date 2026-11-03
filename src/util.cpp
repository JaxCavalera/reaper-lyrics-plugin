#include "util.h"
#include "api.h"
#include <cstdarg>
#include <cstdio>

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

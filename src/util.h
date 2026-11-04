#pragma once
#include "platform.h"
#include <string>
#include <vector>

std::string FormatStr(const char* fmt, ...);
std::vector<std::string> SplitLines(const std::string& s);   // splits on \n, strips \r
std::string JoinLines(const std::vector<std::string>& v, const char* sep = "\n");
std::string Trim(const std::string& s);

// UTF-8 code point stepping (byte offsets)
int Utf8Next(const std::string& s, int i);
int Utf8Prev(const std::string& s, int i);
void Utf8Append(std::string& s, unsigned cp);

// Settings persisted next to REAPER's own ini (reaper_lyrics.ini)
std::string IniPath();
int IniGetInt(const char* section, const char* key, int def);
void IniSetInt(const char* section, const char* key, int v);
std::string IniGetStr(const char* section, const char* key, const char* def);
void IniSetStr(const char* section, const char* key, const char* v);

// colours
COLORREF NativeToColorref(int native);  // REAPER native|0x1000000 -> COLORREF
int ColorrefToNative(COLORREF c);        // COLORREF -> native|0x1000000
bool IsLightColor(COLORREF c);
COLORREF Blend(COLORREF a, COLORREF b, double t);

// LCS alignment of two sequences of strings. Returns pairs (i in a, j in b) of equal
// elements, in increasing order, maximising the number of matches.
std::vector<std::pair<int, int>> LcsMatch(const std::vector<std::string>& a, const std::vector<std::string>& b);

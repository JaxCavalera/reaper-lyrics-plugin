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

// ---- lyric text: paragraphs (blocks) are runs of non-blank lines

// editor lines -> text with exactly one blank line between paragraphs
std::string CanonicalText(const std::vector<std::string>& lines);
// paragraphs that start before line upTo
int CountParagraphs(const std::vector<std::string>& lines, int upTo);
std::string StripCR(const std::string& s);

// Copy: which blocks a selection (aLine,aCol)-(bLine,bCol) takes text from, in order, and
// whether each was copied whole. spans = each block's first/last text line, blockTexts =
// each block's text. Only blocks that give the copied text a paragraph are listed.
struct CopiedBlock { int block; bool whole; };
std::vector<CopiedBlock> CopiedBlocks(const std::vector<std::string>& lines, const std::vector<std::pair<int, int>>& spans,
                                      const std::vector<std::string>& blockTexts, int aLine, int aCol, int bLine, int bCol);

// Paste of copied blocks with the caret at caretLine (no selection). True only on a clean
// line - an empty line, outside every block: then text is trimmed of blank lines at its ends
// and padded with line breaks so its paragraphs stand apart from the blocks around, and
// *firstPara is the paragraph index the first one will have. False: paste as plain text.
bool PrepareBlockPaste(const std::vector<std::string>& lines, int caretLine, std::string& text, int* firstPara);

// ---- dragging a block's label up/down in the editor
// spans = each block's top/bottom (pixels, in order); ghostMid = middle of the dragged ghost.
// Insertion index in the current order (as LyricDoc::MoveBlock takes it): before the first
// other block whose middle is below the ghost; == dragged means it stays put.
int DragInsertIndex(const std::vector<std::pair<int, int>>& spans, int dragged, int ghostMid);

// Portable replacements for libc calls that would tie the Linux build to a recent glibc
// (strtol/strtoll become __isoc23_* on glibc 2.38, pow is versioned 2.29).
long long ParseInt(const char* p, const char** end);   // like strtoll(p, end, 10)
double WheelZoomFactor(int wheelDelta);                // 1.25 per notch (120), smooth in between

// Where a key pressed in the sequencer window goes. wantsKeys: the lyric text was clicked
// into, or a label is being renamed.
enum KeyRoute { KEY_DEFAULT, KEY_TO_REAPER, KEY_TO_EDITOR };
KeyRoute RouteEditorKey(bool wantsKeys, unsigned msg, int vk, bool ctrl);
// Where each block's label goes if dropped at insertion index `to`: the blocks laid out from
// y0 in the new order, each taking `heights[i]` - so the others shunt up or down to make room.
// slotY receives the dragged block's landing position.
std::vector<int> DragShuntTargets(const std::vector<int>& heights, int dragged, int to, int y0, int* slotY);

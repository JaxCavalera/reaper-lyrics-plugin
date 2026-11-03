// Lyric document model and its two-way sync with REAPER.
//
// Timeline representation:
//   lyric track  = MediaTrack tagged with P_EXT:LYR=1
//   block        = empty MediaItem on that track (name = label, notes = text, colour = block colour)
//   line         = project marker (line 0 sits on the item start; markers carry the block colour)
#pragma once
#include "api.h"
#include <string>
#include <vector>
#include <map>
#include <set>

struct LyricLine
{
  std::string text;
  double pos = 0;
  int markerId = -1;
};

struct LyricBlock
{
  std::string guid;          // item GUID string
  MediaItem* item = nullptr;
  std::string label;
  bool customLabel = false;
  int color = 0;             // native|0x1000000
  bool customColor = false;
  double start = 0, end = 0;
  std::vector<LyricLine> lines;

  std::string Text() const;
  double Len() const { return end - start; }
};

// Filled by the editor when the user hits Return while tracking + playing.
struct LiveContext
{
  bool valid = false;
  double pos = 0;
  unsigned long stamp = 0;
};

class LyricDoc
{
public:
  ReaProject* proj = nullptr;
  MediaTrack* track = nullptr;
  std::string trackGuid;
  std::vector<LyricBlock> blocks;
  int lastStateCount = -1;

  bool IsBound() const { return proj && track; }
  bool Bind(ReaProject* p, MediaTrack* t);
  void Unbind();
  bool ValidateTrack();                 // false (and unbinds) if the track/project went away

  static bool IsLyricTrack(MediaTrack* t);
  static void FindLyricTracks(ReaProject* p, std::vector<MediaTrack*>& out);
  static MediaTrack* CreateLyricTrack(ReaProject* p);
  static std::string TrackName(MediaTrack* t);

  // REAPER -> model. Returns true when the editor text needs regenerating
  // (block/line order changed, text edited in REAPER, blocks added/removed).
  bool SyncFromReaper();
  bool NeedsSync() const;

  // Editor text -> model -> REAPER.
  void ApplyText(const std::string& utf8, LiveContext* live);
  std::string ToText() const;

  // structural ops (each creates an undo point)
  void MoveBlock(int from, int to);
  void SetLabel(int i, const std::string& label, bool custom);
  void SetColor(int i, int nativeColor, bool custom);
  void DeleteBlock(int i);
  void AppendBlock(double atPos = -1);

  int FindBlockAt(double t, int* lineOut) const;
  double DefaultBlockLen() const;
  double BarLen() const;
  double PlayOrEditPos() const;
  bool IsPlaying() const;

  static std::string AutoLabel(int idx, int count);
  static int DefaultColorForLabel(const std::string& label);
  static const char** PresetLabels(int* count);

private:
  struct MarkerInfo { double pos; std::string name; int color; };

  void ReadMarkers(std::map<int, MarkerInfo>& out) const;
  bool ReadBlockFromItem(MediaItem* item, LyricBlock& b, const std::map<int, MarkerInfo>& markers, std::set<int>* usedIds);
  void WriteBlock(LyricBlock& b);
  void WriteExtState(LyricBlock& b);
  void DeleteBlockMarkers(LyricBlock& b);
  void DeleteBlockFromReaper(LyricBlock& b);
  void RelabelAuto();
  void NormaliseLines(LyricBlock& b);   // line0 = start, monotonic, inside [start,end]
  void ScaleLines(LyricBlock& b, double oldStart, double oldLen);
  void FinishWrite();
  void DiffLines(LyricBlock& b, const std::vector<std::string>& newLines, LiveContext* live, const LyricBlock* next);
  bool BlocksDiffer(const LyricBlock& a, const LyricBlock& b) const;
};

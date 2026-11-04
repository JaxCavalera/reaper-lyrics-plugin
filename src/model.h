// Lyric document model and its sync with REAPER.
//
// Timeline representation (like a MIDI item):
//   lyric track = MediaTrack tagged with P_EXT:LYR=1
//   lyric item  = the one empty MediaItem on that track carrying P_EXT:LYRD - the whole lyric
//                 document (blocks, labels, colours, line timings), with times relative to the
//                 item start. Moving the item moves the lyrics. The item auto-fits: it starts at
//                 the first block and never ends before the last block (it may be longer).
//   Nothing else goes on the main timeline (no project markers): blocks and lines are shown
//   and edited in the lyric sequencer.
#pragma once
#include "api.h"
#include <string>
#include <vector>

struct LyricLine
{
  std::string text;
  double pos = 0;            // absolute project time
};

struct LyricBlock
{
  std::string label;
  bool customLabel = false;
  int color = 0;             // native|0x1000000
  bool customColor = false;
  bool autoLen = false;      // still default-sized: 2 bars per line, grows/shrinks as lines are typed
  double start = 0, end = 0; // absolute project time
  std::vector<LyricLine> lines;

  std::string Text() const;
  double Len() const { return end - start; }
};

// A block copied in the editor, pasted on a clean line (outside every block): the pasted
// paragraph becomes a block with the same label and colour; a whole copied block also keeps
// its length and line timing.
struct PastedBlock
{
  int para = 0;                 // paragraph index in the text passed to ApplyText
  std::string label;
  int color = 0;
  bool whole = false;           // the entire block was copied (not just some of its lines)
  bool autoLen = false;
  double len = 0;
  std::vector<double> offsets;  // line positions relative to the block start
  static PastedBlock From(const LyricBlock& b, bool whole);
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
  MediaItem* item = nullptr;            // the lyric item (null until the first block exists)
  std::vector<LyricBlock> blocks;       // sorted by start, never overlapping
  int lastStateCount = -1;

  bool IsBound() const { return proj && track; }
  bool Bind(ReaProject* p, MediaTrack* t);
  void Unbind();
  bool ValidateTrack();                 // false (and unbinds) if the track/project went away

  static bool IsLyricTrack(MediaTrack* t);
  static bool IsLyricItem(MediaItem* it);
  static void FindLyricTracks(ReaProject* p, std::vector<MediaTrack*>& out);
  static MediaTrack* CreateLyricTrack(ReaProject* p);
  static std::string TrackName(MediaTrack* t);

  // REAPER -> model (item moved/trimmed, undo/redo, older projects converted).
  // Returns true when the editor text needs regenerating.
  bool SyncFromReaper();
  bool NeedsSync() const;

  // Editor text -> model -> REAPER. Text edits get one coalesced undo point (FlushUndo).
  // pasted: paragraphs that were pasted on a clean line with their block data (else nullptr)
  void ApplyText(const std::string& utf8, LiveContext* live, const std::vector<PastedBlock>* pasted = nullptr);
  std::string ToText() const;
  void FlushUndo(bool force = false);   // call from a timer: adds the undo point after typing pauses

  // structural ops (each creates an undo point)
  void MoveBlock(int from, int to);     // reorder (to = insertion index in the current order)
  void SetLabel(int i, const std::string& label, bool custom);       // rename: colour kept
  void ApplyLabelPreset(int i, const std::string& label);             // preset: its colour too ("" = automatic)
  void SetColor(int i, int nativeColor, bool custom);
  void DeleteBlock(int i);
  void AppendBlock(double atPos = -1);

  // sequencer drags: apply live without an undo point, then EndDrag(name) adds one
  void DragBlockTo(int i, double newStart);              // clamped into the free space around it
  // Resize. Right edge with ripple (the default in the sequencer): the next block snaps to the
  // new end and every later block shifts by the same amount, keeping the gaps between them.
  // Without ripple (and always for the left edge) the edge stops at the neighbour.
  void DragBlockEdge(int i, bool left, double t, bool ripple = false);
  void DragLineStart(int b, int line, double t);         // line > 0, clamped between its neighbours
  void EndDrag(const char* undoName);

  int FindBlockAt(double t, int* lineOut) const;
  double LineSlot() const { return BarLen() * 2; }       // default length per lyric line
  double DefaultBlockLen() const { return LineSlot(); }  // a new block with one line
  double BarLen() const;
  double MinBlockLen() const { return BarLen() / 4; }
  double MinLineLen() const { return 0.05; }
  double PlayOrEditPos() const;
  bool IsPlaying() const;
  double ItemStart() const;
  double ItemEnd() const;

  static std::string AutoLabel(int idx, int count);
  static int DefaultColorForLabel(const std::string& label);
  static const char** PresetLabels(int* count);

  // serialisation (public for tests)
  static std::string Serialize(const std::vector<LyricBlock>& blocks, double origin);
  static bool Deserialize(const std::string& s, double origin, std::vector<LyricBlock>& out);

private:
  MediaItem* FindLyricItem() const;
  bool MigrateOldItems();               // pre-sequencer projects: one item per block + markers
  void Write();                         // model -> lyric item (creates it if needed)
  void RelabelAuto();
  void FixOverlaps();
  void ShiftFrom(size_t first, double d);   // move blocks [first..] by d
  void RelayoutAuto(LyricBlock& b);    // default-sized block: 2 bars per line, evenly spaced
  void NormaliseLines(LyricBlock& b);   // line0 = start, monotonic, inside [start,end]
  void ScaleLines(LyricBlock& b, double oldStart, double oldLen);
  void DiffLines(LyricBlock& b, const std::vector<std::string>& newLines, LiveContext* live, const LyricBlock* next);
  void UndoPoint(const char* name);

  std::string m_written;                // last LYRD value written/read
  double m_itemPos = 0, m_itemLen = 0;  // item geometry as last written/read
  double m_tail = 0;                    // extra item length after the last block (user-dragged)
  bool m_undoPending = false; unsigned long m_lastTextEdit = 0;
};

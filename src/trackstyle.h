// How a lyric track looks in REAPER, so it doesn't read like an audio track.
//
// REAPER draws the track panel from the theme, so an extension can't draw its own. What it
// can do: put the track on one of the theme's track-panel layouts (chosen in the sequencer,
// a "Minimal"-style one by default), hide it from the mixer, give it a fixed locked height,
// a speech-bubble icon and its own colour, and switch off record arm / monitoring / input.
#pragma once
#include "api.h"
#include <string>
#include <vector>

namespace TrackStyle
{
  // layout setting: kAuto = a "Minimal"-like layout if the theme has one, "" = the theme's
  // default layout, anything else = that layout name
  extern const char* const kAuto;

  void LoadSettings();                                  // from reaper_lyrics.ini (plugin load)
  std::string LayoutSetting();
  void SetLayoutSetting(const std::string& s, bool save = true);
  std::vector<std::string> TcpLayouts();                // the current theme's track-panel layouts
  std::string PickLayout(const std::string& setting, const std::vector<std::string>& layouts);
  std::string ResolvedLayout();                         // PickLayout(setting, TcpLayouts())

  int Height();                                         // the fixed lyric track height (px)
  int DefaultColor();                                   // native|0x1000000
  bool IsStyled(MediaTrack* t);

  // full: layout, mixer, height, icon, colour, recording (and marks the track styled);
  // otherwise only the layout. iconFile: full path of the icon, or "" for none.
  void Apply(MediaTrack* t, bool full, const std::string& iconFile);
  std::string IconFile();                               // the stock speech-bubble icon, "" if missing

  // style every lyric track of the project that hasn't been styled yet (older projects,
  // tracks tagged by hand). Returns how many were styled.
  int StyleNewTracks(ReaProject* p);
  // after the layout setting changed (layoutOnly) or "reset style": every lyric track
  void ApplyAll(ReaProject* p, bool layoutOnly);
}

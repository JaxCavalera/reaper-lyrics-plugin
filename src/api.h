// Central list of REAPER API functions this extension uses.
// main.cpp defines REAPERAPI_IMPLEMENT before including this; every other
// file just includes it to get the extern declarations.
#pragma once

#include "platform.h"   // windows.h / swell.h first, so the SDK sees the same definitions

#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_plugin_register
#define REAPERAPI_WANT_ShowConsoleMsg
#define REAPERAPI_WANT_GetMainHwnd
#define REAPERAPI_WANT_GetResourcePath
#define REAPERAPI_WANT_EnumProjects
#define REAPERAPI_WANT_ValidatePtr2
#define REAPERAPI_WANT_GetProjectStateChangeCount
#define REAPERAPI_WANT_PreventUIRefresh
#define REAPERAPI_WANT_UpdateArrange
#define REAPERAPI_WANT_UpdateTimeline
#define REAPERAPI_WANT_TrackList_AdjustWindows
#define REAPERAPI_WANT_Undo_BeginBlock2
#define REAPERAPI_WANT_Undo_EndBlock2
// tracks
#define REAPERAPI_WANT_CountTracks
#define REAPERAPI_WANT_GetTrack
#define REAPERAPI_WANT_InsertTrackAtIndex
#define REAPERAPI_WANT_CountSelectedTracks
#define REAPERAPI_WANT_GetSelectedTrack
#define REAPERAPI_WANT_SetOnlyTrackSelected
#define REAPERAPI_WANT_GetSetMediaTrackInfo_String
#define REAPERAPI_WANT_GetMediaTrackInfo_Value
#define REAPERAPI_WANT_SetMediaTrackInfo_Value
#define REAPERAPI_WANT_GetTrackGUID
#define REAPERAPI_WANT_guidToString
// items
#define REAPERAPI_WANT_AddMediaItemToTrack
#define REAPERAPI_WANT_DeleteTrackMediaItem
#define REAPERAPI_WANT_CountTrackMediaItems
#define REAPERAPI_WANT_GetTrackMediaItem
#define REAPERAPI_WANT_GetSetMediaItemInfo_String
#define REAPERAPI_WANT_GetMediaItemInfo_Value
#define REAPERAPI_WANT_SetMediaItemInfo_Value
#define REAPERAPI_WANT_GetMediaItem_Track
#define REAPERAPI_WANT_GetItemFromPoint
#define REAPERAPI_WANT_Undo_OnStateChange_Item
// markers: only to remove the ones older builds created
#define REAPERAPI_WANT_DeleteProjectMarker
#define REAPERAPI_WANT_EnumProjectMarkers3
// transport / time
#define REAPERAPI_WANT_GetPlayPosition2Ex
#define REAPERAPI_WANT_GetPlayStateEx
#define REAPERAPI_WANT_GetCursorPositionEx
#define REAPERAPI_WANT_SetEditCurPos2
#define REAPERAPI_WANT_TimeMap2_beatsToTime
#define REAPERAPI_WANT_TimeMap2_timeToBeats
#define REAPERAPI_WANT_format_timestr_pos
// colour
#define REAPERAPI_WANT_ColorToNative
#define REAPERAPI_WANT_ColorFromNative

#include "../reaper-sdk/sdk/reaper_plugin.h"
#include "../reaper-sdk/sdk/reaper_plugin_functions.h"

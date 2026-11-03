// SWELL (Linux) equivalent of resource.rc: two empty, resizable dialogs that the
// editor and panel paint themselves.
#ifndef _WIN32
#include "../WDL/WDL/swell/swell.h"
#include "../WDL/WDL/swell/swell-dlggen.h"
#include "resource.h"

SWELL_DEFINE_DIALOG_RESOURCE_BEGIN2(IDD_EDITOR, SWELL_DLG_WS_RESIZABLE | SWELL_DLG_WS_FLIPPED | SWELL_DLG_WS_OPAQUE, "Lyrics", 400, 300)
BEGIN
END
SWELL_DEFINE_DIALOG_RESOURCE_END2(IDD_EDITOR)

SWELL_DEFINE_DIALOG_RESOURCE_BEGIN2(IDD_PANEL, SWELL_DLG_WS_RESIZABLE | SWELL_DLG_WS_FLIPPED | SWELL_DLG_WS_OPAQUE, "Lyrics panel", 300, 120)
BEGIN
END
SWELL_DEFINE_DIALOG_RESOURCE_END2(IDD_PANEL)
#endif

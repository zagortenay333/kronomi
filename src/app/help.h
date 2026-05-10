#pragma once

#include "base/core.h"
#include "ui/ui.h"
#include "ui/ui_view.h"

Void   help_view_init      (UiViewInstance *);
Void   help_view_free      (UiViewInstance *);
UiIcon help_view_get_icon  (UiViewInstance *, Bool visible);
String help_view_get_title (UiViewInstance *, Bool visible);
Void   help_view_build     (UiViewInstance *, Bool visible);

#pragma once

#include "base/core.h"
#include "ui/ui.h"
#include "ui/ui_view.h"

Void   stopwatch_view_init      (UiViewInstance *);
Void   stopwatch_view_free      (UiViewInstance *);
UiIcon stopwatch_view_get_icon  (UiViewInstance *, Bool visible);
String stopwatch_view_get_title (UiViewInstance *, Bool visible);
Void   stopwatch_view_build     (UiViewInstance *, Bool visible);


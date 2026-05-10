#pragma once

#include "base/core.h"
#include "ui/ui.h"
#include "ui/ui_view.h"

Void   alarm_view_init      (UiViewInstance *);
Void   alarm_view_free      (UiViewInstance *);
UiIcon alarm_view_get_icon  (UiViewInstance *, Bool visible);
String alarm_view_get_title (UiViewInstance *, Bool visible);
Void   alarm_view_build     (UiViewInstance *, Bool visible);


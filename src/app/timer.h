#pragma once

#include "base/core.h"
#include "ui/ui.h"
#include "ui/ui_view.h"

Void   timer_view_init      (UiViewInstance *);
Void   timer_view_free      (UiViewInstance *);
UiIcon timer_view_get_icon  (UiViewInstance *, Bool visible);
String timer_view_get_title (UiViewInstance *, Bool visible);
Void   timer_view_build     (UiViewInstance *, Bool visible);


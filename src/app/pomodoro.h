#pragma once

#include "base/core.h"
#include "ui/ui.h"
#include "ui/ui_view.h"

Void   pomodoro_view_init      (UiViewInstance *);
Void   pomodoro_view_free      (UiViewInstance *);
UiIcon pomodoro_view_get_icon  (UiViewInstance *, Bool visible);
String pomodoro_view_get_title (UiViewInstance *, Bool visible);
Void   pomodoro_view_build     (UiViewInstance *, Bool visible);


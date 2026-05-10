#pragma once

#include "base/core.h"
#include "ui/ui.h"
#include "ui/ui_view.h"

Void   todo_view_init      (UiViewInstance *);
Void   todo_view_free      (UiViewInstance *);
UiIcon todo_view_get_icon  (UiViewInstance *, Bool visible);
String todo_view_get_title (UiViewInstance *, Bool visible);
Void   todo_view_build     (UiViewInstance *, Bool visible);


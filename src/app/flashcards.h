#pragma once

#include "base/core.h"
#include "ui/ui.h"
#include "ui/ui_view.h"

Void   flashcards_view_init      (UiViewInstance *);
Void   flashcards_view_free      (UiViewInstance *);
UiIcon flashcards_view_get_icon  (UiViewInstance *, Bool visible);
String flashcards_view_get_title (UiViewInstance *, Bool visible);
Void   flashcards_view_build     (UiViewInstance *, Bool visible);

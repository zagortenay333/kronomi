#pragma once

#include "base/core.h"
#include "base/mem.h"
#include "base/string.h"
#include "ui/ui.h"

fenum (UiMarkupFlags, U8) {
    UI_MARKUP_MONO           = flag(0),
    UI_MARKUP_BOLD           = flag(1),
    UI_MARKUP_ITALIC         = flag(2),
    UI_MARKUP_SUBSCRIPT      = flag(3),
    UI_MARKUP_SUPERSCRIPT    = flag(4),
    UI_MARKUP_UNDERSCORE     = flag(5),
    UI_MARKUP_STRIKETHROUGH  = flag(6),
};

istruct (UiMarkup) {
    UiMarkupFlags flags;
    Vec4 bg_color;
    Vec4 fg_color;
};

istruct (UiMarkupRange) {
    U64 start;
    U64 end;
    UiMarkup markup;
};

array_typedef(UiMarkupRange, UiMarkupRange);

extern UiMarkup default_markup;

UiBox *ui_text_view                 (UiBoxFlags, String id, String text, F32 font_size, Bool, SliceUiMarkupRange);
U64    ui_text_view_coord_to_offset (UiBox *box, Vec2 coord);
Void   ui_markup_flatten_ranges     (String, SliceUiMarkupRange, ArrayUiMarkupRange *);

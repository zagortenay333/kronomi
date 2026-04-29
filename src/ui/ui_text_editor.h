#pragma once

#include "ui/ui.h"
#include "base/core.h"
#include "buffer/buffer.h"
#include "base/string.h"
#include "font/font.h"

ienum (UiTextEditorWrapMode, U8) {
    LINE_WRAP_NONE,
    LINE_WRAP_CHAR,
    LINE_WRAP_WORD,
};

istruct (UiTextEditorVisualLine) {
    U64 logical_line_offset; // Byte offset of containing logical line.
    U64 logical_line_count; // Byte length of logical line.
    U64 logical_col; // Column in logical line counting codepoints.
    U64 offset; // Byte offset of visual line.
    U64 count; // Byte length of visual line.
};

istruct (UiTextEditorCursor) {
    U64 byte_offset;
    U64 selection_offset;
    U64 line; // Index of a visual line, 0-indexed.
    U64 column; // Index into a visual line, 0-indexed, and counting codepoints.
    U64 preferred_column;
};

istruct (UiTextEditorInfo) {
    UiBoxData header;
    Buf *buf;
    U64 buf_version;
    U64 prev_buf_len;
    UiTextEditorCursor cursor;
    Vec2 cursor_coord;
    Vec2 scroll_coord;
    Vec2 scroll_coord_n;
    F32 total_width;
    F32 total_height;
    Bool dragging;
    Bool single_line_mode;
    Bool dirty;
    UiTextEditorWrapMode wrap_mode;
    Array(UiTextEditorVisualLine) visual_lines;
    U64 widest_line;
    U64 viewport_width;
    U64 tab_width;
    Font *font;
};

UiBox *ui_ted                           (String id, Buf *buf, Bool single_line_mode, UiTextEditorWrapMode);
UiBox *ui_ted_resizable                 (String id, Buf *buf, F32 *w, F32 *h, UiTextEditorWrapMode);
Void   ui_ted_cursor_move_left          (UiTextEditorInfo *, UiTextEditorCursor *, Bool move_selection);
Void   ui_ted_cursor_move_right         (UiTextEditorInfo *, UiTextEditorCursor *, Bool move_selection);
Void   ui_ted_cursor_move_up            (UiTextEditorInfo *, UiTextEditorCursor *, Bool move_selection);
Void   ui_ted_cursor_move_left_word     (UiTextEditorInfo *, UiTextEditorCursor *, Bool move_selection);
Void   ui_ted_cursor_move_right_word    (UiTextEditorInfo *, UiTextEditorCursor *, Bool move_selection);
Void   ui_ted_cursor_move_down          (UiTextEditorInfo *, UiTextEditorCursor *, Bool move_selection);
Void   ui_ted_cursor_move_to_start      (UiTextEditorInfo *, UiTextEditorCursor *, Bool move_selection);
Void   ui_ted_cursor_move_to_end        (UiTextEditorInfo *, UiTextEditorCursor *, Bool move_selection);
Void   ui_ted_cursor_clamp              (UiTextEditorInfo *, UiTextEditorCursor *);
String ui_ted_cursor_get_selection      (UiTextEditorInfo *, Mem *, UiTextEditorCursor *);
U64    ui_ted_cursor_line_col_to_offset (UiTextEditorInfo *, UiTextEditorCursor *);
Void   ui_ted_cursor_offset_to_line_col (UiTextEditorInfo *, UiTextEditorCursor *);
Void   ui_ted_cursor_swap_offset        (UiTextEditorInfo *, UiTextEditorCursor *);
Void   ui_ted_cursor_delete             (UiTextEditorInfo *, UiTextEditorCursor *);
Void   ui_ted_cursor_insert             (UiTextEditorInfo *, UiTextEditorCursor *, String);

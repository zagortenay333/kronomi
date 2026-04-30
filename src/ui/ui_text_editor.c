#include "ui/ui_text_editor.h"
#include "ui/ui_widgets.h"
#include "base/string.h"

static Vec2 cursor_to_coord (UiTextEditorInfo *info, UiBox *box, UiTextEditorCursor *pos);
static UiTextEditorCursor coord_to_cursor (UiTextEditorInfo *info, UiBox *box, Vec2 coord);

static U32 get_visual_col_count (UiTextEditorInfo *info, U32 ch, U32 column) {
    if (ch != '\t') {
        return 1;
    } else {
        return info->tab_width - (column % info->tab_width);
    }
}

static Void compute_visual_lines (UiTextEditorInfo *info) {
    if (! info->dirty) return;
    info->dirty = false;

    info->visual_lines.count = 0;
    info->widest_line = 0;

    U64 viewport_width_in_chars = info->viewport_width / info->font->width;
    if (viewport_width_in_chars == 0) viewport_width_in_chars = 120;

    tmem_new(tm);
    buf_iter_lines (line, info->buf, tm) {
        switch (info->wrap_mode) {
        case LINE_WRAP_NONE: {
            U64 logical_len = 0;
            str_utf8_iter (it, line->text) logical_len += get_visual_col_count(info, it.decode.codepoint, logical_len);
            if (logical_len > info->widest_line) info->widest_line = logical_len;

            UiTextEditorVisualLine *vline = array_push_slot(&info->visual_lines);
            vline->logical_line_offset = line->offset;
            vline->logical_line_count = line->text.count;
            vline->logical_col = 0;
            vline->offset = line->offset;
            vline->count = line->text.count;
        } break;

        case LINE_WRAP_WORD: {
            badpath;
        } break;

        case LINE_WRAP_CHAR: {
            U64 vcol = 0;
            U64 total_col = 0;
            U64 vcount = 0;
            U64 voffset = line->offset;

            str_utf8_iter (it, line->text) {
                U32 cols = get_visual_col_count(info, it.decode.codepoint, vcol);

                if (vcol + cols > viewport_width_in_chars) {
                    UiTextEditorVisualLine *vline = array_push_slot(&info->visual_lines);
                    vline->logical_line_offset = line->offset;
                    vline->logical_line_count = line->text.count;
                    vline->logical_col = total_col;
                    vline->offset = voffset;
                    vline->count = vcount;

                    voffset += vcount;
                    vcount = 0;
                    vcol = 0;
                }

                vcol += cols;
                total_col++;
                vcount += it.decode.inc;
            }

            if (vcount || line->text.count == 0) {
                UiTextEditorVisualLine *vline = array_push_slot(&info->visual_lines);
                vline->logical_line_offset = line->offset;
                vline->logical_line_count = line->text.count;
                vline->logical_col = total_col;
                vline->offset = voffset;
                vline->count = vcount;
            }
        } break;
        }
    }
}

static String get_line_text (UiTextEditorInfo *info, Mem *mem, U64 idx) {
    if (idx >= info->visual_lines.count) return (String){};
    UiTextEditorVisualLine *line = array_ref(&info->visual_lines, idx);
    return buf_get_slice(info->buf, mem, line->offset, line->count);
}

U64 ui_ted_cursor_line_col_to_offset (UiTextEditorInfo *info, UiTextEditorCursor *cursor) {
    tmem_new(tm);
    if (cursor->line >= info->visual_lines.count) return 0;
    UiTextEditorVisualLine *line = array_ref(&info->visual_lines, cursor->line);
    String line_text = buf_get_slice(info->buf, tm, line->offset, line->count);
    U64 off = 0;
    U64 idx = 0;
    str_utf8_iter (c, line_text) {
        if (idx == cursor->column) break;
        off += c.decode.inc;
        idx++;
    }
    return line->offset + off;
}

Void ui_ted_cursor_offset_to_line_col (UiTextEditorInfo *info, UiTextEditorCursor *cursor) {
    compute_visual_lines(info);

    cursor->line = sat_sub64(info->visual_lines.count, 1);
    cursor->column = 0;

    array_iter (line, &info->visual_lines, *) {
        U64 end_of_line = line->offset + line->count;
        if (end_of_line >= cursor->byte_offset) {
            cursor->line = ARRAY_IDX;
            break;
        }
    }

    if (cursor->line >= info->visual_lines.count) return;

    tmem_new(tm);
    UiTextEditorVisualLine *line = array_ref(&info->visual_lines, cursor->line);
    String line_text = buf_get_slice(info->buf, tm, line->offset, line->count);
    U64 off = line->offset;
    str_utf8_iter (c, line_text) {
        if (off >= cursor->byte_offset) break;
        off += c.decode.inc;
        cursor->column++;
    }
}

Void ui_ted_cursor_swap_offset (UiTextEditorInfo *info, UiTextEditorCursor *cursor) {
    swap(cursor->byte_offset, cursor->selection_offset);
    ui_ted_cursor_offset_to_line_col(info, cursor);
}

Void ui_ted_cursor_delete (UiTextEditorInfo *info, UiTextEditorCursor *cursor) {
    if (cursor->byte_offset > cursor->selection_offset) ui_ted_cursor_swap_offset(info, cursor);
    buf_delete(info->buf, cursor->byte_offset, cursor->selection_offset - cursor->byte_offset);
    info->dirty = true;
    cursor->selection_offset = cursor->byte_offset;
    cursor->preferred_column = cursor->column;
}

Void ui_ted_cursor_insert (UiTextEditorInfo *info, UiTextEditorCursor *cursor, String str) {
    if (cursor->byte_offset != cursor->selection_offset) ui_ted_cursor_delete(info, cursor);
    buf_insert(info->buf, cursor->byte_offset, str);
    info->dirty = true;
    cursor->byte_offset += str.count;
    cursor->selection_offset = cursor->byte_offset;
    ui_ted_cursor_offset_to_line_col(info, cursor);
    cursor->preferred_column = cursor->column;
}

UiTextEditorCursor cursor_new (UiTextEditorInfo *info, U64 line, U64 column) {
    UiTextEditorCursor cursor = {};
    cursor.line = line;
    cursor.column = column;
    cursor.preferred_column = column;
    cursor.byte_offset = ui_ted_cursor_line_col_to_offset(info, &cursor);
    cursor.selection_offset = cursor.byte_offset;
    return cursor;
}

Void ui_ted_cursor_move_left (UiTextEditorInfo *info, UiTextEditorCursor *cursor, Bool move_selection) {
    if (cursor->column > 0) {
        cursor->preferred_column--;
    } else if (cursor->line > 0) {
        cursor->line--;
        tmem_new(tm);
        String line = get_line_text(info, tm, cursor->line);
        cursor->preferred_column = str_codepoint_count(line);
    }

    cursor->column = cursor->preferred_column;
    cursor->byte_offset = ui_ted_cursor_line_col_to_offset(info, cursor);
    if (move_selection) cursor->selection_offset = cursor->byte_offset;
}

Void ui_ted_cursor_move_right (UiTextEditorInfo *info, UiTextEditorCursor *cursor, Bool move_selection) {
    tmem_new(tm);
    String line = get_line_text(info, tm, cursor->line);
    U64 count = str_codepoint_count(line);

    if (cursor->preferred_column < count) {
        cursor->preferred_column++;
        cursor->column = cursor->preferred_column;
    } else if (cursor->line < sat_sub64(info->visual_lines.count, 1)) {
        cursor->line++;
        cursor->column = 0;
        cursor->preferred_column = 0;
    }

    cursor->byte_offset = ui_ted_cursor_line_col_to_offset(info, cursor);
    if (move_selection) cursor->selection_offset = cursor->byte_offset;
}

Void ui_ted_cursor_move_up (UiTextEditorInfo *info, UiTextEditorCursor *cursor, Bool move_selection) {
    if (cursor->line > 0) cursor->line--;

    tmem_new(tm);
    String line = get_line_text(info, tm, cursor->line);
    U64 count = str_codepoint_count(line);
    if (cursor->preferred_column > count) {
        cursor->column = count;
    } else {
        cursor->column = cursor->preferred_column;
    }

    cursor->byte_offset = ui_ted_cursor_line_col_to_offset(info, cursor);
    if (move_selection) cursor->selection_offset = cursor->byte_offset;
}

Void ui_ted_cursor_move_left_word (UiTextEditorInfo *info, UiTextEditorCursor *cursor, Bool move_selection) {
    cursor->byte_offset = buf_find_prev_word(info->buf, cursor->byte_offset);
    ui_ted_cursor_offset_to_line_col(info, cursor);
    cursor->preferred_column = cursor->column;
    if (move_selection) cursor->selection_offset = cursor->byte_offset;
}

Void ui_ted_cursor_move_right_word (UiTextEditorInfo *info, UiTextEditorCursor *cursor, Bool move_selection) {
    cursor->byte_offset = buf_find_next_word(info->buf, cursor->byte_offset);
    ui_ted_cursor_offset_to_line_col(info, cursor);
    cursor->preferred_column = cursor->column;
    if (move_selection) cursor->selection_offset = cursor->byte_offset;
}

Void ui_ted_cursor_move_down (UiTextEditorInfo *info, UiTextEditorCursor *cursor, Bool move_selection) {
    if (cursor->line < sat_sub64(info->visual_lines.count, 1)) cursor->line++;

    tmem_new(tm);
    String line = get_line_text(info, tm, cursor->line);
    U64 count = str_codepoint_count(line);
    if (cursor->preferred_column > count) {
        cursor->column = count;
    } else {
        cursor->column = cursor->preferred_column;
    }

    cursor->byte_offset = ui_ted_cursor_line_col_to_offset(info, cursor);
    if (move_selection) cursor->selection_offset = cursor->byte_offset;
}

Void ui_ted_cursor_move_to_start (UiTextEditorInfo *info, UiTextEditorCursor *cursor, Bool move_selection) {
    cursor->byte_offset = 0;
    cursor->line = 0;
    cursor->column = 0;
    cursor->preferred_column = 0;
    if (move_selection) cursor->selection_offset = 0;
}

Void ui_ted_cursor_move_to_end (UiTextEditorInfo *info, UiTextEditorCursor *cursor, Bool move_selection) {
    cursor->byte_offset = buf_get_count(info->buf);
    ui_ted_cursor_offset_to_line_col(info, cursor);
    cursor->preferred_column = cursor->column;
    if (move_selection) cursor->selection_offset = cursor->byte_offset;
}

Void ui_ted_cursor_clamp (UiTextEditorInfo *info, UiTextEditorCursor *cursor) {
    if (cursor->byte_offset > buf_get_count(info->buf)) {
        ui_ted_cursor_move_to_end(info, cursor, true);
    }
}

String ui_ted_cursor_get_selection (UiTextEditorInfo *info, Mem *mem, UiTextEditorCursor *cursor) {
    U64 start = cursor->byte_offset;
    U64 end = cursor->selection_offset;
    if (start > end) swap(start, end);
    return buf_get_slice(info->buf, mem, start, end - start);
}


static Void draw_line (UiTextEditorInfo *info, UiBox *box, U64 line_idx, UiTextEditorVisualLine *line, Vec4 color, F32 x, F32 y) {
    tmem_new(tm);
    dr_bind_texture(&info->font->atlas_texture);

    String line_text = buf_get_slice(info->buf, tm, line->offset, line->count);

    U64 cell_w = info->font->width;
    U64 cell_h = info->font->height;
    SliceGlyphInfo infos = font_get_glyph_infos(info->font, tm, line_text);

    x = floor(x - info->scroll_coord.x);

    F32 descent = cast(F32, info->font->descent);
    F32 line_spacing = ui->config->line_spacing;

    U64 selection_start = info->cursor.byte_offset;
    U64 selection_end   = info->cursor.selection_offset;
    if (selection_end < selection_start) swap(selection_start, selection_end);

    U64 vcol = 0;
    array_iter (glyph_info, &infos, *) {
        if (x > box->rect.x + box->rect.w) break;

        U32 cols = get_visual_col_count(info, glyph_info->codepoint, vcol);
        F32 advance = cell_w * cols;

        if (x + advance > box->rect.x) {
            UiTextEditorCursor current = cursor_new(info, line_idx, ARRAY_IDX);
            Bool selected = current.byte_offset >= selection_start && current.byte_offset < selection_end;

            if (selected) dr_rect(
                .color        = ui->theme->bg_color_selection,
                .color2       = ui->theme->bg_color_selection,
                .top_left     = {x, y - cell_h - line_spacing},
                .bottom_right = {x + advance, y},
            );

            if (glyph_info->codepoint != '\t') {
                AtlasSlot *slot = font_get_atlas_slot(info->font, glyph_info);
                Vec2 top_left = {x + slot->bearing_x, y - descent - line_spacing/2 - slot->bearing_y};
                Vec2 bottom_right = {top_left.x + slot->width, top_left.y + slot->height};
                Vec4 final_text_color = selected ? ui->theme->text_color_selection : color;

                dr_rect(
                    .top_left     = top_left,
                    .bottom_right = bottom_right,
                    .texture_rect = {slot->x, slot->y, slot->width, slot->height},
                    .text_color   = final_text_color,
                    .text_is_grayscale = (slot->pixel_mode == FT_PIXEL_MODE_GRAY),
                );
            }
        }

        x += advance;
        vcol += cols;
    }
}

static Void draw (UiBox *box) {
    UiBox *container = box->parent;
    UiTextEditorInfo *info = ui_get_box_data(container, 0, 0);

    compute_visual_lines(info);

    U64 cell_h = info->font->height;
    U64 cell_w = info->font->width;

    F32 line_spacing   = ui->config->line_spacing;
    info->total_width  = info->widest_line * cell_w;
    info->total_height = info->visual_lines.count * (cell_h + line_spacing);

    F32 line_height = cell_h + line_spacing;
    UiTextEditorCursor pos = coord_to_cursor(info, box, box->rect.top_left);
    F32 y = box->rect.y + (pos.line+1) * line_height - info->scroll_coord.y;

    array_iter_from (line, &info->visual_lines, pos.line, *) {
        if (y - line_height > box->rect.y + box->rect.h) break;
        draw_line(info, box, ARRAY_IDX, line, container->style.text_color, box->rect.x, floor(y));
        y += line_height;
    }

    if (box->signals.focused) dr_rect(
        .color = ui->theme->color_accent,
        .color2 = {-1},
        .top_left = info->cursor_coord,
        .bottom_right = { info->cursor_coord.x + 2, info->cursor_coord.y + cell_h },
    );
}

static Void vscroll (UiTextEditorInfo *info, UiBox *box, U64 line, UiAlign align) {
    F32 line_spacing = ui->config->line_spacing;
    U64 cell_h = info->font->height;
    info->scroll_coord_n.y = cast(F32, line) * (cell_h + line_spacing);

    F32 visible_h = box->rect.h;

    if (info->total_height <= visible_h) {
        info->scroll_coord_n.y = 0;
    } else if (align == UI_ALIGN_MIDDLE) {
        info->scroll_coord_n.y -= round(visible_h / 2);
    } else if (align == UI_ALIGN_END) {
        info->scroll_coord_n.y -= visible_h - cell_h - line_spacing;
    }
}

static Void hscroll (UiTextEditorInfo *info, UiBox *box, U64 line, U64 column, UiAlign align) {
    tmem_new(tm);
    String line_text = get_line_text(info, tm, line);

    U64 cell_w = info->font->width;
    U64 vcol = 0;
    U64 col = 0;

    info->scroll_coord_n.x = 0;
    str_utf8_iter (it, line_text) {
        if (col >= column) break;
        vcol += get_visual_col_count(info, it.decode.codepoint, vcol);
        col++;
    }

    info->scroll_coord_n.x = cast(F32, vcol) * cell_w;

    F32 visible_w = box->rect.w;

    if (info->total_width <= visible_w) {
        info->scroll_coord_n.x = 0;
    } else if (align == UI_ALIGN_MIDDLE) {
        info->scroll_coord_n.x -= round(visible_w / 2);
    } else if (align == UI_ALIGN_END) {
        info->scroll_coord_n.x -= visible_w - cell_w;
    }
}

static Void text_box_scroll_into_view (UiTextEditorInfo *info, UiBox *box, UiTextEditorCursor *pos, U64 padding) {
    U64 cell_w = info->font->width;
    U64 cell_h = info->font->height;
    F32 line_spacing = ui->config->line_spacing;

    Vec2 coord = cursor_to_coord(info, box, pos);

    U64 x_padding = padding * cell_w;
    U64 y_padding = padding * (cell_h + line_spacing);

    if (coord.x < box->rect.x + x_padding) {
        hscroll(info, box, pos->line, sat_sub32(pos->column, padding), UI_ALIGN_START);
    } else if (coord.x > box->rect.x + box->rect.w - x_padding) {
        hscroll(info, box, pos->line, clamp(sat_add32(pos->column, padding), 0u, info->widest_line), UI_ALIGN_END);
    }

    if (coord.y < box->rect.y + y_padding) {
        vscroll(info, box, sat_sub32(pos->line, padding), UI_ALIGN_START);
    } else if (coord.y + cell_h > box->rect.y + box->rect.h - y_padding) {
        vscroll(info, box, clamp(sat_add32(pos->line, padding), 0u, sat_sub64(info->visual_lines.count, 1)), UI_ALIGN_END);
    }
}

static UiTextEditorCursor coord_to_cursor (UiTextEditorInfo *info, UiBox *box, Vec2 coord) {
    F32 cell_w = info->font->width;
    F32 cell_h = info->font->height;
    F32 line_spacing = ui->config->line_spacing;

    coord.x = coord.x - box->rect.x + info->scroll_coord.x;
    coord.y = coord.y - box->rect.y + info->scroll_coord.y;

    U64 line_idx = clamp(coord.y / (cell_h + line_spacing), cast(F32, 0), cast(F32, sat_sub64(info->visual_lines.count, 1)));

    tmem_new(tm);
    String line_text = get_line_text(info, tm, line_idx);

    F32 x = 0;
    U64 col = 0;
    U64 vcol = 0;
    str_utf8_iter (it, line_text) {
        U32 cols = get_visual_col_count(info, it.decode.codepoint, vcol);;
        U32 advance = cols * cell_w;
        if (x + (advance * 0.5) > coord.x) break;
        col++;
        vcol += cols;
        x += advance;
    }

    return cursor_new(info, line_idx, col);
}

static Vec2 cursor_to_coord (UiTextEditorInfo *info, UiBox *box, UiTextEditorCursor *pos) {
    Vec2 coord = {};

    F32 char_width  = info->font->width;
    F32 line_spacing = ui->config->line_spacing;
    F32 line_height = info->font->height + line_spacing;

    coord.y = pos->line * line_height;

    tmem_new(tm);
    String line_text = get_line_text(info, tm, pos->line);

    U64 i = 0;
    U64 vcol = 0;
    str_utf8_iter (it, line_text) {
        if (i >= pos->column) break;
        U32 cols = get_visual_col_count(info, it.decode.codepoint, vcol);
        coord.x += cols * char_width;
        vcol += cols;
        i++;
    }

    coord.x += box->rect.x - info->scroll_coord.x;
    coord.y += box->rect.y - info->scroll_coord.y;

    return coord;
}

UiBox *ui_ted (String label, Buf *buf, Bool single_line_mode, UiTextEditorWrapMode wrap_mode) {
    UiBox *container = ui_box_str(0, label) {
        ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_normal);

        UiTextEditorInfo *info = ui_get_box_data(container, sizeof(UiTextEditorInfo), sizeof(UiTextEditorInfo));

        info->font = font_get(ui->font_cache, ui->config->font_path_mono, ui->config->font_size);
        info->buf = buf;
        info->single_line_mode = single_line_mode;
        info->tab_width = ui->config->tab_width;

        if (container->start_frame == ui->frame) {
            info->wrap_mode = single_line_mode ? LINE_WRAP_NONE : wrap_mode;
            info->dirty = true;
            array_init(&info->visual_lines, info->header.mem);
        }

        U64 v = buf_get_version(info->buf);
        U64 l = buf_get_count(info->buf);
        if (info->prev_buf_len != l || info->buf_version != v) {
            info->dirty = true;
            info->buf_version = v;
        }

        ui_ted_cursor_clamp(info, &info->cursor); // In case the buffer changed.

        F32 line_spacing = ui->config->line_spacing;

        if (info->single_line_mode) {
            U64 height = 2*container->style.padding.y + info->font->height;
            ui_style_box_size(container, UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, height, 1});
        }

        UiBox *text_box = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE|UI_BOX_CLIPPING, "text") {
            F32 visible_w = text_box->rect.w;
            F32 visible_h = text_box->rect.h;
            Bool scroll_y = info->total_height > visible_h && visible_h > 0;
            Bool scroll_x = info->total_width  > visible_w && visible_w > 0;
            F32 scrollbar_width = ui->config->scrollbar_width;

            if (info->viewport_width != text_box->rect.w) info->dirty = true;
            info->viewport_width = text_box->rect.w;

            ui_style_u32(UI_ANIMATION, UI_MASK_HEIGHT|UI_MASK_WIDTH);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, container->rect.w - container->style.padding.x - (scroll_y ? scrollbar_width : 0), 1});
            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, container->rect.h - container->style.padding.y - (scroll_x ? scrollbar_width : 0), 1});

            if (!info->single_line_mode && text_box->signals.hovered && ui->event->tag == EVENT_SCROLL) {
                U64 cell_w = info->font->width;
                U64 cell_h = info->font->height;

                if (scroll_y && !ui_is_key_pressed(KEY_SHIFT)) {
                    info->scroll_coord_n.y -= (cell_h + line_spacing) * ui->event->y;
                    info->scroll_coord_n.y  = clamp(info->scroll_coord_n.y, 0, info->total_height - visible_h);
                    ui_eat_event();
                } else if (scroll_x) {
                    info->scroll_coord_n.x -= cell_w * ui->event->y;
                    info->scroll_coord_n.x  = clamp(info->scroll_coord_n.x, 0, info->total_width - visible_w);
                    ui_eat_event();
                }
            }

            text_box->draw_fn = draw;
        }

        F32 visible_w = text_box->rect.w;
        F32 visible_h = text_box->rect.h;
        Bool scroll_y = info->total_height > visible_h && visible_h > 0;
        Bool scroll_x = info->total_width  > visible_w && visible_w > 0;
        F32 scrollbar_width = ui->config->scrollbar_width;

        if (scroll_y && !info->single_line_mode) {
            F32 ratio = visible_h / info->total_height;
            Rect rect = { container->rect.w - scrollbar_width, 0, scrollbar_width, container->rect.h };
            if (scroll_x) rect.h -= scrollbar_width;

            F32 max_y_offset = max(0.0f, info->total_height - visible_h);
            F32 knob_height  = rect.h * (visible_h / info->total_height);
            F32 max_knob_v   = rect.h - knob_height;
            F32 before       = (info->scroll_coord.y / max_y_offset) * max_knob_v;
            F32 after        = before;

            ui_vscroll_bar(str("scroll_bar_y"), rect, ratio, &after);
            if (before != after) info->scroll_coord.y = info->scroll_coord_n.y = clamp(after / max_knob_v, 0, 1) * max_y_offset;
        }

        if (scroll_x && !info->single_line_mode) {
            F32 ratio = visible_w / info->total_width;
            Rect rect = { 0, container->rect.h - scrollbar_width, container->rect.w, scrollbar_width };
            if (scroll_y) rect.w -= scrollbar_width;

            F32 max_x_offset = max(0.0f, info->total_width - visible_w);
            F32 knob_width   = rect.w * (visible_w / info->total_width);
            F32 max_knob_h   = rect.w - knob_width;
            F32 before       = (info->scroll_coord.x / max_x_offset) * max_knob_h;
            F32 after        = before;

            ui_hscroll_bar(str("scroll_bar_x"), rect, ratio, &after);
            if (before != after) info->scroll_coord.x = info->scroll_coord_n.x = clamp(after / max_knob_h, 0, 1) * max_x_offset;
        }

        if (text_box->signals.hovered) {
            ui_set_mouse_cursor(MOUSE_CURSOR_TEXT);
        }

        if (text_box->signals.focused && ui->event->tag == EVENT_KEY_PRESS) {
            switch (ui->event->key) {
            case KEY_DEL:
                if (ui->event->mods & KEY_MOD_CTRL) {
                    ui_ted_cursor_move_right_word(info, &info->cursor, false);
                    ui_ted_cursor_delete(info, &info->cursor);
                } else {
                    ui_ted_cursor_move_right(info, &info->cursor, false);
                    ui_ted_cursor_delete(info, &info->cursor);
                }
                ui_eat_event();
                text_box_scroll_into_view(info, text_box, &info->cursor, 4);
                break;
            case KEY_W:
                if (ui->event->mods & KEY_MOD_CTRL) {
                    ui_ted_cursor_move_left_word(info, &info->cursor, false);
                    ui_ted_cursor_delete(info, &info->cursor);
                    text_box_scroll_into_view(info, text_box, &info->cursor, 4);
                    ui_eat_event();
                }
                break;
            case KEY_A:
                if (ui->event->mods & KEY_MOD_CTRL) {
                    ui_ted_cursor_move_to_end(info, &info->cursor, true);
                    ui_ted_cursor_move_to_start(info, &info->cursor, false);
                    text_box_scroll_into_view(info, text_box, &info->cursor, 4);
                    ui_eat_event();
                }
                break;
            case KEY_V:
                if (ui->event->mods & KEY_MOD_CTRL) {
                    tmem_new(tm);
                    String text = win_get_clipboard_text(tm);
                    if (info->single_line_mode) array_iter (c, &text, *) if (*c == '\n' || *c == '\r') *c = ' ';
                    ui_ted_cursor_insert(info, &info->cursor, text);
                }
                break;
            case KEY_X:
                if (ui->event->mods & KEY_MOD_CTRL) {
                    tmem_new(tm);
                    String text = ui_ted_cursor_get_selection(info, tm, &info->cursor);
                    if (text.count) {
                        win_set_clipboard_text(text);
                        ui_ted_cursor_delete(info, &info->cursor);
                    }
                }
                break;
            case KEY_C:
                if (ui->event->mods & KEY_MOD_CTRL) {
                    tmem_new(tm);
                    String text = ui_ted_cursor_get_selection(info, tm, &info->cursor);
                    if (text.count) win_set_clipboard_text(text);
                }
                break;
            case KEY_RETURN:
                if (info->single_line_mode) break;

                Bool special_case = (info->cursor.byte_offset == buf_get_count(info->buf)) && !buf_ends_with_newline(info->buf);
                ui_ted_cursor_insert(info, &info->cursor, str("\n"));

                if (special_case) {
                    info->cursor.byte_offset--;
                    info->cursor.selection_offset--;
                    ui_ted_cursor_insert(info, &info->cursor, str("\n"));
                }

                text_box_scroll_into_view(info, text_box, &info->cursor, 4);
                ui_eat_event();
                break;
            case KEY_BACKSPACE:
                if (info->cursor.byte_offset == info->cursor.selection_offset) ui_ted_cursor_move_left(info, &info->cursor, false);
                ui_ted_cursor_delete(info, &info->cursor);
                text_box_scroll_into_view(info, text_box, &info->cursor, 4);
                ui_eat_event();
                break;
            case KEY_LEFT:
                ui_ted_cursor_move_left(info, &info->cursor, !(ui->event->mods & KEY_MOD_SHIFT));
                text_box_scroll_into_view(info, text_box, &info->cursor, 4);
                ui_eat_event();
                break;
            case KEY_RIGHT:
                ui_ted_cursor_move_right(info, &info->cursor, !(ui->event->mods & KEY_MOD_SHIFT));
                text_box_scroll_into_view(info, text_box, &info->cursor, 4);
                ui_eat_event();
                break;
            case KEY_UP:
                ui_ted_cursor_move_up(info, &info->cursor, !(ui->event->mods & KEY_MOD_SHIFT));
                text_box_scroll_into_view(info, text_box, &info->cursor, 4);
                ui_eat_event();
                break;
            case KEY_DOWN:
                ui_ted_cursor_move_down(info, &info->cursor, !(ui->event->mods & KEY_MOD_SHIFT));
                text_box_scroll_into_view(info, text_box, &info->cursor, 4);
                ui_eat_event();
                break;
            default:
                break;
            }
        }

        if (text_box->signals.clicked && ui->event->key == KEY_MOUSE_LEFT) {
            info->dragging = false;
        }

        if (text_box->signals.pressed) {
            ui_grab_focus(text_box);
            U64 soff = info->cursor.selection_offset;
            info->cursor = coord_to_cursor(info, text_box, ui->mouse);
            info->cursor.selection_offset = soff;

            if (info->dragging) {
                text_box_scroll_into_view(info, text_box, &info->cursor, 0);
            } else {
                info->dragging = true;
                info->cursor.selection_offset = info->cursor.byte_offset;
            }
        }

        if (text_box->signals.focused && ui->event->tag == EVENT_TEXT_INPUT) {
            ui_ted_cursor_insert(info, &info->cursor, ui->event->text);
            text_box_scroll_into_view(info, text_box, &info->cursor, 4);
            ui_eat_event();
        }

        ui_animate_vec2(&info->scroll_coord, info->scroll_coord_n, ui->config->animation_time);
        info->cursor_coord = cursor_to_coord(info, text_box, &info->cursor);
    }

    return container;
}

UiBox *ui_ted_resizable (String id, Buf *buf, F32 *width, F32 *height, UiTextEditorWrapMode wrap_mode) {
    UiBox *text_box = ui_ted(str("text_box"), buf, false, LINE_WRAP_NONE);
    ui_style_box_vec4(text_box, UI_RADIUS, ui->theme->radius);
    ui_style_box_vec4(text_box, UI_BG_COLOR, ui->theme->bg_color_z1);
    ui_style_box_vec4(text_box, UI_BORDER_COLOR, ui->theme->border_color);
    ui_style_box_vec4(text_box, UI_BORDER_WIDTHS, ui->theme->border_width);
    ui_style_box_f32(text_box, UI_INSET_SHADOW_WIDTH, ui->theme->in_shadow_width);
    ui_style_box_vec4(text_box, UI_INSET_SHADOW_COLOR, ui->theme->in_shadow_color);
    ui_style_box_vec2(text_box, UI_PADDING, ui->theme->padding);

    ui_parent(text_box) {
        UiBox *resizer = ui_box(UI_BOX_REACTIVE, "resizer") {
            F32 w = 10;
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, w, 1});
            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, w, 1});
            ui_style_f32(UI_FLOAT_X, text_box->rect.w - w/2);
            ui_style_f32(UI_FLOAT_Y, text_box->rect.h - w/2);

            if (resizer->signals.hovered) {
                ui_set_mouse_cursor(MOUSE_CURSOR_SE_RESIZE);
            }

            if (resizer->signals.pressed && ui->event->tag == EVENT_MOUSE_MOVE) {
                *width  += ui->mouse_dt.x;
                *height += ui->mouse_dt.y;
            }
        }
    }

    ui_style_box_size(text_box, UI_WIDTH, (UiSize){UI_SIZE_PIXELS, *width, 1});
    ui_style_box_size(text_box, UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, *height, 1});

    return text_box;
}

#include "ui/ui_text_view.h"

istruct (StyledGlyphInfo) {
    GlyphInfo *glyph;
    UiMarkup *markup;
};

istruct (VisualLine) {
    F32 width;
    Array(StyledGlyphInfo) glyphs;
};

istruct (UiTextView) {
    UiBoxData header;
    F32 font_size;
    String text;
    F32 viewport_width;
    ArrayUiMarkupRange markup;
    Array(StyledGlyphInfo) styled_glyphs;
    Array(VisualLine) lines;
};

istruct (MarkupEvent) {
    U64 pos;
    Bool is_start; // else end
    UiMarkup markup;
};

UiMarkup default_markup = {
    .flags = 0,
    .bg_color = {-1},
    .fg_color = {-1},
};

Int sort_markup_event (Void *A, Void *B) {
    MarkupEvent *a = A;
    MarkupEvent *b = B;
    if (a->pos < b->pos) return -1;
    if (a->pos > b->pos) return 1;
    if (a->is_start)     return -1;
    if (b->is_start)     return 1;
    return 0;
}

Void ui_markup_flatten_ranges (String text, SliceUiMarkupRange ranges, ArrayUiMarkupRange *out_ranges) {
    tmem_new(tm);

    Array(MarkupEvent) events;
    array_init(&events, tm);

    array_iter (range, &ranges, *) {
        array_push_lit(&events, .pos=range->start, .is_start=true, .markup=range->markup);
        array_push_lit(&events, .pos=range->end, .is_start=false, .markup=range->markup);
    }

    array_sort_cmp(&events, sort_markup_event);

    Array(UiMarkup) active;
    array_init(&active, tm);
    out_ranges->count = 0;
    U64 cursor = 0;

    array_iter (event, &events, *) {
        if (event->pos > cursor) {
            UiMarkup combined_markup = { .bg_color={-1}, .fg_color={-1} };
            array_iter (m, &active, *) {
                combined_markup.flags |= m->flags;
                if (m->bg_color.x != -1) combined_markup.bg_color = m->bg_color;
                if (m->fg_color.x != -1) combined_markup.fg_color = m->fg_color;
            }

            array_push_lit(out_ranges, .start=cursor, .end=event->pos-1, .markup=combined_markup);
            cursor = event->pos;
        }

        if (event->is_start) {
            array_push(&active, event->markup);
        } else {
            array_iter (m, &active, *) {
                if (! memcmp(m, &event->markup, sizeof(UiMarkup))) {
                    array_remove(&active, ARRAY_IDX);
                    break;
                }
            }
        }
    }

    if (cursor < text.count) {
        UiMarkup combined_markup = { .bg_color={-1}, .fg_color={-1} };
        array_iter (m, &active, *) {
            combined_markup.flags |= m->flags;
            if (m->bg_color.x != -1) combined_markup.bg_color = m->bg_color;
            if (m->fg_color.x != -1) combined_markup.fg_color = m->fg_color;
        }

        array_push_lit(out_ranges, .start=cursor, .end=text.count-1, .markup=combined_markup);
    }
}

static Void compute_visual_lines (UiTextView *info, F32 max_width) {
    tmem_new(tm);

    info->lines.count = 0;

    if (max_width == 0) return;
    if (info->styled_glyphs.count == 0) return;

    info->viewport_width = max_width;

    VisualLine line = {};
    array_init(&line.glyphs, info->header.mem);

    Array(StyledGlyphInfo) token;
    array_init(&token, tm);

    #define push_line() {\
        array_push(&info->lines, line);\
        line.width = 0;\
        array_init(&line.glyphs, info->header.mem);\
    }

    U64 glyph_idx = 0;

    while (glyph_idx < info->styled_glyphs.count) {
        U32 token_type = 0;
        F32 token_width = 0;

        { // Build token:
            StyledGlyphInfo glyph = array_get(&info->styled_glyphs, glyph_idx);

            if (glyph.glyph->codepoint == ' ') {
                token_type = ' ';
                while (glyph.glyph->codepoint == ' ') {
                    token_width += glyph.glyph->x_advance;
                    glyph_idx++;
                    array_push(&token, glyph);
                    if (glyph_idx >= info->styled_glyphs.count) break;
                    glyph = array_get(&info->styled_glyphs, glyph_idx);
                }
            } else if (glyph.glyph->codepoint == '\n') {
                token_type = '\n';
                glyph_idx++;
            } else {
                while (glyph.glyph->codepoint != ' ' && glyph.glyph->codepoint != '\n') {
                    token_width += glyph.glyph->x_advance;
                    glyph_idx++;
                    array_push(&token, glyph);
                    if (glyph_idx >= info->styled_glyphs.count) break;
                    glyph = array_get(&info->styled_glyphs, glyph_idx);
                }
            }
        }

        if (token_type == '\n') {
            push_line();
        } else if (line.width + token_width <= max_width) {
            array_push_many(&line.glyphs, &token);
            line.width += token_width;
        } else if (token_type == ' ' || line.width == 0) {
            F32 remaining = max_width - line.width;
            array_iter (g, &token) {
                if (remaining < g.glyph->x_advance) {
                    glyph_idx -= (token.count - ARRAY_IDX);
                    break;
                }
                array_push(&line.glyphs, g);
                line.width += g.glyph->x_advance;
                remaining -= g.glyph->x_advance;
            }
            push_line();
        } else {
            push_line();
            glyph_idx -= token.count;
        }

        token.count = 0;
    }

    if (line.glyphs.count) push_line();
}

static Void compute_glyphs (UiTextView *info, SliceUiMarkupRange markup_ranges) {
    Font *font_normal       = font_get(ui->font_cache, ui->config->font_path_normal, info->font_size);
    Font *font_bold         = font_get(ui->font_cache, ui->config->font_path_bold, info->font_size);
    Font *font_italic       = font_get(ui->font_cache, ui->config->font_path_italic, info->font_size);
    Font *font_mono         = font_get(ui->font_cache, ui->config->font_path_mono, info->font_size);
    Font *font_normal_small = font_get(ui->font_cache, ui->config->font_path_normal, info->font_size * .7);
    Font *font_bold_small   = font_get(ui->font_cache, ui->config->font_path_bold, info->font_size * .7);
    Font *font_italic_small = font_get(ui->font_cache, ui->config->font_path_italic, info->font_size * .7);
    Font *font_mono_small   = font_get(ui->font_cache, ui->config->font_path_mono, info->font_size * .7);

    ui_markup_flatten_ranges(info->text, markup_ranges, &info->markup);

    U64 offset = 0;
    array_iter (range, &info->markup, *) {
        Font *font = (range->markup.flags & UI_MARKUP_BOLD) ? font_bold :
                     (range->markup.flags & UI_MARKUP_ITALIC) ? font_italic :
                     (range->markup.flags & UI_MARKUP_MONO) ? font_mono :
                     font_normal;

        if (range->markup.flags & (UI_MARKUP_SUPERSCRIPT | UI_MARKUP_SUBSCRIPT)) {
            if (font == font_normal) {
                font = font_normal_small;
            } else if (font == font_bold) {
                font = font_bold_small;
            } else if (font == font_mono) {
                font = font_mono_small;
            } else {
                font = font_italic_small;
            }
        }

        String slice = str_slice(info->text, range->start, range->end - range->start + 1);
        SliceGlyphInfo glyphs = font_get_glyph_infos(font, info->header.mem, slice);

        array_iter (glyph, &glyphs, *) {
            glyph->x += offset;
            glyph->byte_offset += range->start;
            array_push_lit(&info->styled_glyphs, .glyph=glyph, .markup=&range->markup);
        }

        if (glyphs.count > 0) {
            GlyphInfo *last_glyph = array_ref(&glyphs, glyphs.count - 1);
            AtlasSlot *last_slot = font_get_atlas_slot(font, last_glyph);
            offset = last_glyph->x + last_slot->advance;
        }
    }
}

U64 ui_text_view_coord_to_offset (UiBox *box, Vec2 coord) {
    UiTextView *info     = ui_get_box_data(box, 0, 0);
    Font *font_normal    = font_get(ui->font_cache, ui->config->font_path_normal, info->font_size);
    F32 start_x          = box->rect.x;
    F32 start_y          = box->rect.y + font_normal->height;
    F32 current_y_offset = 0;

    array_iter (line, &info->lines, *) {
        U32 x_offset = array_get(&line->glyphs, 0).glyph->x;

        array_iter (styled_glyph, &line->glyphs) {
            GlyphInfo *glyph = styled_glyph.glyph;
            AtlasSlot *slot = font_get_atlas_slot(glyph->font, glyph);
            F32 local_x = glyph->x - x_offset;

            F32 x0 = start_x + local_x;
            F32 x1 = x0 + slot->advance;
            F32 y0 = start_y + glyph->y + current_y_offset - glyph->font->descent - slot->bearing_y;
            F32 y1 = y0 + glyph->font->height;

            if (coord.y >= y0 && coord.y <= y1 && coord.x >= x0 && coord.x <= x1) {
                return glyph->byte_offset;
            }
        }

        current_y_offset += font_normal->height;
    }

    return ARRAY_NIL_IDX;
}

static Void size (UiBox *box, U64 axis) {
    // Height sizing done in draw() function.
}

static Void draw (UiBox *box) {
    UiTextView *info     = ui_get_box_data(box, 0, 0);
    Font *font_normal    = font_get(ui->font_cache, ui->config->font_path_normal, info->font_size);
    F32 start_x          = box->rect.x;
    F32 start_y          = box->rect.y + font_normal->height;
    F32 max_width        = box->rect.w;
    F32 current_y_offset = 0;

    if (info->viewport_width != max_width) compute_visual_lines(info, max_width);

    array_iter (line, &info->lines, *) {
        U32 x_offset = array_get(&line->glyphs, 0).glyph->x;

        array_iter (styled_glyph, &line->glyphs) {
            UiMarkup *markup = styled_glyph.markup;
            GlyphInfo *glyph = styled_glyph.glyph;

            Font *font = glyph->font;

            dr_bind_texture(&font->atlas_texture);

            AtlasSlot *slot = font_get_atlas_slot(font, glyph);
            U32 local_x = glyph->x - x_offset;

            if (markup->bg_color.x != -1.f) {
                Vec2 top_left     = { start_x + local_x, box->rect.y + current_y_offset };
                Vec2 bottom_right = { start_x + local_x + glyph->x_advance, box->rect.y + current_y_offset + font->height };
                dr_rect(
                    .color        = markup->bg_color,
                    .color2       = {-1},
                    .top_left     = top_left,
                    .bottom_right = bottom_right,
                );
            }

            Vec2 top_left = {
                start_x + local_x + slot->bearing_x,
                start_y + glyph->y + current_y_offset - font->descent - slot->bearing_y
            };
            Vec2 bottom_right = {top_left.x + slot->width, top_left.y + slot->height};

            if (markup->flags & UI_MARKUP_SUPERSCRIPT) {
                top_left.y -= font_normal->height * .3;
                bottom_right.y -= font_normal->height * .3;
            }

            Vec4 fg_color = (markup->fg_color.x == -1.f) ? box->style.text_color : markup->fg_color;

            dr_rect(
                .top_left          = top_left,
                .bottom_right      = bottom_right,
                .texture_rect      = {slot->x, slot->y, slot->width, slot->height},
                .text_color        = fg_color,
                .text_is_grayscale = (slot->pixel_mode == FT_PIXEL_MODE_GRAY),
            );

            if (markup->flags & UI_MARKUP_UNDERSCORE) {
                F32 line_width    = round(font->size * .1);
                Vec2 top_left     = { start_x + local_x, start_y + glyph->y + current_y_offset - font->descent + 1 };
                Vec2 bottom_right = { top_left.x + slot->advance, top_left.y + line_width };
                dr_rect(
                    .color        = fg_color,
                    .color2       = {-1},
                    .top_left     = top_left,
                    .bottom_right = bottom_right,
                );
            }

            if (markup->flags & UI_MARKUP_STRIKETHROUGH) {
                F32 line_width    = round(font->size * .1);
                Vec2 top_left     = { start_x + local_x, start_y + glyph->y + current_y_offset - font->height/2 };
                Vec2 bottom_right = { top_left.x + slot->advance, top_left.y + line_width };
                dr_rect(
                    .color        = fg_color,
                    .color2       = {-1},
                    .top_left     = top_left,
                    .bottom_right = bottom_right,
                );
            }
        }

        current_y_offset += font_normal->height;
    }

    if (current_y_offset == 0.0 && info->text.count) current_y_offset += font_normal->height;
    box->rect.h = current_y_offset;
}

UiBox *ui_text_view (UiBoxFlags flags, String id, String text, F32 font_size, SliceUiMarkupRange markup_ranges) {
    UiBox *container = ui_box_str(flags, id) {
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_CUSTOM, 1, 1});
        ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_normal);
        container->size_fn = size;
        container->draw_fn = draw;

        UiTextView *info = ui_get_box_data(container, sizeof(UiTextView), 1*KB);

        if (! info->text.data) {
            info->font_size = font_size;
            info->text = str_copy(info->header.mem, text);
            array_init(&info->markup, info->header.mem);
            array_init(&info->styled_glyphs, info->header.mem);
            array_init(&info->lines, info->header.mem);
            compute_glyphs(info, markup_ranges);
        } else if (! str_match(text, info->text)) {
            info->text = str_copy(info->header.mem, text);
            info->markup.count = 0;
            info->styled_glyphs.count = 0;
            info->viewport_width = 0;
            info->lines.count = 0;
            compute_glyphs(info, markup_ranges);
        }
    }

    return container;
}

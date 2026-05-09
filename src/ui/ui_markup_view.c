#include "ui/ui_markup_view.h"
#include "ui/ui_text_view.h"
#include "ui/ui_widgets.h"

istruct (MarkupViewWrapper) {
    UiBoxData header;
    Buf *buf;
};

istruct (ClickableInline) {
    MarkupAst *node;
    U64 start;
    U64 end;
};

istruct (ParagraphContext) {
    MarkupView *info;
    U64 length;
    AString raw_text;
    ArrayUiMarkupRange ranges;
    ClickableInline hovered_clickable;
    Array(ClickableInline) clickables;
};

istruct (ImageInfo) {
    UiBoxData header;
    Texture texture;
};

static Void build_block (MarkupView *info, MarkupAst *node);

static Bool ast_cmp (Void *a, Void *b) {
    return *cast(MarkupAst**, a) == *cast(MarkupAst**, b);
}

static UMapHash ast_hash (Void *a) {
    return markup_ast_hash(*cast(MarkupAst**, a));
}

static Void free_image_texture (Void *info_) {
    ImageInfo *info = info_;
    dr_texture_free(&info->texture);
}

static UiBox *build_meta (MarkupView *info, MarkupAst *node) {
    tmem_new(tm);

    UiBox *box;
    Auto n = cast(MarkupAstMeta*, node);
    String id = astr_fmt(tm, "block%lu", array_get_last(&ui->box_stack)->children.count);

    if (n->config->flags & MARKUP_AST_META_CONFIG_HAS_IMAGE_PATH) {
        U64 width = (n->config->flags & MARKUP_AST_META_CONFIG_HAS_IMAGE_WIDTH) ? n->config->image_width : 64;
        box = ui_box_str(UI_BOX_INVISIBLE_BG, id) {
            ui_tag("block");
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

            ImageInfo *info = ui_get_box_data(box, sizeof(ImageInfo), sizeof(ImageInfo));

            if (box->start_frame == ui->frame) {
                info->texture = dr_image(cstr(tm, n->config->image_path), false);
                info->header.free = free_image_texture;
            }

            ui_image(id, &info->texture, false, vec4(0, 0, 0, 0), width);
        }
    } else if (n->config->flags & MARKUP_AST_META_CONFIG_HAS_ADMONITION) {
        box = ui_box_str(0, id) {
            ui_tag("block");
            ui_tag("admonition");

            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
            ui_style_vec4(UI_RADIUS, ui->theme->radius);
            ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
            ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
            ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

            ui_style_rule("#header.tip *")       ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_blue);
            ui_style_rule("#header.note *")      ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_green);
            ui_style_rule("#header.warning *")   ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_yellow);
            ui_style_rule("#header.important *") ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_red);

            ui_box(0, "header") {
                F32 b = ui->theme->border_width.x;
                ui_style_f32(UI_SPACING, ui->theme->spacing);
                ui_style_vec2(UI_PADDING, ui->theme->padding);
                ui_style_vec4(UI_BORDER_WIDTHS, vec4(0, 0, 0, b));
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

                ui_icon(0, "icon", UI_ICON_ISSUE);

                switch (n->config->admonition) {
                case MARKUP_AST_ADMONITION_TIP:       ui_tag("tip"); ui_label_extra(0, "title", str("Tip"), ui->config->font_path_bold, ui->config->font_size, 0); break;
                case MARKUP_AST_ADMONITION_NOTE:      ui_tag("note"); ui_label_extra(0, "title", str("Note"), ui->config->font_path_bold, ui->config->font_size, 0); break;
                case MARKUP_AST_ADMONITION_WARNING:   ui_tag("warning"); ui_label_extra(0, "title", str("Warning"), ui->config->font_path_bold, ui->config->font_size, 0); break;
                case MARKUP_AST_ADMONITION_IMPORTANT: ui_tag("important"); ui_label_extra(0, "title", str("Important"), ui->config->font_path_bold, ui->config->font_size, 0); break;
                }
            }

            ui_box(0, "body") {
                ui_tag("block");
                ui_style_vec2(UI_PADDING, ui->theme->padding);
                array_iter (child, &node->children) build_block(info, child);
            }
        }
    } else if (n->config->flags & MARKUP_AST_META_CONFIG_HAS_SPOILER) {
        box = ui_drawer(id, n->config->spoiler) {
            ui_tag("block");
            array_iter (child, &node->children) build_block(info, child);
        }
    } else {
        box = ui_box_str(0, id) {
            ui_tag("block");
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
            array_iter (child, &node->children) build_block(info, child);
        }
    }

    return box;
}

static Void build_inline (MarkupView *info, MarkupAst *node) {
    switch (node->tag) {
    case MARKUP_AST_TEXT: {
        if (info->inside_raw_block) {
            UiMarkupRange *range = array_push_slot(&info->paragraph_context->ranges);
            range->markup = default_markup;
            range->markup.flags |= UI_MARKUP_MONO;
            range->start = info->paragraph_context->length;
        }
        String text = markup_ast_get_text(node, info->text);
        astr_push_str(&info->paragraph_context->raw_text, text);
        info->paragraph_context->length += text.count;
        if (info->inside_raw_block) array_ref_last(&info->paragraph_context->ranges)->end = info->paragraph_context->length;
    } break;
    case MARKUP_AST_ITALIC: {
        UiMarkupRange *range = array_push_slot(&info->paragraph_context->ranges);
        range->markup = default_markup;
        range->markup.flags |= UI_MARKUP_ITALIC;
        range->start = info->paragraph_context->length;
        U64 idx = range - info->paragraph_context->ranges.data;
        array_iter (child, &node->children) build_inline(info, child);
        array_ref(&info->paragraph_context->ranges, idx)->end = info->paragraph_context->length;
    } break;
    case MARKUP_AST_BOLD: {
        UiMarkupRange *range = array_push_slot(&info->paragraph_context->ranges);
        range->markup = default_markup;
        range->markup.flags |= UI_MARKUP_BOLD;
        range->start = info->paragraph_context->length;
        U64 idx = range - info->paragraph_context->ranges.data;
        array_iter (child, &node->children) build_inline(info, child);
        array_ref(&info->paragraph_context->ranges, idx)->end = info->paragraph_context->length;
    } break;
    case MARKUP_AST_HIGHLIGHT: {
        UiMarkupRange *range = array_push_slot(&info->paragraph_context->ranges);
        range->markup = default_markup;
        range->markup.flags |= UI_MARKUP_BOLD;
        range->markup.bg_color = ui->theme->color_yellow;
        range->markup.fg_color = ui->theme->text_color_selection;
        range->start = info->paragraph_context->length;
        U64 idx = range - info->paragraph_context->ranges.data;
        astr_push_byte(&info->paragraph_context->raw_text, ' ');
        info->paragraph_context->length++;
        array_iter (child, &node->children) build_inline(info, child);
        astr_push_byte(&info->paragraph_context->raw_text, ' ');
        info->paragraph_context->length++;
        array_ref(&info->paragraph_context->ranges, idx)->end = info->paragraph_context->length;
    } break;
    case MARKUP_AST_SUP: {
        UiMarkupRange *range = array_push_slot(&info->paragraph_context->ranges);
        range->markup = default_markup;
        range->markup.flags |= UI_MARKUP_SUPERSCRIPT;
        range->start = info->paragraph_context->length;
        U64 idx = range - info->paragraph_context->ranges.data;
        array_iter (child, &node->children) build_inline(info, child);
        array_ref(&info->paragraph_context->ranges, idx)->end = info->paragraph_context->length;
    } break;
    case MARKUP_AST_SUB: {
        UiMarkupRange *range = array_push_slot(&info->paragraph_context->ranges);
        range->markup = default_markup;
        range->markup.flags |= UI_MARKUP_SUBSCRIPT;
        range->start = info->paragraph_context->length;
        U64 idx = range - info->paragraph_context->ranges.data;
        array_iter (child, &node->children) build_inline(info, child);
        array_ref(&info->paragraph_context->ranges, idx)->end = info->paragraph_context->length;
    } break;
    case MARKUP_AST_STRIKE: {
        UiMarkupRange *range = array_push_slot(&info->paragraph_context->ranges);
        range->markup = default_markup;
        range->markup.flags |= UI_MARKUP_STRIKETHROUGH;
        range->start = info->paragraph_context->length;
        U64 idx = range - info->paragraph_context->ranges.data;
        array_iter (child, &node->children) build_inline(info, child);
        array_ref(&info->paragraph_context->ranges, idx)->end = info->paragraph_context->length;
    } break;
    case MARKUP_AST_RAW_INLINE: {
        UiMarkupRange *range = array_push_slot(&info->paragraph_context->ranges);
        range->markup = default_markup;
        range->markup.bg_color = ui->theme->bg_color_z1;
        range->start = info->paragraph_context->length;
        U64 idx = range - info->paragraph_context->ranges.data;

        astr_push_byte(&info->paragraph_context->raw_text, ' ');
        info->paragraph_context->length++;

        array_iter (child, &node->children) build_inline(info, child);

        astr_push_byte(&info->paragraph_context->raw_text, ' ');
        info->paragraph_context->length++;

        array_ref(&info->paragraph_context->ranges, idx)->end = info->paragraph_context->length;
    } break;
    case MARKUP_AST_LINK: {
        Auto n = cast(MarkupAstLink*, node);

        UiMarkupRange *range = array_push_slot(&info->paragraph_context->ranges);
        range->markup = default_markup;
        range->markup.flags |= UI_MARKUP_BOLD | UI_MARKUP_UNDERSCORE;
        range->markup.fg_color = ui->theme->text_color_blue;
        range->start = info->paragraph_context->length;
        U64 idx = range - info->paragraph_context->ranges.data;

        if (n->alias.data) {
            astr_push_str(&info->paragraph_context->raw_text, n->alias);
            info->paragraph_context->length += n->alias.count;
        } else {
            astr_push_str(&info->paragraph_context->raw_text, n->link);
            info->paragraph_context->length += n->link.count;
        }

        array_ref(&info->paragraph_context->ranges, idx)->end = info->paragraph_context->length;
        array_push_lit(&info->paragraph_context->clickables, .node=node, .start=range->start, .end=range->end);
    } break;
    case MARKUP_AST_TAG_REF: {
        UiMarkupRange *range = array_push_slot(&info->paragraph_context->ranges);
        range->markup = default_markup;
        range->markup.flags |= UI_MARKUP_BOLD;
        range->markup.fg_color = ui->theme->text_color_yellow;
        range->start = info->paragraph_context->length;
        U64 idx = range - info->paragraph_context->ranges.data;
        array_iter (child, &node->children) build_inline(info, child);
        array_ref(&info->paragraph_context->ranges, idx)->end = info->paragraph_context->length;
        array_push_lit(&info->paragraph_context->clickables, .node=node, .start=range->start, .end=range->end);
    } break;
    default: badpath;
    }
}

static UiBox *build_paragraph (MarkupView *info, MarkupAst *node) {
    tmem_new(tm);

    ParagraphContext *ctx = mem_new(info->ast_mem, ParagraphContext);
    ctx->info = info;
    ctx->raw_text = astr_new(info->ast_mem);
    array_init(&ctx->ranges, info->ast_mem);
    array_init(&ctx->clickables, info->ast_mem);
    info->paragraph_context = ctx;

    array_iter (child, &node->children) build_inline(info, child);

    F32 font_size = ui->config->font_size;

    if (info->inside_header) {
        switch (info->inside_header->size) {
        case 1: font_size *= 2; break;
        case 2: font_size *= 1.5; break;
        case 3: font_size *= 1.25; break;
        }
    }

    String id = astr_fmt(tm, "block%lu", array_get_last(&ui->box_stack)->children.count);
    if (info->purge_cache) ui_free_box_data(ui_build_key(id));
    UiBox *box = ui_text_view(UI_BOX_REACTIVE, id, ctx->raw_text.as_slice, font_size, ctx->ranges.as_slice);
    ui_tag_box(box, "block");
    ui_tag_box(box, "paragraph");

    if (box->signals.hovered) {
        U64 idx = ui_text_view_coord_to_offset(box, ui->mouse);

        ctx->hovered_clickable = (ClickableInline){};
        ui_set_mouse_cursor(MOUSE_CURSOR_DEFAULT);
        array_iter (c, &ctx->clickables) {
            if (idx >= c.start && idx < c.end) {
                ctx->hovered_clickable = c;
                ui_set_mouse_cursor(MOUSE_CURSOR_POINTER);
                break;
            }
        }
    }

    if (box->signals.clicked && ctx->hovered_clickable.node) {
        if (ctx->hovered_clickable.node->tag == MARKUP_AST_LINK) {
            String url = cast(MarkupAstLink*, ctx->hovered_clickable.node)->link;
            if      (str_starts_with(url, str("/"))) win_open_file_url(url);
            else if (str_starts_with(url, str("~"))) win_open_file_url(url);
            else                                     win_open_web_url(url);
        } else if (ctx->hovered_clickable.node->tag == MARKUP_AST_TAG_REF) {
            if (info->out_clicked_tag) {
                *info->out_clicked_tag = markup_ast_get_text(ctx->hovered_clickable.node, info->text);
            }
        }
    }

    return box;
}

static Void size_table_cell (UiBox *cell, U64 axis) {
   assert_dbg(axis == 1);

    UiBox *row = cell->parent;
    F32 max_height = 0;
    array_iter (sibling, &row->children) {
        F32 h = sibling->style.padding.y * 2;
        F32 spacing = sibling->style.spacing;
        array_iter (child, &sibling->children) {
            h += child->rect.h;
            if (! ARRAY_ITER_DONE) h += spacing;
        }
        if (h > max_height) max_height = h;
    }

    cell->rect.h = max_height;
}

static Void build_block (MarkupView *info, MarkupAst *node) {
    tmem_new(tm);

    if (info->custom_builder) {
        UiBox *box = info->custom_builder(info, node);
        if (box) {
            if (info->build_ast_to_box) map_add(&info->ast_to_box, node, box);
            return;
        }
    }

    U64 n = array_get_last(&ui->box_stack)->children.count;
    UiBox *box = 0;

    switch (node->tag) {
    case MARKUP_AST_META: box = build_meta(info, node); break;
    case MARKUP_AST_PARAGRAPH: box = build_paragraph(info, node); break;

    case MARKUP_AST_TABLE: {
        U64 n = array_get_last(&ui->box_stack)->children.count;
        U64 col_count = array_get(&node->children, 0)->children.count;

        Auto dummy_cell = markup_ast_alloc(info->ast_mem, MARKUP_AST_TABLE_CELL);
        cast(MarkupAstTableCell*, dummy_cell)->config = default_table_cell_config;

        box = ui_box_fmt(0, "block%lu", n) {
            ui_tag("table");
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            F32 r = ui->theme->radius.x;
            F32 b = ui->theme->border_width.x;

            array_iter (row, &node->children) {
                U64 row_idx = ARRAY_IDX;

                // Add missing cells in this row in the form of dummy cells.
                U64 prev_row_length = row->children.count;
                while (row->children.count < col_count) array_push(&row->children, dummy_cell);

                ui_box_fmt(UI_BOX_INVISIBLE_BG, "row%lu", ARRAY_IDX) {
                    array_iter (cell, &row->children) {
                        U64 cell_idx = ARRAY_IDX;
                        if (cell_idx == col_count) break; // Ignore excess cells.

                        UiBox *cell_box = ui_box_fmt(0, "cell%lu", ARRAY_IDX) {
                            ui_tag("cell");

                            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_CUSTOM, 1, 1});
                            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                            ui_style_vec2(UI_PADDING, ui->theme->padding);
                            ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                            ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);

                            Vec4 borders = {0, b, b, 0};
                            if (row_idx == node->children.count-1 && cell_idx == col_count-1) {
                                borders.x = b;
                                borders.w = b;
                            } else if (row_idx == node->children.count-1) {
                                borders.w = b;
                            } else if (cell_idx == col_count-1) {
                                borders.x = b;
                            }
                            ui_style_vec4(UI_BORDER_WIDTHS, borders);

                            Vec4 corners = {};
                            if (!row_idx && !cell_idx)                                        corners.y = r;
                            if (row_idx == node->children.count-1 && !cell_idx)               corners.w = r;
                            if (!row_idx && cell_idx == col_count-1)                          corners.x = r;
                            if (row_idx == node->children.count-1 && cell_idx == col_count-1) corners.z = r;
                            ui_style_vec4(UI_RADIUS, corners);

                            ui_tag("cell");
                            ui_tag("block");
                            cell_box->size_fn = size_table_cell;
                            array_iter (child, &cell->children) build_block(info, child);
                            if (info->build_ast_to_box) map_add(&info->ast_to_box, cell, cell_box);
                        }
                    }
                }

                row->children.count = prev_row_length;
            }
        }
    } break;

    case MARKUP_AST_LIST:
    case MARKUP_AST_ORDERED_LIST: {
        Bool ordered = (node->tag == MARKUP_AST_ORDERED_LIST);

        box = ui_box_fmt(0, "block%lu", n) {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            array_iter (child, &node->children) {
                UiBox *box = ui_box_fmt(0, "child_box%lu", ARRAY_IDX) {
                    ui_tag("bullet_item");

                    ui_label(0, "bullet", ordered ? astr_fmt(tm, "%lu.", ARRAY_IDX + 1) : str("•"));

                    ui_box_fmt(0, "content%lu", ARRAY_IDX) {
                        ui_tag("block");
                        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                        array_iter (c, &child->children) build_block(info, c);
                    }

                    if (info->build_ast_to_box) map_add(&info->ast_to_box, child, box);
                }
            }
        }
    } break;

    case MARKUP_AST_SEPARATOR: {
        box = ui_box_fmt(0, "block%lu", n) {
            ui_tag("block");
            ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, ui_em(.16), 1});
            ui_style_f32(UI_EDGE_SOFTNESS, 0);
        }
    } break;

    case MARKUP_AST_RAW_BLOCK: {
        box = ui_box_fmt(0, "block%lu", n) {
            ui_tag("block");
            ui_tag("raw_block");
            ui_style_vec4(UI_RADIUS, ui->theme->radius);
            ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
            ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
            ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
            ui_style_vec2(UI_PADDING, ui->theme->padding);
            info->inside_raw_block = true;
            array_iter (child, &node->children) build_block(info, child);
            info->inside_raw_block = false;
        }
    } break;

    case MARKUP_AST_HEADER: {
        box = ui_box_fmt(0, "block%lu", n) {
            ui_tag("header");
            ui_tag("block");
            ui_tag_str(astr_fmt(tm, "h%lu", cast(MarkupAstHeader*, node)->size));
            ui_style_vec4(UI_BORDER_COLOR, ui->theme->bg_color_z3);
            ui_style_vec4(UI_BORDER_WIDTHS, vec4(0, 0, 0, 2));
            ui_style_f32(UI_EDGE_SOFTNESS, 0);
            ui_style_vec2(UI_PADDING, vec2(0, 6));
            info->inside_header = cast(MarkupAstHeader*, node);
            build_block(info, array_get(&node->children, 0));
            info->inside_header = 0;
        }
    } break;

    default: badpath;
    }

    if (info->build_ast_to_box) map_add(&info->ast_to_box, node, box);
}

static Void free_markup_view (Void *info_) {
    MarkupView *info = info_;
    arena_destroy(cast(Arena*, info->ast_mem));
}

UiBox *ui_markup_view_buf (String id, Buf *buf, Bool build_ast_to_box, String *out_clicked_tag, MarkupBuilder builder) {
    UiBox *container = ui_box_str(0, id) {
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_CHILDREN_SUM, 1, 1});

        ui_style_rule(".block") {
            ui_style_f32(UI_SPACING, ui->theme->spacing);
        }

        ui_style_rule(".bullet_item") {
            ui_style_f32(UI_SPACING, ui->theme->spacing);
        }

        ui_style_rule(".bullet_item #bullet") {
            ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_blue);
        }

        MarkupView *info = ui_get_box_data(container, sizeof(MarkupView), sizeof(MarkupView));

        if (! info->buf) {
            info->buf = buf;
            info->build_ast_to_box = build_ast_to_box;
            info->buf_version = buf_get_version(buf);
            info->header.free = free_markup_view;
            info->ast_mem = cast(Mem*, arena_new(mem_root, 1*KB));
            info->text = buf_get_str(info->buf, info->ast_mem);
            map_init_hc(&info->ast_to_box, info->ast_mem, 0, ast_hash, ast_cmp);
            info->ast = markup_parse(info->ast_mem, info->text);
        } else if (info->buf_version != buf_get_version(info->buf)) {
            info->build_ast_to_box = build_ast_to_box;
            info->purge_cache = true;
            info->buf_version = buf_get_version(info->buf);
            arena_pop_all(cast(Arena*, info->ast_mem));
            map_init_hc(&info->ast_to_box, info->ast_mem, 0, ast_hash, ast_cmp);
            info->text = buf_get_str(info->buf, info->ast_mem);
            info->ast = markup_parse(info->ast_mem, info->text);
        }

        info->out_clicked_tag = out_clicked_tag;
        info->custom_builder = builder;

        info->root = ui_box(0, "content") {
            ui_tag("block");
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
            array_iter (child, &info->ast->children) build_block(info, child);
        }

        info->purge_cache = false;
    }

    return container;
}

UiBox *ui_markup_view (String id, String text, String *out_clicked_tag) {
    UiBox *container = ui_box_str(UI_BOX_INVISIBLE_BG, id) {
        MarkupViewWrapper *info = ui_get_box_data(container, sizeof(MarkupViewWrapper), 1*KB);
        if (info->buf) {
            tmem_new(tm);
            String t = buf_get_str(info->buf, tm);
            if (! str_match(t, text)) {
                buf_clear(info->buf);
                buf_insert(info->buf, 0, text);
            }
        } else {
            info->buf = buf_new(info->header.mem, text);
        }
        ui_markup_view_buf(str("markup"), info->buf, false, out_clicked_tag, 0);
    }

    return container;
}

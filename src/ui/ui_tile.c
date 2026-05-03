#include "ui/ui_tile.h"
#include "ui/ui_widgets.h"
#include "window/window.h"

istruct (UiTile) {
    UiBoxData header;

    struct {
        Bool active;
        U64 tab_idx;
        UiTileNode *node;
        UiViewInstance *tab_id;
    } drag;

    Mem *tree_mem;
    UiTileNode **root;
    UiViewStore *view_store;

    UiBox *tile_preview_container;
    UiBox *tile_splitter_container;

    Bool tree_modified;
};

static F32 preview_tile_width = 80;

static Void tile_insert (UiTile *info, UiTileNode *node, UiViewInstance *tab_id_to_insert, UiTileSplit split, U64 idx) {
    assert_dbg(idx == 0 || idx == 1);
    assert_dbg(split != UI_TILE_SPLIT_NONE);

    UiTileNode *new_child = mem_new(info->tree_mem, UiTileNode);
    new_child->split = UI_TILE_SPLIT_NONE;
    array_init(&new_child->tab_ids, info->tree_mem);
    array_push(&new_child->tab_ids, tab_id_to_insert);

    UiTileNode *new_parent = mem_new(info->tree_mem, UiTileNode);
    new_parent->split = split;
    new_parent->ratio = 0.5;
    new_parent->parent = node->parent;

    if (new_parent->parent) {
        if (new_parent->parent->child[0] == node) {
            new_parent->parent->child[0] = new_parent;
        } else {
            new_parent->parent->child[1] = new_parent;
        }
    }

    if (idx == 0) {
        new_parent->child[0] = new_child;
        new_parent->child[1] = node;
    } else {
        new_parent->child[1] = new_child;
        new_parent->child[0] = node;
    }

    node->parent = new_parent;
    new_child->parent = new_parent;
    info->tree_modified = true;
}

static Void tile_tab_remove (UiTile *info, UiTileNode *node, U64 tab_idx) {
    if (node->active_tab_idx > tab_idx) node->active_tab_idx--;
    array_remove(&node->tab_ids, tab_idx);
    node->active_tab_idx = clamp(node->active_tab_idx, 0u, node->tab_ids.count - 1);
    info->tree_modified = true;
}

static Void tile_tab_add (UiTile *info, UiTileNode *node, UiViewType *type) {
    UiViewInstance *instance = ui_view_instance_new(info->view_store, type->id);
    array_push(&node->tab_ids, instance);
    node->active_tab_idx = node->tab_ids.count - 1;
    info->tree_modified = true;
}

static Void tile_remove_empty_leaf (UiTile *info, UiTileNode *node) {
    assert_dbg(node->split == UI_TILE_SPLIT_NONE);
    assert_dbg(node->tab_ids.count == 0);

    UiTileNode *parent = node->parent;
    if (! parent) return;

    UiTileNode *sibling = (parent->child[0] == node) ? parent->child[1] : parent->child[0];
    sibling->parent = parent->parent;

    if (parent->parent) {
        if (parent->parent->child[0] == parent) {
            parent->parent->child[0] = sibling;
        } else {
            parent->parent->child[1] = sibling;
        }
    } else {
        *info->root = sibling;
    }

    info->tree_modified = true;
}

static Void build_tabs_panel (UiTile *info, UiTileNode *node) {
    tmem_new(tm);

    F32 b = ui->theme->border_width.x;

    UiBox *tabs_panel = ui_scroll_box(str("tabs_panel"), false) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 1});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_CHILDREN_SUM, 1, 1});
        ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
        ui_style_vec4(UI_BORDER_WIDTHS, vec4(0, 0, 0, b));
        ui_style_f32(UI_EDGE_SOFTNESS, 0);

        Vec2 padding = ui->theme->padding;

        ui_box(0, "top_padding") ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, padding.x, 1});

        UiBox *tabs = ui_box(0, "tabs") {
            ui_style_vec2(UI_PADDING, vec2(padding.x, 0));
            ui_style_u32(UI_ALIGN_Y, UI_ALIGN_MIDDLE);
            ui_style_f32(UI_SPACING, ui->theme->spacing);

            F32 tab_width = (tabs_panel->rect.w - 32) / cast(F32, node->tab_ids.count) - 2*padding.x;
            tab_width = clamp(tab_width, 96, 256);

            array_iter (id, &node->tab_ids) {
                UiBox *tab = ui_box_fmt(UI_BOX_REACTIVE, "tab%lu", ARRAY_IDX) {
                    F32 r = ui->theme->radius.x;

                    ui_style_vec2(UI_PADDING, vec2(4, 4));
                    ui_style_f32(UI_SPACING, ui->theme->spacing);
                    ui_style_u32(UI_ALIGN_Y, UI_ALIGN_MIDDLE);
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, tab_width, 1});
                    ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                    ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                    ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);

                    if (ARRAY_IDX == node->active_tab_idx) {
                        ui_style_vec4(UI_BG_COLOR, ui->theme->color_accent);
                        ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_selection);
                    } else {
                        ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                    }

                    if (tab->signals.hovered) {
                        ui_push_clip(tab, true);
                        ui_box(UI_BOX_CLICK_THROUGH, "highlight") {
                            F32 s = tab->rect.h/8;
                            ui_style_f32(UI_EDGE_SOFTNESS, 60);
                            ui_style_vec4(UI_RADIUS, vec4(s, s, s, s));
                            ui_style_f32(UI_FLOAT_X, ui->mouse.x - tab->rect.x - s);
                            ui_style_f32(UI_FLOAT_Y, ui->mouse.y - tab->rect.y - s);
                            ui_style_vec4(UI_BG_COLOR, ui->theme->button_highlight_color);
                            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 2*s, 1});
                            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, 2*s, 1});
                        }
                        ui_pop_clip();
                    }

                    if (tab->signals.clicked && ui->event->key == KEY_MOUSE_LEFT) {
                        node->active_tab_idx = ARRAY_IDX;
                        info->tree_modified = true;
                    }

                    if (tab->signals.pressed && (ui->event->tag == EVENT_MOUSE_MOVE)) {
                        info->drag.active = true;
                        info->drag.node = node;
                        info->drag.tab_id = id;
                        info->drag.tab_idx = ARRAY_IDX;
                    }

                    UiIcon icon = id->type->get_icon(id, ARRAY_IDX == node->active_tab_idx);
                    ui_icon(UI_BOX_CLICK_THROUGH, "icon", icon);

                    String title = id->type->get_title(id, ARRAY_IDX == node->active_tab_idx);
                    ui_label(UI_BOX_CLICK_THROUGH, "title", title);

                    ui_hspacer();

                    UiBox *close_button = ui_button(str("close_button")) {
                        ui_style_vec2(UI_PADDING, vec2(2, 2));
                        ui_style_vec4(UI_BG_COLOR, vec4(0, 0, 0, 0));
                        ui_style_vec4(UI_BORDER_COLOR, vec4(0, 0, 0, 0));
                        ui_style_vec4(UI_BG_COLOR2, vec4(-1, 0, 0, 0));
                        ui_style_f32(UI_OUTSET_SHADOW_WIDTH, 0);
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_CHILDREN_SUM, 0, 1});

                        if (close_button->signals.hovered || close_button->signals.pressed) {
                            ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                            ui_style_vec4(UI_RADIUS, ui->theme->radius);
                            ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                            ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                        }

                        if (close_button->signals.clicked) tile_tab_remove(info, node, ARRAY_IDX);

                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_CLOSE);
                    }
                }
            }

            if (info->drag.active && ui_within_box(tabs_panel->rect, ui->mouse)) {
                UiBox *preview_tab = ui_box(UI_BOX_REACTIVE, "preview_tab") {
                    F32 r = ui->theme->radius.x;
                    ui_style_vec2(UI_PADDING, vec2(2, 0));
                    ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                    ui_style_u32(UI_ALIGN_Y, UI_ALIGN_MIDDLE);
                    ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, array_get(&tabs->children, 0)->rect.h ?: 12, 1});
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, tab_width, 1});
                    ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                    ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                    ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                }

                U64 preview_tab_idx = tabs->children.count - 1;
                array_iter (tab, &tabs->children) {
                    F32 midpoint = tab->rect.x + tab->rect.w/2;
                    if (midpoint > ui->mouse.x) {
                        preview_tab_idx = ARRAY_IDX;
                        break;
                    }
                }

                array_insert(&tabs->children, array_pop(&tabs->children), preview_tab_idx);

                if (ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT && preview_tab->signals.hovered) {
                    tile_tab_remove(info, info->drag.node, info->drag.tab_idx);
                    preview_tab_idx = clamp(preview_tab_idx, 0u, node->tab_ids.count);
                    array_insert(&node->tab_ids, info->drag.tab_id, preview_tab_idx);
                }
            }

            UiBox *add_button = ui_button(str("add_button")) {
                ui_style_vec2(UI_PADDING, vec2(6, 6));
                ui_style_vec4(UI_BG_COLOR, vec4(0, 0, 0, 0));
                ui_style_vec4(UI_BORDER_COLOR, vec4(0, 0, 0, 0));
                ui_style_vec4(UI_BG_COLOR2, vec4(-1, 0, 0, 0));
                ui_style_f32(UI_OUTSET_SHADOW_WIDTH, 0);

                if (add_button->signals.hovered || add_button->signals.pressed) {
                    F32 r = ui->theme->radius.x;
                    ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                    ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                    ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                    ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                }

                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PLUS);

                Bool open = add_button->scratch;
                if (open || add_button->signals.clicked) {
                    ui_popup(str("popup"), &open, false, add_button) {
                        array_iter (t, &info->view_store->types) {
                            UiBox *btn = ui_button(astr_fmt(tm, "button%lu", ARRAY_IDX)) {
                                ui_style_f32(UI_SPACING, ui->theme->spacing);
                                ui_style_u32(UI_ALIGN_X, UI_ALIGN_START);
                                ui_style_vec4(UI_BG_COLOR, vec4(0, 0, 0, 0));
                                ui_style_vec4(UI_BORDER_COLOR, vec4(0, 0, 0, 0));
                                ui_style_vec4(UI_BG_COLOR2, vec4(-1, 0, 0, 0));
                                ui_style_f32(UI_OUTSET_SHADOW_WIDTH, 0);
                                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 200, 1});
                                if (btn->signals.hovered) ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);

                                if (btn->signals.clicked) {
                                    open = false;
                                    tile_tab_add(info, node, t);
                                }

                                ui_icon(UI_BOX_CLICK_THROUGH, "icon", t->get_icon(0, 0));
                                ui_label(UI_BOX_CLICK_THROUGH, "label", t->get_title(0, 0));
                            }
                        }
                    }
                }

                add_button->scratch = open;
            }
        }
    }
}

static void build_tile_splitter_ring (UiTile *info, UiTileNode *node) {
    UiBox *left;
    UiBox *right;
    UiBox *top;
    UiBox *bottom;

    UiBox *overlay = ui_box(0, "overlay") {
        ui_style_f32(UI_FLOAT_X, 0);
        ui_style_f32(UI_FLOAT_Y, 0);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_u32(UI_ALIGN_Y, UI_ALIGN_MIDDLE);
        ui_style_vec4(UI_BG_COLOR, vec4(0, 0, 0, .3));
        ui_style_vec4(UI_BG_COLOR, vec4(0, 0, 0, .3));
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, overlay->parent->rect.w, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, overlay->parent->rect.h, 0});

        ui_box(0, "ring") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
            ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);

            ui_style_rule(".tile") {
                ui_style_f32(UI_EDGE_SOFTNESS, 0);
                ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                ui_style_vec4(UI_RADIUS, ui->theme->radius);
                ui_style_vec2(UI_PADDING, ui->theme->padding);
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
            }

            top = ui_box(UI_BOX_REACTIVE, "top") {
                ui_tag("tile");
                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PAN_UP);
            }

            ui_box(0, "middle_row"){
                left = ui_box(UI_BOX_REACTIVE, "left") {
                    ui_tag("tile");
                    ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PAN_LEFT);
                }

                ui_box(UI_BOX_INVISIBLE_BG, "spacer") {
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, left->rect.w, 0});
                    ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, left->rect.h, 0});
                }

                right = ui_box(UI_BOX_REACTIVE, "right") {
                    ui_tag("tile");
                    ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PAN_RIGHT);
                }
            }

            bottom = ui_box(UI_BOX_REACTIVE, "bottom") {
                ui_tag("tile");
                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PAN_DOWN);
            }
        }
    }

    ui_style_rule("#preview_tile") {
        ui_style_f32(UI_EDGE_SOFTNESS, 0);
        ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
        ui_style_u32(UI_ANIMATION, UI_MASK_WIDTH|UI_MASK_HEIGHT);
    }

    if (left->signals.hovered) {
        ui_parent(overlay) {
            ui_box(0, "preview_tile") {
                ui_style_f32(UI_FLOAT_X, 0);
                ui_style_f32(UI_FLOAT_Y, 0);
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, preview_tile_width, 0});
                ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
            }
        }

        if (ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT) {
            tile_insert(info, node, info->drag.tab_id, UI_TILE_SPLIT_HORI, 0);
            tile_tab_remove(info, info->drag.node, info->drag.tab_idx);
        }
    }

    if (right->signals.hovered) {
        ui_parent(overlay) {
            ui_box(0, "preview_tile") {
                ui_style_f32(UI_FLOAT_X, overlay->rect.w - preview_tile_width);
                ui_style_f32(UI_FLOAT_Y, 0);
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, preview_tile_width, 0});
                ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
            }
        }

        if (ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT) {
            tile_insert(info, node, info->drag.tab_id, UI_TILE_SPLIT_HORI, 1);
            tile_tab_remove(info, info->drag.node, info->drag.tab_idx);
        }
    }

    if (top->signals.hovered) {
        ui_parent(overlay) {
            ui_box(0, "preview_tile") {
                ui_style_f32(UI_FLOAT_X, 0);
                ui_style_f32(UI_FLOAT_Y, 0);
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, preview_tile_width, 0});
            }
        }

        if (ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT) {
            tile_insert(info, node, info->drag.tab_id, UI_TILE_SPLIT_VERT, 0);
            tile_tab_remove(info, info->drag.node, info->drag.tab_idx);
        }
    }

    if (bottom->signals.hovered) {
        ui_parent(overlay) {
            ui_box(0, "preview_tile") {
                ui_style_f32(UI_FLOAT_X, 0);
                ui_style_f32(UI_FLOAT_Y, overlay->rect.h - preview_tile_width);
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, preview_tile_width, 0});
            }
        }

        if (ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT) {
            tile_insert(info, node, info->drag.tab_id, UI_TILE_SPLIT_VERT, 1);
            tile_tab_remove(info, info->drag.node, info->drag.tab_idx);
        }
    }

    // Move tile preview below the tile ring.
    if (overlay->children.count == 2) array_swap(&overlay->children, 0, 1);
}

static Void build_tile_resizer (UiTile *info, UiTileNode *node, F32 offset) {
    F32 width = 8;

    UiBox *resizer = ui_box(UI_BOX_REACTIVE, "resizer") {
        if (node->split == UI_TILE_SPLIT_HORI) {
            ui_style_f32(UI_FLOAT_X, offset - width/2);
            ui_style_f32(UI_FLOAT_Y, 0);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, width, 1});
            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 1});
            if (! info->drag.active) {
                if (resizer->signals.pressed && ui->event->tag == EVENT_MOUSE_MOVE) {
                    node->ratio = (ui->mouse.x - resizer->parent->rect.x) / resizer->parent->rect.w;
                    info->tree_modified = true;
                }
                if (resizer->signals.hovered || resizer->signals.pressed) ui_set_mouse_cursor(MOUSE_CURSOR_EW_RESIZE);
            }
        } else {
            ui_style_f32(UI_FLOAT_X, 0);
            ui_style_f32(UI_FLOAT_Y, offset - width/2);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 1});
            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, width, 1});
            if (! info->drag.active) {
                if (resizer->signals.pressed && ui->event->tag == EVENT_MOUSE_MOVE) {
                    node->ratio = (ui->mouse.y - resizer->parent->rect.y) / resizer->parent->rect.h;
                    info->tree_modified = true;
                }
                if (resizer->signals.hovered || resizer->signals.pressed) ui_set_mouse_cursor(MOUSE_CURSOR_NS_RESIZE);
            }
        }

        node->ratio = clamp(node->ratio, 0.1f, 0.9f);
    }

}

static Void build_tile_splitter (UiTile *info, UiTileNode *node, Rect first_tile_rect) {
    assert_dbg(node->split != UI_TILE_SPLIT_NONE);

    ui_parent(info->tile_splitter_container) {
        UiBox *splitter = ui_box_fmt(UI_BOX_REACTIVE, "tile_splitter%lu", info->tile_splitter_container->children.count) {
            ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
            ui_style_vec4(UI_RADIUS, ui->theme->radius);
            ui_style_vec2(UI_PADDING, ui->theme->padding);
            ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
            ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);

            if (node->split == UI_TILE_SPLIT_VERT) {
                ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                ui_style_f32(UI_FLOAT_X, first_tile_rect.x - info->tile_preview_container->rect.x + first_tile_rect.w/2 - splitter->rect.w/2);
                ui_style_f32(UI_FLOAT_Y, first_tile_rect.y - info->tile_preview_container->rect.y + first_tile_rect.h - splitter->rect.h/2);
                ui_icon(UI_BOX_CLICK_THROUGH, "icon1", UI_ICON_PAN_UP);
                ui_icon(UI_BOX_CLICK_THROUGH, "icon2", UI_ICON_PAN_DOWN);
            } else {
                ui_style_f32(UI_FLOAT_X, first_tile_rect.x - info->tile_preview_container->rect.x + first_tile_rect.w - splitter->rect.w/2);
                ui_style_f32(UI_FLOAT_Y, first_tile_rect.y - info->tile_preview_container->rect.y + first_tile_rect.h/2 - splitter->rect.h/2);
                ui_icon(UI_BOX_CLICK_THROUGH, "icon1", UI_ICON_PAN_LEFT);
                ui_icon(UI_BOX_CLICK_THROUGH, "icon2", UI_ICON_PAN_RIGHT);
            }
        }

        if (splitter->signals.hovered) {
            ui_parent(info->tile_preview_container) {
                ui_box(0, "tile_preview") {
                    ui_style_f32(UI_EDGE_SOFTNESS, 0);
                    ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                    ui_style_u32(UI_ANIMATION, UI_MASK_WIDTH|UI_MASK_HEIGHT);

                    if (node->split == UI_TILE_SPLIT_VERT) {
                        ui_style_f32(UI_FLOAT_X, first_tile_rect.x - info->tile_preview_container->rect.x);
                        ui_style_f32(UI_FLOAT_Y, first_tile_rect.y - info->tile_preview_container->rect.y + first_tile_rect.h - preview_tile_width/2);
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, first_tile_rect.w, 1});
                        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, preview_tile_width, 1});
                    } else {
                        ui_style_f32(UI_FLOAT_X, first_tile_rect.x - info->tile_preview_container->rect.x + first_tile_rect.w - preview_tile_width/2);
                        ui_style_f32(UI_FLOAT_Y, first_tile_rect.y - info->tile_preview_container->rect.y);
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, preview_tile_width, 1});
                        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, first_tile_rect.h, 1});
                    }
                }
            }

            if (ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT) {
                tile_insert(info, node->child[0], info->drag.tab_id, node->split, 1);
                tile_tab_remove(info, info->drag.node, info->drag.tab_idx);
            }
        }
    }
}

static Void build_outermost_tile_splitters (UiTile *info) {
    ui_parent(info->tile_splitter_container) {
        ui_style_rule(".splitter") {
            ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
            ui_style_vec2(UI_PADDING, ui->theme->padding);
            ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
        }

        F32 r = ui->theme->radius.x;

        UiBox *leftmost_splitter = ui_box(UI_BOX_REACTIVE, "leftmost_splitter") {
            ui_tag("splitter");
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PAN_RIGHT);
            ui_style_f32(UI_FLOAT_X, 0);
            ui_style_f32(UI_FLOAT_Y, info->tile_splitter_container->rect.h/2 - leftmost_splitter->rect.h/2);
            ui_style_vec4(UI_RADIUS, vec4(r, 0, r, 0));
            ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
            leftmost_splitter->next_style.border_widths.z = 0;
            if (leftmost_splitter->signals.hovered) {
                ui_parent(info->tile_preview_container) {
                    ui_box(0, "preview_tile") {
                        ui_style_f32(UI_FLOAT_X, 0);
                        ui_style_f32(UI_FLOAT_Y, 0);
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, preview_tile_width, 0});
                        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                        ui_style_f32(UI_EDGE_SOFTNESS, 0);
                        ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                        ui_style_u32(UI_ANIMATION, UI_MASK_WIDTH|UI_MASK_HEIGHT);
                    }
                }

                if (ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT) {
                    tile_insert(info, *info->root, info->drag.tab_id, UI_TILE_SPLIT_HORI, 0);
                    tile_tab_remove(info, info->drag.node, info->drag.tab_idx);
                }
            }
        }

        UiBox *rightmost_splitter = ui_box(UI_BOX_REACTIVE, "rightmost_splitter") {
            ui_tag("splitter");
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PAN_LEFT);
            ui_style_f32(UI_FLOAT_X, info->tile_splitter_container->rect.w - leftmost_splitter->rect.w);
            ui_style_f32(UI_FLOAT_Y, info->tile_splitter_container->rect.h/2 - leftmost_splitter->rect.h/2);
            ui_style_vec4(UI_RADIUS, vec4(0, r, 0, r));
            ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
            rightmost_splitter->next_style.border_widths.x = 0;
            if (rightmost_splitter->signals.hovered) {
                ui_parent(info->tile_preview_container) {
                    ui_box(0, "preview_tile") {
                        ui_style_f32(UI_FLOAT_X, info->tile_preview_container->rect.w - preview_tile_width);
                        ui_style_f32(UI_FLOAT_Y, 0);
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, preview_tile_width, 0});
                        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                        ui_style_f32(UI_EDGE_SOFTNESS, 0);
                        ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                        ui_style_u32(UI_ANIMATION, UI_MASK_WIDTH|UI_MASK_HEIGHT);
                    }
                }

                if (ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT) {
                    tile_insert(info, *info->root, info->drag.tab_id, UI_TILE_SPLIT_HORI, 1);
                    tile_tab_remove(info, info->drag.node, info->drag.tab_idx);
                }
            }
        }

        UiBox *topmost_splitter = ui_box(UI_BOX_REACTIVE, "topmost_splitter") {
            ui_tag("splitter");
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PAN_DOWN);
            ui_style_f32(UI_FLOAT_X, info->tile_splitter_container->rect.w/2 - leftmost_splitter->rect.w/2);
            ui_style_f32(UI_FLOAT_Y, 0);
            ui_style_vec4(UI_RADIUS, vec4(0, 0, r, r));
            ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
            topmost_splitter->next_style.border_widths.y = 0;
            if (topmost_splitter->signals.hovered) {
                ui_parent(info->tile_preview_container) {
                    ui_box(0, "preview_tile") {
                        ui_style_f32(UI_FLOAT_X, 0);
                        ui_style_f32(UI_FLOAT_Y, 0);
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, preview_tile_width, 0});
                        ui_style_f32(UI_EDGE_SOFTNESS, 0);
                        ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                        ui_style_u32(UI_ANIMATION, UI_MASK_WIDTH|UI_MASK_HEIGHT);
                    }
                }

                if (ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT) {
                    tile_insert(info, *info->root, info->drag.tab_id, UI_TILE_SPLIT_VERT, 0);
                    tile_tab_remove(info, info->drag.node, info->drag.tab_idx);
                }
            }
        }

        UiBox *bottommost_splitter = ui_box(UI_BOX_REACTIVE, "bottommost_splitter") {
            ui_tag("splitter");
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PAN_UP);
            ui_style_f32(UI_FLOAT_X, info->tile_splitter_container->rect.w/2 - leftmost_splitter->rect.w/2);
            ui_style_f32(UI_FLOAT_Y, info->tile_splitter_container->rect.h - leftmost_splitter->rect.h);
            ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
            ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
            bottommost_splitter->next_style.border_widths.w = 0;
            if (bottommost_splitter->signals.hovered) {
                ui_parent(info->tile_preview_container) {
                    ui_box(0, "preview_tile") {
                        ui_style_f32(UI_FLOAT_X, 0);
                        ui_style_f32(UI_FLOAT_Y, info->tile_preview_container->rect.h - preview_tile_width);
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, preview_tile_width, 0});
                        ui_style_f32(UI_EDGE_SOFTNESS, 0);
                        ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                        ui_style_u32(UI_ANIMATION, UI_MASK_WIDTH|UI_MASK_HEIGHT);
                    }
                }

                if (ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT) {
                    tile_insert(info, *info->root, info->drag.tab_id, UI_TILE_SPLIT_VERT, 1);
                    tile_tab_remove(info, info->drag.node, info->drag.tab_idx);
                }
            }
        }

        ui_box(0, "dragged_tab") {
            ui_tag("splitter");
            ui_style_f32(UI_FLOAT_X, ui->mouse.x + 10 - info->tile_splitter_container->rect.x);
            ui_style_f32(UI_FLOAT_Y, ui->mouse.y + 10 - info->tile_splitter_container->rect.y);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 64, 1});
            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, 20, 1});
            ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
            ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
            ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
        }
    }
}

static Void build_node (UiTile *info, UiTileNode *node) {
    if (node->split != UI_TILE_SPLIT_NONE) {
        ui_style_u32(UI_AXIS, node->split == UI_TILE_SPLIT_HORI ? UI_AXIS_HORIZONTAL : UI_AXIS_VERTICAL);

        UiBox *first = ui_box(UI_BOX_CLICK_THROUGH, "first") {
            ui_style_f32(UI_EDGE_SOFTNESS, 0);

            if (node->split == UI_TILE_SPLIT_HORI) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, node->ratio, 0});
                ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
            } else {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, node->ratio, 0});
            }

            build_node(info, node->child[0]);
        }

        ui_box(UI_BOX_CLICK_THROUGH, "second") {
            ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);

            if (node->split == UI_TILE_SPLIT_HORI) {
                ui_box(0, "border") {
                    ui_style_f32(UI_EDGE_SOFTNESS, 0);
                    ui_style_vec4(UI_BG_COLOR, ui->theme->border_color);
                    ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 1, 0});
                }
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1.f - node->ratio, 0});
                ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
            } else {
                ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                ui_box(0, "border") {
                    ui_style_f32(UI_EDGE_SOFTNESS, 0);
                    ui_style_vec4(UI_BG_COLOR, ui->theme->border_color);
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, 1, 0});
                }
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1.f - node->ratio, 0});
            }

            build_node(info, node->child[1]);
        }

        if (info->drag.active) build_tile_splitter(info, node, first->rect);
        build_tile_resizer(info, node, node->split == UI_TILE_SPLIT_HORI ? first->rect.w : first->rect.h);
    } else {
        ui_box(0, "leaf") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

            build_tabs_panel(info, node);

            UiBox *box = ui_box(UI_BOX_CLIPPING, "content") {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

                if (node->tab_ids.count) {
                    array_iter (tab, &node->tab_ids) {
                        Bool visible = ARRAY_IDX == node->active_tab_idx;
                        ui_box_fmt(visible ? UI_BOX_INVISIBLE_BG : UI_BOX_HIDDEN, "tile%lu", ARRAY_IDX) {
                            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                            tab->type->build(tab, visible);
                        }
                    }
                } else {
                    ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
                    ui_style_u32(UI_ALIGN_Y, UI_ALIGN_MIDDLE);
                    UiBox *close_button = ui_button_label_str(str("close_button"), str("Close Tile"));
                    if (close_button->signals.clicked) tile_remove_empty_leaf(info, node);
                }

                if (info->drag.active && ui_within_box(box->rect, ui->mouse)) {
                    build_tile_splitter_ring(info, node);
                }
            }
        }
    }
}

UiBox *ui_tile (String id, Mem *tree_mem, UiTileNode **root, UiViewStore *view_store, Bool *out_tree_modified) {
    UiBox *container = ui_box_str(0, id) {
        UiTile *info = ui_get_box_data(container, sizeof(UiTile), 3*sizeof(UiTile));
        info->tree_modified = false;
        info->tree_mem = tree_mem;
        info->root = root;
        info->view_store = view_store;
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

        info->tile_preview_container = ui_box(UI_BOX_INVISIBLE_BG|UI_BOX_CLICK_THROUGH, "tile_preview_container") {
            ui_style_f32(UI_FLOAT_X, 0);
            ui_style_f32(UI_FLOAT_Y, 0);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        }

        info->tile_splitter_container = ui_box(UI_BOX_INVISIBLE_BG|UI_BOX_CLICK_THROUGH, "tile_splitter_container") {
            ui_style_f32(UI_FLOAT_X, 0);
            ui_style_f32(UI_FLOAT_Y, 0);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        }

        ui_box(UI_BOX_INVISIBLE_BG, "content_box") {
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
            build_node(info, *info->root);
        }

        if (info->drag.active) {
            array_insert(&container->children, array_pop(&container->children), 0);
            build_outermost_tile_splitters(info);
            if (ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT) info->drag.active = false;
        }

        while ((*root)->parent) {
            *root = (*root)->parent;
        }

        *out_tree_modified = info->tree_modified;
    }

    return container;
}

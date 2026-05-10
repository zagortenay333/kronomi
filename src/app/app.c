#include "app/app.h"
#include "ui/ui.h"
#include "ui/ui_widgets.h"
#include "ui/ui_text_editor.h"
#include "ui/ui_text_view.h"
#include "ui/ui_markup_view.h"
#include "ui/ui_tile.h"
#include "buffer/buffer.h"
#include "window/window.h"
#include "config/config.h"
#include "app/settings.h"
#include "app/flashcards.h"
#include "app/stopwatch.h"
#include "app/pomodoro.h"
#include "app/timer.h"
#include "app/alarm.h"
#include "app/todo.h"
#include "app/help.h"

App *app;

Int app_cmp_search_results (Void *a_, Void *b_) {
    SearchResult *a = a_;
    SearchResult *b = b_;
    return (a->score < b->score) ? -1 : (a->score > b->score) ? 1 : 0;
}

Int app_cmp_search_results_on_idx (Void *a_, Void *b_) {
    SearchResult *a = a_;
    SearchResult *b = b_;
    return (a->idx < b->idx) ? -1 : (a->idx > b->idx) ? 1 : 0;
}

UiBox *app_show_more_button (String id, U64 *idx, U64 count) {
    if (*idx == 0 && count) *idx = ui->config->show_more_inc;
    if (*idx >= count) return 0;

    UiBox *show_more_button = ui_button(id) {
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, ui->config->card_width, 1});
        ui_label(UI_BOX_CLICK_THROUGH, "label", str("Show more"));
        if (show_more_button->signals.clicked) *idx = sat_add64(*idx, ui->config->show_more_inc);
    }

    return show_more_button;
}

Void app_sync_scroll (UiBox *scrollbox, UiBox *editor, UiBox *markup, U64 *prev_cursor) {
    UiTextEditorInfo *editor_info = ui_get_box_data(editor, 0, 0);
    MarkupView *markup_info = ui_get_box_data(markup, 0, 0);

    tmem_new(tm);
    SliceMarkupAst ast_path = markup_idx_to_ast_path(tm, markup_info->ast, editor_info->cursor.byte_offset);
    UiBox *box = 0;
    array_iter_back (ast, &ast_path) {
        box = map_get_ptr(&markup_info->ast_to_box, ast);
        if (box) break;
    }

    if (box) {
        if (editor_info->cursor.byte_offset != *prev_cursor) ui_scroll_to(scrollbox, box, false);
        *prev_cursor = editor_info->cursor.byte_offset;
        ui_style_box_vec4(box, UI_BORDER_COLOR, ui->theme->border_color_focus);
        ui_style_box_vec4(box, UI_BORDER_WIDTHS, ui->theme->border_width_focus);
    }
}

static Void tile_tree_to_str (UiTileNode *node, AString *astr, Int depth) {
    astr_push_cstr(astr, "{\n");
    depth++;

    astr_push_fmt(astr, "%*ssplit = %u\n", 4*depth, "", node->split);

    if (node->split == UI_TILE_SPLIT_NONE) {
        astr_push_fmt(astr, "%*sactive_tab = %lu\n", 4*depth, "", node->active_tab_idx);
        astr_push_fmt(astr, "%*stabs = [", 4*depth, "");
        array_iter (tab, &node->tab_ids) {
            astr_push_fmt(astr, "%u", tab->type->id);
            if (! ARRAY_ITER_DONE) astr_push_byte(astr, ' ');
        }
        astr_push_cstr(astr, "]\n");
    } else {
        astr_push_fmt(astr, "%*sratio = %.2f\n", 4*depth, "", node->ratio);

        astr_push_fmt(astr, "%*schild0 = ", 4*depth, "");
        tile_tree_to_str(node->child[0], astr, depth);

        astr_push_fmt(astr, "%*schild1 = ", 4*depth, "");
        tile_tree_to_str(node->child[1], astr, depth);
    }

    depth--;
    astr_push_fmt(astr, "%*s}\n", 4*depth, "");
}

static UiTileNode *ast_to_tile_tree (Config *cfg, ConfigAst *ast) {
    UiTileNode *node = mem_new(app->config_mem, UiTileNode);

    node->split = config_get_u64(cfg, ast, "split");
    node->split = clamp(node->split, 0, UI_TILE_SPLIT_VERT);

    if (node->split == UI_TILE_SPLIT_NONE) {
        array_init(&node->tab_ids, app->config_mem);
        node->active_tab_idx = config_get_u64(cfg, ast, "active_tab");

        ConfigAst *tabs = config_get_array(cfg, ast, "tabs");
        if (tabs->children.count) assert_always(array_get(&tabs->children, 0)->tag == CONFIG_AST_U64);
        array_iter (tab, &tabs->children) {
            U32 view_type_id = cast(ConfigAstU64*, tab)->value;
            assert_always(view_type_id < VIEW_COUNT);
            UiViewInstance *instance = ui_view_instance_new(app->views, view_type_id);
            array_push(&node->tab_ids, instance);
        }
    } else {
        node->ratio = config_get_f64(cfg, ast, "ratio");
        node->ratio = clamp(node->ratio, 0.0, 1.0);
        ConfigAst *child0 = config_get_struct(cfg, ast, "child0");
        ConfigAst *child1 = config_get_struct(cfg, ast, "child1");
        node->child[0] = ast_to_tile_tree(cfg, child0);
        node->child[1] = ast_to_tile_tree(cfg, child1);
        node->child[0]->parent = node;
        node->child[1]->parent = node;
    }

    return node;
}

Void app_load_ui_theme (String filepath) {
    tmem_new(tm);
    Config *cfg = config_parse(tm, filepath);

    app->ui_theme.spacing                = config_get_f64(cfg, cfg->root, "spacing");
    app->ui_theme.padding                = config_get_vec2(cfg, cfg->root, "padding");
    app->ui_theme.radius                 = config_get_vec4(cfg, cfg->root, "radius");
    app->ui_theme.border_color           = config_get_vec4(cfg, cfg->root, "border_color");
    app->ui_theme.border_color_focus     = config_get_vec4(cfg, cfg->root, "border_color_focus");
    app->ui_theme.border_width           = config_get_vec4(cfg, cfg->root, "border_width");
    app->ui_theme.border_width_focus     = config_get_vec4(cfg, cfg->root, "border_width_focus");
    app->ui_theme.in_shadow_width        = config_get_f64(cfg, cfg->root, "in_shadow_width");
    app->ui_theme.in_shadow_color        = config_get_vec4(cfg, cfg->root, "in_shadow_color");
    app->ui_theme.out_shadow_width       = config_get_f64(cfg, cfg->root, "out_shadow_width");
    app->ui_theme.out_shadow_color       = config_get_vec4(cfg, cfg->root, "out_shadow_color");
    app->ui_theme.bg_titlebar            = config_get_vec4(cfg, cfg->root, "bg_titlebar");
    app->ui_theme.padding_titlebar       = config_get_vec2(cfg, cfg->root, "padding_titlebar");
    app->ui_theme.radius_titlebar        = config_get_f64(cfg, cfg->root, "radius_titlebar");
    app->ui_theme.border_color_titlebar  = config_get_vec4(cfg, cfg->root, "border_color_titlebar");
    app->ui_theme.text_color_normal      = config_get_vec4(cfg, cfg->root, "text_color_normal");
    app->ui_theme.text_color_faint       = config_get_vec4(cfg, cfg->root, "text_color_faint");
    app->ui_theme.text_color_inactive    = config_get_vec4(cfg, cfg->root, "text_color_inactive");
    app->ui_theme.text_color_selection   = config_get_vec4(cfg, cfg->root, "text_color_selection");
    app->ui_theme.text_color_red         = config_get_vec4(cfg, cfg->root, "text_color_red");
    app->ui_theme.text_color_green       = config_get_vec4(cfg, cfg->root, "text_color_green");
    app->ui_theme.text_color_blue        = config_get_vec4(cfg, cfg->root, "text_color_blue");
    app->ui_theme.text_color_yellow      = config_get_vec4(cfg, cfg->root, "text_color_yellow");
    app->ui_theme.color_accent           = config_get_vec4(cfg, cfg->root, "color_accent");
    app->ui_theme.color_red              = config_get_vec4(cfg, cfg->root, "color_red");
    app->ui_theme.color_green            = config_get_vec4(cfg, cfg->root, "color_green");
    app->ui_theme.color_blue             = config_get_vec4(cfg, cfg->root, "color_blue");
    app->ui_theme.color_yellow           = config_get_vec4(cfg, cfg->root, "color_yellow");
    app->ui_theme.bg_color_z1            = config_get_vec4(cfg, cfg->root, "bg_color_z1");
    app->ui_theme.bg_color_z2            = config_get_vec4(cfg, cfg->root, "bg_color_z2");
    app->ui_theme.bg_color_z3            = config_get_vec4(cfg, cfg->root, "bg_color_z3");
    app->ui_theme.bg_color_selection     = config_get_vec4(cfg, cfg->root, "bg_color_selection");
    app->ui_theme.bg_color_popup         = config_get_vec4(cfg, cfg->root, "bg_color_popup");
    app->ui_theme.button_highlight_color = config_get_vec4(cfg, cfg->root, "button_highlight_color");
    app->ui_theme.slider_knob_color      = config_get_vec4(cfg, cfg->root, "slider_knob_color");
}

Void app_config_save () {
    tmem_new(tm);

    AString astr = astr_new(tm);

    F32 s = win_get_display_scale();
    ui_scale(1/s);

    astr_push_fmt(&astr, "version = %lu\n", app->config_version);
    astr_push_fmt(&astr, "icon_size = %.2f\n", app->ui_config.icon_size);
    astr_push_fmt(&astr, "font_size = %.2f\n", app->ui_config.font_size);
    astr_push_fmt(&astr, "font_path_normal = \"%.*s\"\n", STR(app->ui_config.font_path_normal));
    astr_push_fmt(&astr, "font_path_bold = \"%.*s\"\n", STR(app->ui_config.font_path_bold));
    astr_push_fmt(&astr, "font_path_italic = \"%.*s\"\n", STR(app->ui_config.font_path_italic));
    astr_push_fmt(&astr, "font_path_mono = \"%.*s\"\n", STR(app->ui_config.font_path_mono));
    astr_push_fmt(&astr, "font_path_icons = \"%.*s\"\n", STR(app->ui_config.font_path_icons));
    astr_push_fmt(&astr, "show_more_inc = %lu\n", app->ui_config.show_more_inc);
    astr_push_fmt(&astr, "card_width = %lu\n", app->ui_config.card_width);
    astr_push_fmt(&astr, "animation_time = %.2f\n", app->ui_config.animation_time);
    astr_push_fmt(&astr, "show_titlebar = %s\n", app->ui_config.show_titlebar ? "true" : "false");
    astr_push_fmt(&astr, "tab_width = %u\n", app->ui_config.tab_width);
    astr_push_fmt(&astr, "line_spacing = %.2f\n", app->ui_config.line_spacing);
    astr_push_fmt(&astr, "scrollbar_width = %.2f\n", app->ui_config.scrollbar_width);
    astr_push_fmt(&astr, "blur_strength = %.2f\n", app->ui_config.blur_strength);

    astr_push_cstr(&astr, "tile_tree = ");
    tile_tree_to_str(app->tile_tree, &astr, 0);

    fs_ensure_dir(app->config_dir_path);
    fs_write_entire_file(app->config_file_path, astr_to_str(&astr));

    astr.count = 0;
    UiTheme *t = &app->ui_theme;
    #define V4(F) astr_push_fmt(&astr, #F" = [%.2f %.2f %.2f %.2f]\n", t->F.x, t->F.y, t->F.z, t->F.w);
    astr_push_fmt(&astr, "spacing = %.2f\n", t->spacing);
    astr_push_fmt(&astr, "padding = [%.2f %.2f]\n", t->padding.x, t->padding.y);
    V4(radius)
    V4(border_color);
    V4(border_color_focus);
    V4(border_width);
    V4(border_width_focus);
    astr_push_fmt(&astr, "in_shadow_width = %.2f\n", t->in_shadow_width);
    V4(in_shadow_color);
    astr_push_fmt(&astr, "out_shadow_width = %.2f\n", t->out_shadow_width);
    V4(out_shadow_color);
    V4(border_color_titlebar);
    V4(bg_titlebar);
    astr_push_fmt(&astr, "padding_titlebar = [%.2f %.2f]\n", t->padding_titlebar.x, t->padding_titlebar.y);
    astr_push_fmt(&astr, "radius_titlebar = %.2f\n", t->radius_titlebar);
    V4(text_color_normal);
    V4(text_color_faint);
    V4(text_color_inactive);
    V4(text_color_selection);
    V4(text_color_red);
    V4(text_color_green);
    V4(text_color_blue);
    V4(text_color_yellow);
    V4(color_accent);
    V4(color_red);
    V4(color_green);
    V4(color_blue);
    V4(color_yellow);
    V4(bg_color_z1);
    V4(bg_color_z2);
    V4(bg_color_z3);
    V4(bg_color_selection);
    V4(bg_color_popup);
    V4(button_highlight_color);
    V4(slider_knob_color);
    #undef V4

    fs_write_entire_file(app->theme_file_path, astr_to_str(&astr));
    ui_scale(s);
}

static Void load_config () {
    tmem_new(tm);
    arena_pop_all(cast(Arena*, app->config_mem));

    fs_ensure_file(app->config_file_path, str("data/settings/global.txt"));
    fs_ensure_file(app->theme_file_path, str("data/themes/dark.txt"));

    Config *cfg = config_parse(tm, app->config_file_path);

    U64 disk_version = config_get_u64(cfg, cfg->root, "version");
    if (disk_version != app->config_version) {
        fs_delete_file(app->config_file_path);
        load_config();
        return;
    }

    app->tile_tree = ast_to_tile_tree(cfg, config_get_struct(cfg, cfg->root, "tile_tree"));

    app->ui_config.icon_size        = config_get_f64(cfg, cfg->root, "icon_size");
    app->ui_config.font_size        = config_get_f64(cfg, cfg->root, "font_size");
    app->ui_config.font_path_normal = config_get_string(cfg, cfg->root, "font_path_normal", app->config_mem);
    app->ui_config.font_path_bold   = config_get_string(cfg, cfg->root, "font_path_bold", app->config_mem);
    app->ui_config.font_path_italic = config_get_string(cfg, cfg->root, "font_path_italic", app->config_mem);
    app->ui_config.font_path_mono   = config_get_string(cfg, cfg->root, "font_path_mono", app->config_mem);
    app->ui_config.font_path_icons  = config_get_string(cfg, cfg->root, "font_path_icons", app->config_mem);
    app->ui_config.show_more_inc    = config_get_u64(cfg, cfg->root, "show_more_inc");
    app->ui_config.card_width       = config_get_u64(cfg, cfg->root, "card_width");
    app->ui_config.animation_time   = config_get_f64(cfg, cfg->root, "animation_time");
    app->ui_config.show_titlebar    = config_get_bool(cfg, cfg->root, "show_titlebar");
    app->ui_config.tab_width        = config_get_u64(cfg, cfg->root, "tab_width");
    app->ui_config.line_spacing     = config_get_f64(cfg, cfg->root, "line_spacing");
    app->ui_config.scrollbar_width  = config_get_f64(cfg, cfg->root, "scrollbar_width");
    app->ui_config.blur_strength    = config_get_f64(cfg, cfg->root, "blur_strength");

    app_load_ui_theme(app->theme_file_path);
    ui_scale(win_get_display_scale());
}

static Void build_global_style_rules () {
    ui_style_rule(".row") {
        ui_style_u32(UI_ALIGN_Y, UI_ALIGN_MIDDLE);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z2);
        ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
        F32 b = ui->theme->border_width.x;
        ui_style_vec4(UI_BORDER_WIDTHS, vec4(b, b, b, 0));
        ui_style_vec4(UI_RADIUS, vec4(0, 0, 0, 0));
    }

    ui_style_rule(".row:first") {
        F32 r = ui->theme->radius.x;
        ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
    }

    ui_style_rule(".row:last") {
        F32 r = ui->theme->radius.x;
        ui_style_vec4(UI_RADIUS, vec4(0, 0, r, r));
        ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
    }

    ui_style_rule(".row:first:last") {
        F32 r = ui->theme->radius.x;
        ui_style_vec4(UI_RADIUS, vec4(r, r, r, r));
    }

    ui_style_rule(".card") {
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, ui->config->card_width, 1});
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_vec4(UI_RADIUS, ui->theme->radius);
        ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
        ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
        ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
        ui_style_u32(UI_ANIMATION, UI_MASK_WIDTH);
    }

    ui_style_rule(".card #header") {
        F32 b = ui->theme->border_width.x;
        F32 r = ui->theme->radius.x;
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec4(UI_BORDER_WIDTHS, vec4(0, 0, 0, b));
        ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
        ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
        ui_style_u32(UI_ALIGN_Y, UI_ALIGN_MIDDLE);
    }

    ui_style_rule(".card #header #autohide_icons") {
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_CHILDREN_SUM, 0, 1});
    }

    ui_style_rule(".card #autohide_icons .button.focus") {
        ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color_focus);
        ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width_focus);
    }

    ui_style_rule(".card #autohide_icons .button.hover *") {
        ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_blue);
    }

    ui_style_rule(".card #autohide_icons .button.press *") {
        ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_blue);
    }

    ui_style_rule(".card #body") {
        F32 r = ui->theme->radius.x;
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_f32(UI_EDGE_SOFTNESS, 0);
        ui_style_vec4(UI_RADIUS, vec4(0, 0, r, r));
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
    }

    ui_style_rule(".card .table .cell") {
        ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z2);
    }

    ui_style_rule(".sidebar") {
        F32 b = ui->theme->border_width.x;
        ui_style_f32(UI_EDGE_SOFTNESS, 0);
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
        ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
        ui_style_vec4(UI_BORDER_WIDTHS, vec4(b, 0, 0, 0));
        ui_style_u32(UI_ANIMATION, UI_MASK_WIDTH);
    }
}

Void app_build () {
    ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z2);
    build_global_style_rules();
    Bool tree_modified = false;
    ui_tile(str("tiles"), app->config_mem, &app->tile_tree, app->views, &tree_modified);
    if (tree_modified) app_config_save();
}

Void app_init () {
    app = mem_new(mem_root, App);

    ui_init(&app->ui_config, &app->ui_theme);

    app->config_version = 1;
    app->config_mem = cast(Mem*, arena_new(mem_root, 1*KB));

    { // Build config file/dir paths:
        tmem_new(tm);
        AString a = astr_new(mem_root);
        astr_push_str(&a, fs_get_home_dir(tm));
        astr_push_cstr(&a, "/.config/chronomi/global.txt");
        app->config_file_path = astr_to_str(&a);
        app->config_dir_path = str_prefix_to_last(astr_to_str(&a), '/');

    }

    { // Build theme file path:
        tmem_new(tm);
        AString a = astr_new(mem_root);
        astr_push_str(&a, fs_get_home_dir(tm));
        astr_push_cstr(&a, "/.config/chronomi/theme.txt");
        app->theme_file_path = astr_to_str(&a);
    }

    { // Init view store:
        app->views = ui_view_store_new(mem_root);

        ui_view_type_add(app->views, (UiViewType){
            .id = VIEW_SETTINGS,
            .init = settings_view_init,
            .free = settings_view_free,
            .get_icon = settings_view_get_icon,
            .get_title = settings_view_get_title,
            .build = settings_view_build,
        });

        ui_view_type_add(app->views, (UiViewType){
            .id = VIEW_FLASHCARDS,
            .init = flashcards_view_init,
            .free = flashcards_view_free,
            .get_icon = flashcards_view_get_icon,
            .get_title = flashcards_view_get_title,
            .build = flashcards_view_build,
        });

        ui_view_type_add(app->views, (UiViewType){
            .id = VIEW_STOPWATCH,
            .init = stopwatch_view_init,
            .free = stopwatch_view_free,
            .get_icon = stopwatch_view_get_icon,
            .get_title = stopwatch_view_get_title,
            .build = stopwatch_view_build,
        });

        ui_view_type_add(app->views, (UiViewType){
            .id = VIEW_POMODORO,
            .init = pomodoro_view_init,
            .free = pomodoro_view_free,
            .get_icon = pomodoro_view_get_icon,
            .get_title = pomodoro_view_get_title,
            .build = pomodoro_view_build,
        });

        ui_view_type_add(app->views, (UiViewType){
            .id = VIEW_TIMER,
            .init = timer_view_init,
            .free = timer_view_free,
            .get_icon = timer_view_get_icon,
            .get_title = timer_view_get_title,
            .build = timer_view_build,
        });

        ui_view_type_add(app->views, (UiViewType){
            .id = VIEW_ALARM,
            .init = alarm_view_init,
            .free = alarm_view_free,
            .get_icon = alarm_view_get_icon,
            .get_title = alarm_view_get_title,
            .build = alarm_view_build,
        });

        ui_view_type_add(app->views, (UiViewType){
            .id = VIEW_TODO,
            .init = todo_view_init,
            .free = todo_view_free,
            .get_icon = todo_view_get_icon,
            .get_title = todo_view_get_title,
            .build = todo_view_build,
        });

        ui_view_type_add(app->views, (UiViewType){
            .id = VIEW_HELP,
            .init = help_view_init,
            .free = help_view_free,
            .get_icon = help_view_get_icon,
            .get_title = help_view_get_title,
            .build = help_view_build,
        });
    }

    load_config();
}

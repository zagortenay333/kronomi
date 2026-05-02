#include "app/app.h"
#include "ui/ui_widgets.h"
#include "ui/ui_markup_view.h"
#include "ui/ui_text_editor.h"
#include "config/config.h"
#include "buffer/buffer.h"
#include "app/stopwatch.h"
#include "os/time.h"

ienum (StopwatchState, U8) {
    STOPWATCH_RESET,
    STOPWATCH_PAUSED,
    STOPWATCH_RUNNING,
};

istruct (Lap) {
    Millisec lap;
    Millisec total;
};

istruct (Stopwatch) {
    F32 clock_size;
    String message;
    Millisec lap;
    Millisec current;
    Millisec monotonic;
    StopwatchState state;
    Array(Lap) laps;
    Buf *lap_buf;
};

ienum (ViewTag, U8) {
    VIEW_MAIN,
    VIEW_SEARCH,
    VIEW_EDITOR,
};

istruct (View) {
    ViewTag tag;

    union {
        struct {
            Buf *buf;
            F32 clock_size;
            U64 editor_cursor;
            U64 stopwatch_idx;
        } editor;

        struct {
            Buf *buf;
            Bool delete_searched;
            U64 buf_version;
            Array(SearchResult) searched;
        } search;
    };
};

ienum (CommandTag, U8) {
    CMD_ADD,
    CMD_DEL,
    CMD_START,
    CMD_PAUSE,
    CMD_RESET,
    CMD_LAP,
    CMD_MOVE_TOP,
    CMD_SAVE_CONFIG,
    CMD_VIEW_MAIN,
    CMD_VIEW_EDITOR,
    CMD_VIEW_SEARCH,
};

istruct (Command) {
    CommandTag tag;
    Bool skip_config_save;
    U64 idx;
    F32 clock_size;
    String text;
};

istruct (Context) {
    View view;
    Mem *view_mem;
    U64 config_version;
    String config_file_path;
    Array(Command) commands;
    Mem *config_mem;
    U64 config_mem_fragmentation;
    Array(Stopwatch) stopwatches;
    F32 editor_width;
    F32 editor_height;
    TickId tick_id;
    U64 n_running;
};

static Context *context;

#define push_command(...) array_push_lit(&context->commands, __VA_ARGS__)

static String time_to_str (Mem *mem, Millisec ms) {
    U64 cseconds = (ms / 10) % 100;
    U64 seconds  = (ms / 1000) % 60;
    U64 minutes  = (ms / 60000) % 60;
    U64 hours    = ms / 3600000;
    return astr_fmt(mem, "%02lu:%02lu:%02lu.%02lu", hours, minutes, seconds, cseconds);
}

static Void save_config () {
    tmem_new(tm);

    AString astr = astr_new(tm);
    astr_push_fmt(&astr, "version = %lu\n", context->config_version);
    astr_push_fmt(&astr, "editor_width = %f\n", context->editor_width);
    astr_push_fmt(&astr, "editor_height = %f\n", context->editor_height);
    astr_push_cstr(&astr, "stopwatches = [\n");
    array_iter (stopwatch, &context->stopwatches, *) {
        astr_push_cstr(&astr, "    {\n");
        astr_push_fmt(&astr,  "        clock_size = %f\n", stopwatch->clock_size);
        astr_push_fmt(&astr,  "        message = \"%.*s\"\n", STR(stopwatch->message));
        astr_push_cstr(&astr, "    }\n");
    }
    astr_push_cstr(&astr, "]\n");

    fs_ensure_file(context->config_file_path, (String){});
    fs_write_entire_file(context->config_file_path, astr_to_str(&astr));
}

static Void load_config () {
    tmem_new(tm);

    context->config_mem_fragmentation = 0;
    arena_pop_all(cast(Arena*, context->config_mem));
    array_init(&context->stopwatches, context->config_mem);

    fs_ensure_file(context->config_file_path, str("data/settings/stopwatch.txt"));

    Config *cfg = config_parse(tm, context->config_file_path);

    U64 disk_version = config_get_u64(cfg, cfg->root, "version");
    if (disk_version != context->config_version) {
        fs_delete_file(context->config_file_path);
        load_config();
        return;
    }

    context->editor_width  = config_get_f64(cfg, cfg->root, "editor_width");
    context->editor_height = config_get_f64(cfg, cfg->root, "editor_height");

    ConfigAst *stopwatches = config_get_array(cfg, cfg->root, "stopwatches");
    array_iter (stopwatch_ast, &stopwatches->children) {
        Stopwatch *sw  = array_push_slot(&context->stopwatches);
        *sw = (Stopwatch){};
        sw->clock_size = config_get_f64(cfg, stopwatch_ast, "clock_size"),
        sw->message    = config_get_string(cfg, stopwatch_ast, "message", context->config_mem),
        array_init(&sw->laps, context->config_mem);
    }
}

static Void build_stopwatch (U64 idx, Bool *out_card_deleted) {
    tmem_new(tm);

    Stopwatch *sw = array_ref(&context->stopwatches, idx);

    UiBox *box = ui_box_fmt(UI_BOX_CAN_FOCUS, "card%lu", idx) {
        ui_tag("card");
        ui_style_vec2(UI_PADDING, vec2(ui->theme->border_width.x, ui->theme->border_width.x));

        ui_box(0, "header") {
            if (sw->state == STOPWATCH_RUNNING) {
                ui_style_vec4(UI_BG_COLOR, ui->theme->color_red);
            }

            String label = time_to_str(tm, sw->current);
            UiBox *clock = ui_label_extra(UI_BOX_REACTIVE, "clock", label, ui->config->font_path_normal, (sw->clock_size == 0.0) ? ui->config->font_size : sw->clock_size, false);
            if (clock->signals.hovered && ui->event->tag == EVENT_SCROLL) {
                sw->clock_size += 5 * ui->event->y;
                sw->clock_size = clamp(sw->clock_size, 0, INFINITY);
                save_config();
            }

            ui_hspacer();

            if (ui_within_box(box->rect, ui->mouse) || (ui->focused && ui_is_descendant(box, ui->focused))) {
                ui_box(0, "autohide_icons") {
                    UiBox *move_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "move") {
                        ui_tag("button");
                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_MOVE_TOP);
                        if (move_button->signals.clicked) push_command(.tag=CMD_MOVE_TOP, .idx=idx);
                    }

                    UiBox *edit_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "edit") {
                        ui_tag("button");
                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_EDIT);
                        if (edit_button->signals.clicked) push_command(.tag=CMD_VIEW_EDITOR, .idx=idx);
                    }

                    UiBox *delete_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "delete") {
                        ui_tag("button");
                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_TRASH);
                        if (delete_button->signals.clicked) {
                            push_command(.tag=CMD_DEL, .idx=idx);
                            if (out_card_deleted) *out_card_deleted = true;
                        }
                    }
                }
            }
        }

        ui_box(0, "body") {
            ui_button_group(str("buttons")) {
                StopwatchState s = sw->state;

                if (s == STOPWATCH_RESET || s == STOPWATCH_PAUSED) {
                    UiBox *start_button = ui_button(str("start")) {
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                        ui_label(UI_BOX_CLICK_THROUGH, "label", str("Start"));
                        if (start_button->signals.clicked) push_command(.tag=CMD_START, .idx=idx);
                    }
                }

                if (s == STOPWATCH_RUNNING) {
                    UiBox *pause_button = ui_button(str("pause")) {
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                        ui_label(UI_BOX_CLICK_THROUGH, "label", str("Pause"));
                        if (pause_button->signals.clicked) push_command(.tag=CMD_PAUSE, .idx=idx);
                    }

                    UiBox *lap_button = ui_button(str("lap")) {
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                        ui_label(UI_BOX_CLICK_THROUGH, "label", str("Lap"));
                        if (lap_button->signals.clicked) push_command(.tag=CMD_LAP, .idx=idx);
                    }
                }

                if (s == STOPWATCH_PAUSED) {
                    UiBox *reset_button = ui_button(str("reset")) {
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                        ui_label(UI_BOX_CLICK_THROUGH, "label", str("Reset"));
                        if (reset_button->signals.clicked) push_command(.tag=CMD_RESET, .idx=idx);
                    }
                }
            }

            if (sw->laps.count) ui_markup_view_buf(str("laps"), sw->lap_buf, false, 0, 0);

            ui_markup_view(str("message"), sw->message, 0);
        }
    }
}

static Void build_view_search () {
    Auto view = &context->view.search;

    ui_scroll_box(str("left_box"), true) {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, .35, 1});

        ui_entry(str("entry"), view->buf, -1, str("Search stopwatches..."));

        ui_box(UI_BOX_INVISIBLE_BG, "row_group") {
            ui_box(0, "row") {
                ui_tag("row");
                ui_label(0, "title", str("Delete searched stopwatches"));
                ui_hspacer();
                ui_checkbox("checkbox", &view->delete_searched);
            }
        }

        ui_button_group(str("buttons")) {
            UiBox *cancel_button = ui_button(str("cancel")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Cancel"));
                if (cancel_button->signals.clicked) push_command(.tag=CMD_VIEW_MAIN);
            }

            UiBox *apply_button = ui_button(str("apply")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Apply"));
                if (apply_button->signals.clicked) {
                    if (view->delete_searched) {
                        array_sort_cmp(&view->searched, app_cmp_search_results_on_idx);
                        array_iter_back (it, &view->searched) push_command(.tag=CMD_DEL, .idx=it.idx, .skip_config_save=true);
                        view->searched.count = 0;
                        push_command(.tag=CMD_SAVE_CONFIG);
                    }
                }
            }
        }
    }

    ui_scroll_box(str("right_box"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);

        if (view->buf_version != buf_get_version(view->buf)) {
            view->searched.count = 0;
            view->buf_version = buf_get_version(view->buf);

            tmem_new(tm);
            String needle = buf_get_str(view->buf, tm);

            array_iter (sw, &context->stopwatches, *) {
                I64 score = str_fuzzy_search(needle, sw->message, 0);
                if (score != INT64_MIN) array_push_lit(&view->searched, .score=score, .idx=ARRAY_IDX);
            }

            array_sort_cmp(&view->searched, app_cmp_search_results);
        }

        Bool deleted = false;
        array_iter (card, &view->searched, *) build_stopwatch(card->idx, &deleted);
        if (deleted) view->buf_version--; // To refresh the searched array.
    }
}

static Void build_view_editor () {
    Auto view = &context->view.editor;

    UiBox *editor = 0;
    UiBox *preview = 0;

    ui_scroll_box(str("left_box"), true) {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_CHILDREN_SUM, 0, 1});

        F32 r = ui->theme->radius.x;
        F32 w = context->editor_width;
        F32 h = context->editor_height;

        ui_box(UI_BOX_INVISIBLE_BG, "message") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            UiBox *header = ui_box(0, "header") {
                ui_style_vec2(UI_PADDING, ui->theme->padding);
                ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z2);
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                ui_style_box_size(header, UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(0, "title", str("Message"));
            }

            if (! view->buf) {
                String text = (view->stopwatch_idx == ARRAY_NIL_IDX) ? str("") : array_ref(&context->stopwatches, view->stopwatch_idx)->message;
                view->buf = buf_new(context->view_mem, text);
            }

            editor = ui_ted_resizable(str("editor"), view->buf, &context->editor_width, &context->editor_height, LINE_WRAP_NONE);
            ui_style_box_vec4(editor, UI_RADIUS, vec4(0, 0, r, r));
            ui_style_box_u32(editor, UI_ANIMATION, UI_MASK_WIDTH);
        }

        if (w != context->editor_width || h != context->editor_height) save_config();

        ui_box(UI_BOX_INVISIBLE_BG, "rows") {
            ui_box(0, "clock_size") {
                ui_tag("row");
                ui_label(0, "title", str("Clock size"));
                ui_hspacer();
                if (view->stopwatch_idx != ARRAY_NIL_IDX) view->clock_size = array_ref(&context->stopwatches, view->stopwatch_idx)->clock_size;
                F64 val = view->clock_size;
                UiBox *picker = ui_f64_picker(str("picker"), &val, 0, INFINITY, 4);
                picker->next_style.size.width.strictness = 1;
                view->clock_size = val;
            }
        }

        ui_button_group(str("buttons")) {
            UiBox *cancel_button = ui_button(str("cancel")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Cancel"));
                if (cancel_button->signals.clicked) push_command(.tag=CMD_VIEW_MAIN);
            }

            UiBox *ok_button = ui_button(str("ok")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Ok"));
                if (ok_button->signals.clicked) {
                    String msg = buf_get_str(view->buf, context->config_mem);
                    if (view->stopwatch_idx != ARRAY_NIL_IDX) push_command(.tag=CMD_DEL, .idx=view->stopwatch_idx, .skip_config_save=true);
                    push_command(.tag=CMD_ADD, .clock_size=view->clock_size, .text=msg);
                    push_command(.tag=CMD_VIEW_MAIN);
                }
            }
        }
    }

    UiBox *scrollbox = ui_scroll_box(str("right_box"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);

        F32 r = ui->theme->radius.x;
        F32 b = ui->theme->border_width.x;

        ui_box(UI_BOX_INVISIBLE_BG, "preview") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 30*ui->config->font_size, 1});

            ui_box(0, "header") {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 1});
                ui_style_vec2(UI_PADDING, ui->theme->padding);
                ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                ui_label(0, "title", str("Message preview"));
            }

            preview = ui_markup_view_buf(str("markup"), view->buf, true, 0, 0);
            ui_style_box_vec2(preview, UI_PADDING, ui->theme->padding);
            ui_style_box_vec4(preview, UI_BG_COLOR, ui->theme->bg_color_z3);
            ui_style_box_vec4(preview, UI_BORDER_COLOR, ui->theme->border_color);
            ui_style_box_vec4(preview, UI_BORDER_WIDTHS, ui->theme->border_width);
            ui_style_box_vec4(preview, UI_RADIUS, vec4(0, 0, r, r));
            ui_style_box_vec4(preview, UI_BORDER_WIDTHS, vec4(b, 0, b, b));
        }
    }

    if (ui->focused && ui_is_descendant(editor, ui->focused)) {
        app_sync_scroll(scrollbox, editor, preview, &view->editor_cursor);
    }
}

static Void build_view_main () {
    ui_box(0, "navbox") {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_CHILDREN_SUM, 0, 1});

        ui_style_rule(".button")       { ui_style_vec4(UI_RADIUS, vec4(0, 0, 0, 0)); }
        ui_style_rule(".button:first") { ui_style_vec4(UI_RADIUS, vec4(ui->theme->radius.x, ui->theme->radius.x, 0, 0)); }
        ui_style_rule(".button:last")  { ui_style_vec4(UI_RADIUS, vec4(0, 0, ui->theme->radius.x, ui->theme->radius.x)); }

        UiBox *add_button = ui_button(str("add")) {
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PLUS);
            if (add_button->signals.clicked) push_command(.tag=CMD_VIEW_EDITOR, .idx=ARRAY_NIL_IDX);
            if (add_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Add stopwatch")); }
        }

        UiBox *search_button = ui_button(str("search")) {
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_SEARCH);
            if (search_button->signals.clicked) push_command(.tag=CMD_VIEW_SEARCH);
            if (search_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Search stopwatches")); }
        }
    }

    ui_scroll_box(str("cards"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        array_iter (_, &context->stopwatches, *) { _; build_stopwatch(ARRAY_IDX, 0); }
    }
}

Void stopwatch_view_init (UiViewInstance *) {
}

Void stopwatch_view_free (UiViewInstance *) {
}

UiIcon stopwatch_view_get_icon (UiViewInstance *, Bool visible) {
    return UI_ICON_STOPWATCH;
}

String stopwatch_view_get_title (UiViewInstance *, Bool visible) {
    return str("Stopwatch");
}

static Void destroy_current_view () {
    switch (context->view.tag) {
    case VIEW_MAIN: break;
    case VIEW_SEARCH: break;
    case VIEW_EDITOR: break;
    }

    arena_pop_all(cast(Arena*, context->view_mem));
    context->view = (View){};
}

static Void execute_commands () {
    tmem_new(tm);

    array_iter (cmd, &context->commands) {
        switch (cmd.tag) {
        case CMD_SAVE_CONFIG: {
            save_config();
        } break;

        case CMD_ADD: {
            Stopwatch *sw  = array_push_slot(&context->stopwatches);
            *sw = (Stopwatch){};
            sw->clock_size = cmd.clock_size;
            sw->message    = cmd.text;
            array_init(&sw->laps, context->config_mem);
            if (! cmd.skip_config_save) save_config();
        } break;

        case CMD_DEL: {
            array_remove(&context->stopwatches, cmd.idx);
            context->config_mem_fragmentation++;
            if (! cmd.skip_config_save) save_config();
        } break;

        case CMD_START: {
            Stopwatch *sw = array_ref(&context->stopwatches, cmd.idx);
            assert_dbg(sw->state == STOPWATCH_PAUSED || sw->state == STOPWATCH_RESET);
            sw->state = STOPWATCH_RUNNING;
            sw->monotonic = os_get_time_ms();
            if (context->n_running == 0) context->tick_id = win_tick_start(30);
            context->n_running++;
        } break;

        case CMD_PAUSE: {
            Stopwatch *sw = array_ref(&context->stopwatches, cmd.idx);
            assert_dbg(sw->state == STOPWATCH_RUNNING);
            sw->state = STOPWATCH_PAUSED;
            assert_dbg(context->n_running);
            context->n_running--;
            if (context->n_running == 0) win_tick_end(context->tick_id);
        } break;

        case CMD_RESET: {
            Stopwatch *sw = array_ref(&context->stopwatches, cmd.idx);
            assert_dbg(sw->state == STOPWATCH_PAUSED);
            sw->state = STOPWATCH_RESET;
            sw->current = 0;
            sw->laps.count = 0;
            if (sw->lap_buf) {
                buf_clear(sw->lap_buf);
                String header = astr_fmt(tm, "|**%s**\n|**%s**\n|**%s**\n|-\n", "Position", "Lap", "Total");
                buf_insert(sw->lap_buf, 0, header);
            }
        } break;

        case CMD_LAP: {
            Stopwatch *sw = array_ref(&context->stopwatches, cmd.idx);

            if (! sw->lap_buf) {
                String header = astr_fmt(tm, "|**%s**\n|**%s**\n|**%s**\n|-\n", "Position", "Lap", "Total");
                sw->lap_buf = buf_new(context->config_mem, header);
            }

            String l = time_to_str(tm, sw->lap);
            String t = time_to_str(tm, sw->current);
            String s = astr_fmt(tm, "|\\#%lu\n|%.*s\n|%.*s\n|-\n", ARRAY_IDX+1, STR(l), STR(t));
            buf_insert(sw->lap_buf, buf_get_count(sw->lap_buf), s);

            array_insert_lit(&sw->laps, 0, .lap=sw->lap, .total=sw->current);

            sw->lap = 0;
        } break;

        case CMD_MOVE_TOP: {
            Stopwatch sw = array_get(&context->stopwatches, cmd.idx);
            array_remove(&context->stopwatches, cmd.idx);
            array_insert(&context->stopwatches, sw, 0);
            if (! cmd.skip_config_save) save_config();
        } break;

        case CMD_VIEW_MAIN: {
            destroy_current_view();
            context->view.tag = VIEW_MAIN;
        } break;

        case CMD_VIEW_SEARCH: {
            destroy_current_view();
            context->view.tag = VIEW_SEARCH;
            context->view.search.buf = buf_new(context->view_mem, str(""));
            array_init(&context->view.search.searched, context->view_mem);
        } break;

        case CMD_VIEW_EDITOR: {
            destroy_current_view();
            context->view.tag = VIEW_EDITOR;
            context->view.editor.stopwatch_idx = cmd.idx;
        } break;
        }
    }

    context->commands.count = 0;
}

Void stopwatch_view_build (UiViewInstance *, Bool visible) {
    execute_commands();
    if (context->config_mem_fragmentation > 100) {
        load_config();
        push_command(.tag=CMD_VIEW_MAIN);
        execute_commands();
    }

    array_iter (sw, &context->stopwatches, *) {
        if (sw->state != STOPWATCH_RUNNING) continue;
        U64 now = os_get_time_ms();
        sw->current += now - sw->monotonic;
        sw->lap += now - sw->monotonic;
        sw->monotonic = now;
    }

    switch (context->view.tag) {
    case VIEW_MAIN:   build_view_main(); break;
    case VIEW_SEARCH: build_view_search(); break;
    case VIEW_EDITOR: build_view_editor(); break;
    }
}

Void stopwatch_init () {
    if (context) return;

    context = mem_new(mem_root, Context);
    context->config_version = 0;
    context->view_mem = cast(Mem*, arena_new(mem_root, 1*KB));
    context->config_mem = cast(Mem*, arena_new(mem_root, 1*KB));
    array_init(&context->commands, mem_root);

    { // Build config file path:
        tmem_new(tm);
        AString a = astr_new(mem_root);
        astr_push_str(&a, fs_get_home_dir(tm));
        astr_push_cstr(&a, "/.config/chronomi/stopwatch.txt");
        context->config_file_path = astr_to_str(&a);
    }

    load_config();
    push_command(.tag=CMD_VIEW_MAIN);
}

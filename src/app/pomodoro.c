#include "app/app.h"
#include "ui/ui_widgets.h"
#include "ui/ui_markup_view.h"
#include "ui/ui_text_editor.h"
#include "config/config.h"
#include "buffer/buffer.h"
#include "app/pomodoro.h"
#include "os/time.h"

ienum (PomodoroPhase, U8) {
    POMO_PHASE_WORK,
    POMO_PHASE_SHORT_BREAK,
    POMO_PHASE_LONG_BREAK,
};

istruct (Pomodoro) {
    F32 clock_size;
    String message;
    String sound_file;
    Millisec work_length;
    Millisec short_break_length;
    Millisec long_break_length;
    U64 long_break_every_n_pomos;
    U64 n_completed_pomos;
    U64 pomos_until_long_break;
    PomodoroPhase phase;
    Bool running;
    Millisec start;
    Millisec remaining;
    Sound *sound_id;
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
            Buf *sound_buf;
            Pomodoro pomo;
            U64 editor_cursor;
            Pomodoro *pomo_to_edit;
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
    CMD_SAVE_CONFIG,
    CMD_VIEW_MAIN,
    CMD_VIEW_EDITOR,
    CMD_VIEW_SEARCH,
};

istruct (Command) {
    CommandTag tag;
    Bool skip_config_save;
    U64 index;
    Pomodoro *pomo;
};

istruct (Context) {
    View view;
    Mem *view_mem;
    U64 config_version;
    String config_file_path;
    Array(Command) commands;
    Mem *config_mem;
    U64 config_mem_fragmentation;
    Array(Pomodoro*) pomodoros;
    F32 editor_width;
    F32 editor_height;
    TickId tick_id;
    U64 n_running;
    SliceString phase_strings;
};

static Context *context;

#define push_command(...) array_push_lit(&context->commands, __VA_ARGS__)

static Void destroy_sound (Pomodoro *pomo) {
    if (! pomo->sound_id) return;
    win_sound_cancel(pomo->sound_id);
    mem_free(mem_root, .old_ptr=pomo->sound_id);
    pomo->sound_id = 0;
}

static Void play_sound (Pomodoro *pomo) {
    if (pomo->sound_id) return;
    if (! pomo->sound_file.count) return;
    pomo->sound_id = win_sound_play(mem_root, pomo->sound_file, false);
}

static Void save_config () {
    tmem_new(tm);

    AString astr = astr_new(tm);
    astr_push_fmt(&astr, "version = %lu\n", context->config_version);
    astr_push_fmt(&astr, "editor_width = %f\n", context->editor_width);
    astr_push_fmt(&astr, "editor_height = %f\n", context->editor_height);
    astr_push_cstr(&astr, "pomodoros = [\n");
    array_iter (pomo, &context->pomodoros) {
        astr_push_cstr(&astr, "    {\n");
        astr_push_fmt(&astr,  "        clock_size = %f\n", pomo->clock_size);
        astr_push_fmt(&astr,  "        message = \"%.*s\"\n", STR(pomo->message));
        astr_push_fmt(&astr,  "        sound_file = \"%.*s\"\n", STR(pomo->sound_file));
        astr_push_fmt(&astr,  "        work_length = %lu\n", pomo->work_length);
        astr_push_fmt(&astr,  "        short_break_length = %lu\n", pomo->short_break_length);
        astr_push_fmt(&astr,  "        long_break_length = %lu\n", pomo->long_break_length);
        astr_push_fmt(&astr,  "        n_completed_pomos = %lu\n", pomo->n_completed_pomos);
        astr_push_fmt(&astr,  "        pomos_until_long_break = %lu\n", pomo->pomos_until_long_break);
        astr_push_fmt(&astr,  "        long_break_every_n_pomos = %lu\n", pomo->long_break_every_n_pomos);
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
    array_init(&context->pomodoros, context->config_mem);

    fs_ensure_file(context->config_file_path, str("data/settings/pomodoro.txt"));

    Config *cfg = config_parse(tm, context->config_file_path);

    U64 disk_version = config_get_u64(cfg, cfg->root, "version");
    if (disk_version != context->config_version) {
        fs_delete_file(context->config_file_path);
        load_config();
        return;
    }

    context->editor_width  = config_get_f64(cfg, cfg->root, "editor_width");
    context->editor_height = config_get_f64(cfg, cfg->root, "editor_height");

    ConfigAst *pomos = config_get_array(cfg, cfg->root, "pomodoros");
    array_iter (pomo_ast, &pomos->children) {
        Pomodoro *pomo = mem_new(context->config_mem, Pomodoro);
        pomo->clock_size               = config_get_f64(cfg, pomo_ast, "clock_size");
        pomo->message                  = config_get_string(cfg, pomo_ast, "message", context->config_mem);
        pomo->sound_file               = config_get_string(cfg, pomo_ast, "sound_file", context->config_mem);
        pomo->work_length              = config_get_u64(cfg, pomo_ast, "work_length");
        pomo->short_break_length       = config_get_u64(cfg, pomo_ast, "short_break_length");
        pomo->long_break_length        = config_get_u64(cfg, pomo_ast, "long_break_length");
        pomo->long_break_every_n_pomos = config_get_u64(cfg, pomo_ast, "long_break_every_n_pomos");
        pomo->n_completed_pomos        = config_get_u64(cfg, pomo_ast, "n_completed_pomos");
        pomo->pomos_until_long_break   = config_get_u64(cfg, pomo_ast, "pomos_until_long_break");
        array_push(&context->pomodoros, pomo);
    }
}

static Void build_pomo (Pomodoro *pomo, Bool *out_card_deleted) {
    tmem_new(tm);

    U64 n = array_get_last(&ui->box_stack)->children.count;
    UiBox *box = ui_box_fmt(UI_BOX_CAN_FOCUS, "card%lu", n) {
        ui_tag("card");
        ui_style_vec2(UI_PADDING, vec2(ui->theme->border_width.x, ui->theme->border_width.x));

        ui_box(0, "header") {
            if (pomo->running) {
                switch (pomo->phase) {
                case POMO_PHASE_WORK:        ui_style_vec4(UI_BG_COLOR, ui->theme->color_red); break;
                case POMO_PHASE_SHORT_BREAK: ui_style_vec4(UI_BG_COLOR, ui->theme->color_green); break;
                case POMO_PHASE_LONG_BREAK:  ui_style_vec4(UI_BG_COLOR, ui->theme->color_green); break;
                }
            }

            U64 seconds  = (pomo->remaining / 1000) % 60;
            U64 minutes  = (pomo->remaining / 60000) % 60;
            U64 hours    = pomo->remaining / 3600000;
            String label = astr_fmt(tm, "%02lu:%02lu:%02lu%c", hours, minutes, seconds, 0);
            UiBox *clock = ui_label_extra(UI_BOX_REACTIVE, "clock", label, ui->config->font_path_normal, (pomo->clock_size == 0.0) ? ui->config->font_size : pomo->clock_size, false);
            if (clock->signals.hovered && ui->event->tag == EVENT_SCROLL) {
                pomo->clock_size += 5 * ui->event->y;
                pomo->clock_size = clamp(pomo->clock_size, 0, INFINITY);
                save_config();
            }

            ui_hspacer();

            if (ui_within_box(box->rect, ui->mouse) || (ui->focused && ui_is_descendant(box, ui->focused))) {
                ui_box(0, "autohide_icons") {
                    UiBox *edit_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "edit") {
                        ui_tag("button");
                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_EDIT);
                        if (edit_button->signals.clicked) push_command(.tag=CMD_VIEW_EDITOR, .pomo=pomo);
                    }

                    UiBox *delete_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "delete") {
                        ui_tag("button");
                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_TRASH);
                        if (delete_button->signals.clicked) {
                            push_command(.tag=CMD_DEL, .pomo=pomo);
                            if (out_card_deleted) *out_card_deleted = true;
                        }
                    }
                }
            }
        }

        ui_box(0, "body") {
            ui_button_group(str("buttons")) {
                if (pomo->running) {
                    UiBox *pause_button = ui_button(str("pause")) {
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                        ui_label(UI_BOX_CLICK_THROUGH, "label", str("Pause"));
                        if (pause_button->signals.clicked) push_command(.tag=CMD_PAUSE, .pomo=pomo);
                    }
                } else {
                    UiBox *start_button = ui_button(str("start")) {
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                        ui_label(UI_BOX_CLICK_THROUGH, "label", str("Start"));
                        if (start_button->signals.clicked) push_command(.tag=CMD_START, .pomo=pomo);
                    }
                }
            }

            ui_box(UI_BOX_INVISIBLE_BG, "stats") {
                ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

                ui_box(0, "phase") {
                    ui_tag("row");
                    ui_label(0, "title", str("Phase"));
                    ui_hspacer();
                    ui_label(0, "val", array_get(&context->phase_strings, pomo->phase));
                }

                ui_box(0, "completed_pomos") {
                    ui_tag("row");
                    ui_label(0, "title", str("Completed pomos"));
                    ui_hspacer();
                    ui_label(0, "val", astr_fmt(tm, "%lu", pomo->n_completed_pomos));
                }

                ui_box(0, "pomos_until_long_break") {
                    ui_tag("row");
                    ui_label(0, "title", str("Pomos until long break"));
                    ui_hspacer();
                    ui_label(0, "val", astr_fmt(tm, "%lu", pomo->pomos_until_long_break));
                }
            }

            ui_markup_view(str("message"), pomo->message, 0);
        }
    }
}

static Void build_view_search () {
    Auto view = &context->view.search;

    ui_scroll_box(str("left_box"), true) {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, .35, 1});

        ui_entry(str("entry"), view->buf, -1, str("Search pomodoros..."));

        ui_box(UI_BOX_INVISIBLE_BG, "row_group") {
            ui_box(0, "row") {
                ui_tag("row");
                ui_label(0, "title", str("Delete searched pomodoros"));
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
                        array_iter_back (it, &view->searched) push_command(.tag=CMD_DEL, .pomo=array_get(&context->pomodoros, it.idx), .skip_config_save=true);
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
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

        if (view->buf_version != buf_get_version(view->buf)) {
            view->searched.count = 0;
            view->buf_version = buf_get_version(view->buf);

            tmem_new(tm);
            String needle = buf_get_str(view->buf, tm);

            array_iter (pomo, &context->pomodoros) {
                I64 score = str_fuzzy_search(needle, pomo->message, 0);
                if (score != INT64_MIN) array_push_lit(&view->searched, .score=score, .idx=ARRAY_IDX);
            }

            array_sort_cmp(&view->searched, app_cmp_search_results);
        }

        Bool deleted = false;
        array_iter (it, &view->searched) build_pomo(array_get(&context->pomodoros, it.idx), &deleted);
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

            editor = ui_ted_resizable(str("editor"), view->buf, &context->editor_width, &context->editor_height, LINE_WRAP_NONE);
            ui_style_box_vec4(editor, UI_RADIUS, vec4(0, 0, r, r));
            ui_style_box_u32(editor, UI_ANIMATION, UI_MASK_WIDTH);
        }

        if (w != context->editor_width || h != context->editor_height) save_config();

        ui_box(UI_BOX_INVISIBLE_BG, "rows1") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            ui_box(0, "work_length") {
                ui_tag("row");
                ui_label(0, "title", str("Work length"));
                ui_hspacer();
                Time val = os_ms_to_time(view->pomo.work_length);
                UiBox *picker = ui_time_picker(str("picker"), &val, TIME_PICKER_HMS);
                picker->next_style.size.width.strictness = 1;
                view->pomo.work_length = os_time_to_ms(val);
            }

            ui_box(0, "short_break_length") {
                ui_tag("row");
                ui_label(0, "title", str("Short break length"));
                ui_hspacer();
                Time val = os_ms_to_time(view->pomo.short_break_length);
                UiBox *picker = ui_time_picker(str("picker"), &val, TIME_PICKER_HMS);
                picker->next_style.size.width.strictness = 1;
                view->pomo.short_break_length = os_time_to_ms(val);
            }

            ui_box(0, "long_break_length") {
                ui_tag("row");
                ui_label(0, "title", str("Long break length"));
                ui_hspacer();
                Time val = os_ms_to_time(view->pomo.long_break_length);
                UiBox *picker = ui_time_picker(str("picker"), &val, TIME_PICKER_HMS);
                picker->next_style.size.width.strictness = 1;
                view->pomo.long_break_length = os_time_to_ms(val);
            }

            ui_box(0, "sound_file") {
                ui_tag("row");
                ui_label(0, "title", str("Sound"));
                ui_hspacer();
                UiBox *picker = ui_file_picker_entry(str("picker"), view->sound_buf, str("Select sound file..."), 25, false, false, str("data/sounds/"));
                picker->next_style.size.width.strictness = 1;
            }

            ui_box(0, "clock_size") {
                ui_tag("row");
                ui_label(0, "title", str("Clock size"));
                ui_hspacer();
                F64 val = view->pomo.clock_size;
                UiBox *picker = ui_f64_picker(str("picker"), &val, 0, INFINITY, 4);
                picker->next_style.size.width.strictness = 1;
                view->pomo.clock_size = val;
            }
        }

        ui_box(UI_BOX_INVISIBLE_BG, "rows2") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            ui_box(0, "phase") {
                ui_tag("row");
                ui_label(0, "title", str("Phase"));
                ui_hspacer();
                U64 val = view->pomo.phase;
                UiBox *picker = ui_dropdown(str("picker"), &val, context->phase_strings);
                picker->next_style.size.width.strictness = 1;
                view->pomo.phase = val;
            }

            ui_box(0, "completed_pomos") {
                ui_tag("row");
                ui_label(0, "title", str("Completed pomos"));
                ui_hspacer();
                I64 val = view->pomo.n_completed_pomos;
                UiBox *picker = ui_int_picker(str("picker"), &val, 0, INT64_MAX, 3);
                picker->next_style.size.width.strictness = 1;
                view->pomo.n_completed_pomos = val;
            }

            ui_box(0, "long_break_every") {
                ui_tag("row");
                ui_label(0, "title", str("Long break every n pomos"));
                ui_hspacer();
                I64 val = view->pomo.long_break_every_n_pomos;
                UiBox *picker = ui_int_picker(str("picker"), &val, 0, INT64_MAX, 3);
                picker->next_style.size.width.strictness = 1;
                view->pomo.long_break_every_n_pomos = val;
            }

            ui_box(0, "pomos_until_long_break") {
                ui_tag("row");
                ui_label(0, "title", str("Pomos until long break"));
                ui_hspacer();
                I64 val = view->pomo.pomos_until_long_break;
                UiBox *picker = ui_int_picker(str("picker"), &val, 0, INT64_MAX, 3);
                picker->next_style.size.width.strictness = 1;
                view->pomo.pomos_until_long_break = val;
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
                    view->pomo.message = buf_get_str(view->buf, context->config_mem);
                    view->pomo.sound_file = buf_get_str(view->sound_buf, context->config_mem);
                    if (view->pomo_to_edit) push_command(.tag=CMD_DEL, .pomo=view->pomo_to_edit, .skip_config_save=true);
                    Pomodoro *new_pomo = mem_new(context->config_mem, Pomodoro);
                    *new_pomo = view->pomo;
                    push_command(.tag=CMD_ADD, .pomo=new_pomo);
                    push_command(.tag=CMD_VIEW_MAIN);
                }
            }
        }
    }

    UiBox *scrollbox = ui_scroll_box(str("right_box"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

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
            if (add_button->signals.clicked) push_command(.tag=CMD_VIEW_EDITOR);
            if (add_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Add pomodoro")); }
        }

        UiBox *search_button = ui_button(str("search")) {
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_SEARCH);
            if (search_button->signals.clicked) push_command(.tag=CMD_VIEW_SEARCH);
            if (search_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Search pomodoros")); }
        }
    }

    ui_scroll_box(str("cards"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        array_iter (pomo, &context->pomodoros) build_pomo(pomo, 0);
    }
}

Void pomodoro_view_init (UiViewInstance *) {
}

Void pomodoro_view_free (UiViewInstance *) {
}

UiIcon pomodoro_view_get_icon (UiViewInstance *, Bool visible) {
    return UI_ICON_POMODORO;
}

String pomodoro_view_get_title (UiViewInstance *, Bool visible) {
    return str("Pomodoro");
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
    array_iter (cmd, &context->commands, *) {
        switch (cmd->tag) {
        case CMD_SAVE_CONFIG: {
            save_config();
        } break;

        case CMD_ADD: {
            array_push(&context->pomodoros, cmd->pomo);
            if (! cmd->skip_config_save) save_config();
        } break;

        case CMD_DEL: {
            destroy_sound(cmd->pomo);
            array_find_remove(&context->pomodoros, IT == cmd->pomo);
            context->config_mem_fragmentation++;
            if (! cmd->skip_config_save) save_config();
        } break;

        case CMD_START: {
            assert_dbg(! cmd->pomo->running);
            cmd->pomo->running = true;
            cmd->pomo->start = os_get_time_ms();
            if (context->n_running == 0) context->tick_id = win_tick_start(1000);
            context->n_running++;
        } break;

        case CMD_PAUSE: {
            assert_dbg(cmd->pomo->running);
            destroy_sound(cmd->pomo);
            cmd->pomo->running = false;
            context->n_running--;
            if (context->n_running == 0) win_tick_end(context->tick_id);
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
            Pomodoro *pomo = cmd->pomo;
            context->view.editor.pomo_to_edit = pomo;
            if (pomo) destroy_sound(pomo);
            context->view.editor.buf = buf_new(context->view_mem, pomo ? pomo->message : str(""));
            context->view.editor.sound_buf = buf_new(context->view_mem, pomo ? pomo->sound_file : str(""));
            if (pomo) context->view.editor.pomo = *pomo;
        } break;
        }
    }

    context->commands.count = 0;
}

Void pomodoro_view_build (UiViewInstance *, Bool visible) {
    execute_commands();
    if (context->config_mem_fragmentation > 100) {
        load_config();
        push_command(.tag=CMD_VIEW_MAIN);
        execute_commands();
    }

    array_iter (pomo, &context->pomodoros) {
        if (! pomo->running) continue;

        U64 now = os_get_time_ms();
        pomo->remaining = sat_sub64(pomo->remaining, now - pomo->start);
        pomo->start = now;

        if (pomo->remaining == 0) {
            if (pomo->phase != POMO_PHASE_WORK) {
                pomo->phase = POMO_PHASE_WORK;
                pomo->remaining = pomo->work_length;
            } else if (pomo->pomos_until_long_break == 1) {
                pomo->n_completed_pomos++;
                pomo->phase = POMO_PHASE_LONG_BREAK;
                pomo->remaining = pomo->long_break_length;
                pomo->pomos_until_long_break = pomo->long_break_every_n_pomos;
            } else {
                pomo->n_completed_pomos++;
                pomo->phase = POMO_PHASE_SHORT_BREAK;
                pomo->remaining = pomo->short_break_length;
                pomo->pomos_until_long_break = sat_sub64(pomo->pomos_until_long_break, 1);
            }

            destroy_sound(pomo);
            play_sound(pomo);

            array_find_remove(&context->pomodoros, IT == pomo);
            array_insert(&context->pomodoros, pomo, 0);
        }
    }

    switch (context->view.tag) {
    case VIEW_MAIN:   build_view_main(); break;
    case VIEW_SEARCH: build_view_search(); break;
    case VIEW_EDITOR: build_view_editor(); break;
    }
}

Void pomodoro_init () {
    if (context) return;

    context = mem_new(mem_root, Context);
    context->config_version = 0;
    context->view_mem = cast(Mem*, arena_new(mem_root, 1*KB));
    context->config_mem = cast(Mem*, arena_new(mem_root, 1*KB));
    array_init(&context->commands, mem_root);

    { // Build phase strings:
        ArrayString a;
        array_init(&a, mem_root);
        array_push(&a, astr_fmt(mem_root, "Work"));
        array_push(&a, astr_fmt(mem_root, "Short Break"));
        array_push(&a, astr_fmt(mem_root, "Long Break"));
        context->phase_strings = a.as_slice;
    }

    { // Build config file path:
        tmem_new(tm);
        AString a = astr_new(mem_root);
        astr_push_str(&a, fs_get_home_dir(tm));
        astr_push_cstr(&a, "/.config/chronomi/pomodoro.txt");
        context->config_file_path = astr_to_str(&a);
    }

    load_config();
    push_command(.tag=CMD_VIEW_MAIN);
}

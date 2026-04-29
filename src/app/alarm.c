#include "app/app.h"
#include "ui/ui_widgets.h"
#include "ui/ui_markup_view.h"
#include "ui/ui_text_editor.h"
#include "config/config.h"
#include "buffer/buffer.h"
#include "app/alarm.h"
#include "os/time.h"

istruct (Alarm) {
    Minutes time;
    Minutes snooze;
    String message;
    String sound;
    Bool enabled;
    Bool playing;
    U8 days; // bitflags; least sig bit = Sunday
    Sound *sound_id;
};

istruct (SnoozedAlarm) {
    Minutes time;
    U64 idx;
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
            U64 editor_cursor;
            U64 alarm_idx;
            Alarm alarm;
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
    CMD_TOGGLE,
    CMD_DISMISS,
    CMD_SNOOZE,
    CMD_SAVE_CONFIG,
    CMD_VIEW_MAIN,
    CMD_VIEW_EDITOR,
    CMD_VIEW_SEARCH,
};

istruct (Command) {
    CommandTag tag;
    Bool skip_config_save;
    U64 idx;
    Alarm alarm;
};

istruct (Context) {
    View view;
    Mem *view_mem;
    U64 config_version;
    String config_file_path;
    Array(Command) commands;
    Mem *config_mem;
    U64 config_mem_fragmentation;
    Array(Alarm) alarms;
    Array(SnoozedAlarm) snoozed_alarms;
    F32 editor_width;
    F32 editor_height;
    TickId tick_id;
    Minutes now;
};

static Context *context;

#define push_command(...) array_push_lit(&context->commands, __VA_ARGS__)

static Void destroy_sound (U64 idx) {
    Alarm *alarm = array_ref(&context->alarms, idx);
    if (! alarm->sound_id) return;
    win_sound_cancel(alarm->sound_id);
    mem_free(mem_root, .old_ptr=alarm->sound_id);
    alarm->sound_id = 0;
}

static Void play_sound (U64 idx) {
    Alarm *alarm = array_ref(&context->alarms, idx);
    if (alarm->sound_id) return;
    if (! alarm->sound.count) return;
    alarm->sound_id = win_sound_play(mem_root, alarm->sound, true);
}

static Void save_config () {
    tmem_new(tm);

    AString astr = astr_new(tm);
    astr_push_fmt(&astr, "version = %lu\n", context->config_version);
    astr_push_fmt(&astr, "editor_width = %f\n", context->editor_width);
    astr_push_fmt(&astr, "editor_height = %f\n", context->editor_height);
    astr_push_cstr(&astr, "alarms = [\n");
    array_iter (alarm, &context->alarms, *) {
        astr_push_cstr(&astr, "    {\n");
        astr_push_fmt(&astr,  "        message = \"%.*s\"\n", STR(alarm->message));
        astr_push_fmt(&astr,  "        sound = \"%.*s\"\n", STR(alarm->sound));
        astr_push_fmt(&astr,  "        time = %lu\n", alarm->time);
        astr_push_fmt(&astr,  "        snooze = %lu\n", alarm->snooze);
        astr_push_fmt(&astr,  "        enabled = %s\n", alarm->enabled ? "true" : "false");
        astr_push_fmt(&astr,  "        days = %u\n", alarm->days);
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
    array_init(&context->alarms, context->config_mem);

    fs_ensure_file(context->config_file_path, str("data/settings/alarm.txt"));

    Config *cfg = config_parse(tm, context->config_file_path);

    U64 disk_version = config_get_u64(cfg, cfg->root, "version");
    if (disk_version != context->config_version) {
        fs_delete_file(context->config_file_path);
        load_config();
        return;
    }

    context->editor_width  = config_get_f64(cfg, cfg->root, "editor_width");
    context->editor_height = config_get_f64(cfg, cfg->root, "editor_height");

    ConfigAst *alarms = config_get_array(cfg, cfg->root, "alarms");
    array_iter (alarm_ast, &alarms->children) {
        Alarm *alarm    = array_push_slot(&context->alarms);
        alarm->message  = config_get_string(cfg, alarm_ast, "message", context->config_mem);
        alarm->sound    = config_get_string(cfg, alarm_ast, "sound", context->config_mem);
        alarm->time     = config_get_u64(cfg, alarm_ast, "time");
        alarm->snooze   = config_get_u64(cfg, alarm_ast, "snooze");
        alarm->enabled  = config_get_bool(cfg, alarm_ast, "enabled");
        alarm->days     = config_get_u64(cfg, alarm_ast, "days");
        alarm->playing  = false;
        alarm->sound_id = 0;
    }
}

static Void build_alarm (U64 idx, Bool *out_card_deleted) {
    tmem_new(tm);

    Alarm *alarm = array_ref(&context->alarms, idx);

    UiBox *box = ui_box_fmt(UI_BOX_CAN_FOCUS, "card%lu", idx) {
        ui_tag("card");

        if (alarm->playing) {
            ui_style_rule(".card") ui_style_vec4(UI_BG_COLOR, ui->theme->color_red);
        }

        ui_box(0, "header") {
            String label = astr_fmt(tm, "%02lu:%02lu", alarm->time / 60, alarm->time % 60);
            ui_label(UI_BOX_REACTIVE, "clock", label);

            ui_hspacer();

            if (ui_within_box(box->rect, ui->mouse) || (ui->focused && ui_is_descendant(box, ui->focused))) {
                ui_box(0, "autohide_icons") {
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

            ui_toggle("toggle", &alarm->enabled);
        }

        ui_box(0, "body") {
            if (alarm->playing) {
                ui_button_group(str("buttons")) {
                    UiBox *dismiss_button = ui_button(str("dismiss")) {
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                        ui_label(UI_BOX_CLICK_THROUGH, "label", str("Dismiss"));
                        if (dismiss_button->signals.clicked) push_command(.tag=CMD_DISMISS, .idx=idx);
                    }

                    UiBox *snooze_button = ui_button(str("snooze")) {
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                        ui_label(UI_BOX_CLICK_THROUGH, "label", str("Snooze"));
                        if (snooze_button->signals.clicked) push_command(.tag=CMD_SNOOZE, .idx=idx);
                    }
                }
            }

            ui_markup_view(str("message"), alarm->message, 0);
        }
    }
}

static Void build_view_search () {
    Auto view = &context->view.search;

    ui_scroll_box(str("left_box"), true) {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, .35, 1});

        ui_entry(str("entry"), view->buf, -1, str("Search alarms..."));

        ui_box(UI_BOX_INVISIBLE_BG, "row_group") {
            ui_box(0, "row") {
                ui_tag("row");
                ui_label(0, "title", str("Delete searched alarms"));
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

            array_iter (alarm, &context->alarms, *) {
                I64 score = str_fuzzy_search(needle, alarm->message, 0);
                if (score != INT64_MIN) array_push_lit(&view->searched, .score=score, .idx=ARRAY_IDX);
            }

            array_sort_cmp(&view->searched, app_cmp_search_results);
        }

        Bool deleted = false;
        array_iter (card, &view->searched, *) build_alarm(card->idx, &deleted);
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

        ui_days_picker(str("days_picker"), &view->alarm.days);

        ui_box(UI_BOX_INVISIBLE_BG, "rows") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            ui_box(0, "time") {
                ui_tag("row");
                ui_label(0, "title", str("Time"));
                ui_hspacer();
                Time val = { .hours = view->alarm.time / 60, .minutes = view->alarm.time % 60 };
                UiBox *picker = ui_time_picker(str("picker"), &val, TIME_PICKER_ALARM);
                picker->next_style.size.width.strictness = 1;
                view->alarm.time = val.hours*60 + val.minutes;
            }

            ui_box(0, "snooze_time") {
                ui_tag("row");
                ui_label(0, "title", str("Snooze time (in minutes)"));
                ui_hspacer();
                I64 val = view->alarm.snooze;
                UiBox *picker = ui_int_picker(str("picker"), &val, 0, INT64_MAX, 3);
                picker->next_style.size.width.strictness = 1;
                view->alarm.snooze = val;
            }

            ui_box(0, "sound") {
                ui_tag("row");
                ui_label(0, "title", str("Sound"));
                ui_hspacer();
                UiBox *picker = ui_file_picker_entry(str("picker"), view->sound_buf, str("Select sound file..."), 25, false, false, str("data/sounds/"));
                picker->next_style.size.width.strictness = 1;
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
                    if (view->alarm_idx != ARRAY_NIL_IDX) push_command(.tag=CMD_DEL, .idx=view->alarm_idx, .skip_config_save=true);
                    view->alarm.message = buf_get_str(view->buf, context->config_mem),
                    view->alarm.sound = buf_get_str(view->sound_buf, context->config_mem),
                    push_command(.tag=CMD_ADD, .alarm=view->alarm);
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
            if (add_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Add alarm")); }
        }

        UiBox *search_button = ui_button(str("search")) {
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_SEARCH);
            if (search_button->signals.clicked) push_command(.tag=CMD_VIEW_SEARCH);
            if (search_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Search alarms")); }
        }
    }

    ui_scroll_box(str("cards"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        array_iter (_, &context->alarms, *) { _; build_alarm(ARRAY_IDX, 0); }
    }
}

Void alarm_view_init (UiViewInstance *) {
}

Void alarm_view_free (UiViewInstance *) {
}

UiIcon alarm_view_get_icon (UiViewInstance *, Bool visible) {
    return UI_ICON_ALARM;
}

String alarm_view_get_title (UiViewInstance *, Bool visible) {
    return str("Alarm");
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
            array_push(&context->alarms, cmd->alarm);
            if (! cmd->skip_config_save) save_config();
        } break;

        case CMD_DEL: {
            destroy_sound(cmd->idx);
            array_find_remove_fast(&context->snoozed_alarms, IT.idx == cmd->idx);
            array_remove(&context->alarms, cmd->idx);
            context->config_mem_fragmentation++;
            if (! cmd->skip_config_save) save_config();
        } break;

        case CMD_TOGGLE: {
            Alarm *alarm = array_ref(&context->alarms, cmd->idx);
            alarm->enabled = !alarm->enabled;
            if (! cmd->skip_config_save) save_config();
        } break;

        case CMD_DISMISS: {
            Alarm *alarm = array_ref(&context->alarms, cmd->idx);
            alarm->playing = false;
            destroy_sound(cmd->idx);
            array_find_remove_fast(&context->snoozed_alarms, IT.idx == cmd->idx);
        } break;

        case CMD_SNOOZE: {
            Alarm *alarm = array_ref(&context->alarms, cmd->idx);
            alarm->playing = false;
            destroy_sound(cmd->idx);
            array_find_remove_fast(&context->snoozed_alarms, IT.idx == cmd->idx);
            Time time = os_get_wall_time();
            Minutes now = time.hours*60 + time.minutes;
            Minutes then = (now + alarm->snooze) % (24 * 60);
            array_push_lit(&context->snoozed_alarms, .time=then, .idx=cmd->idx);
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
            Alarm *alarm = (cmd->idx != ARRAY_NIL_IDX) ? array_ref(&context->alarms, cmd->idx) : 0;
            if (alarm) {
                context->view.editor.alarm = *alarm;
                destroy_sound(cmd->idx);
                array_find_remove_fast(&context->snoozed_alarms, IT.idx == cmd->idx);
            } else {
                context->view.editor.alarm.days = 255;
                context->view.editor.alarm.enabled = true;
            }
            context->view.editor.alarm_idx = cmd->idx;
            context->view.editor.buf = buf_new(context->view_mem, alarm ? alarm->message : str(""));
            context->view.editor.sound_buf = buf_new(context->view_mem, alarm ? alarm->sound : str(""));
        } break;
        }
    }

    context->commands.count = 0;
}

static Void tick () {
    Date date   = os_get_date();
    Time time   = os_get_wall_time();
    U8 day_flag = (1 << date.wday);
    Minutes now = time.hours*60 + time.minutes;

    if (now == context->now) {
        return; // Tick once per minute
    } else {
        context->now = now;
    }

    array_iter (alarm, &context->alarms, *) {
        if ((alarm->time == now) && alarm->enabled && (alarm->days & day_flag) && !alarm->playing) {
            play_sound(ARRAY_IDX);
            alarm->playing = true;
            Alarm a = *alarm;
            array_remove(&context->alarms, ARRAY_IDX);
            array_insert(&context->alarms, a, 0);
        }
    }

    array_iter (alarm, &context->snoozed_alarms, *) {
        if (alarm->time == now) {
            play_sound(alarm->idx);
            Alarm *a = array_ref(&context->alarms, alarm->idx);
            a->playing = true;
            array_remove_fast(&context->snoozed_alarms, ARRAY_IDX--);
            Alarm av = *a;
            array_remove(&context->alarms, alarm->idx);
            array_insert(&context->alarms, av, 0);
        }
    }
}

Void alarm_view_build (UiViewInstance *, Bool visible) {
    execute_commands();
    if (context->config_mem_fragmentation > 100) load_config();
    if (context->tick_id == 0) context->tick_id = win_tick_start(2000);
    tick();

    switch (context->view.tag) {
    case VIEW_MAIN:   build_view_main(); break;
    case VIEW_SEARCH: build_view_search(); break;
    case VIEW_EDITOR: build_view_editor(); break;
    }
}

Void alarm_init () {
    if (context) return;

    context = mem_new(mem_root, Context);
    context->config_version = 0;
    context->view_mem = cast(Mem*, arena_new(mem_root, 1*KB));
    context->config_mem = cast(Mem*, arena_new(mem_root, 1*KB));
    array_init(&context->commands, mem_root);
    array_init(&context->snoozed_alarms, mem_root);

    { // Build config file path:
        tmem_new(tm);
        AString a = astr_new(mem_root);
        astr_push_str(&a, fs_get_home_dir(tm));
        astr_push_cstr(&a, "/.config/chronomi/alarm.txt");
        context->config_file_path = astr_to_str(&a);
    }

    load_config();
    push_command(.tag=CMD_VIEW_MAIN);
}

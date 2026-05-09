#include "app/settings.h"
#include "ui/ui_widgets.h"
#include "config/config.h"
#include "app/app.h"

Void settings_view_init (UiViewInstance *view) {
}

Void settings_view_free (UiViewInstance *view) {
}

UiIcon settings_view_get_icon (UiViewInstance *view, Bool visible) {
    return UI_ICON_WRENCH;
}

String settings_view_get_title (UiViewInstance *view, Bool visible) {
    return str("Settings");
}

static Void build_global_settings () {
    F32 s = win_get_display_scale();

    UiBox *title = ui_label_extra(0, "global_settings", str("Global settings"), ui->config->font_path_normal, 1.25*ui->config->font_size, false);
    ui_style_box_size(title, UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 30*ui->config->font_size, 1});

    ui_box(0, "global_settings_rows") {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 30*ui->config->font_size, 1});

        ui_box(0, "icon_size") {
            ui_tag("row");
            ui_label(0, "title", str("Icon size"));
            ui_hspacer();
            F64 val = ui->config->icon_size/s;
            UiBox *picker = ui_f64_picker(str("picker"), &val, 0, INFINITY, 4);
            picker->next_style.size.width.strictness = 1;
            ui->config->icon_size = val*s;
        }

        ui_box(0, "font_size") {
            ui_tag("row");
            ui_label(0, "title", str("Font size"));
            ui_hspacer();
            F64 val = ui->config->font_size/s;
            UiBox *picker = ui_f64_picker(str("picker"), &val, 0, INFINITY, 4);
            picker->next_style.size.width.strictness = 1;
            ui->config->font_size = val*s;
        }

        ui_box(0, "font_path_normal") {
            ui_tag("row");
            ui_label(0, "title", str("Normal font"));
            ui_hspacer();
            Buf *buf = buf_new(ui->frame_mem, ui->config->font_path_normal);
            UiBox *picker = ui_file_picker_entry(str("picker"), buf, str("Select font file..."), 25, false, false, str("data/fonts/"));
            picker->next_style.size.width.strictness = 1;
            ui->config->font_path_normal = buf_get_str(buf, app->config_mem);
        }

        ui_box(0, "font_path_bold") {
            ui_tag("row");
            ui_label(0, "title", str("Bold font"));
            ui_hspacer();
            Buf *buf = buf_new(ui->frame_mem, ui->config->font_path_bold);
            UiBox *picker = ui_file_picker_entry(str("picker"), buf, str("Select font file..."), 25, false, false, str("data/fonts/"));
            picker->next_style.size.width.strictness = 1;
            ui->config->font_path_bold = buf_get_str(buf, app->config_mem);
        }

        ui_box(0, "font_path_italic") {
            ui_tag("row");
            ui_label(0, "title", str("Italic font"));
            ui_hspacer();
            Buf *buf = buf_new(ui->frame_mem, ui->config->font_path_italic);
            UiBox *picker = ui_file_picker_entry(str("picker"), buf, str("Select font file..."), 25, false, false, str("data/fonts/"));
            picker->next_style.size.width.strictness = 1;
            ui->config->font_path_italic = buf_get_str(buf, app->config_mem);
        }

        ui_box(0, "font_path_mono") {
            ui_tag("row");
            ui_label(0, "title", str("Monospace font"));
            ui_hspacer();
            Buf *buf = buf_new(ui->frame_mem, ui->config->font_path_mono);
            UiBox *picker = ui_file_picker_entry(str("picker"), buf, str("Select font file..."), 25, false, false, str("data/fonts/"));
            picker->next_style.size.width.strictness = 1;
            ui->config->font_path_mono = buf_get_str(buf, app->config_mem);
        }

        ui_box(0, "font_path_icons") {
            ui_tag("row");
            ui_label(0, "title", str("Icons font"));
            ui_hspacer();
            Buf *buf = buf_new(ui->frame_mem, ui->config->font_path_icons);
            UiBox *picker = ui_file_picker_entry(str("picker"), buf, str("Select font file..."), 25, false, false, str("data/fonts/"));
            picker->next_style.size.width.strictness = 1;
            ui->config->font_path_icons = buf_get_str(buf, app->config_mem);
        }

        ui_box(0, "show_more_inc") {
            ui_tag("row");
            ui_label(0, "title", str("Show more increment"));
            ui_hspacer();

            String info = str("Number of extra items to show when a `Show more` button is pressed.");
            UiBox *info_button = ui_button_info_popup(str("info_button"), false, info, false);
            info_button->next_style.size.width.strictness = 1;

            I64 val = ui->config->show_more_inc;
            UiBox *picker = ui_int_picker(str("picker"), &val, 0, INT64_MAX, 4);
            picker->next_style.size.width.strictness = 1;
            ui->config->show_more_inc = val;
        }

        ui_box(0, "card_width") {
            ui_tag("row");
            ui_label(0, "title", str("Card width"));
            ui_hspacer();

            String info = str("The width of various ui elements like todo cards, flash cards, etc...");
            UiBox *info_button = ui_button_info_popup(str("info_button"), false, info, false);
            info_button->next_style.size.width.strictness = 1;

            I64 val = ui->config->card_width/s;
            UiBox *picker = ui_int_picker(str("picker"), &val, 0, INT64_MAX, 4);
            picker->next_style.size.width.strictness = 1;
            ui->config->card_width = val*s;
        }

        ui_box(0, "animation_time") {
            ui_tag("row");
            ui_label(0, "title", str("Animation duration"));
            ui_hspacer();
            F64 val = ui->config->animation_time;
            UiBox *picker = ui_f64_picker(str("picker"), &val, 0, INFINITY, 4);
            picker->next_style.size.width.strictness = 1;
            ui->config->animation_time = val;
        }

        ui_box(0, "tab_width") {
            ui_tag("row");
            ui_label(0, "title", str("Tab width (in spaces)"));
            ui_hspacer();
            I64 val = ui->config->tab_width;
            UiBox *picker = ui_int_picker(str("picker"), &val, 0, INT64_MAX, 4);
            picker->next_style.size.width.strictness = 1;
            ui->config->tab_width = val;
        }

        ui_box(0, "line_spacing") {
            ui_tag("row");
            ui_label(0, "title", str("Line spacing"));
            ui_hspacer();
            F64 val = ui->config->line_spacing/s;
            UiBox *picker = ui_f64_picker(str("picker"), &val, 0, INFINITY, 4);
            picker->next_style.size.width.strictness = 1;
            ui->config->line_spacing = val*s;
        }

        ui_box(0, "scrollbar_width") {
            ui_tag("row");
            ui_label(0, "title", str("Scrollbar width"));
            ui_hspacer();
            F64 val = ui->config->scrollbar_width/s;
            UiBox *picker = ui_f64_picker(str("picker"), &val, 0, INFINITY, 4);
            picker->next_style.size.width.strictness = 1;
            ui->config->scrollbar_width = val*s;
        }

        ui_box(0, "blur_strength") {
            ui_tag("row");
            ui_label(0, "title", str("Blur strength"));
            ui_hspacer();
            F64 val = ui->config->blur_strength;
            UiBox *picker = ui_f64_picker(str("picker"), &val, 0, INFINITY, 4);
            picker->next_style.size.width.strictness = 1;
            ui->config->blur_strength = val;
        }
    }
}

static Void build_window_settings () {
    F32 s = win_get_display_scale();
    UiBox *title = ui_label_extra(0, "window_settings", str("Window settings"), ui->config->font_path_normal, 1.25*ui->config->font_size, false);
    ui_style_box_size(title, UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 30*ui->config->font_size, 1});

    ui_box(0, "window_settings_rows") {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 30*ui->config->font_size, 1});

        ui_box(0, "show_titlebar") {
            ui_tag("row");
            ui_label(0, "title", str("Show title bar"));
            ui_hspacer();
            Bool v = ui->config->show_titlebar;
            UiBox *picker = ui_toggle("picker", &v);
            picker->next_style.size.width.strictness = 1;
            ui->config->show_titlebar = v;
        }

        ui_box(0, "bg_titlebar") {
            ui_tag("row");
            ui_label(0, "title", str("Background color"));
            ui_hspacer();
            Vec4 v = rgba_to_hsva(ui->theme->bg_titlebar);
            UiBox *picker = ui_color_picker_button(str("picker"), &v.x, &v.y, &v.z, &v.w);
            picker->next_style.size.width.strictness = 1;
            ui->theme->bg_titlebar = hsva_to_rgba(v);
        }

        ui_box(0, "border_color_titlebar") {
            ui_tag("row");
            ui_label(0, "title", str("Border color"));
            ui_hspacer();
            Vec4 v = rgba_to_hsva(ui->theme->border_color_titlebar);
            UiBox *picker = ui_color_picker_button(str("picker"), &v.x, &v.y, &v.z, &v.w);
            picker->next_style.size.width.strictness = 1;
            ui->theme->border_color_titlebar = hsva_to_rgba(v);
        }

        ui_box(0, "padding") {
            ui_tag("row");
            ui_label(0, "title", str("Padding"));
            ui_hspacer();
            UiBox *box = ui_box (UI_BOX_INVISIBLE_BG, "pickers") {
                ui_style_f32(UI_SPACING, ui->theme->spacing);
                box->next_style.size.width.strictness = 1;

                F64 val = ui->theme->padding_titlebar.x/s;
                ui_f64_picker(str("picker1"), &val, 0, INFINITY, 3);
                ui->theme->padding_titlebar.x = val*s;

                val = ui->theme->padding_titlebar.y/s;
                ui_f64_picker(str("picker2"), &val, 0, INFINITY, 3);
                ui->theme->padding_titlebar.y = val*s;
            }
        }

        ui_box(0, "radius") {
            ui_tag("row");
            ui_label(0, "title", str("Corner radius"));
            ui_hspacer();
            F64 val = ui->theme->radius_titlebar/s;
            UiBox *picker = ui_f64_picker(str("picker"), &val, 0, INFINITY, 4);
            picker->next_style.size.width.strictness = 1;
            ui->theme->radius_titlebar = val*s;
        }
    }
}

static UiBox *build_theme_presets_picker (String id) {
    tmem_new(tm);

    UiBox *container = ui_button(id) {
        Bool opened = container->scratch;

        if (opened || container->signals.clicked) {
            ui_popup(str("popup"), &opened, false, container) {
                FsIter *it = fs_iter_new(tm, str("data/themes"), true, false);
                while (fs_iter_next(it)) {
                    if (! str_ends_with(it->current_full_path.as_slice, str(".txt"))) continue;

                    UiBox *button = ui_button(it->current_full_path.as_slice) {
                        ui_style_u32(UI_ALIGN_X, UI_ALIGN_START);
                        ui_style_vec4(UI_BG_COLOR, vec4(0, 0, 0, 0));
                        ui_style_vec4(UI_BORDER_COLOR, vec4(0, 0, 0, 0));
                        ui_style_vec4(UI_BG_COLOR2, vec4(-1, 0, 0, 0));
                        ui_style_f32(UI_OUTSET_SHADOW_WIDTH, 0);
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, ui_em(16), 1});
                        if (button->signals.hovered) ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);

                        String label = str_cut_suffix(it->current_file_name, str(".txt"));
                        label = str_replace_all(tm, label, str("_"), str(" "));
                        ui_label(UI_BOX_CLICK_THROUGH, "label", label);

                        ui_hspacer();

                        UiBox *preview = ui_box(0, "preview") {
                            Config *cfg = config_parse(tm, it->current_full_path.as_slice);
                            Vec4 bg_color = config_get_vec4(cfg, cfg->root, "bg_color_z2");
                            Vec4 fg_color = config_get_vec4(cfg, cfg->root, "text_color_normal");

                            ui_style_vec4(UI_BG_COLOR, bg_color);
                            ui_style_vec2(UI_PADDING, ui->theme->padding);
                            ui_style_vec4(UI_RADIUS, ui->theme->radius);
                            ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                            ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                            preview->next_style.size.width.strictness = 1;

                            UiBox *label = ui_label(0, "text", str("Lorem ipsum."));
                            ui_style_box_vec4(label, UI_TEXT_COLOR, fg_color);
                        }

                        if (button->signals.clicked) {
                            fs_delete_file(app->theme_file_path);
                            fs_ensure_file(app->theme_file_path, it->current_full_path.as_slice);
                            app_load_ui_theme(it->current_full_path.as_slice);
                        }
                    }
                }
            }

            ui_tag_box(container, "press");
        }

        ui_label(UI_BOX_CLICK_THROUGH, "label", str("Load preset"));
        container->scratch = opened;
    }

    return container;
}

static Void build_theme_settings () {
    F32 s = win_get_display_scale();
    UiBox *title = ui_label_extra(0, "theme_settings", str("Theme"), ui->config->font_path_normal, 1.25*ui->config->font_size, false);
    ui_style_box_size(title, UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 30*ui->config->font_size, 1});

    ui_box(0, "theme_settings_rows") {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 30*ui->config->font_size, 1});

        #define build_color_picker(FIELD, TITLE) ({\
            ui_box(0, #FIELD) {\
                ui_tag("row");\
                ui_label(0, "title", str(TITLE));\
                ui_hspacer();\
                Vec4 v = rgba_to_hsva(ui->theme->FIELD);\
                UiBox *picker = ui_color_picker_button(str("picker"), &v.x, &v.y, &v.z, &v.w);\
                picker->next_style.size.width.strictness = 1;\
                ui->theme->FIELD = hsva_to_rgba(v);\
            }\
        })

        ui_box(0, "presets") {
            ui_tag("row");
            ui_label(0, "title", str("Presets"));
            ui_hspacer();
            F64 val = ui->theme->spacing;
            UiBox *picker = build_theme_presets_picker(str("picker"));
            picker->next_style.size.width.strictness = 1;
            ui->theme->spacing = val;
        }

        ui_box(0, "spacing") {
            ui_tag("row");
            ui_label(0, "title", str("Spacing"));
            ui_hspacer();
            F64 val = ui->theme->spacing/s;
            UiBox *picker = ui_f64_picker(str("picker"), &val, 0, INFINITY, 4);
            picker->next_style.size.width.strictness = 1;
            ui->theme->spacing = val*s;
        }

        ui_box(0, "padding") {
            ui_tag("row");
            ui_label(0, "title", str("Padding"));
            ui_hspacer();
            UiBox *box = ui_box (UI_BOX_INVISIBLE_BG, "pickers") {
                ui_style_f32(UI_SPACING, ui->theme->spacing);
                box->next_style.size.width.strictness = 1;

                F64 val = ui->theme->padding.x/s;
                ui_f64_picker(str("picker1"), &val, 0, INFINITY, 3);
                ui->theme->padding.x = val*s;

                val = ui->theme->padding.y/s;
                ui_f64_picker(str("picker2"), &val, 0, INFINITY, 3);
                ui->theme->padding.y = val*s;
            }
        }

        ui_box(0, "radius") {
            ui_tag("row");
            ui_label(0, "title", str("Corner radius"));
            ui_hspacer();
            UiBox *box = ui_box (UI_BOX_INVISIBLE_BG, "pickers") {
                ui_style_f32(UI_SPACING, ui->theme->spacing);
                box->next_style.size.width.strictness = 1;

                F64 val = ui->theme->radius.x/s;
                ui_f64_picker(str("picker1"), &val, 0, INFINITY, 3);
                ui->theme->radius.x = val*s;

                val = ui->theme->radius.y/s;
                ui_f64_picker(str("picker2"), &val, 0, INFINITY, 3);
                ui->theme->radius.y = val*s;

                val = ui->theme->radius.z/s;
                ui_f64_picker(str("picker3"), &val, 0, INFINITY, 3);
                ui->theme->radius.z = val*s;

                val = ui->theme->radius.w/s;
                ui_f64_picker(str("picker4"), &val, 0, INFINITY, 3);
                ui->theme->radius.w = val*s;
            }
        }

        build_color_picker(border_color, "Border color");
        build_color_picker(border_color_focus, "Focus border color");

        ui_box(0, "border_widths") {
            ui_tag("row");
            ui_label(0, "title", str("Border widths"));
            ui_hspacer();
            UiBox *box = ui_box (UI_BOX_INVISIBLE_BG, "pickers") {
                ui_style_f32(UI_SPACING, ui->theme->spacing);
                box->next_style.size.width.strictness = 1;

                F64 val = ui->theme->border_width.x/s;
                ui_f64_picker(str("picker1"), &val, 0, INFINITY, 3);
                ui->theme->border_width.x = val*s;

                val = ui->theme->border_width.y/s;
                ui_f64_picker(str("picker2"), &val, 0, INFINITY, 3);
                ui->theme->border_width.y = val*s;

                val = ui->theme->border_width.z/s;
                ui_f64_picker(str("picker3"), &val, 0, INFINITY, 3);
                ui->theme->border_width.z = val*s;

                val = ui->theme->border_width.w/s;
                ui_f64_picker(str("picker4"), &val, 0, INFINITY, 3);
                ui->theme->border_width.w = val*s;
            }
        }

        ui_box(0, "border_focus_widths") {
            ui_tag("row");
            ui_label(0, "title", str("Border focus widths"));
            ui_hspacer();
            UiBox *box = ui_box (UI_BOX_INVISIBLE_BG, "pickers") {
                ui_style_f32(UI_SPACING, ui->theme->spacing);
                box->next_style.size.width.strictness = 1;

                F64 val = ui->theme->border_width_focus.x/s;
                ui_f64_picker(str("picker1"), &val, 0, INFINITY, 3);
                ui->theme->border_width_focus.x = val*s;

                val = ui->theme->border_width_focus.y/s;
                ui_f64_picker(str("picker2"), &val, 0, INFINITY, 3);
                ui->theme->border_width_focus.y = val*s;

                val = ui->theme->border_width_focus.z/s;
                ui_f64_picker(str("picker3"), &val, 0, INFINITY, 3);
                ui->theme->border_width_focus.z = val*s;

                val = ui->theme->border_width_focus.w/s;
                ui_f64_picker(str("picker4"), &val, 0, INFINITY, 3);
                ui->theme->border_width_focus.w = val*s;
            }
        }

        ui_box(0, "in_shadow_width") {
            ui_tag("row");
            ui_label(0, "title", str("Inset shadow width"));
            ui_hspacer();
            F64 val = ui->theme->in_shadow_width/s;
            UiBox *picker = ui_f64_picker(str("picker"), &val, 0, INFINITY, 4);
            picker->next_style.size.width.strictness = 1;
            ui->theme->in_shadow_width = val*s;
        }

        build_color_picker(in_shadow_color, "Inset shadow color");

        ui_box(0, "out_shadow_width") {
            ui_tag("row");
            ui_label(0, "title", str("Outset shadow width"));
            ui_hspacer();
            F64 val = ui->theme->out_shadow_width/s;
            UiBox *picker = ui_f64_picker(str("picker"), &val, 0, INFINITY, 4);
            picker->next_style.size.width.strictness = 1;
            ui->theme->out_shadow_width = val*s;
        }

        build_color_picker(out_shadow_color, "Outset shadow color");
        build_color_picker(text_color_normal, "Normal text color");
        build_color_picker(text_color_faint, "Faint text color");
        build_color_picker(text_color_inactive, "Inactive text color");
        build_color_picker(text_color_selection, "Selected text color");
        build_color_picker(text_color_red, "Red text color");
        build_color_picker(text_color_green, "Green text color");
        build_color_picker(text_color_blue, "Blue text color");
        build_color_picker(text_color_yellow, "Yellow text color");
        build_color_picker(color_accent, "Accent color");
        build_color_picker(color_red, "Red color");
        build_color_picker(color_green, "Green color");
        build_color_picker(color_blue, "Blue color");
        build_color_picker(color_yellow, "Yellow color");
        build_color_picker(bg_color_z1, "Bg color z1");
        build_color_picker(bg_color_z2, "Bg color z2");
        build_color_picker(bg_color_z3, "Bg color z3");
        build_color_picker(bg_color_selection, "Selection bg color");
        build_color_picker(bg_color_popup, "Popup bg color");
        build_color_picker(button_highlight_color, "Button highlight");
        build_color_picker(slider_knob_color, "Slider knob");
    }
}

Void settings_view_build (UiViewInstance *view, Bool visible) {
    ui_scroll_box(str("settings"), true) {
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

        EventTag event_tag = ui->event->tag;

        ui_style_rule(".row") ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);

        build_global_settings();
        build_window_settings();
        build_theme_settings();

        if (ui->event->tag != event_tag) app_config_save();
    }
}

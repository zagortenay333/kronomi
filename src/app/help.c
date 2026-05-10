#include "app/app.h"
#include "app/help.h"
#include "ui/ui_widgets.h"
#include "ui/ui_markup_view.h"
#include "buffer/buffer.h"

istruct (Context) {
    Buf *buf;
};

static Context *context;

Void help_view_build (UiViewInstance *, Bool visible) {
    tmem_new(tm);

    UiBox *container = ui_box(0, "container") {
        MarkupView *info = 0;

        UiBox *right_box = ui_scroll_box(str("right_box"), true) {
            ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
            ui_style_f32(UI_SPACING, ui->theme->spacing);
            ui_style_vec2(UI_PADDING, ui->theme->padding);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

            UiBox *markup = ui_markup_view_buf(str("markup"), context->buf, true, 0, 0);
            ui_style_box_size(markup, UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 40*ui->config->font_size, 1});
            info = ui_get_box_data(markup, 0, 0);
        }

        ui_scroll_box(str("left_box"), true) {
            ui_tag("sidebar");
            ui_style_rule(".sidebar") ui_style_f32(UI_SPACING, 0);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, .25, 1});

            array_iter (node, &info->ast->children) {
                if (node->tag != MARKUP_AST_HEADER) continue;

                U64 size = cast(MarkupAstHeader*, node)->size;
                String text = markup_ast_get_text(array_get(&node->children, 0), info->text);
                if (array_get_last(&text) == '\n') text.count--;

                AString a = astr_new(tm);
                astr_push_fmt(&a, "%*s", 8 * cast(Int, size - 1), "");
                astr_push_str(&a, text);
                astr_push_byte(&a, 0);

                UiBox *button = ui_button(astr_fmt(tm, "header%lu", ARRAY_IDX)) {
                    ui_style_u32(UI_ALIGN_X, UI_ALIGN_START);
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    ui_style_vec4(UI_BORDER_COLOR, vec4(0, 0, 0, 0));
                    ui_style_vec4(UI_BG_COLOR, vec4(0, 0, 0, 0));
                    ui_style_vec4(UI_BG_COLOR2, vec4(-1, 0, 0, 0));
                    ui_style_f32(UI_OUTSET_SHADOW_WIDTH, 0);
                    if (button->signals.hovered) ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z2);

                    ui_label(UI_BOX_CLICK_THROUGH, "label", astr_to_str(&a));

                    if (button->signals.clicked) {
                        UiBox *widget = map_get_ptr(&info->ast_to_box, node);
                        ui_scroll_to(right_box, widget, true);
                    }
                }
            }
        }

        array_swap(&container->children, 0, 1);
    }
}

Void help_view_init (UiViewInstance *) {
    if (! context) {
        tmem_new(tm);
        context = mem_new(mem_root, Context);
        AString a = astr_new(tm);
        astr_push_str(&a, fs_read_entire_file(tm, str("data/docs/help.txt"), 0));
        astr_push_str(&a, fs_read_entire_file(tm, str("data/docs/markup.txt"), 0));
        context->buf = buf_new(mem_root, astr_to_str(&a));
    }
}

Void help_view_free (UiViewInstance *) {
}

UiIcon help_view_get_icon (UiViewInstance *, Bool) {
    return UI_ICON_QUESTION;
}

String help_view_get_title (UiViewInstance *, Bool) {
    return str("Help");
}

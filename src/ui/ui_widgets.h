#pragma once

#include "base/core.h"
#include "ui/ui.h"
#include "buffer/buffer.h"
#include "os/time.h"

ienum (UiColorPickerMode, U8) {
    COLOR_PICKER_HEX,
    COLOR_PICKER_RGBA,
    COLOR_PICKER_HSVA,
};

ienum (UiTimePickerMode, U8) {
    TIME_PICKER_ALARM,
    TIME_PICKER_HM,
    TIME_PICKER_HMS,
    TIME_PICKER_HMSM,
};

istruct (UiGraphRecord) {
    U64 value;
};

istruct (UiGraphData) {
    U64 y_max;
    UiGraphRecord *hovered; // Set by widget. 0 if none.
    ArrayU64 guidelines;
    Array(UiGraphRecord) records;
};

UiBox *ui_hspacer              ();
UiBox *ui_vspacer              ();
UiBox *ui_label                (UiBoxFlags, CString id, String label);
UiBox *ui_label_extra          (UiBoxFlags, CString id, String label, String font_path, F32 font_size, Bool monospace);
UiBox *ui_line_graph           (String id, UiGraphData *);
UiBox *ui_icon                 (UiBoxFlags, CString id, U32 icon);
UiBox *ui_icon_button          (UiBoxFlags flags, CString id, U32 icon);
UiBox *ui_checkbox             (CString id, Bool *val);
UiBox *ui_image                (String id, Texture *, Bool blur, Vec4 tint, F32 pref_width);
UiBox *ui_toggle               (CString id, Bool *val);
UiBox *ui_button_group_push    (String id);
Void   ui_button_group_pop     ();
UiBox *ui_button_push          (String id);
Void   ui_button_pop           ();
UiBox *ui_button_info_popup    (String id, Bool sideways, String markup_filepath, Bool is_filepath);
UiBox *ui_button_label_str     (String id, String label);
UiBox *ui_button_label         (CString id);
UiBox *ui_vscroll_bar          (String id, Rect rect, F32 ratio, F32 *val);
UiBox *ui_hscroll_bar          (String id, Rect rect, F32 ratio, F32 *val);
Void   ui_scroll_to            (UiBox *container, UiBox *child, Bool to_top);
UiBox *ui_scroll_box_push      (String id, Bool show_scrollbars);
Void   ui_scroll_box_pop       ();
UiBox *ui_entry                (String id, Buf *buf, F32 width_in_chars, String hint);
UiBox *ui_shortcut_picker      (String id, Key *, KeyMod *);
UiBox *ui_int_picker           (String id, I64 *val, I64 min, I64 max, U8 width_in_chars);
UiBox *ui_f64_picker           (String id, F64 *val, F64 min, F64 max, U8 width_in_chars);
UiBox *ui_time_picker          (String id, Time *, UiTimePickerMode);
UiBox *ui_date_picker          (String id, Date *);
UiBox *ui_date_picker_entry    (String id, Date *, String hint, F32 width_in_chars);
UiBox *ui_days_picker          (String id, U8 *);
UiBox *ui_dropdown             (String id, U64 *selection, SliceString options);
UiBox *ui_slider_str           (String id, F32 *val);
UiBox *ui_slider               (CString id, F32 *val);
UiBox *ui_color_sat_val_picker (String id, F32 hue, F32 *sat, F32 *val);
UiBox *ui_color_hue_picker     (String id, F32 *hue);
UiBox *ui_color_alpha_picker   (String id, F32 *alpha);
UiBox *ui_color_picker         (String id, UiColorPickerMode mode, F32 *h, F32 *s, F32 *v, F32 *a);
UiBox *ui_color_picker_button  (String id, F32 *h, F32 *s, F32 *v, F32 *a);
UiBox *ui_file_picker          (String id, Buf *, Bool *shown, Bool multiple, Bool dir_only, String start_dir);
UiBox *ui_file_picker_entry    (String id, Buf *, String hint, F32 width_in_chars, Bool multiple, Bool dir_only, String start_dir);
UiBox *ui_popup_push           (String id, Bool *shown, Bool sideways, UiBox *anchor);
Void   ui_popup_pop            ();
UiBox *ui_tooltip_push         (String id);
Void   ui_tooltip_pop          ();
UiBox *ui_modal_push           (String id, Bool *shown);
Void   ui_modal_pop            ();
UiBox *ui_drawer_push          (String id, String title);
Void   ui_drawer_pop           ();

#define ui_grid(...)         ui_grid_push(__VA_ARGS__);         if (cleanup(ui_grid_pop_) U8 _; 1)
#define ui_modal(...)        ui_modal_push(__VA_ARGS__);        if (cleanup(ui_modal_pop_) U8 _; 1)
#define ui_popup(...)        ui_popup_push(__VA_ARGS__);        if (cleanup(ui_popup_pop_) U8 _; 1)
#define ui_drawer(...)       ui_drawer_push(__VA_ARGS__);       if (cleanup(ui_drawer_pop_) U8 _; 1)
#define ui_button(...)       ui_button_push(__VA_ARGS__);       if (cleanup(ui_button_pop_) U8 _; 1)
#define ui_tooltip(...)      ui_tooltip_push(__VA_ARGS__);      if (cleanup(ui_tooltip_pop_) U8 _; 1)
#define ui_scroll_box(...)   ui_scroll_box_push(__VA_ARGS__);   if (cleanup(ui_scroll_box_pop_) U8 _; 1)
#define ui_button_group(...) ui_button_group_push(__VA_ARGS__); if (cleanup(ui_button_group_pop_) U8 _; 1)

inl Void ui_modal_pop_        (Void *) { ui_modal_pop(); }
inl Void ui_popup_pop_        (Void *) { ui_popup_pop(); }
inl Void ui_button_pop_       (Void *) { ui_button_pop(); }
inl Void ui_drawer_pop_       (Void *) { ui_drawer_pop(); }
inl Void ui_tooltip_pop_      (Void *) { ui_tooltip_pop(); }
inl Void ui_scroll_box_pop_   (Void *) { ui_scroll_box_pop(); }
inl Void ui_button_group_pop_ (Void *) { ui_button_group_pop(); }

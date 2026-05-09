#pragma once

#include "base/core.h"
#include "base/math.h"
#include "font/font.h"
#include "window/window.h"

istruct (UiConfig) {
    String theme;
    F32 icon_size;
    F32 font_size;
    String font_path_normal;
    String font_path_bold;
    String font_path_italic;
    String font_path_mono;
    String font_path_icons;
    U64 show_more_inc;
    U64 card_width;
    F32 animation_time;
    Bool show_titlebar;
    U32 tab_width;
    F32 line_spacing;
    F32 scrollbar_width;
    F32 blur_strength;
};

istruct (UiTheme) {
    F32 spacing;
    Vec2 padding;
    Vec4 radius;
    Vec4 border_color;
    Vec4 border_color_focus;
    Vec4 border_width;
    Vec4 border_width_focus;
    F32 in_shadow_width;
    Vec4 in_shadow_color;
    F32 out_shadow_width;
    Vec4 out_shadow_color;
    Vec4 bg_titlebar;
    Vec2 padding_titlebar;
    F32 radius_titlebar;
    Vec4 border_color_titlebar;
    Vec4 text_color_normal;
    Vec4 text_color_faint;
    Vec4 text_color_inactive;
    Vec4 text_color_selection;
    Vec4 text_color_red;
    Vec4 text_color_green;
    Vec4 text_color_blue;
    Vec4 text_color_yellow;
    Vec4 color_accent;
    Vec4 color_red;
    Vec4 color_green;
    Vec4 color_blue;
    Vec4 color_yellow;
    Vec4 bg_color_z1;
    Vec4 bg_color_z2;
    Vec4 bg_color_z3;
    Vec4 bg_color_selection;
    Vec4 bg_color_popup;
    Vec4 button_highlight_color;
    Vec4 slider_knob_color;
};

ienum (UiIcon, U32) {
    UI_ICON_MOVE_TOP = 0xe900,
    UI_ICON_WRENCH,
    UI_ICON_UNDERSCORE,
    UI_ICON_TRASH,
    UI_ICON_TRANSLATE,
    UI_ICON_TODO,
    UI_ICON_TODO_LOADING,
    UI_ICON_TIME_TRACKER,
    UI_ICON_TIMER,
    UI_ICON_STRIKETHROUGH,
    UI_ICON_STOPWATCH,
    UI_ICON_START,
    UI_ICON_SORT_DESC,
    UI_ICON_SORT_ASC,
    UI_ICON_SEARCH,
    UI_ICON_QUESTION,
    UI_ICON_POMODORO,
    UI_ICON_PLUS,
    UI_ICON_PIN,
    UI_ICON_PAUSE,
    UI_ICON_PAN_UP,
    UI_ICON_PAN_RIGHT,
    UI_ICON_PAN_LEFT,
    UI_ICON_PAN_DOWN,
    UI_ICON_MINUS,
    UI_ICON_MARK,
    UI_ICON_LINK,
    UI_ICON_KANBAN,
    UI_ICON_ITALIC,
    UI_ICON_ISSUE,
    UI_ICON_IMPORT_EXPORT,
    UI_ICON_HOME,
    UI_ICON_HIDDEN,
    UI_ICON_HEATMAP,
    UI_ICON_HEADER,
    UI_ICON_HAMBURGER,
    UI_ICON_GRAPH,
    UI_ICON_GRAPH_INTERVAL,
    UI_ICON_FULLSCREEN,
    UI_ICON_FOLDER,
    UI_ICON_FLASH,
    UI_ICON_FIRE,
    UI_ICON_FILTER,
    UI_ICON_FILE,
    UI_ICON_EYE,
    UI_ICON_EYE_CLOSED,
    UI_ICON_EXAM,
    UI_ICON_EDIT,
    UI_ICON_CODE,
    UI_ICON_CLOSE,
    UI_ICON_CHECK,
    UI_ICON_BOLD,
    UI_ICON_ALARM,
    UI_ICON_NONE,
};

typedef U64 UiKey;

ienum (UiSizeTag, U8) {
    UI_SIZE_CUSTOM,
    UI_SIZE_PIXELS,
    UI_SIZE_PCT_PARENT,
    UI_SIZE_CHILDREN_SUM,
};

istruct (UiSize) {
    UiSizeTag tag;
    F32 value;
    F32 strictness;
};

ienum (UiAlign, U8) {
    UI_ALIGN_START,
    UI_ALIGN_MIDDLE,
    UI_ALIGN_END,
};

iunion (UiBoxSize) {
    struct { UiSize width, height; };
    UiSize v[2];
};

ienum (UiAxis, U8) {
    UI_AXIS_HORIZONTAL,
    UI_AXIS_VERTICAL,
};

assert_static(UI_AXIS_HORIZONTAL == 0);
assert_static(UI_AXIS_VERTICAL == 1);

ienum (UiStyleAttribute, U32) {
    UI_WIDTH,
    UI_HEIGHT,
    UI_AXIS,
    UI_BG_COLOR,
    UI_BG_COLOR2,
    UI_TEXT_COLOR,
    UI_RADIUS,
    UI_PADDING,
    UI_SPACING,
    UI_ALIGN_X,
    UI_ALIGN_Y,
    UI_FLOAT_X,
    UI_FLOAT_Y,
    UI_OVERFLOW_X,
    UI_OVERFLOW_Y,
    UI_EDGE_SOFTNESS,
    UI_BORDER_COLOR,
    UI_BORDER_WIDTHS,
    UI_INSET_SHADOW_COLOR,
    UI_OUTSET_SHADOW_COLOR,
    UI_INSET_SHADOW_WIDTH,
    UI_OUTSET_SHADOW_WIDTH,
    UI_SHADOW_OFFSETS,
    UI_BLUR_RADIUS,
    UI_ANIMATION,
    UI_ANIMATION_TIME,
    UI_ATTRIBUTE_COUNT,
};

fenum (UiStyleMask, U32) {
    UI_MASK_WIDTH               = 1 << UI_WIDTH,
    UI_MASK_HEIGHT              = 1 << UI_HEIGHT,
    UI_MASK_AXIS                = 1 << UI_AXIS,
    UI_MASK_BG_COLOR            = 1 << UI_BG_COLOR,
    UI_MASK_BG_COLOR2           = 1 << UI_BG_COLOR2,
    UI_MASK_TEXT_COLOR          = 1 << UI_TEXT_COLOR,
    UI_MASK_RADIUS              = 1 << UI_RADIUS,
    UI_MASK_PADDING             = 1 << UI_PADDING,
    UI_MASK_SPACING             = 1 << UI_SPACING,
    UI_MASK_ALIGN_X             = 1 << UI_ALIGN_X,
    UI_MASK_ALIGN_Y             = 1 << UI_ALIGN_Y,
    UI_MASK_FLOAT_X             = 1 << UI_FLOAT_X,
    UI_MASK_FLOAT_Y             = 1 << UI_FLOAT_Y,
    UI_MASK_OVERFLOW_X          = 1 << UI_OVERFLOW_X,
    UI_MASK_OVERFLOW_Y          = 1 << UI_OVERFLOW_Y,
    UI_MASK_EDGE_SOFTNESS       = 1 << UI_EDGE_SOFTNESS,
    UI_MASK_BORDER_COLOR        = 1 << UI_BORDER_COLOR,
    UI_MASK_BORDER_WIDTHS       = 1 << UI_BORDER_WIDTHS,
    UI_MASK_INSET_SHADOW_COLOR  = 1 << UI_INSET_SHADOW_COLOR,
    UI_MASK_OUTSET_SHADOW_COLOR = 1 << UI_OUTSET_SHADOW_COLOR,
    UI_MASK_INSET_SHADOW_WIDTH  = 1 << UI_INSET_SHADOW_WIDTH,
    UI_MASK_OUTSET_SHADOW_WIDTH = 1 << UI_OUTSET_SHADOW_WIDTH,
    UI_MASK_SHADOW_OFFSETS      = 1 << UI_SHADOW_OFFSETS,
    UI_MASK_BLUR_RADIUS         = 1 << UI_BLUR_RADIUS,
    UI_MASK_ANIMATION           = 1 << UI_ANIMATION,
    UI_MASK_ANIMATION_TIME      = 1 << UI_ANIMATION_TIME,
};

istruct (UiStyle) {
    UiBoxSize size;
    UiAxis axis;
    Vec4 bg_color;
    Vec4 bg_color2; // If x = -1, no gradient.
    Vec4 text_color;
    Vec4 radius;
    Vec2 padding;
    F32 spacing;
    UiAlign align[2];
    F32 edge_softness;
    F32 floating[2]; // If NAN no floating.
    U32 overflow[2];
    Vec4 border_color;
    Vec4 border_widths;
    Vec4 inset_shadow_color;
    Vec4 outset_shadow_color;
    F32 inset_shadow_width;
    F32 outset_shadow_width;
    Vec2 shadow_offsets;
    F32 blur_radius; // 0 means no background blur.
    UiStyleMask animation_mask;
    F32 animation_time;
};

ienum (UiPatternTag, U8) {
    UI_PATTERN_PATH,
    UI_PATTERN_AND,
    UI_PATTERN_ANY,
    UI_PATTERN_ID,
    UI_PATTERN_TAG,
    UI_PATTERN_IS_ODD,
    UI_PATTERN_IS_EVEN,
    UI_PATTERN_IS_FIRST,
    UI_PATTERN_IS_LAST,
};

istruct (UiSpecificity) {
    U32 id;
    U32 tag;
};

istruct (UiPattern) {
    UiPatternTag tag;
    String string;
    UiSpecificity specificity;
    Array(UiPattern*) patterns;
};

istruct (UiStyleRule) {
    UiStyle *style;
    UiPattern *pattern;
    UiStyleMask mask;
};

ienum (UiConfigTag, U8) {
    UI_CONFIG_U32,
    UI_CONFIG_F32,
    UI_CONFIG_VEC2,
    UI_CONFIG_VEC4,
    UI_CONFIG_STRING,
};

istruct (UiBox);
array_typedef(UiBox*, UiBox);
array_typedef(UiPattern*, UiPattern);
array_typedef(UiStyleRule, UiStyleRule);
array_typedef(UiSpecificity, UiSpecificity);

istruct (UiSignals) {
    Bool hovered;
    Bool pressed;
    Bool clicked;
    Bool focused;
};

fenum (UiBoxFlags, U8) {
    UI_BOX_HIDDEN        = flag(0),
    UI_BOX_REACTIVE      = flag(1),
    UI_BOX_CAN_FOCUS     = flag(2),
    UI_BOX_CLIPPING      = flag(3),
    UI_BOX_INVISIBLE_BG  = flag(4),
    UI_BOX_CLICK_THROUGH = flag(5),
    UI_BOX_IS_FOCUS_TRAP = flag(6),
};

typedef Void (*UiBoxDrawFn)(UiBox*);
typedef Void (*UiBoxSizeFn)(UiBox*, U64 axis);

istruct (UiBoxData) {
    Mem *mem;
    Void (*free)(Void *);
};

istruct (UiBox) {
    UiBox *parent;
    ArrayUiBox children;
    UiStyle style;
    UiStyle next_style;
    ArrayUiStyleRule style_rules;
    ArrayString tags;
    UiSignals signals;
    String label;
    UiKey key;
    U64 start_frame;
    UiBoxFlags flags;
    U8 gc_flag;
    Rect rect;
    UiBoxDrawFn draw_fn;
    UiBoxSizeFn size_fn;

    union {
        U64 scratch;
        F32 scratch_f32;
        U8 bools[8];
    };

    // The x/y components of this field are set independently
    // by the user build code for the purpose of scrolling the
    // content. The w/h components are set by the layout code.
    Rect content;
};

istruct (UiBoxCallback) {
    Void (*fn)(UiBox*);
    UiBox *box;
};

istruct (Ui) {
    Mem *perm_mem;
    Mem *frame_mem;
    U8 gc_flag;
    Event *event;
    Vec2 mouse_dt;
    Vec2 mouse;
    Bool animation_running;
    MouseCursor mouse_cursor;
    Map(U32, U8) pressed_keys;
    U64 frame;
    F64 dt;
    UiBox *root;
    UiBox *titlebar;
    UiBox *body;
    UiBox *hovered;
    UiBox *focus_trap;
    UiBox *focused;
    U64 focus_idx;
    ArrayUiBox depth_first;
    ArrayUiBox free_boxes;
    ArrayUiBox box_stack;
    Map(UiKey, UiBox*) box_cache;
    Map(UiKey, Void*) box_data;
    Array(Rect) clip_stack;
    Array(UiBoxCallback) deferred_layout_fns;
    UiStyleRule *current_style_rule;
    FontCache *font_cache;
    UiConfig *config;
    UiTheme *theme;
};

extern Ui *ui;

Void   ui_init             (UiConfig *, UiTheme *);
F32    ui_em               (F32);
Void   ui_frame            (Void(*)(), F64 dt);
Void   ui_scale            (F32);
Void   ui_set_mouse_cursor (MouseCursor);
Void   ui_eat_event        ();
Void   ui_grab_focus       (UiBox *);
Bool   ui_is_animating     ();
Bool   ui_is_descendant    (UiBox *ancestor, UiBox *child);
Bool   ui_within_box       (Rect r, Vec2 p);
Void   ui_push_parent      (UiBox *);
UiBox *ui_pop_parent       ();
UiBox *ui_box_push_str     (UiBoxFlags, String);
UiBox *ui_box_push_fmt     (UiBoxFlags, CString fmt, ...);
UiBox *ui_box_push         (UiBoxFlags, CString);
Void  *ui_get_box_data     (UiBox *, U64 size, U64 arena_block_size);
Void   ui_free_box_data    (UiKey);
UiKey  ui_build_key        (String id);
Rect   ui_push_clip        (UiBox *, Bool is_sub_clip);
Rect   ui_pop_clip         ();
Void   ui_style_rule_push  (UiBox *box, String pattern);
Void   ui_style_rule_pop   (Void *);
Void   ui_tag_box_str      (UiBox *, String);
Void   ui_tag_str          (String);
Void   ui_tag_box          (UiBox *, CString);
Void   ui_tag              (CString);
Bool   ui_is_key_pressed   (Int key);
Void   ui_animate_f32      (F32 *current, F32 final, F32 duration);
Void   ui_animate_vec2     (Vec2 *current, Vec2 final, F32 duration);
Void   ui_animate_vec4     (Vec4 *current, Vec4 final, F32 duration);
Void   ui_animate_size     (UiSize *current, UiSize final, F32 duration);
Void   ui_style_box_u32    (UiBox *, UiStyleAttribute, U32);
Void   ui_style_box_f32    (UiBox *, UiStyleAttribute, F32);
Void   ui_style_box_vec2   (UiBox *, UiStyleAttribute, Vec2);
Void   ui_style_box_vec4   (UiBox *, UiStyleAttribute, Vec4);
Void   ui_style_box_size   (UiBox *, UiStyleAttribute, UiSize);
Void   ui_style_u32        (UiStyleAttribute, U32);
Void   ui_style_f32        (UiStyleAttribute, F32);
Void   ui_style_vec2       (UiStyleAttribute, Vec2);
Void   ui_style_vec4       (UiStyleAttribute, Vec4);
Void   ui_style_size       (UiStyleAttribute, UiSize);

#define ui_parent(...)     ui_push_parent(__VA_ARGS__);  if (cleanup(ui_pop_parent_) U8 _; 1)
#define ui_box(...)        ui_box_push(__VA_ARGS__);     if (cleanup(ui_pop_parent_) U8 _; 1)
#define ui_box_str(...)    ui_box_push_str(__VA_ARGS__); if (cleanup(ui_pop_parent_) U8 _; 1)
#define ui_box_fmt(...)    ui_box_push_fmt(__VA_ARGS__); if (cleanup(ui_pop_parent_) U8 _; 1)
#define ui_style_rule(...) ui_style_rule_push(array_get_last(&ui->box_stack), str(__VA_ARGS__)); if (cleanup(ui_style_rule_pop) U8 _; 1)

inl Void ui_pop_parent_ (Void *) { array_pop(&ui->box_stack); }

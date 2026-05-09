#pragma once

#include "base/core.h"
#include "base/log.h"
#include "base/math.h"
#include "base/string.h"
#include "os/time.h"
#include "base/map.h"
#include "os/fs.h"

// =============================================================================
// Window:
// =============================================================================
#define EACH_KEY(X)\
    X(KEY_UNKNOWN, "Unknown")\
    X(KEY_A, "A")\
    X(KEY_B, "B")\
    X(KEY_C, "C")\
    X(KEY_D, "D")\
    X(KEY_E, "E")\
    X(KEY_F, "F")\
    X(KEY_G, "G")\
    X(KEY_H, "H")\
    X(KEY_I, "I")\
    X(KEY_J, "J")\
    X(KEY_K, "K")\
    X(KEY_L, "L")\
    X(KEY_M, "M")\
    X(KEY_N, "N")\
    X(KEY_O, "O")\
    X(KEY_P, "P")\
    X(KEY_Q, "Q")\
    X(KEY_R, "R")\
    X(KEY_S, "S")\
    X(KEY_T, "T")\
    X(KEY_U, "U")\
    X(KEY_V, "V")\
    X(KEY_W, "W")\
    X(KEY_X, "X")\
    X(KEY_Y, "Y")\
    X(KEY_Z, "Z")\
    X(KEY_0, "0")\
    X(KEY_1, "1")\
    X(KEY_2, "2")\
    X(KEY_3, "3")\
    X(KEY_4, "4")\
    X(KEY_5, "5")\
    X(KEY_6, "6")\
    X(KEY_7, "7")\
    X(KEY_8, "8")\
    X(KEY_9, "9")\
    X(KEY_F1, "F1")\
    X(KEY_F2, "F2")\
    X(KEY_F3, "F3")\
    X(KEY_F4, "F4")\
    X(KEY_F5, "F5")\
    X(KEY_F6, "F6")\
    X(KEY_F7, "F7")\
    X(KEY_F8, "F8")\
    X(KEY_F9, "F9")\
    X(KEY_F10, "F10")\
    X(KEY_F11, "F11")\
    X(KEY_F12, "F12")\
    X(KEY_DEL, "Del")\
    X(KEY_BACKSPACE, "Backspace")\
    X(KEY_RETURN, "Return")\
    X(KEY_SHIFT, "Shift")\
    X(KEY_CTRL, "Ctrl")\
    X(KEY_ALT, "Alt")\
    X(KEY_TAB, "Tab")\
    X(KEY_ESC, "Esc")\
    X(KEY_LEFT, "Left")\
    X(KEY_RIGHT, "Right")\
    X(KEY_UP, "Up")\
    X(KEY_DOWN, "Down")\
    X(KEY_MOUSE_LEFT, "MouseLeft")\
    X(KEY_MOUSE_MIDDLE,"MouseMiddle")\
    X(KEY_MOUSE_RIGHT, "MouseRight")

ienum (Key, U32) {
    #define X(KEY, ...) KEY,
    EACH_KEY(X)
    #undef X
};

fenum (KeyMod, U8) {
    KEY_MOD_SHIFT = flag(0),
    KEY_MOD_CTRL  = flag(1),
    KEY_MOD_ALT   = flag(2),
};

ienum (EventTag, U8) {
    EVENT_DUMMY,
    EVENT_EATEN,
    EVENT_WINDOW_SIZE,
    EVENT_MOUSE_MOVE,
    EVENT_SCROLL,
    EVENT_KEY_PRESS,
    EVENT_KEY_RELEASE,
    EVENT_TEXT_INPUT,
};

istruct (Event) {
    EventTag tag;
    F64 x;
    F64 y;
    Key key;
    KeyMod mods;
    String text;
};

array_typedef(Event, Event);

ienum (MouseCursor, U32) {
    MOUSE_CURSOR_DEFAULT,
    MOUSE_CURSOR_TEXT,
    MOUSE_CURSOR_WAIT,
    MOUSE_CURSOR_CROSSHAIR,
    MOUSE_CURSOR_PROGRESS,
    MOUSE_CURSOR_NWSE_RESIZE,
    MOUSE_CURSOR_NESW_RESIZE,
    MOUSE_CURSOR_EW_RESIZE,
    MOUSE_CURSOR_NS_RESIZE,
    MOUSE_CURSOR_MOVE,
    MOUSE_CURSOR_NOT_ALLOWED,
    MOUSE_CURSOR_POINTER,
    MOUSE_CURSOR_NW_RESIZE,
    MOUSE_CURSOR_N_RESIZE,
    MOUSE_CURSOR_NE_RESIZE,
    MOUSE_CURSOR_E_RESIZE,
    MOUSE_CURSOR_SE_RESIZE,
    MOUSE_CURSOR_S_RESIZE,
    MOUSE_CURSOR_SW_RESIZE,
    MOUSE_CURSOR_W_RESIZE,
};

istruct (Sound);

typedef U32 TickId;

Void        win_init               (CString);
Void        win_run                (Void (*)(F64 dt));
SliceEvent *win_get_events         ();
Void        win_set_clipboard_text (String);
String      win_get_clipboard_text (Mem *);
Vec2        win_get_size           ();
Void        win_set_cursor         (MouseCursor);
Sound      *win_sound_play         (Mem *, String, Bool);
Void        win_sound_cancel       (Sound *);
Bool        win_sound_is_playing   (Sound *);
TickId      win_tick_start         (U32 ms);
Void        win_tick_end           (TickId tick);
Void        win_open_web_url       (String);
Void        win_open_file_url      (String);
F32         win_get_display_scale  ();
Void        win_close              ();
Void        win_minimize           ();
Void        win_toggle_maximize    ();
Void        win_toggle_pin         ();
Bool        win_is_pinned          ();

// =============================================================================
// Drawing:
// =============================================================================
istruct (Rect) {
    union { struct { F32 x, y; }; Vec2 top_left; };
    union { struct { F32 w, h; }; F32 size[2]; };
};

istruct (RectAttributes) {
    Vec4 color;
    Vec4 color2; // If x = -1, no gradient.
    Vec2 top_left;
    Vec2 bottom_right;
    Vec4 radius;
    F32  edge_softness;
    Vec4 border_color;
    Vec4 border_widths;
    Vec4 inset_shadow_color;
    Vec4 outset_shadow_color;
    F32  outset_shadow_width;
    F32  inset_shadow_width;
    Vec2 shadow_offsets;
    Vec4 texture_rect;
    Vec4 text_color;
    F32 text_is_grayscale;
};

istruct (Vertex) {
    Vec2 position;
    Vec4 color;
    Vec2 top_left;
    Vec2 bottom_right;
    Vec4 radius;
    F32 edge_softness;
    Vec4 border_color;
    Vec4 border_widths;
    Vec4 inset_shadow_color;
    Vec4 outset_shadow_color;
    F32 outset_shadow_width;
    F32 inset_shadow_width;
    Vec2 shadow_offsets;
    Vec2 uv;
    Vec4 text_color;
    F32 text_is_grayscale;
};

istruct (Texture) {
    U32 id;
    F32 width;
    F32 height;
};

array_typedef(Vertex, Vertex);

Void        dr_flush_vertices   ();
Vertex     *dr_reserve_vertices (U32 n);
SliceVertex dr_rect_fn          (RectAttributes *);
Void        dr_blur             (Rect, F32 strength, Vec4 corner_radius);
Void        dr_line             (Vec2 a, Vec2 b, Vec4 color, F32 softness, F32 thickness);
Void        dr_triangle         (Vec2 a, Vec2 b, Vec2 c, Vec4 color, F32 softness);
Void        dr_circle           (Vec2 c, F32 r, Vec4 color, F32 softness);
Void        dr_scissor          (Rect);
Texture     dr_image            (CString filepath, Bool flip);
Void        dr_bind_texture     (Texture *);
Texture     dr_texture_alloc    (U32 w, U32 h);
Void        dr_texture_free     (Texture *texture);
Void        dr_texture_update   (Texture *, U32 x, U32 y, U32 w, U32 h, U8 *buf);

#define dr_rect(...) dr_rect_fn(&(RectAttributes){__VA_ARGS__})

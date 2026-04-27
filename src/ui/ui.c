#include "font/font.h"
#include "base/log.h"
#include "base/math.h"
#include "base/string.h"
#include "os/time.h"
#include "base/map.h"
#include "buffer/buffer.h"
#include "os/fs.h"
#include "window/window.h"
#include "ui/ui_widgets.h"
#include "ui/ui.h"

Ui *ui;

Noreturn static Void error () {
    log_scope_end_all();
    panic();
}

Noreturn static Void error_fmt Fmt(1, 2) (CString fmt, ...) {
    log_msg(m, LOG_ERROR, "Ui", 1);
    astr_push_fmt_vam(m, fmt);
    astr_push_byte(m, '\n');
    error();
}

static UiStyle default_box_style = {
    .size.width     = {UI_SIZE_CHILDREN_SUM, 0, 0},
    .size.height    = {UI_SIZE_CHILDREN_SUM, 0, 0},
    .bg_color2      = {-1},
    .text_color     = {1, 1, 1, .8},
    .edge_softness  = .75,
    .floating[0]    = NAN,
    .floating[1]    = NAN,
    .animation_time = .3,
};

Void ui_set_mouse_cursor (MouseCursor cursor) {
    ui->mouse_cursor = cursor;
}

Void ui_free_box_data (UiKey key) {
    Void *data = map_get_ptr(&ui->box_data, key);

    if (data) {
        UiBoxData *d = data;
        if (d->free) d->free(d);
        arena_destroy(cast(Arena*, d->mem));
        map_remove(&ui->box_data, key);
    }
}

Void *ui_get_box_data (UiBox *box, U64 size, U64 arena_block_size) {
    Void *data = map_get_ptr(&ui->box_data, box->key);

    if (! data) {
        assert_always(size);
        assert_always(arena_block_size);
        Arena *arena = arena_new(mem_root, arena_block_size);
        UiBoxData *header = mem_alloc(arena, UiBoxData, .size=size, .zeroed=true);
        header->mem = cast(Mem*, arena);
        data = header;
        map_add(&ui->box_data, box->key, data);
    }

    return data;
}

Bool ui_is_key_pressed (Int key) {
    U8 val; Bool pressed = map_get(&ui->pressed_keys, key, &val);
    return pressed;
}

Bool ui_within_box (Rect r, Vec2 p) {
    return (p.x > r.x) && (p.x < (r.x + r.w)) && (p.y > r.y) && (p.y < (r.y + r.h));
}

static Rect compute_rect_intersect (Rect r0, Rect r1) {
    F32 x0 = max(r0.x, r1.x);
    F32 y0 = max(r0.y, r1.y);
    F32 x1 = min(r0.x + r0.w, r1.x + r1.w);
    F32 y1 = min(r0.y + r0.h, r1.y + r1.h);
    return (Rect){ x0, y0, max(0, x1 - x0), max(0, y1 - y0) };
}

static Void compute_signals (UiBox *box) {
    UiSignals *sig = &box->signals;
    Bool pressed = sig->pressed;
    *sig = (UiSignals){};

    if (! (box->flags & UI_BOX_REACTIVE)) return;

    sig->focused = (box == ui->focused);
    sig->clicked = sig->focused && (ui->event->tag == EVENT_KEY_PRESS) && (ui->event->key == KEY_RETURN);

    sig->hovered = false;
    for (UiBox *b = ui->hovered; b; b = b->parent) {
        if (b == box) {
            Rect intersection = compute_rect_intersect(box->rect, array_get_last(&ui->clip_stack));
            sig->hovered = ui_within_box(intersection, ui->mouse);
            break;
        }
    }

    if (! pressed) {
        Int k = ui->event->key;
        sig->pressed = (ui->hovered == box) && (ui->event->tag == EVENT_KEY_PRESS) && (k == KEY_MOUSE_LEFT || k == KEY_MOUSE_MIDDLE || k == KEY_MOUSE_RIGHT);
    } else if ((ui->event->tag == EVENT_KEY_RELEASE) && (ui->event->key == KEY_MOUSE_LEFT)) {
        sig->pressed = false;
        if (sig->hovered) sig->clicked = true;
    } else {
        sig->pressed = true;
    }
}

UiKey ui_build_key (String string) {
    UiBox *parent = array_try_get_last(&ui->box_stack);
    U64 seed = parent ? parent->key : 0;
    return str_hash_seed(string, seed);
}

Void ui_push_parent (UiBox *box) {
    array_push(&ui->box_stack, box);
}

UiBox *ui_pop_parent () {
    return array_pop(&ui->box_stack);
}

UiBox *ui_box_push_str (UiBoxFlags flags, String label) {
    UiKey key  = ui_build_key(label);
    UiBox *box = map_get_ptr(&ui->box_cache, key);

    if (box) {
        if (box->gc_flag == ui->gc_flag) error_fmt("UiBox label hash collision: [%.*s] vs [%.*s].", STR(box->label), STR(label));
        box->parent = 0;
        box->draw_fn = 0;
        box->size_fn = 0;
        box->tags.count = 0;
        box->children.count = 0;
        box->style_rules.count = 0;
    } else if (ui->free_boxes.count) {
        box = array_pop(&ui->free_boxes);
        box->parent = 0;
        box->tags.count = 0;
        box->children.count = 0;
        box->style_rules.count = 0;
        box->style = default_box_style;
        box->rect = (Rect){};
        box->content = (Rect){};
        box->start_frame = ui->frame;
        box->scratch = 0;
        box->draw_fn = 0;
        box->size_fn = 0;
        map_add(&ui->box_cache, key, box);
    } else {
        box = mem_new(ui->perm_mem, UiBox);
        array_init(&box->children, ui->perm_mem);
        array_init(&box->style_rules, ui->perm_mem);
        array_init(&box->tags, ui->perm_mem);
        box->style = default_box_style;
        box->start_frame = ui->frame;
        map_add(&ui->box_cache, key, box);
    }

    if (! (flags & UI_BOX_HIDDEN)) array_push(&ui->depth_first, box);
    box->next_style = default_box_style;
    box->label = str_copy(ui->frame_mem, label);
    box->key = key;
    box->gc_flag = ui->gc_flag;
    box->flags = flags;
    if (flags & UI_BOX_IS_FOCUS_TRAP) ui->focus_trap = box;
    Auto parent = array_try_get_last(&ui->box_stack);
    if (parent && !(flags & UI_BOX_HIDDEN)) {
        array_push(&parent->children, box);
        box->parent = parent;
    }
    ui_push_parent(box);
    compute_signals(box);
    if (box->signals.focused) ui_tag("focus");
    if (box->signals.hovered) ui_tag("hover");
    if (box->signals.pressed) ui_tag("press");
    return box;
}

UiBox *ui_box_push_fmt (UiBoxFlags flags, CString fmt, ...) {
    tmem_new(tm);
    AString a = astr_new(tm);
    astr_push_fmt_vam(&a, fmt);
    return ui_box_push_str(flags, astr_to_str(&a));
}

UiBox *ui_box_push (UiBoxFlags flags, CString label) {
    return ui_box_push_str(flags, str(label));
}

static Rect ui_push_clip_rect (Rect rect) {
    Rect intersection = compute_rect_intersect(rect, array_get_last(&ui->clip_stack));
    array_push(&ui->clip_stack, intersection);
    return intersection;
}

Rect ui_push_clip (UiBox *box, Bool is_sub_clip) {
    box->flags |= UI_BOX_CLIPPING;
    Rect rect = box->rect;
    rect.x += box->style.border_widths.z;
    rect.y += box->style.border_widths.y;
    rect.w -= box->style.border_widths.x + box->style.border_widths.z;
    rect.h -= box->style.border_widths.w + box->style.border_widths.y;
    if (is_sub_clip) return ui_push_clip_rect(rect);
    array_push(&ui->clip_stack, rect);
    return rect;
}

Rect ui_pop_clip () {
    array_pop(&ui->clip_stack);
    return array_get_last(&ui->clip_stack);
}

Void ui_animate_f32 (F32 *current, F32 final, F32 duration) {
    if (isnan(*current)) *current = 0;
    const F32 epsilon = 0.001f;
    if (duration <= 0.0f) { *current = final; return; }
    if (fabsf(*current - final) <= epsilon) { *current = final; return; }
    ui->animation_running = true;
    *current = lerp_f32(*current, final, 1.0f - powf(epsilon, ui->dt / duration));
}

Void ui_animate_vec2 (Vec2 *current, Vec2 final, F32 duration) {
    ui_animate_f32(&current->x, final.x, duration);
    ui_animate_f32(&current->y, final.y, duration);
}

Void ui_animate_vec4 (Vec4 *current, Vec4 final, F32 duration) {
    ui_animate_f32(&current->x, final.x, duration);
    ui_animate_f32(&current->y, final.y, duration);
    ui_animate_f32(&current->z, final.z, duration);
    ui_animate_f32(&current->w, final.w, duration);
}

Void ui_animate_size (UiSize *current, UiSize final, F32 duration) {
    current->tag = final.tag;
    current->strictness = final.strictness;
    ui_animate_f32(&current->value, final.value, duration);
}

static Void animate_style (UiBox *box) {
    UiStyle *a = &box->style;
    UiStyle *b = &box->next_style;
    F32 duration = box->next_style.animation_time;
    UiStyleMask mask = box->next_style.animation_mask;

    #define A(T, M, F) if (mask & M) ui_animate_##T(&a->F, b->F, duration); else a->F = b->F;

    A(size, UI_MASK_WIDTH, size.width);
    A(size, UI_MASK_HEIGHT, size.height);
    A(vec4, UI_MASK_BG_COLOR, bg_color);
    A(vec4, UI_MASK_BG_COLOR2, bg_color2);
    A(vec4, UI_MASK_TEXT_COLOR, text_color);
    A(vec4, UI_MASK_RADIUS, radius);
    A(vec2, UI_MASK_PADDING, padding);
    A(f32, UI_MASK_SPACING, spacing);
    A(vec4, UI_MASK_BORDER_COLOR, border_color);
    A(vec4, UI_MASK_BORDER_WIDTHS, border_widths);
    A(vec4, UI_MASK_INSET_SHADOW_COLOR, inset_shadow_color);
    A(vec4, UI_MASK_OUTSET_SHADOW_COLOR, outset_shadow_color);
    A(f32, UI_MASK_INSET_SHADOW_WIDTH, inset_shadow_width);
    A(f32, UI_MASK_OUTSET_SHADOW_WIDTH, outset_shadow_width);
    A(vec2, UI_MASK_SHADOW_OFFSETS, shadow_offsets);
    A(f32, UI_MASK_BLUR_RADIUS, blur_radius);
    A(f32, UI_MASK_FLOAT_X, floating[0]);
    A(f32, UI_MASK_FLOAT_Y, floating[1]);

    #undef A

    a->axis = b->axis;
    a->align[0] = b->align[0];
    a->align[1] = b->align[1];
    a->edge_softness = b->edge_softness;
    a->overflow[0] = b->overflow[0];
    a->overflow[1] = b->overflow[1];
}

// =============================================================================
// Style rules:
// =============================================================================
static UiPattern *pattern_alloc (Mem *mem, UiPatternTag tag) {
    Auto p = mem_new(mem, UiPattern);
    p->tag = tag;
    array_init(&p->patterns, mem);
    return p;
}

#define pattern_advance(S, N) (S)->data += N; (S)->count -= N;

static String parse_pattern_name (String *chunk) {
    U64 n = chunk->count;
    array_iter (c, chunk) {
        if (c == '#' || c == '.' || c == ':' || c == ' ') {
            n = ARRAY_IDX;
            break;
        }
    }

    if (n == 0) error_fmt("Expected selector name: [%.*s]", STR(*chunk));

    String slice = str_slice(*chunk, 0, n);
    pattern_advance(chunk, n);
    return slice;
}

static UiPattern *parse_pattern_and (String chunk, Mem *mem) {
    Auto result = pattern_alloc(mem, UI_PATTERN_AND);

    while (chunk.count) {
        Auto selector = pattern_alloc(mem, 0);
        array_push(&result->patterns, selector);

        Auto c = array_get(&chunk, 0);
        pattern_advance(&chunk, 1);

        switch (c) {
        case '*': selector->tag = UI_PATTERN_ANY; break;
        case '#': result->specificity.id++; selector->tag = UI_PATTERN_ID;  selector->string = parse_pattern_name(&chunk); break;
        case '.': result->specificity.tag++; selector->tag = UI_PATTERN_TAG; selector->string = parse_pattern_name(&chunk); break;
        case ':': {
            result->specificity.tag++;
            if      (str_starts_with(chunk, str("first"))) { pattern_advance(&chunk, 5); selector->tag = UI_PATTERN_IS_FIRST; }
            else if (str_starts_with(chunk, str("last")))  { pattern_advance(&chunk, 4); selector->tag = UI_PATTERN_IS_LAST; }
            else if (str_starts_with(chunk, str("odd")))   { pattern_advance(&chunk, 3); selector->tag = UI_PATTERN_IS_ODD; }
            else if (str_starts_with(chunk, str("even")))  { pattern_advance(&chunk, 4); selector->tag = UI_PATTERN_IS_EVEN; }
            else                                           error_fmt("Invalid pseudo tag: [%.*s]", STR(chunk));
        } break;

        default: error_fmt("Invalid selector: [%.*s]", STR(chunk));
        }
    }

    return result;
}

// Root node is UI_PATTERN_PATH and it's children UI_PATTERN_AND.
static UiPattern *parse_pattern (String pattern, Mem *mem) {
    tmem_new(tm);

    ArrayString chunks;
    array_init(&chunks, tm);
    str_split(pattern, str(" "), false, false, &chunks);

    Auto p = pattern_alloc(mem, UI_PATTERN_PATH);

    array_iter (chunk, &chunks) {
        UiPattern *child = parse_pattern_and(chunk, mem);
        array_push(&p->patterns, child);
        p->specificity.id += child->specificity.id;
        p->specificity.tag += child->specificity.tag;
    }

    return p;
}

static Void print_pattern (String text, UiPattern *pattern) {
    printf("%.*s\nspecificity=[%i, %i]\n\n", STR(text), pattern->specificity.id, pattern->specificity.tag);

    array_iter (chunk, &pattern->patterns) {
        printf("  %*s", cast(int, ARRAY_IDX), "");

        array_iter (selector, &chunk->patterns) {
            printf("[");
            switch (selector->tag) {
            case UI_PATTERN_ANY:      printf("*"); break;
            case UI_PATTERN_ID:       printf("#%.*s", STR(selector->string)); break;
            case UI_PATTERN_TAG:      printf(".%.*s", STR(selector->string)); break;
            case UI_PATTERN_IS_ODD:   printf(":odd"); break;
            case UI_PATTERN_IS_EVEN:  printf(":even"); break;
            case UI_PATTERN_IS_FIRST: printf(":first"); break;
            case UI_PATTERN_IS_LAST:  printf(":last"); break;
            case UI_PATTERN_PATH:     badpath;
            case UI_PATTERN_AND:      badpath;
            }
            printf("] ");
        }

        printf("\n");
    }

    printf("\n");
}

static UiStyleMask style_attr_to_mask (UiStyleAttribute attr) {
    return 1 << attr;
}

Void ui_style_box_u32 (UiBox *box, UiStyleAttribute attr, U32 val) {
    Auto s = ui->current_style_rule ? ui->current_style_rule->style : &box->next_style;
    switch (attr) {
    case UI_ANIMATION:  s->animation_mask = val; break;
    case UI_ALIGN_X:    s->align[0] = val; break;
    case UI_ALIGN_Y:    s->align[1] = val; break;
    case UI_OVERFLOW_X: s->overflow[0] = val; break;
    case UI_OVERFLOW_Y: s->overflow[1] = val; break;
    case UI_AXIS:       s->axis = val; break;
    default:            error_fmt("Given attribute is not of type U32.");
    }
    if (ui->current_style_rule) ui->current_style_rule->mask |= style_attr_to_mask(attr);
}

Void ui_style_box_f32 (UiBox *box, UiStyleAttribute attr, F32 val) {
    Auto s = ui->current_style_rule ? ui->current_style_rule->style : &box->next_style;
    switch (attr) {
    case UI_ANIMATION_TIME:      s->animation_time = val; break;
    case UI_BLUR_RADIUS:         s->blur_radius = val; break;
    case UI_FLOAT_X:             s->floating[0] = val; break;
    case UI_FLOAT_Y:             s->floating[1] = val; break;
    case UI_SPACING:             s->spacing = val; break;
    case UI_EDGE_SOFTNESS:       s->edge_softness = val; break;
    case UI_INSET_SHADOW_WIDTH:  s->inset_shadow_width = val; break;
    case UI_OUTSET_SHADOW_WIDTH: s->outset_shadow_width = val; break;
    default:                     error_fmt("Given attribute is not of type F32.");
    }
    if (ui->current_style_rule) ui->current_style_rule->mask |= style_attr_to_mask(attr);
}

Void ui_style_box_vec2 (UiBox *box, UiStyleAttribute attr, Vec2 val) {
    Auto s = ui->current_style_rule ? ui->current_style_rule->style : &box->next_style;
    switch (attr) {
    case UI_PADDING:        s->padding = val; break;
    case UI_SHADOW_OFFSETS: s->shadow_offsets = val; break;
    default:                error_fmt("Given attribute is not of type Vec2.");
    }
    if (ui->current_style_rule) ui->current_style_rule->mask |= style_attr_to_mask(attr);
}

Void ui_style_box_vec4 (UiBox *box, UiStyleAttribute attr, Vec4 val) {
    Auto s = ui->current_style_rule ? ui->current_style_rule->style : &box->next_style;
    switch (attr) {
    case UI_BG_COLOR:            s->bg_color = val; break;
    case UI_BG_COLOR2:           s->bg_color2 = val; break;
    case UI_TEXT_COLOR:          s->text_color = val; break;
    case UI_RADIUS:              s->radius = val; break;
    case UI_BORDER_COLOR:        s->border_color = val; break;
    case UI_BORDER_WIDTHS:       s->border_widths = val; break;
    case UI_INSET_SHADOW_COLOR:  s->inset_shadow_color = val; break;
    case UI_OUTSET_SHADOW_COLOR: s->outset_shadow_color = val; break;
    default:                     error_fmt("Given attribute is not of type Vec4.");
    }
    if (ui->current_style_rule) ui->current_style_rule->mask |= style_attr_to_mask(attr);
}

Void ui_style_box_size (UiBox *box, UiStyleAttribute attr, UiSize val) {
    Auto s = ui->current_style_rule ? ui->current_style_rule->style : &box->next_style;
    switch (attr) {
    case UI_WIDTH:  s->size.width = val; break;
    case UI_HEIGHT: s->size.height = val; break;
    default:        error_fmt("Given attribute is not of type UiSize.");
    }
    if (ui->current_style_rule) ui->current_style_rule->mask |= style_attr_to_mask(attr);
}

Void ui_style_u32  (UiStyleAttribute attr, U32 val)    { ui_style_box_u32(array_get_last(&ui->box_stack), attr, val); }
Void ui_style_f32  (UiStyleAttribute attr, F32 val)    { ui_style_box_f32(array_get_last(&ui->box_stack), attr, val); }
Void ui_style_vec2 (UiStyleAttribute attr, Vec2 val)   { ui_style_box_vec2(array_get_last(&ui->box_stack), attr, val); }
Void ui_style_vec4 (UiStyleAttribute attr, Vec4 val)   { ui_style_box_vec4(array_get_last(&ui->box_stack), attr, val); }
Void ui_style_size (UiStyleAttribute attr, UiSize val) { ui_style_box_size(array_get_last(&ui->box_stack), attr, val); }

Void ui_style_rule_push (UiBox *box, String pattern) {
    if (ui->current_style_rule) error_fmt("Style rule declarations cannot be nested.");
    UiStyleRule rule = {};
    rule.pattern = parse_pattern(pattern, ui->frame_mem);
    rule.style = mem_new(ui->frame_mem, UiStyle);
    *rule.style = default_box_style;
    array_push(&box->style_rules, rule);
    ui->current_style_rule = array_ref_last(&box->style_rules);
}

Void ui_style_rule_pop (Void *) {
    ui->current_style_rule = 0;
}

static Bool rule_applies (UiStyleRule *rule, UiSpecificity a, UiSpecificity *specs, UiStyleAttribute attr) {
    if (! (rule->mask & style_attr_to_mask(attr))) return false;
    UiSpecificity b = specs[attr];
    return (a.id > b.id) || ((a.id == b.id) && (a.tag >= b.tag));
}

static Void apply_style_rule (UiBox *box, UiStyleRule *rule, UiSpecificity *specs) {
    Auto s = rule->pattern->specificity;
    if (rule_applies(rule, s, specs, UI_WIDTH))               { box->next_style.size.width = rule->style->size.width; specs[UI_WIDTH] = s; }
    if (rule_applies(rule, s, specs, UI_HEIGHT))              { box->next_style.size.height = rule->style->size.height; specs[UI_HEIGHT] = s; }
    if (rule_applies(rule, s, specs, UI_AXIS))                { box->next_style.axis = rule->style->axis; specs[UI_AXIS] = s; }
    if (rule_applies(rule, s, specs, UI_BG_COLOR))            { box->next_style.bg_color = rule->style->bg_color; specs[UI_BG_COLOR] = s; }
    if (rule_applies(rule, s, specs, UI_BG_COLOR2))           { box->next_style.bg_color2 = rule->style->bg_color2; specs[UI_BG_COLOR2] = s; }
    if (rule_applies(rule, s, specs, UI_TEXT_COLOR))          { box->next_style.text_color = rule->style->text_color; specs[UI_TEXT_COLOR] = s; }
    if (rule_applies(rule, s, specs, UI_RADIUS))              { box->next_style.radius = rule->style->radius; specs[UI_RADIUS] = s; }
    if (rule_applies(rule, s, specs, UI_PADDING))             { box->next_style.padding = rule->style->padding; specs[UI_PADDING] = s; }
    if (rule_applies(rule, s, specs, UI_SPACING))             { box->next_style.spacing = rule->style->spacing; specs[UI_SPACING] = s; }
    if (rule_applies(rule, s, specs, UI_ALIGN_X))             { box->next_style.align[0] = rule->style->align[0]; specs[UI_ALIGN_X] = s; }
    if (rule_applies(rule, s, specs, UI_ALIGN_Y))             { box->next_style.align[1] = rule->style->align[1]; specs[UI_ALIGN_Y] = s; }
    if (rule_applies(rule, s, specs, UI_FLOAT_X))             { box->next_style.floating[0] = rule->style->floating[0]; specs[UI_FLOAT_X] = s; }
    if (rule_applies(rule, s, specs, UI_FLOAT_Y))             { box->next_style.floating[1] = rule->style->floating[1]; specs[UI_FLOAT_Y] = s; }
    if (rule_applies(rule, s, specs, UI_OVERFLOW_X))          { box->next_style.overflow[0] = rule->style->overflow[0]; specs[UI_OVERFLOW_X] = s; }
    if (rule_applies(rule, s, specs, UI_OVERFLOW_Y))          { box->next_style.overflow[1] = rule->style->overflow[1]; specs[UI_OVERFLOW_Y] = s; }
    if (rule_applies(rule, s, specs, UI_EDGE_SOFTNESS))       { box->next_style.edge_softness = rule->style->edge_softness; specs[UI_EDGE_SOFTNESS] = s; }
    if (rule_applies(rule, s, specs, UI_BORDER_COLOR))        { box->next_style.border_color = rule->style->border_color; specs[UI_BORDER_COLOR] = s; }
    if (rule_applies(rule, s, specs, UI_BORDER_WIDTHS))       { box->next_style.border_widths = rule->style->border_widths; specs[UI_BORDER_WIDTHS] = s; }
    if (rule_applies(rule, s, specs, UI_INSET_SHADOW_COLOR))  { box->next_style.inset_shadow_color = rule->style->inset_shadow_color; specs[UI_INSET_SHADOW_COLOR] = s; }
    if (rule_applies(rule, s, specs, UI_OUTSET_SHADOW_COLOR)) { box->next_style.outset_shadow_color = rule->style->outset_shadow_color; specs[UI_OUTSET_SHADOW_COLOR] = s; }
    if (rule_applies(rule, s, specs, UI_INSET_SHADOW_WIDTH))  { box->next_style.inset_shadow_width = rule->style->inset_shadow_width; specs[UI_INSET_SHADOW_WIDTH] = s; }
    if (rule_applies(rule, s, specs, UI_OUTSET_SHADOW_WIDTH)) { box->next_style.outset_shadow_width = rule->style->outset_shadow_width; specs[UI_OUTSET_SHADOW_WIDTH] = s; }
    if (rule_applies(rule, s, specs, UI_SHADOW_OFFSETS))      { box->next_style.shadow_offsets = rule->style->shadow_offsets; specs[UI_SHADOW_OFFSETS] = s; }
    if (rule_applies(rule, s, specs, UI_BLUR_RADIUS))         { box->next_style.blur_radius = rule->style->blur_radius; specs[UI_BLUR_RADIUS] = s; }
    if (rule_applies(rule, s, specs, UI_ANIMATION))           { box->next_style.animation_mask = rule->style->animation_mask; specs[UI_ANIMATION] = s; }
    if (rule_applies(rule, s, specs, UI_ANIMATION_TIME))      { box->next_style.animation_time = rule->style->animation_time; specs[UI_ANIMATION_TIME] = s; }
}

static Bool match_pattern (UiBox *box, UiPattern *pattern) {
    assert_dbg(pattern->tag == UI_PATTERN_AND);

    U64 box_idx = box->parent ? array_find(&box->parent->children, IT == box) : 0;
    assert_dbg(box_idx != ARRAY_NIL_IDX);

    Bool result = true;
    array_iter (selector, &pattern->patterns) {
        switch (selector->tag) {
        case UI_PATTERN_ID:       result = str_match(box->label, selector->string); break;
        case UI_PATTERN_IS_ODD:   result = (box_idx % 2); break;
        case UI_PATTERN_IS_EVEN:  result = !(box_idx % 2); break;
        case UI_PATTERN_IS_FIRST: result = (box_idx == 0); break;
        case UI_PATTERN_IS_LAST:  result = (box_idx == box->parent->children.count - 1); break;
        case UI_PATTERN_TAG:      result = array_find_ref(&box->tags, str_match(*IT, selector->string)); break;
        case UI_PATTERN_ANY:      break;
        case UI_PATTERN_PATH:     badpath;
        case UI_PATTERN_AND:      badpath;
        }

        if (! result) break;
    }

    return result;
}

static UiStyleRule derive_new_rule (UiStyleRule *old_rule, Mem *mem) {
    UiStyleRule new_rule = {};
    new_rule.style = old_rule->style;
    new_rule.mask = old_rule->mask;
    new_rule.pattern = pattern_alloc(mem, UI_PATTERN_PATH);
    *new_rule.pattern = *old_rule->pattern;
    new_rule.pattern->patterns.data++;
    new_rule.pattern->patterns.count--;
    return new_rule;
}

static Void apply_style_rules_box (UiBox *box, ArrayUiStyleRule *active_rules, Mem *mem) {
    U64 restore_point = active_rules->count;
    array_push_many(active_rules, &box->style_rules);

    UiSpecificity specs[UI_ATTRIBUTE_COUNT] = {};

    Auto stop_at = active_rules->count - 1; // Don't loop over newly added derived rules.
    array_iter (rule, active_rules) {
        UiPattern *head_of_rule = array_get(&rule.pattern->patterns, 0);
        Bool match = match_pattern(box, head_of_rule);

        if (match) {
            if (rule.pattern->patterns.count == 1) {
                apply_style_rule(box, &rule, specs);
            } else {
                array_push(active_rules, derive_new_rule(&rule, mem));
            }
        }

        if (ARRAY_IDX == stop_at) break;
    }

    array_iter (child, &box->children) apply_style_rules_box(child, active_rules, mem);
    active_rules->count = restore_point;
    animate_style(box);
}

static Void apply_style_rules (UiBox *box) {
    if (box->flags & UI_BOX_HIDDEN) return;
    tmem_new(tm);
    ArrayUiStyleRule active_rules;
    array_init(&active_rules, tm);
    apply_style_rules_box(box, &active_rules, tm);
}

Void ui_tag_box_str (UiBox *box, String tag)  { array_push(&box->tags, tag); }
Void ui_tag_str     (String tag)              { return ui_tag_box_str(array_get_last(&ui->box_stack), tag); }
Void ui_tag_box     (UiBox *box, CString tag) { return ui_tag_box_str(box, str(tag)); }
Void ui_tag         (CString tag)             { return ui_tag_box_str(array_get_last(&ui->box_stack), str(tag)); }

// =============================================================================
// Layout:
// =============================================================================
static Void compute_standalone_sizes (ArrayUiBox *boxes, U64 axis) {
    array_iter (box, boxes) {
        Auto size = &box->style.size.v[axis];
        if (size->tag == UI_SIZE_PIXELS) box->rect.size[axis] = size->value;
    }
}

static Void compute_downward_dependent_sizes (ArrayUiBox *boxes, U64 axis) {
    array_iter_back (box, boxes) {
        Auto size = &box->style.size.v[axis];

        if (size->tag == UI_SIZE_CUSTOM) {
            box->size_fn(box, axis);
        } else if (size->tag == UI_SIZE_CHILDREN_SUM) {
            F32 final_size = 2*box->style.padding.v[axis];

            if (box->style.axis == axis) {
                U64 idx = array_find(&box->children, IT->style.size.v[axis].tag == UI_SIZE_PCT_PARENT);
                if (idx != ARRAY_NIL_IDX) {
                    // Cycle: Parent defined by children and one child defined by parent.
                    size->tag = UI_SIZE_PCT_PARENT;
                    size->value = 1;
                    continue;
                }

                array_iter (child, &box->children) {
                    if (! isnan(child->style.floating[axis])) continue;
                    if (child->style.size.v[axis].tag == UI_SIZE_PCT_PARENT) continue;
                    final_size += child->rect.size[axis];
                    if (! ARRAY_ITER_DONE) final_size += box->style.spacing;
                }
            } else {
                U64 idx = array_find(&box->children, IT->style.size.v[axis].tag != UI_SIZE_PCT_PARENT);
                if (idx == ARRAY_NIL_IDX) {
                    // Cycle: Parent defined by children and all children defined by parent.
                    size->tag = UI_SIZE_PCT_PARENT;
                    size->value = 1;
                    continue;
                }

                array_iter (child, &box->children) {
                    if (! isnan(child->style.floating[axis])) continue;
                    if (child->style.size.v[axis].tag == UI_SIZE_PCT_PARENT) continue;
                    final_size = max(final_size, child->rect.size[axis] + 2*box->style.padding.v[axis]);
                }
            }

            box->rect.size[axis] = final_size;
        }
    }
}

static Void compute_upward_dependent_sizes (ArrayUiBox *boxes, U64 axis) {
    array_iter (box, boxes) {
        Auto size = &box->style.size.v[axis];
        if (size->tag == UI_SIZE_PCT_PARENT) box->rect.size[axis] = size->value * (box->parent->rect.size[axis] - 2*box->parent->style.padding.v[axis]);
    }
}

static Void fix_overflow (ArrayUiBox *boxes, U64 axis) {
    array_iter (box, boxes) {
        F32 box_size = box->rect.size[axis] - 2*box->style.padding.v[axis];

        if (box->style.axis == axis) {
            F32 children_size = 0;
            array_iter (child, &box->children) {
                if (! isnan(child->style.floating[axis])) continue;
                children_size += child->rect.size[axis];
                if (! ARRAY_ITER_DONE) children_size += box->style.spacing;
            }

            if (box_size < children_size && !box->style.overflow[axis]) {
                F32 overflow = children_size - box_size;

                F32 total_slack = 0;
                array_iter (child, &box->children) {
                    if (! isnan(child->style.floating[axis])) continue;
                    total_slack += child->rect.size[axis] * (1 - child->style.size.v[axis].strictness);
                }

                if (total_slack >= overflow) {
                    F32 slack_fraction = overflow / total_slack;
                    array_iter (child, &box->children) {
                        if (! isnan(child->style.floating[axis])) continue;
                        child->rect.size[axis] -= child->rect.size[axis] * (1 - child->style.size.v[axis].strictness) * slack_fraction;
                    }
                }
            }
        } else {
            array_iter (child, &box->children) {
                if (! isnan(child->style.floating[axis])) continue;
                if (box_size >= child->rect.size[axis]) continue;
                if (box->style.overflow[axis]) continue;
                F32 overflow = child->rect.size[axis] - box_size;
                F32 slack = child->rect.size[axis] * (1 - child->style.size.v[axis].strictness);
                if (slack >= overflow) child->rect.size[axis] -= overflow;
            }
        }
    }
}

static Void compute_positions (ArrayUiBox *boxes, U64 axis) {
    array_iter (box, boxes) {
        if (box->style.axis == axis) {
            F32 content_size = 2*box->style.padding.v[axis];
            array_iter (child, &box->children) {
                if (! isnan(child->style.floating[axis])) continue;
                content_size += child->rect.size[axis];
                if (! ARRAY_ITER_DONE) content_size += box->style.spacing;
            }

            box->content.size[axis] = floor(content_size);

            F32 align_offset = 0;
            switch (box->style.align[axis]) {
            case UI_ALIGN_START:  break;
            case UI_ALIGN_MIDDLE: align_offset = floor(box->rect.size[axis]/2 - content_size/2); break;
            case UI_ALIGN_END:    align_offset = box->rect.size[axis] - content_size; break;
            }

            F32 pos = box->rect.top_left.v[axis] + box->style.padding.v[axis] + align_offset + box->content.top_left.v[axis];
            array_iter (child, &box->children) {
                if (isnan(child->style.floating[axis])) {
                    child->rect.top_left.v[axis] = pos;
                    pos += child->rect.size[axis] + box->style.spacing;
                } else {
                    child->rect.top_left.v[axis] = box->rect.top_left.v[axis] + child->style.floating[axis];
                }
            }
        } else {
            box->content.size[axis] = 0;

            array_iter (child, &box->children) {
                if (isnan(child->style.floating[axis])) {
                    F32 content_size = child->rect.size[axis] + 2*box->style.padding.v[axis];

                    box->content.size[axis] = floor(max(box->content.size[axis], content_size));

                    F32 align_offset = 0;
                    switch (box->style.align[axis]) {
                    case UI_ALIGN_START:  break;
                    case UI_ALIGN_MIDDLE: align_offset = floor(box->rect.size[axis]/2 - content_size/2); break;
                    case UI_ALIGN_END:    align_offset = box->rect.size[axis] - content_size; break;
                    }

                    child->rect.top_left.v[axis] = box->rect.top_left.v[axis] + box->style.padding.v[axis] + align_offset + box->content.top_left.v[axis];
                } else {
                    child->rect.top_left.v[axis] = box->rect.top_left.v[axis] + child->style.floating[axis];
                }
            }
        }

        array_iter (child, &box->children) {
            child->rect.x = round(child->rect.x);
            child->rect.y = round(child->rect.y);
            child->rect.w = round(child->rect.w);
            child->rect.h = round(child->rect.h);
        }
    }
}

static Void collect_nodes_dfs (UiBox *box, ArrayUiBox *out) {
    if (box->flags & UI_BOX_HIDDEN) return;
    array_push(out, box);
    array_iter (child, &box->children) collect_nodes_dfs(child, out);
}

static Void compute_layout (UiBox *box) {
    ArrayUiBox nodes;

    if (box == ui->root) {
        nodes = ui->depth_first;
    } else {
        array_init(&nodes, ui->frame_mem);
        collect_nodes_dfs(box, &nodes);
    }

    for (U64 axis = 0; axis < 2; ++axis) {
        compute_standalone_sizes(&nodes, axis);
        compute_downward_dependent_sizes(&nodes, axis);
        compute_upward_dependent_sizes(&nodes, axis);
        fix_overflow(&nodes, axis);
        compute_positions(&nodes, axis);
    }
}

static Void find_topmost_hovered_box (UiBox *box) {
    if (! (box->flags & UI_BOX_CLICK_THROUGH)) {
        Rect r = compute_rect_intersect(box->rect, array_get_last(&ui->clip_stack));
        if (ui_within_box(r, ui->mouse)) ui->hovered = box;
    }

    if (box->flags & UI_BOX_CLIPPING) ui_push_clip(box, true);
    array_iter (child, &box->children) find_topmost_hovered_box(child);
    if (box->flags & UI_BOX_CLIPPING) ui_pop_clip();
}

static Void draw_box (UiBox *box) {
    if (box->flags & UI_BOX_HIDDEN) return;

    F32 win_height = win_get_size().y;

    if (!(box->flags & UI_BOX_INVISIBLE_BG) && box->style.blur_radius) {
        F32 blur_radius = max(1, cast(Int, box->style.blur_radius));
        dr_blur(box->rect, blur_radius, box->style.radius);
        Rect r = array_get_last(&ui->clip_stack);
        dr_scissor((Rect){r.x, win_height - r.y - r.h, r.w, r.h});

    }

    if (! (box->flags & UI_BOX_INVISIBLE_BG)) dr_rect(
        .top_left            = box->rect.top_left,
        .bottom_right        = vec2(box->rect.x + box->rect.w, box->rect.y + box->rect.h),
        .color               = box->style.bg_color,
        .color2              = box->style.bg_color2,
        .radius              = box->style.radius,
        .edge_softness       = box->style.edge_softness,
        .border_color        = box->style.border_color,
        .border_widths       = box->style.border_widths,
        .inset_shadow_color  = box->style.inset_shadow_color,
        .outset_shadow_color = box->style.outset_shadow_color,
        .inset_shadow_width  = box->style.inset_shadow_width,
        .outset_shadow_width = box->style.outset_shadow_width,
        .shadow_offsets      = box->style.shadow_offsets,
    );

    if (box->flags & UI_BOX_CLIPPING) {
        dr_flush_vertices();
        Rect r = ui_push_clip(box, true);
        dr_scissor((Rect){r.x, win_height - r.y - r.h, r.w, r.h});
    }

    if (box->draw_fn) box->draw_fn(box);
    array_iter (c, &box->children) draw_box(c);

    if (box->flags & UI_BOX_CLIPPING) {
        dr_flush_vertices();
        Rect r = ui_pop_clip();
        dr_scissor((Rect){r.x, win_height - r.y - r.h, r.w, r.h});
    }
}

Void ui_eat_event () {
    ui->event->tag = EVENT_EATEN;
}

static Void update_input_state (Event *event) {
    ui->event = event;

    switch (event->tag) {
    case EVENT_DUMMY:       break;
    case EVENT_EATEN:       break;
    case EVENT_SCROLL:      break;
    case EVENT_WINDOW_SIZE: break;
    case EVENT_TEXT_INPUT:  break;
    case EVENT_KEY_PRESS:   map_add(&ui->pressed_keys, event->key, 0); break;
    case EVENT_KEY_RELEASE: map_remove(&ui->pressed_keys, event->key); break;
    case EVENT_MOUSE_MOVE:
        ui->mouse_dt.x = event->x - ui->mouse.x;
        ui->mouse_dt.y = event->y - ui->mouse.y;
        ui->mouse.x = event->x;
        ui->mouse.y = event->y;
        break;
    }
}

Bool ui_is_descendant (UiBox *ancestor, UiBox *child) {
    for (UiBox *box = child; box; box = box->parent) {
        if (box == ancestor) return true;
    }

    return false;
}

Void ui_grab_focus (UiBox *box) {
    U64 idx = array_find(&ui->depth_first, IT == box);
    assert_dbg(idx != ARRAY_NIL_IDX);
    ui->focus_idx = idx;
    ui->focused = box;
}

static Void find_next_focus () {
    U64 start = ui->focus_idx;
    U64 candidate_idx = start;
    while (true) {
        candidate_idx = (candidate_idx + 1) % ui->depth_first.count;
        UiBox *candidate = array_get(&ui->depth_first, candidate_idx);
        Bool valid = candidate->flags & UI_BOX_CAN_FOCUS;
        if (valid && ui->focus_trap) valid = ui_is_descendant(ui->focus_trap, candidate);
        if (valid) {
            ui->focus_idx = candidate_idx;
            ui->focused = candidate;
            break;
        }
        if (candidate_idx == start) break;
    }
}

static Void find_prev_focus () {
    U64 start = ui->focus_idx;
    U64 candidate_idx = start;
    while (true) {
        candidate_idx = (candidate_idx == 0) ? (ui->depth_first.count - 1) : (candidate_idx - 1);
        UiBox *candidate = array_get(&ui->depth_first, candidate_idx);
        Bool valid = candidate->flags & UI_BOX_CAN_FOCUS;
        if (valid && ui->focus_trap) valid = ui_is_descendant(ui->focus_trap, candidate);
        if (valid) {
            ui->focus_idx = candidate_idx;
            ui->focused = candidate;
            break;
        }
        if (candidate_idx == start) break;
    }
}

Bool ui_is_animating () {
    return ui->animation_running;
}

Void ui_frame (Void(*app_build)(), F64 dt) {
    ui->dt = dt;
    ui->animation_running = false;

    array_iter (event, win_get_events(), *) {
        update_input_state(event);

        Rect *root_clip = array_ref_last(&ui->clip_stack);
        Vec2 win = win_get_size();
        root_clip->w = win.x;
        root_clip->h = win.y;

        ui->mouse_cursor = MOUSE_CURSOR_DEFAULT;
        ui->deferred_layout_fns.count = 0;
        ui->depth_first.count = 0;
        ui->focus_trap = 0;

        ui->root = ui_box(0, "root") {
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, win.x, 1});
            ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, win.y, 1});
            ui->root->rect.w = win.x;
            ui->root->rect.h = win.y;

            ui_box(0, "subroot") {
                F32 r = ui->theme->radius_titlebar;
                F32 b = ui->theme->border_width.x;
                F32 s = 0;

                ui_style_f32(UI_FLOAT_X, s);
                ui_style_f32(UI_FLOAT_Y, s);
                ui_style_vec2(UI_PADDING, vec2(b, b));
                ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, win.x-2*s, 0});
                ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, win.y-2*s, 0});
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z2);
                ui_style_f32(UI_OUTSET_SHADOW_WIDTH, s/2);
                ui_style_vec4(UI_OUTSET_SHADOW_COLOR, vec4(0, 0, 0, .6));

                if (ui->config->show_titlebar) {
                    ui->titlebar = ui_box(UI_BOX_REACTIVE, "titlebar") {
                        ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                        ui_style_u32(UI_ALIGN_Y, UI_ALIGN_MIDDLE);
                        ui_style_f32(UI_SPACING, ui->theme->spacing);
                        ui_style_vec2(UI_PADDING, ui->theme->padding_titlebar);
                        ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color_titlebar);
                        ui_style_vec4(UI_BG_COLOR, ui->theme->bg_titlebar);
                        ui_style_vec4(UI_BORDER_WIDTHS, vec4(0, 0, 0, b));
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_CHILDREN_SUM, 0, 1});

                        if (ui->titlebar->signals.hovered) {
                            ui_push_clip(ui->titlebar, true);
                            ui_box(UI_BOX_CLICK_THROUGH, "highlight") {
                                F32 s = ui->titlebar->rect.h/8;
                                ui_style_f32(UI_EDGE_SOFTNESS, 60);
                                ui_style_vec4(UI_RADIUS, vec4(s, s, s, s));
                                ui_style_f32(UI_FLOAT_X, ui->mouse.x - ui->titlebar->rect.x - s);
                                ui_style_f32(UI_FLOAT_Y, ui->mouse.y - ui->titlebar->rect.y - s);
                                ui_style_vec4(UI_BG_COLOR, ui->theme->button_highlight_color);
                                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 2*s, 1});
                                ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, 2*s, 1});
                            }
                            ui_pop_clip();
                        }

                        ui_box(UI_BOX_CLICK_THROUGH, "title") {
                            ui_style_f32(UI_FLOAT_X, 0);
                            ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
                            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                            ui_label_extra(UI_BOX_CLICK_THROUGH, "title", str("Chronomi"), ui->config->font_path_bold, ui->config->font_size, 0);
                        }

                        ui_style_rule(".button") {
                            ui_style_vec2(UI_PADDING, vec2(2, 2));
                            ui_style_vec4(UI_BG_COLOR, vec4(0, 0, 0, 0));
                            ui_style_vec4(UI_BORDER_COLOR, vec4(0, 0, 0, 0));
                            ui_style_vec4(UI_BG_COLOR2, vec4(-1, 0, 0, 0));
                            ui_style_f32(UI_OUTSET_SHADOW_WIDTH, 0);
                            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_CHILDREN_SUM, 0, 1});
                        }

                        ui_style_rule(".button.hover") {
                            ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                            ui_style_vec4(UI_RADIUS, ui->theme->radius);
                            ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                            ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                        }

                        UiBox *pin_button = ui_button(str("pin")){
                            ui_tag("button");
                            UiBox *icon = ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PIN);
                            if (win_is_pinned()) ui_style_box_vec4(icon, UI_TEXT_COLOR, ui->theme->text_color_red);
                            if (pin_button->signals.clicked) win_toggle_pin();
                        }

                        ui_hspacer();

                        UiBox *minimize_button = ui_button(str("minimize")){
                            ui_tag("button");
                            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_MINUS);
                            if (minimize_button->signals.clicked) win_minimize();
                        }

                        UiBox *maximize_button = ui_button(str("maximize")){
                            ui_tag("button");
                            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PLUS);
                            if (maximize_button->signals.clicked) win_toggle_maximize();
                        }

                        UiBox *close_button = ui_button(str("close")){
                            ui_tag("button");
                            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_CLOSE);
                            if (close_button->signals.clicked) win_close();
                        }
                    }
                }

                ui->body = ui_box(0, "body") {
                    if (! ui->config->show_titlebar) ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    app_build();
                }
            }

            if ((ui->event->tag == EVENT_KEY_PRESS) && (event->key == KEY_TAB)) {
                if (event->mods & KEY_MOD_SHIFT) find_prev_focus();
                else                             find_next_focus();
            }
        }

        win_set_cursor(ui->mouse_cursor);

        map_iter (slot, &ui->box_cache) {
            Auto box = slot->val;
            if (box->gc_flag != ui->gc_flag) {
                if (box == ui->focused) {
                    ui->focused = 0;
                    ui->focus_idx = 0;
                }
                array_push(&ui->free_boxes, box);
                ui_free_box_data(box->key);
                map_iter_remove(slot, &ui->box_cache);
            }
        }

        ui->gc_flag = !ui->gc_flag;
    }

    apply_style_rules(ui->root);
    compute_layout(ui->root);

    array_iter (it, &ui->deferred_layout_fns) {
        it.fn(it.box);
        apply_style_rules(it.box);
        compute_layout(it.box->parent);
    }

    find_topmost_hovered_box(ui->root);
    draw_box(ui->root);
    ui->frame++;
    arena_pop_all(cast(Arena*, ui->frame_mem));
}

Void ui_init (UiConfig *config, UiTheme *theme) {
    Arena *perm_arena = arena_new(mem_root, 1*KB);
    Arena *frame_arena = arena_new(mem_root, 64*KB);
    ui = mem_new(cast(Mem*, perm_arena), Ui);
    ui->perm_mem = cast(Mem*, perm_arena);
    ui->frame_mem = cast(Mem*, frame_arena);
    ui->config = config;
    ui->theme = theme;
    array_init(&ui->free_boxes, ui->perm_mem);
    array_init(&ui->box_stack, ui->perm_mem);
    array_init(&ui->clip_stack, ui->perm_mem);
    array_init(&ui->depth_first, ui->perm_mem);
    array_init(&ui->deferred_layout_fns, ui->perm_mem);
    map_init(&ui->box_cache, ui->perm_mem);
    map_init(&ui->pressed_keys, ui->perm_mem);
    map_init(&ui->box_data, ui->perm_mem);
    Vec2 win = win_get_size();
    array_push_lit(&ui->clip_stack, .w=win.x, .h=win.y);
    ui->font_cache = font_cache_new(ui->perm_mem, dr_flush_vertices, 16);
}

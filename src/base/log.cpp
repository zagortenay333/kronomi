#include <stdio.h>
#include "base/log.h"
#include "base/map.h"

// =============================================================================
// Stack Trace:
// =============================================================================
#if BUILD_DEBUG

#include <sanitizer/asan_interface.h>
#include <sanitizer/common_interface_defs.h>

// The 'indent' argument refers to spaces added at each line start.
// The 'caller_frames_to_skip' arg refers to callers of this func.
Void push_stack_trace (AString *a, U64 indent, U64 caller_frames_to_skip) {
    assert_dbg((indent < 128) && (caller_frames_to_skip < 32));

    // We use the libc allocator (CMem) for this array so that
    // __asan_get_alloc_stack can return a stack trace for the
    // allocation at address 'frames.data'.
    Mem cmem = cmem_new();
    Auto frames = array_new_cap<Void*>(&cmem, 32);
    defer { array_free(&frames); };
    frames.count = __asan_get_alloc_stack(frames.data, frames.data, 32 * array_elem_size(&frames), /*@todo*/0);

    tmem_new(tm);
    String buf = { .data=mem_alloc(tm, Char, .size=4*KB), .count=4*KB };
    Bool found_this_frame = false;

    array_iter (addr, &frames) {
        // This function emits zero or more non empty CString's into
        // 'buf' followed by a single empty CString. It emits multiple
        // CString's per frame pointer due to inlined functions.
        __sanitizer_symbolize_pc(addr, TERM_CYAN("%s:%l:%c") " %f", buf.data, buf.count);

        for (String line = str(buf.data); line.count; line = str(line.data + line.count + 1)) {
            if (str_ends_with(line, str(__func__))) {
                found_this_frame = true;
            } else if (! found_this_frame) {
                // Skip callee frames of this function.
            } else if (caller_frames_to_skip) {
                caller_frames_to_skip--;
            } else {
                astr_push_bytes(a, ' ', indent);
                astr_push_str(a, line);
                astr_push_byte(a, '\n');
                if (str_ends_with(line, str("main"))) goto brk;
            }
        }
    } brk:;
}

String get_stack_trace (Mem *mem, U64 indent, U64 frames_to_skip) {
    AString astr = astr_new(mem);
    push_stack_trace(&astr, indent, frames_to_skip + 1);
    return astr_to_str(&astr);
}

Void print_stack_trace () {
    tmem_new(tm);
    String s = get_stack_trace(tm, 4, 1);
    printf("%.*s", STR(s));
}

Void print_stack_trace_fmt Fmt(1, 2) (CString fmt, ...) {
    VaList va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    printf("\n\n");
    print_stack_trace();
}

#endif

// =============================================================================
// Log:
// =============================================================================
tls Log *log_data;

assert_static(LOG_PLAIN == 0);

CString log_tag_str [LOG_TAG_COUNT] = {
    #define X(_, __, STR) STR,
        EACH_LOG_MSG(X)
    #undef X
};

CString log_tag_ansi [LOG_TAG_COUNT] = {
    #define X(_, COL, ...) TERM_START_BOLD TERM_START_##COL,
        EACH_LOG_MSG(X)
    #undef X
};

Void log_setup (Mem *mem, U64 min_block_size) {
    Arena *arena    = arena_new(mem, min_block_size);
    log_data        = mem_new(arena, Log);
    log_data->arena = arena;
}

LogScope *log_scope_start (Bool flush_iterables_on_exit) {
    assert_dbg(!log_data->scope || !log_data->open_msg_data);
    Arena *arena      = log_data->arena;
    LogScope *scope   = mem_new(arena, LogScope);
    scope->prev       = log_data->scope;
    log_data->scope   = scope;
    scope->arena_pos  = log_data->arena->total_count;
    scope->flush_iter = flush_iterables_on_exit;
    array_init(&scope->raw_data, &arena->base);
    array_init(&scope->iterable_data, &arena->base);
    array_init(&scope->iter, &arena->base);
    return scope;
}

Void log_scope_end () {
    LogScope *scope = log_data->scope;
    if (log_data->open_msg_data) log_msg_end();
    if (scope->flush_iter) astr_print(&scope->iterable_data);
    astr_print(&scope->raw_data);
    log_data->scope = scope->prev;
    arena_pop_to(log_data->arena, scope->arena_pos);
}

Void log_scope_end_all () {
    while (log_data->scope) log_scope_end();
}

AString *log_msg_start (LogMsgTag tag, CString user_tag, Bool iterable) {
    assert_dbg(! log_data->open_msg_data);

    LogScope *s     = log_data->scope;
    AString *data   = iterable ? &s->iterable_data : &s->raw_data;
    U64 data_offset = data->count;

    s->count[tag] += 1;
    log_data->open_msg_data = data;

    if (tag != LOG_PLAIN) {
        astr_push_2cstr(data, log_tag_ansi[tag], log_tag_str[tag]);
        if (*user_tag) { astr_push_byte(data, '('); astr_push_2cstr(data, user_tag, ")"); }
        astr_push_cstr(data, TERM_END ": ");
    }

    if (iterable) array_push_lit(
        &s->iter,
        .tag         = tag,
        .data_offset = data_offset,
        .body_offset = data->count,
        IF_BUILD_DEBUG(.trace = get_stack_trace(&log_data->arena->base, 4, 1),)
        .user_tag    = str(user_tag),
    );

    return data;
}

Void log_msg_end () {
    assert_dbg(log_data->open_msg_data);

    LogScope *s = log_data->scope;
    AString *a  = log_data->open_msg_data;

    if (a == &s->iterable_data) {
        LogMsg *msg = array_ref_last(&s->iter);
        msg->trace_offset = a->count;
        IF_BUILD_DEBUG(if (msg->trace.count) astr_push_fmt(a, "\n%.*s\n", STR(msg->trace));)
    }

    log_data->open_msg_data = 0;
}

Void log_msg_fmt (LogMsgTag tag, CString header, Bool iterable, CString fmt, ...) {
    log_msg(msg, tag, header, iterable);
    VaList va;
    va_start(va, fmt);
    astr_push_fmt_va(msg, fmt, va);
    va_end(va);
    astr_push_byte(msg, '\n'); 
}

// =============================================================================
// SrcLog:
// =============================================================================
SrcLogConfig slog_default_config = {
    .left_margin                 = 4,
    .max_lines_above_first_pos   = 1,
    .max_lines_below_last_pos    = 1,
    .max_lines_between_positions = 8,
    .normal_text_ansi            = TERM_START_CYAN,
    .marked_text_ansi            = TERM_START_RED,
};

struct LineIter {
    Char *eof;
    String src;
    String line;
    U64 line_num;
};

// If (pos.length == 0), it is normal text, else it
// is highlighted and belongs to the given position.
struct LineSegment {
    SrcPos pos;
    String content;
};

struct Line {
    U64 num;
    String content;
    Bool has_marks;
    Bool ends_with_ellipsis;
    Array<LineSegment> segments;
};

// Array of non-overlapping SrcPos in the same Src.
// We keep it sorted by SrcPos.offset.
struct PosGroup {
    Array<SrcPos> positions;
};

struct Src {
    String header;
    String content;
    Bool has_eol_mark;
    Array<PosGroup*> groups;
};

struct SrcLog {
    Mem *mem;
    SrcLogConfig config;
    Array<Line*> lines;
    Map<SrcId, Src*> sources;
};

static Char *get_line_start (Char *cursor, Char *stop_at) {
    while ((cursor != stop_at) && (*(cursor - 1) != '\n')) cursor--;
    return cursor;
}

static Char *get_line_end (Char *cursor, Char *stop_at) {
    while ((cursor != stop_at) && (*cursor++ != '\n'));
    return cursor;
}

static Bool lit_next (LineIter *lit) {
    Char *start = lit->line.data + lit->line.count;
    if (start == lit->eof) return false;
    lit->line.data = start;
    lit->line.count = get_line_end(start, lit->eof) - start;
    lit->line_num++;
    return true;
}

static Bool lit_prev (LineIter *lit) {
    Char *cursor = lit->line.data;
    if (cursor == lit->src.data) return false;
    cursor = get_line_start(cursor - 1, lit->src.data);
    lit->line.count = lit->line.data - cursor;
    lit->line.data = cursor;
    lit->line_num--;
    return true;
}

static LineIter line_iter_new (String src, SrcPos pos) {
    LineIter lit     = {};
    Char *first_byte = src.data + pos.offset;
    lit.src          = src;
    lit.eof          = src.data + src.count;
    lit.line_num     = pos.first_line;
    lit.line.data    = get_line_start(first_byte, src.data);
    lit.line.count   = get_line_end(first_byte, lit.eof) - lit.line.data;
    return lit;
}

SrcLog *slog_new (Mem *mem, SrcLogConfig *config) {
    SrcLog *log = mem_new(mem, SrcLog);
    log->mem    = mem;
    log->config = *config;
    array_init(&log->lines, mem);
    map_init(&log->sources, mem, 0);
    return log;
}

Void slog_add_src (SrcLog *log, SrcId id, String header, String content) {
    if (! map_get_ptr(&log->sources, id)) {
        Src *src = mem_new(log->mem, Src);
        src->header = header;
        src->content = content;
        array_init(&src->groups, log->mem);
        map_add(&log->sources, id, src);
    }
}

static Line *add_line (SrcLog *log, LineIter *lit) {
    Line *line = array_try_get_last(&log->lines);

    if (!line || line->num != lit->line_num) {
        line = mem_new(log->mem, Line);
        line->num = lit->line_num;
        line->content = lit->line;
        array_init(&line->segments, log->mem);
        array_push(&log->lines, line);
    }

    return line;
}

static Void add_segment (SrcPos pos, Line *line, U64 offset, U64 count) {
    if (count > 0) {
        LineSegment s = { .pos=pos, .content={ .data=(line->content.data + offset), .count=count }};
        array_push(&line->segments, s);
    }
}

static Void parse_lines (SrcLog *log, Src *src, PosGroup *group) {
    log->lines.count = 0;

    SrcPos first_pos = array_get(&group->positions, 0);
    LineIter lit = line_iter_new(src->content, first_pos);

    for (U64 i = 0; i < log->config.max_lines_above_first_pos; ++i) {
        if (! lit_prev(&lit)) break;
    }

    while (lit.line_num < first_pos.first_line) {
        add_line(log, &lit);
        if (! lit_next(&lit)) break;
    }

    array_iter (pos, &group->positions) {
        assert_dbg(lit.line_num <= pos.first_line);

        if ((pos.first_line - lit.line_num) > log->config.max_lines_between_positions) {
            add_line(log, &lit)->ends_with_ellipsis = true;
            while ((lit.line_num < pos.first_line) && lit_next(&lit));
        } else {
            while (lit.line_num < pos.first_line) {
                add_line(log, &lit);
                if (! lit_next(&lit)) break;
            }
        }

        Line *first_line = add_line(log, &lit);
        U64 col = src->content.data + pos.offset - lit.line.data;

        { // Add normal segment before first marked segment:
            LineSegment *seg = array_try_ref_last(&first_line->segments);
            U64 end = seg ? (seg->content.data + seg->content.count - first_line->content.data) : 0;
            add_segment((SrcPos){}, first_line, end, col - end);
        }

        if (pos.first_line == pos.last_line) {
            if (pos.length == 0) src->has_eol_mark = true;
            add_segment(pos, first_line, col, pos.length);
        } else {
            add_segment(pos, first_line, col, lit.line.count - col);
            lit_next(&lit);

            while (lit.line_num < pos.last_line) {
                Line *line = add_line(log, &lit);
                add_segment(pos, line, 0, line->content.count);
                if (! lit_next(&lit)) break;
            }

            Line *last_line = add_line(log, &lit);
            Char *last_byte = lit.src.data + pos.offset + pos.length;
            add_segment(pos, last_line, 0, last_byte - lit.line.data);
        }
    }

    for (U64 i = 0; i < log->config.max_lines_below_last_pos; ++i) {
        if (! lit_next(&lit)) break;
        add_line(log, &lit);
    }

    array_iter (line, &log->lines) {
        if (line->segments.count == 0) {
            add_segment((SrcPos){}, line, 0, line->content.count);
        } else {
            // Add normal segment at line end if any.
            line->has_marks = true;
            LineSegment *seg = array_ref_last(&line->segments);
            U64 end = seg->content.data + seg->content.count - line->content.data;
            add_segment((SrcPos){}, line, end, line->content.count - end);
        }
    }
}

Void slog_add_pos (SrcLog *log, SrcId id, SrcPos new_pos) {
    Src *src = map_get_assert(&log->sources, id);
    U64 new_pos_end = new_pos.offset + new_pos.length - 1;

    assert_dbg(new_pos.offset <= src->content.count);
    assert_dbg(new_pos.length > 0 || new_pos.offset == src->content.count);

    array_iter (group, &src->groups) {
        array_iter (old_pos, &group->positions) {
            U64 old_pos_end = old_pos.offset + old_pos.length - 1;
            if (old_pos_end < new_pos.offset) continue;
            if (old_pos.offset <= new_pos_end) goto continue_outer;
            array_insert(&group->positions, new_pos, ARRAY_IDX);
            return;
        }

        array_push(&group->positions, new_pos);
        return;
        continue_outer:;
    }

    PosGroup *group = mem_new(log->mem, PosGroup);
    array_init(&group->positions, log->mem);
    array_push(&group->positions, new_pos);
    array_push(&src->groups, group);
}

Void slog_flush (SrcLog *log, AString *astr) {
    map_iter (entry, &log->sources) {
        Src *src = entry->val;
        if (src->groups.count == 0) continue;

        astr_push_fmt(astr, "%*s%sFILE" TERM_END ": %.*s\n\n", static_cast<Int>(log->config.left_margin), "", log->config.marked_text_ansi, STR(src->header));

        array_iter (group, &src->groups) {
            parse_lines(log, src, group);

            U64 left_margin = log->config.left_margin + count_digits(array_get_last(&log->lines)->num);

            array_iter (line, &log->lines) {
                astr_push_fmt(astr, "%s%*lu | " TERM_END, log->config.normal_text_ansi, static_cast<Int>(left_margin), line->num);

                array_iter_ptr (seg, &line->segments) {
                    assert_dbg(seg->content.count != 0);
                    CString color = seg->pos.length ? log->config.marked_text_ansi : log->config.normal_text_ansi;
                    astr_push_fmt(astr, "%s%.*s" TERM_END, color, STR(seg->content));
                }

                Bool no_newline = array_get_last(&line->content) != '\n';
                if (no_newline) astr_push_byte(astr, '\n');

                if (line->has_marks) {
                    astr_push_fmt(astr, "%s%*s | %s", log->config.normal_text_ansi, static_cast<Int>(left_margin), " ", log->config.marked_text_ansi);
                    array_iter_ptr (seg, &line->segments) astr_push_bytes(astr, (seg->pos.length ? '^' : ' '), seg->content.count);

                    if (src->has_eol_mark && ARRAY_ITER_DONE) {
                        if (no_newline) astr_push_byte(astr, '^');
                        else            array_set_last(astr, '^');
                    }

                    astr_push_cstr(astr, TERM_END "\n");
                }

                if (line->ends_with_ellipsis) astr_push_fmt(astr, "%s%*s..." TERM_END "\n", log->config.normal_text_ansi, static_cast<Int>(log->config.left_margin), "");
            }

            if (! ARRAY_ITER_DONE) astr_push_cstr(astr, TERM_END "\n");
        }
    }
}

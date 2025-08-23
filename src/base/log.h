#pragma once

#include "base/core.h"
#include "base/string.h"

// =============================================================================
// Stack Trace:
// ------------
//
// In debug mode these functions use the ASAN runtime to print a
// stack trace in the range [main, one_of_these_funcs) including
// inlined frames, while in release mode they produce a single
// function/file/line string.
// =============================================================================
#if BUILD_DEBUG
    Void    push_stack_trace      (AString *, U64, U64);
    String  get_stack_trace       (Mem *, U64, U64);
    Void    print_stack_trace_fmt Fmt(1, 2) (CString, ...);
    Void    print_stack_trace     ();
#else
    #define build_stack_trace(MEM, INDENT, ...) astr_fmt(MEM, "%*s%s " TERM_CYAN("%s") ":%i\n", cast(Int,INDENT), "", __func__, __FILE__, __LINE__)
    #define get_stack_trace(MEM, INDENT, ...)   ({ AString a = astr_new(MEM); build_stack_trace(&a, INDENT); astr_to_str(&a); })
    #define print_stack_trace_fmt(...)
    #define print_stack_trace(...)
#endif

// =============================================================================
// Log:
// ----
//
// This is a thread local logging system. It's organized
// into nested scopes. When a scope is closed all messages
// within it get flushed.
//
// When pushing a message you can choose whether you want
// to be able to iterate over the parsed form later on.
//
// Init the log system per thread via log_setup().
//
// Usage example:
// --------------
//
//     log_scope(ls, 1); // Var 'ls' closed at scope exit.
//
//     log_msg_fmt(LOG_NOTE, "#0", 1, "A note.");
//     log_msg_fmt(LOG_WARNING, "#1", 1, "A warning.");
//     log_msg_fmt(LOG_NOTE, "#2", 0, "Non-iterable note.");
//     log_msg_fmt(LOG_ERROR, "#3", 1, "An error.");
//
//     log_msg(msg, LOG_PLAIN, "", 0); // Var 'msg' freed at scope exit.
//     astr_push_cstr(msg, "\nIterable messages:\n");
//     
//     array_iter (it, &ls->iter, *) {
//         String body = str_slice(astr_to_str(&ls->iterable_data), it->body_offset, it->trace_offset - it->body_offset - 1);
//         astr_push_fmt(msg, "    [%s] [%.*s] [%.*s]\n", log_tag_str[it->tag], STR(it->user_tag), STR(body));
//     }
//
// Output example:
// ---------------
//
//     NOTE(#0): A note.
//     WARNING(#1): A warning.
//     ERROR(#2): An error.
//     NOTE(#3): Non-iterable note.
//
//     Iterable messages:
//         [NOTE] [#0] [A note.]
//         [WARNING] [#1] [A warning.]
//         [ERROR] [#3] [An error.]
//
// =============================================================================
#define EACH_LOG_MSG(X)\
    X(LOG_PLAIN, BLACK, "")\
    X(LOG_NOTE, GREEN, "NOTE")\
    X(LOG_ERROR, RED, "ERROR")\
    X(LOG_WARNING, YELLOW, "WARNING")

enum LogMsgTag: U8 {
    #define X(TAG, ...) TAG,
        EACH_LOG_MSG(X)
    #undef X
    LOG_TAG_COUNT,
};

struct LogMsg {
    LogMsgTag tag;
    U64 data_offset;
    U64 body_offset;
    U64 trace_offset;
    String trace;
    String user_tag;
};

struct LogScope {
    LogScope *prev;
    U64 arena_pos;
    Bool flush_iter;
    AString raw_data;
    AString iterable_data;
    Array<LogMsg> iter;
    U64 count[LOG_TAG_COUNT];
};

struct Log {
    Arena *arena;
    LogScope *scope;
    AString *open_msg_data;
};

extern tls Log *log_data;
extern CString  log_tag_str  [LOG_TAG_COUNT];
extern CString  log_tag_ansi [LOG_TAG_COUNT];

#define log_scope(N, F)           cleanup(log_scope_end) LogScope *N = log_scope_start(F);
#define log_msg(N, T, U, I)       cleanup(log_msg_end)   AString  *N = log_msg_start(T, U, I);
#define log_msg_fmt(T, U, I, ...) ({ log_msg(_(N), T, U, I); astr_push_fmt(_(N), __VA_ARGS__); astr_push_byte(_(N), '\n'); })

Void      log_setup         (Mem *, U64);
LogScope *log_scope_start   (Bool);
Void      log_scope_end     (LogScope **);
Void      log_scope_end_all ();
AString  *log_msg_start     (LogMsgTag, CString, Bool);
Void      log_msg_end       (AString **);

// =============================================================================
// SrcLog:
// -------
//
// These functions are used for logging highlighted parts of
// source code. It is organized into sources which contain
// positions (or slices) into that source.
//
// Usage example:
// --------------
//
//     SrcLog slog = slog_new(...);
//
//     slog_add_src(&slog, 1, ...);
//     slog_add_pos(&slog, 1, ...);
//     slog_add_pos(&slog, 1, ...);
//
//     slog_add_src(&slog, 2, ...);
//     slog_add_pos(&slog, 2, ...);
//
//     AString astr = astr_new(...);
//     slog_flush(&slog, &astr);
//     astr_print(&astr);
//
// Output example:
// ---------------
//
//     FILE: foo/bar
//
//     1 | fn main {
//     2 |     var x = [420, false];
//       |              ^^^  ^^^^^
//     3 |     var y = x[0] + 1;
//
// =============================================================================
typedef U64 SrcId;
struct SrcLog;

struct SrcPos {
    U64 offset;     // In bytes.
    U64 length;     // In bytes.
    U64 first_line; // 1-indexed.
    U64 last_line;  // 1-indexed.
};

struct SrcLogConfig {
    U8 left_margin;
    U8 max_lines_above_first_pos;
    U8 max_lines_below_last_pos;
    U8 max_lines_between_positions;
    CString normal_text_ansi;
    CString marked_text_ansi;
};

extern SrcLogConfig slog_default_config;

SrcLog *slog_new     (Mem *, SrcLogConfig *);
Void    slog_add_src (SrcLog *, SrcId, String header, String content);
Void    slog_add_pos (SrcLog *, SrcId, SrcPos);
Void    slog_flush   (SrcLog *, AString *);

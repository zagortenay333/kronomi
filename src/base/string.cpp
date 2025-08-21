#if 1
#include <errno.h>
#include <stdio.h>
#include "base/string.h"

// =============================================================================
// String:
// =============================================================================
Bool    is_whitespace (Char c)               { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
CString cstr          (Mem *mem, String s)   { return astr_fmt(mem, "%.*s%c", STR(s), 0).data; }
String  str           (CString s)            { return (String){ .data=const_cast<Char*>(s), .count=strlen(s) }; }
Bool    str_match     (String s1, String s2) { return (s1.count == s2.count) && (! strncmp(s1.data, s2.data, s1.count)); }
U64     istr_hash     (IString *i)           { return str_hash(*i); }
U64     cstr_hash     (CString s)            { return str_hash(str(s)); }
Bool    cstr_match    (CString a, CString b) { return str_match(str(a), str(b)); }
Void    str_clear     (String s, U8 b)       { memset(s.data, b, s.count); }

// Fnv64a
U64 str_hash (String str) {
    U64 h = 0xcbf29ce484222325;
    array_iter (b, &str) h = (h ^ static_cast<U64>(b)) * 0x01000193;
    return h;
}

Bool str_starts_with (String str, String prefix) {
    if (str.count < prefix.count) return false;
    str.count = prefix.count;
    return str_match(str, prefix);
}

Bool str_ends_with (String str, String suffix) {
    if (str.count < suffix.count) return false;
    str = str_suffix_from(str, str.count - suffix.count);
    return str_match(str, suffix);
}

// Returns ARRAY_NIL_IDX if not found.
U64 str_index_of_first (String str, U8 byte) {
    return array_find(&str, [&](Auto it){ return it == byte; });
}

// Returns ARRAY_NIL_IDX if not found.
U64 str_index_of_last (String str, U8 byte) {
    array_iter_back (c, &str) if (c == byte) return ARRAY_IDX;
    return ARRAY_NIL_IDX;
}

String str_slice (String str, U64 offset, U64 count) {
    offset = min(offset, str.count);
    count  = min(count, str.count - offset);
    return (String){ .data=(str.data + offset), .count=count };
}

// Gets rid of whitespace at the beggining and end.
String str_trim (String str) {
    U64 start = 0;
    U64 end   = 0;
    array_iter (c, &str)      if (! is_whitespace(c)) { start = ARRAY_IDX; break; }
    array_iter_back (c, &str) if (! is_whitespace(c)) { end = ARRAY_IDX + 1; break; }
    return str_slice(str, start, end - start);
}

String str_cut_prefix (String str, String prefix) {
    if (str_starts_with(str, prefix)) {
        str.data  += prefix.count;
        str.count -= prefix.count;
    }
    return str;
}

String str_cut_suffix (String str, String suffix) {
    if (str_ends_with(str, suffix)) str.count -= suffix.count;
    return str;
}

// Non-inclusive.
String str_prefix_to (String str, U64 to_idx) {
    str.count = min(to_idx, str.count);
    return str;
}

// Inclusive.
String str_suffix_from (String str, U64 from_idx) {
    U64 idx    = min(from_idx, str.count);
    str.data  += idx;
    str.count -= idx;
    return str;
}

// Non-inclusive.
String str_prefix_to_first (String str, U8 byte) {
    array_iter (c, &str) if (c == byte) return str_prefix_to(str, ARRAY_IDX);
    return (String){};
}

// Non-inclusive.
String str_prefix_to_last (String str, U8 byte) {
    array_iter_back (c, &str) if (c == byte) return str_prefix_to(str, ARRAY_IDX);
    return (String){};
}

// Non-inclusive.
String str_suffix_from_last (String str, U8 byte) {
    array_iter_back (c, &str) if (c == byte) return str_suffix_from(str, ARRAY_IDX + 1);
    return (String){};
}

// Non-inclusive.
String str_suffix_from_first (String str, U8 byte) {
    array_iter (c, &str) if (c == byte) return str_suffix_from(str, ARRAY_IDX + 1);
    return (String){};
}

Bool str_to_u64 (CString str, U64 *out, U64 base) {
    errno = 0;
    Char *endptr = 0;
    *out = static_cast<U64>(std::strtoul(str, &endptr, base));
    return (errno == 0) && (endptr != str);
}

Bool str_to_f64 (CString str, F64 *out) {
    errno = 0;
    Char *endptr = 0;
    *out = std::strtod(str, &endptr);
    return (errno == 0) && (endptr != str);
}

String str_copy (Mem *mem, String str) {
    if (! str.count) return (String){};
    Auto p = mem_alloc(mem, Char, .size=str.count);
    memcpy(p, str.data, str.count);
    return (String){.data=p, .count=str.count};
}

// This function splits the given 'str' into tokens separated by
// bytes that appear in the 'separators' string. For example, for
// the inputs:
//
//     str = "/a/b|c//foobar/"
//     separators = "/|"
//
// ... there are 4 possible outputs depending on the values of the
// 'keep_separators' and 'keep_empties' arguments:
//
//     1. [a] [b] [c] [foobar]
//     2. [] [a] [b] [c] [] [foobar] []
//     3. [/] [a] [/] [b] [|] [c] [/] [/] [foobar] [/]
//     4. [] [/] [a] [/] [b] [|] [c] [/] [] [/] [foobar] [/] []
//
Void str_split (String str, String separators, Bool keep_separators, Bool keep_empties, Array<String> *out) {
    U64 prev_pos = 0;

    array_iter (c, &str) {
        if (! array_has(&separators, c)) continue;
        if (keep_empties || (ARRAY_IDX > prev_pos)) array_push(out, str_slice(str, prev_pos, ARRAY_IDX - prev_pos));
        if (keep_separators) array_push(out, str_slice(str, ARRAY_IDX, 1));
        prev_pos = ARRAY_IDX + 1;
    }

    if (keep_empties || (str.count > prev_pos)) array_push(out, str_slice(str, prev_pos, str.count - prev_pos));
}

// This functions searches the haystack for the needle in a fuzzy way.
// If the needle is *not* found it returns INT64_MIN; otherwise, the
// returned val indicates how close of a match it is (higher is better).
//
// If the 'tokens' argument is not NULL, this function will emit into it
// slices into the haystack where the matches were found. The last token
// emitted is special: it is the remainder of the haystack (from the end
// of the last match to the end of the haystack). This is useful if you
// wish to call this function on the remainder of the haystack over and
// over again until you exhaust it.
//
// The algorithm is a fairly simple O(n) search. First we look ahead to
// see if all chars in the needle appear in the haystack in the exact
// order and separated any number of chars. We then search in reverse
// in hopes of finding a shorter match:
//
//     a b c d e abcdef
//     -------------->|
//               |<----
//
// This algorithm does not try to find the optimal match:
//
//     a b c d e ab c def abcdef
//     ---------------->|
//               |<------
//
// The score is computed based on how many consecutive letters in the
// text were found, whether letters appear at word beginnings, number
// of gaps between letters, ...
I64 str_fuzzy_search (String needle, String haystack, Array<String> *tokens) {
    if (needle.count == 0) return INT64_MIN;
    if (needle.count > haystack.count) return INT64_MIN;

    U64 needle_cursor = 0;
    U64 haystack_end  = 0;

    { // 1. Search forwards to find the initial match:
        array_iter (b, &haystack) {
            if (b == needle.data[needle_cursor]) {
                needle_cursor++;
                if (needle_cursor == needle.count) { haystack_end = ARRAY_IDX; break; }
            }
        }

        if (needle_cursor != needle.count) return INT64_MIN;
        needle_cursor--;
    }

    tmem_new(tm);
    Array<U64> indices; // Map from needle idx to haystack idx.
    if (tokens) { array_init(&indices, tm); array_ensure_count(&indices, needle.count, 0); }

    I64 gaps            = 0;
    I64 consecutives    = 0;
    I64 word_beginnings = 0;

    { // 2. Compute score while searching again in reverse:
        U64 prev_match_idx = ARRAY_NIL_IDX;

        array_iter_back_from (b, &haystack, haystack_end) {
            if (b != needle.data[needle_cursor]) {
                gaps++;
            } else {
                if (tokens) array_set(&indices, needle_cursor, ARRAY_IDX);
                if ((ARRAY_IDX + 1) == prev_match_idx) consecutives++;
                if ((ARRAY_IDX > 1) && is_whitespace(haystack.data[ARRAY_IDX - 1])) word_beginnings++;
                if (needle_cursor == 0) break;
                needle_cursor--;
                prev_match_idx = ARRAY_IDX;
            }
        }

        assert_dbg(needle_cursor == 0);
    }

    if (tokens) { // 3. Emit tokens:
        String token = str_slice(haystack, indices.data[0], 1);

        array_iter_from (i, &indices, 1) {
            if (i == indices.data[ARRAY_IDX - 1] + 1) {
                token.count++;
            } else {
                array_push(tokens, token);
                token = str_slice(haystack, i, 1);
            }
        }

        array_push(tokens, token);
        array_push(tokens, str_slice(haystack, array_get_last(&indices) + 1, haystack.count));
    }

    return max(INT64_MIN+1, (consecutives * 4) + (word_beginnings * 3) - gaps);
}

// =============================================================================
// AString:
// =============================================================================
Void    astr_print         (AString *a)                           { if (a->count) printf("%.*s", STR(*a)); }
Void    astr_println       (AString *a)                           { if (a->count) printf("%.*s\n", STR(*a)); }
CString astr_to_cstr       (AString *a)                           { astr_push_byte(a, 0); return a->data; }
String  astr_to_str        (AString *a)                           { return {a->data, a->count}; }
Void    astr_push_u8       (AString *a, U8 v)                     { array_push_n(a, static_cast<Char>(v)); }
Void    astr_push_2u8      (AString *a, U8 x, U8 y)               { array_push_n(a, static_cast<Char>(x), static_cast<Char>(y)); }
Void    astr_push_3u8      (AString *a, U8 x, U8 y, U8 z)         { array_push_n(a, static_cast<Char>(x), static_cast<Char>(y), static_cast<Char>(z)); }
Void    astr_push_byte     (AString *a, U8 b)                     { array_push(a, static_cast<Char>(b)); }
Void    astr_push_bytes    (AString *a, U8 b, U64 n)              { Slice<Char> s = array_increase_count(a, n, false); if (n) memset(s.data, b, n); }
Void    astr_push_str      (AString *a, String s)                 { array_push_many(a, s); }
Void    astr_push_cstr     (AString *a, CString s)                { astr_push_str(a, String{ .data=const_cast<Char*>(s), .count=(U64)(strlen(s)) }); }
Void    astr_push_cstr_nul (AString *a, CString s)                { astr_push_str(a, String{ .data=const_cast<Char*>(s), .count=(U64)(strlen(s) + 1) }); }
Void    astr_push_2cstr    (AString *a, CString s1, CString s2)   { astr_push_cstr(a, s1); astr_push_cstr(a, s2); }
Void    astr_push_fmt      Fmt(2, 3) (AString *a, CString f, ...) { astr_push_fmt_vam(a, f); }

Void astr_push_fmt_va Fmt(2, 0) (AString *astr, CString fmt, VaList va) {
    VaList va2;
    va_copy(va2, va);
    Int fmt_len = vsnprintf(0, 0, fmt, va);
    assert_always(fmt_len >= 0);
    array_ensure_capacity(astr, fmt_len + 1);
    vsnprintf(astr->data + astr->count, fmt_len + 1, fmt, va2);
    astr->count += fmt_len;
    va_end(va2);
}

// Append the str argument wrapped in double quotes with
// any double quotes within str escaped with a backslash:
//
//     (foo "bar" baz)  ->  ("foo \"bar\" baz")
//
Void astr_push_str_quoted (AString *astr, String str) {
    astr_push_byte(astr, '"');

    Bool escaped = false;
    String chunk = { .data = str.data };

    array_iter (c, &str) {
        if (escaped) {
            escaped = false;
            chunk.count++;
        } else if (c == '"') {
            astr_push_str(astr, chunk);
            astr_push_byte(astr, '\\');
            astr_push_byte(astr, '"');
            chunk.count = 0;
            chunk.data = array_try_ref(&str, ARRAY_IDX + 1);
        } else if (c == '\\') {
            escaped = true;
            chunk.count++;
        } else {
            chunk.count++;
        }
    }

    astr_push_str(astr, chunk);
    astr_push_byte(astr, '"');
}
#endif

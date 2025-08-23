#pragma once

#include "base/array.h"

// =============================================================================
// String:
// =============================================================================
typedef Slice<Char> String;
typedef Slice<Char> IString; // Interned string.

#define STR(X) static_cast<Int>((X).count), (X).data

Bool      is_whitespace         (Char);
CString   cstr                  (Mem *, String);
String    str                   (CString);
U64       istr_hash             (IString *);
U64       cstr_hash             (CString);
U64       str_hash              (String);
U64       str_hash_seed         (String str, U64 seed);
Bool      cstr_match            (CString, CString);
Bool      str_match             (String, String);
Bool      str_starts_with       (String, String prefix);
Bool      str_ends_with         (String, String suffix);
String    str_slice             (String, U64 offset, U64 count);
String    str_trim              (String);
U64       str_index_of_first    (String, U8 byte);
U64       str_index_of_last     (String, U8 byte);
String    str_cut_prefix        (String, String prefix);
String    str_cut_suffix        (String, String suffix);
String    str_prefix_to         (String, U64);
String    str_suffix_from       (String, U64);
String    str_prefix_to_first   (String, U8 byte);
String    str_prefix_to_last    (String, U8 byte);
String    str_suffix_from_first (String, U8 byte);
String    str_suffix_from_last  (String, U8 byte);
Void      str_clear             (String, U8 byte);
Bool      str_to_u64            (CString, U64 *out, U64 base);
Bool      str_to_f64            (CString, F64 *out);
Void      str_split             (String, String seps, Bool keep_seps, Bool keep_empties, Array<String> *);
I64       str_fuzzy_search      (String needle, String haystack, Array<String> *);
String    str_copy              (Mem *, String);

// =============================================================================
// AString: Wrapper around Array for string building.
// =============================================================================
typedef Array<Char> AString;

AString astr_new             (Mem *);
AString astr_new_cap             (Mem *);
Void    astr_print           (AString *);
Void    astr_println         (AString *);
CString astr_to_cstr         (AString *);
String  astr_to_str          (AString *);
Void    astr_push_u8         (AString *, U8);
Void    astr_push_2u8        (AString *, U8, U8);
Void    astr_push_3u8        (AString *, U8, U8, U8);
Void    astr_push_u16        (AString *, U16);
Void    astr_push_u32        (AString *, U64);
Void    astr_push_u64        (AString *, U64);
Void    astr_push_byte       (AString *, U8);
Void    astr_push_bytes      (AString *, U8 byte, U64 n_times);
Void    astr_push_str        (AString *, String);
Void    astr_push_cstr       (AString *, CString);
Void    astr_push_2cstr      (AString *, CString, CString);
Void    astr_push_cstr_nul   (AString *, CString);
Void    astr_push_str_quoted (AString *, String);
Void    astr_push_fmt_va     Fmt(2, 0) (AString *, CString fmt, VaList);
Void    astr_push_fmt        Fmt(2, 3) (AString *, CString fmt, ...);
String  astr_fmt             Fmt(2, 3) (Mem *, CString fmt, ...);

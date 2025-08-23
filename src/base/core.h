#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <bit>
#include <algorithm>
#include "base/context.h"

typedef std::int8_t    I8;
typedef std::int16_t   I16;
typedef std::int32_t   I32;
typedef std::int64_t   I64;
typedef std::uint8_t   U8;
typedef std::uint16_t  U16;
typedef std::uint32_t  U32;
typedef std::uint64_t  U64;
typedef std::uintptr_t UIntPtr;
typedef std::size_t    Size;
typedef std::va_list   VaList;
typedef float          F32;
typedef double         F64;
typedef int            Int;
typedef char           Char;
typedef bool           Bool;
typedef void           Void;
typedef const Char    *CString;

#define TERM_END           "\x1b[0m"
#define TERM_START_BLACK   "\x1b[30m"
#define TERM_START_RED     "\x1b[31m"
#define TERM_START_GREEN   "\x1b[32m"
#define TERM_START_YELLOW  "\x1b[33m"
#define TERM_START_BLUE    "\x1b[34m"
#define TERM_START_MAGENTA "\x1b[35m"
#define TERM_START_CYAN    "\x1b[36m"
#define TERM_START_WHITE   "\x1b[37m"
#define TERM_START_BOLD    "\x1b[1m"
#define TERM_BLACK(TXT)    TERM_START_BLACK   TXT TERM_END
#define TERM_RED(TXT)      TERM_START_RED     TXT TERM_END
#define TERM_GREEN(TXT)    TERM_START_GREEN   TXT TERM_END
#define TERM_YELLOW(TXT)   TERM_START_YELLOW  TXT TERM_END
#define TERM_BLUE(TXT)     TERM_START_BLUE    TXT TERM_END
#define TERM_MAGENTA(TXT)  TERM_START_MAGENTA TXT TERM_END
#define TERM_CYAN(TXT)     TERM_START_CYAN    TXT TERM_END
#define TERM_WHITE(TXT)    TERM_START_WHITE   TXT TERM_END
#define TERM_BOLD(TXT)     TERM_START_BOLD    TXT TERM_END

#define KB        (1024u)
#define MB        (1024u*KB)
#define GB        (1024u*MB)
#define MAX_ALIGN (alignof(std::max_align_t))

#define JOIN_(A, B) A ## B
#define JOIN(A, B)  JOIN_(A, B)

#if BUILD_DEBUG
    #define assert_dbg(...) ({ if (!(__VA_ARGS__)) panic(); })
#else
    #define assert_dbg(...)
#endif

#define tls                thread_local
#define Auto               auto
#define Fmt(FMT, VA)       [[gnu::format(printf, FMT, VA)]]
#define Type(...)          std::remove_reference_t<decltype(__VA_ARGS__)>
#define panic()            __builtin_trap()
#define assert_static(...) static_assert(__VA_ARGS__)
#define badpath            panic()
#define through            [[fallthrough]]
#define flag(N)            (1u << (N))
#define nop(...)
#define identity(...)      __VA_ARGS__
#define _(N)               N##__
#define __(N)              N##___ // Must be different from _() and used mainly in the def macros.
#define is_pow2(N)         (popcount(N) == 1)

using std::max;
using std::min;
using std::swap;
using std::clamp;
using std::popcount;

template <typename T> Void assert_always (T x) { if (! x) panic(); }
template <typename T> T next_pow2 (T x)      { Auto r = std::bit_ceil(x); assert_dbg(r); return r; }
template <typename T> T ceil_div  (T a, T b) { return (a + b - 1) / b;}
template <typename T> T safe_add  (T a, T b) { T r; assert_always(! __builtin_add_overflow(a, b, &r)); return r; }
template <typename T> T safe_sub  (T a, T b) { T r; assert_always(! __builtin_sub_overflow(a, b, &r)); return r; }
template <typename T> T safe_mul  (T a, T b) { T r; assert_always(! __builtin_mul_overflow(a, b, &r)); return r; }

inline U8 leading_one_bits (U8 x)  { return (x == UINT8_MAX)  ?  8 : __builtin_clzll(~x); }
inline U8 leading_one_bits (U32 x) { return (x == UINT32_MAX) ? 32 : __builtin_clzll(~x); }
inline U8 leading_one_bits (U64 x) { return (x == UINT64_MAX) ? 64 : __builtin_clzll(~x); }

// Use the def/let macros to avoid evaling macro args multiple times.
// The args are first moved to name-mangled vars then the final ones.
// This way the passed-in snippets only see mangled identifers. This
// will be robust as long as the __() macro is used mostly in here.
//
//     def1(a, 42);
//     def1(b, static_cast<U8>(X));
//     def2(foo, bar, static_cast<U8>(x), 64);
//
// The let macros are similar but use run-once for loops to make the
// definitions scoped. The loops are easily optimized away:
//
//     let1 (a, 1)  { The a var is only visible here. }
//
#define def1_(W, A, AV)\
    W(Type(AV) __(A) = (AV);)\
    W(Type(__(A)) A = __(A);)
#define def2_(W, A, B, AV, BV)\
    W(Type(AV) __(A) = (AV);) W(Type(BV) __(B) = (BV);)\
    W(Type(__(A)) A = __(A);) W(Type(__(B)) B = __(B);)
#define def3_(W, A, B, C, AV, BV, CV)\
    W(Type(AV) __(A) = (AV);) W(Type(BV) __(B) = (BV);) W(Type(CV) __(C) = (CV);)\
    W(Type(__(A)) A = __(A);) W(Type(__(B)) B = __(B);) W(Type(__(C)) C = __(C);)

#define def1(...) def1_(identity, __VA_ARGS__)
#define def2(...) def2_(identity, __VA_ARGS__)
#define def3(...) def3_(identity, __VA_ARGS__)
#define let_(...) if (__VA_ARGS__ true)
#define let1(...) def1_(let_, __VA_ARGS__)
#define let2(...) def2_(let_, __VA_ARGS__)

// Usage example:
//
// Void foo () {
//     defer {
//         printf("2\n");
//     };
//
//     printf("1\n");
// }
//
template <typename T>
struct Defer {
	T f;
	Defer (T f) : f(f) { };
	Defer (const Defer&) = delete;
	~Defer () { f(); }
};

#define defer Defer JOIN(__deferred, __COUNTER__) = [&]()->Void

// Use the reach/reached macros to mark a position in
// code that must be reached before exiting a scope:
//
//     Foo foo () {
//         reach(r);
//         #define RETURN(R) { reached(r); return r; }
//         if (...) RETURN(a);
//         else     return b; // Triggers assert_dbg().
//     }
//
#if BUILD_DEBUG
    #define reach(N)   I64 _(N##REACH) = 1; defer { assert_dbg(*_(N##REACH) == 0); };
    #define reached(N) _(N##REACH) = 0;
#else
    #define reach(N)
    #define reached(N)
#endif

// Counts number of digits base 10.
U8 count_digits (U64);

// Integer hashing.
U64 hash (U32);
U64 hash (U64);
U64 hash (I32);
U64 hash (I64);

// Bitwise left rotate.
U8  rotl8  (U8  x, U64 r);
U32 rotl32 (U32 x, U64 r);
U64 rotl64 (U64 x, U64 r);

// Returns min n where x+n is multiple of a.
U64 padding_to_align (U64 x, U64 a);

#if COMPILER_CLANG || COMPILER_GCC
    #define atomic_load(X)               __atomic_load(X, __ATOMIC_SEQ_CST)
    #define atomic_inc_load(X)           (__atomic_fetch_add(X, 1, __ATOMIC_SEQ_CST) + 1)
    #define atomic_dec_load(X)           (__atomic_fetch_add(X, 1, __ATOMIC_SEQ_CST) - 1)
    #define atomic_exchange(X, C)        __atomic_exchange_n(X, C, __ATOMIC_SEQ_CST)
    #define atomic_cmp_exchange(X, E, D) ({ def3(x, e, d, X, E, D); __atomic_compare_exchange_n(x, &e, d, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); e; })
#else
    #error "No atomics."
#endif

// Saturating ops.
U8  sat_add8  (U8, U8);
U32 sat_add32 (U32, U32);
U64 sat_add64 (U64, U64);
U8  sat_sub8  (U8, U8);
U32 sat_sub32 (U32, U32);
U64 sat_sub64 (U64, U64);
U8  sat_mul8  (U8, U8);
U32 sat_mul32 (U32, U32);
U64 sat_mul64 (U64, U64);

// Pseudo random number generator.
Void random_setup (); // Per thread init.
U64  random_u64   ();
U64  random_range (U64 l, U64 u); // In range [l, u).

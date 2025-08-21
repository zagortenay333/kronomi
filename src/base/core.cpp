#include "base/core.h"
#include "base/string.h"
// #include "os/time.h"

assert_static(sizeof(F32) == 4);
assert_static(sizeof(F64) == 8);

U8 count_digits (U64 n) {
    U8 d = 1;
    while (n >= 10) { d++; n /= 10; }
    return d;
}

U64 padding_to_align (U64 x, U64 a) {
    assert_dbg(is_pow2(a));
    return (a - (x & (a-1))) & (a-1);
}

U64 hash_u32 (U32 n) { return str_hash(String{ .data=reinterpret_cast<Char*>(&n), .count=4 }); }
U64 hash_u64 (U64 n) { return str_hash(String{ .data=reinterpret_cast<Char*>(&n), .count=8 }); }
U64 hash_i32 (I32 n) { return str_hash(String{ .data=reinterpret_cast<Char*>(&n), .count=4 }); }
U64 hash_i64 (I64 n) { return str_hash(String{ .data=reinterpret_cast<Char*>(&n), .count=8 }); }

U8  rotl8  (U8  x, U64 r) { r &= 7;  return (x << r) | (x >> ((8-r) & 7));   }
U32 rotl32 (U32 x, U64 r) { r &= 31; return (x << r) | (x >> ((32-r) & 31)); }
U64 rotl64 (U64 x, U64 r) { r &= 63; return (x << r) | (x >> ((64-r) & 63)); }

U8  sat_sub8  (U8 x, U8 y)   { U8 z;  return __builtin_sub_overflow(x, y, &z) ? 0 : z; }
U32 sat_sub32 (U32 x, U32 y) { U32 z; return __builtin_sub_overflow(x, y, &z) ? 0 : z; }
U64 sat_sub64 (U64 x, U64 y) { U64 z; return __builtin_sub_overflow(x, y, &z) ? 0 : z; }
U8  sat_add8  (U8 x, U8 y)   { U8 z;  return __builtin_add_overflow(x, y, &z) ? UINT8_MAX  : z; }
U32 sat_add32 (U32 x, U32 y) { U32 z; return __builtin_add_overflow(x, y, &z) ? UINT32_MAX : z; }
U64 sat_add64 (U64 x, U64 y) { U64 z; return __builtin_add_overflow(x, y, &z) ? UINT64_MAX : z; }
U8  sat_mul8  (U8 x, U8 y)   { U8 z;  return __builtin_mul_overflow(x, y, &z) ? UINT8_MAX  : z; }
U32 sat_mul32 (U32 x, U32 y) { U32 z; return __builtin_mul_overflow(x, y, &z) ? UINT32_MAX : z; }
U64 sat_mul64 (U64 x, U64 y) { U64 z; return __builtin_mul_overflow(x, y, &z) ? UINT64_MAX : z; }

#if 0 // @todo
// A pseudo random number generator.
// The xorshift64_state must not be initted to zero.
tls U64 xorshift64_state;
U64 xorshift64 () {
	U64 x = xorshift64_state;
	x    ^= x << 13;
	x    ^= x >> 7;
	x    ^= x << 17;
	return xorshift64_state = x;
}

Void random_setup () { xorshift64_state = os_time_ms() ?: 1; }
U64  random_u64   () { return xorshift64(); }

// This uses the non-deterministic openBSD algo.
U64 random_range (U64 l, U64 u) {
    if (u < l) swap(l, u);
    u -= l;
    U64 r;
    do r = random_u64(); while (r < ((-u) % u));
    return (r % u) + l;
}
#endif

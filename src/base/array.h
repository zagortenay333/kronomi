#pragma once

// =============================================================================
// Overview:
// ---------
//
// A polymorphic dynamic array with bounds checking.
//
// All macros here eval their arguments only once.
//
// To ensure bounds checking, elements should only be accessed
// with provided methods such as: array_get, array_set, etc...
//
// The array calls mem_grow/mem_shrink which can invalidate
// slices/pointers to it's elements depending on the allocator.
//
// Usage example:
// --------------
//
//     Array(U64) a;
//     array_init(&a, mem);
//     array_push_n(&a, 42, 1, 420);
//     array_iter (x, &a, *) if (*x == 420) *x = 1000;
//
// Careful when mutating the array while looping over it.
// If looping *forward* you can append to the array as well
// as remove the current element:
//
//     array_iter (x, &a) if (x == 1) array_remove(&a, ARRAY_IDX--);
//     array_iter (x, &a) printf("[%lu] = %lu\n", ARRAY_IDX, x);
//
// =============================================================================
#include "base/mem.h"

template <typename T>
struct Array {
    T *data;
    U64 count;
    U64 capacity;
    Mem *mem;
};

template <typename T>
struct Slice {
    T *data;
    U64 count;
};

#define AElem(A) Type((A)->data[0])

const U64 ARRAY_NIL_IDX = UINT64_MAX;

template <typename T> U64 array_esize (Array<T> *a) { return sizeof(AElem(a)); }
template <typename T> U64 array_size  (Array<T> *a) { return sizeof(T) * a->count; }
template <typename T> U64 array_bounds_check (Array<T> *a, U64 idx) { assert_always(idx < a->count); }

template <typename T>
Void array_maybe_decrease_capacity (Array<T> *a) {
    if ((a->capacity > 4) && (a->count < (safe_mul(a->capacity, 25) / 100))) {
        U64 new_cap = 2 * a->count;
        a->data     = mem_shrink(a->mem, T, .size=(sizeof(T) * new_cap), .old_ptr=a->data, .old_size=(sizeof(T) * a->capacity));
        a->capacity = new_cap;
    }
}

template <typename T>
Void array_increase_capacity (Array<T> *a, U64 n) {
    assert_dbg(n);
    U64 new_cap = safe_add(a->capacity, n);
    a->data     = mem_grow(a->mem, U8, .size=(sizeof(T) * new_cap), .old_ptr=a->data, .old_size=(sizeof(T) * a->capacity));
    a->capacity = new_cap;
}

template <typename T>
Void array_ensure_capacity_min (Array<T> *a, U64 n) {
    U64 unused = a->capacity - a->count;
    if (unused < n) array_increase_capacity(a, (n - unused));
}

template <typename T>
Slice<T> array_increase_count (Array<T> *a, U64 n, Bool zeroed) {
    if (n) array_ensure_capacity(a, n);
    Slice<T> r = { .data=&a->data[a->count], .count=n };
    a->count += n;
    if (zeroed) memset(r.data, 0, sizeof(T) * n);
    return r;
}

template <typename T>
Void *array_increase_count_p (Array<T> *a, U64 n, Bool zeroed) {
    Slice<T> out = array_increase_count(a, n, zeroed);
    return out.data;
}

template <typename T>
Void array_ensure_count (Array<T> *a, U64 n, Bool zeroed) {
    if (a->count < n) array_increase_count(a, (n - a->count), zeroed);
}

template <typename T>
T *array_push_slot (Array<T> *a) {
    if (a->count == a->capacity) {
        U64 new_cap = a->capacity ? static_cast<U64>(1.8 * a->capacity) : 2;
        assert_always(new_cap > a->capacity);
        array_increase_capacity(a, new_cap);
    }
    T *r = &a->data[a->count];
    a->count++;
    return r;
}

template <typename T>
T *array_insert_slot (Array<T> *a, U64 idx) {
    if (idx == a->count) return array_push_slot(a);
    array_bounds_check(a, idx);
    array_ensure_capacity(a, 1);
    T *p = &a->data[idx];
    memmove(p+1, p, sizeof(T) * (a->count - idx));
    a->count++;
    return p;
}

// #define array_increase_count_o(A, N, Z, O) uarray_increase_count(uarray_from(A), array_esize(A), N, Z, uslice_from(O));

template <typename T> Void     array_init     (Array<T> *a, Mem *mem) { *a = { .mem=mem }; }
template <typename T> Void     array_init_cap (Array<T> *a, Mem *mem, U64 cap) { array_init(a, mem); array_increase_capacity(a, cap); }
template <typename T> Array<T> array_new      (Mem *mem)   { Array<T> a; array_init(&a, mem); return a; }
template <typename T> Void     array_free     (Array<T> a) { mem_free(a->mem, .old_ptr=a->data, .old_size=array_size(a)); }

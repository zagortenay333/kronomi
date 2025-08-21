#pragma once

// =============================================================================
// Overview:
// ---------
//
// A dynamic array with bounds checking.
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
//     Auto a = array_new<U64>(mem);
//     array_push_n(&a, 42, 1, 420);
//     array_iter_ptr (x, &a) if (*x == 420) *x = 1000;
//
// Careful when mutating the array while looping over it.
// If looping *forward* you can append to the array as well
// as remove the current element:
//
//     array_iter (x, &a) if (x == 1) array_remove(&a, ARRAY_IDX--);
//     array_iter (x, &a) printf("[%lu] = %lu\n", ARRAY_IDX, x);
//
// =============================================================================
#include <cstring>
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

// =============================================================================
// Iteration:
// =============================================================================
#define array_iter(X, A)              let1(ARRAY, A)        ARRAY_ITER(X, 0, (ARRAY_IDX < ARRAY->count), ++ARRAY_IDX)
#define array_iter_from(X, A, I)      let2(ARRAY, I_, A, I) ARRAY_ITER(X, I_, (ARRAY_IDX < ARRAY->count), ++ARRAY_IDX)
#define array_iter_back(X, A)         let1(ARRAY, A)        ARRAY_ITER(X, ARRAY->count, (ARRAY_IDX-- > 0), /**/)
#define array_iter_back_from(X, A, I) let2(ARRAY, I_, A, I) ARRAY_ITER(X, (ARRAY->count ? I_+1 : 0), (ARRAY_IDX-- > 0), /**/)
#define ARRAY_ITER(X, F, C, INC)      if (U64 ARRAY_IDX=(F); true)\
                                      for (AElem(ARRAY) X; (C) && (X = ARRAY->data[ARRAY_IDX], true); INC)

#define array_iter_ptr(X, A)              let1(ARRAY, A)        ARRAY_ITER_PTR(X, 0, (ARRAY_IDX < ARRAY->count), ++ARRAY_IDX)
#define array_iter_ptr_from(X, A, I)      let2(ARRAY, I_, A, I) ARRAY_ITER_PTR(X, I_, (ARRAY_IDX < ARRAY->count), ++ARRAY_IDX)
#define array_iter_ptr_back(X, A)         let1(ARRAY, A)        ARRAY_ITER_PTR(X, ARRAY->count, (ARRAY_IDX-- > 0), /**/)
#define array_iter_ptr_back_from(X, A, I) let2(ARRAY, I_, A, I) ARRAY_ITER_PTR(X, (ARRAY->count ? I_+1 : 0), (ARRAY_IDX-- > 0), /**/)
#define ARRAY_ITER_PTR(X, F, C, INC)      if (U64 ARRAY_IDX=(F); true)\
                                          for (AElem(ARRAY) *X; (C) && (X = &ARRAY->data[ARRAY_IDX], true); INC)

// =============================================================================
// Memory management:
// =============================================================================
template <typename T> U64 array_elem_size (Array<T> *a) { return sizeof(T); }
template <typename T> U64 array_byte_size (Array<T> *a) { return sizeof(T) * a->count; }

template <typename T>
Void array_maybe_decrease_capacity (Array<T> *a) {
    if ((a->capacity > 4) && (a->count < (safe_mul(a->capacity, 25lu) / 100))) {
        U64 new_cap = 2 * a->count;
        a->data     = mem_shrink(a->mem, T, .size=(sizeof(T) * new_cap), .old_ptr=a->data, .old_size=(sizeof(T) * a->capacity));
        a->capacity = new_cap;
    }
}

template <typename T>
Void array_ensure_capacity (Array<T> *a, U64 n) {
    assert_dbg(n);
    U64 new_cap = a->capacity ?: n;
    while ((new_cap - a->count) < n) new_cap = safe_mul(new_cap, 2lu);
    U64 dt = new_cap - a->capacity;
    if (dt) array_increase_capacity(a, dt);
}

template <typename T>
Void array_increase_capacity (Array<T> *a, U64 n) {
    assert_dbg(n);
    U64 new_cap = safe_add(a->capacity, n);
    a->data     = mem_grow(a->mem, T, .size=(sizeof(T) * new_cap), .old_ptr=a->data, .old_size=(sizeof(T) * a->capacity));
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
    if (zeroed) std::memset(r.data, 0, sizeof(T) * n);
    return r;
}

template <typename T>
Void array_ensure_count (Array<T> *a, U64 n, Bool zeroed) {
    if (a->count < n) array_increase_count(a, (n - a->count), zeroed);
}

// =============================================================================
// Init:
// =============================================================================
template <typename T> Void     array_init     (Array<T> *a, Mem *mem)          { *a = { .mem=mem }; }
template <typename T> Void     array_init_cap (Array<T> *a, Mem *mem, U64 cap) { array_init(a, mem); array_increase_capacity(a, cap); }
template <typename T> Array<T> array_new      (Mem *mem)                       { Array<T> a; array_init(&a, mem); return a; }
template <typename T> Array<T> array_new_cap  (Mem *mem, U64 cap)              { Array<T> a; array_init_cap(&a, mem, cap); return a; }
template <typename T> Void     array_free     (Array<T> a)                     { mem_free(a->mem, .old_ptr=a->data, .old_size=array_byte_size(a)); }

// =============================================================================
// Access:
// =============================================================================
template <typename T> Void array_bounds_check (Array<T> *a, U64 idx)    { assert_always(idx < a->count); }
template <typename T> T   *array_ref          (Array<T> *a, U64 i)      { array_bounds_check(a, i); return &a->data[i]; }
template <typename T> T    array_get          (Array<T> *a, U64 i)      { return *array_ref(a, i); }
template <typename T> T    array_set          (Array<T> *a, U64 i, T v) { return *array_ref(a, i) = v; }
template <typename T> T   *array_try_ref      (Array<T> *a, U64 i)      { return (i < a->count) ? &a->data[i] : 0; }
template <typename T> T    array_try_get      (Array<T> *a, U64 i)      { return (i < a->count) ? a->data[i] : AElem(a){}; }
template <typename T> T   *array_ref_last     (Array<T> *a)             { array_bounds_check(a, 0); return &a->data[a->count - 1]; }
template <typename T> T    array_get_last     (Array<T> *a)             { return *array_ref_last(a); }
template <typename T> T    array_set_last     (Array<T> *a, T v)        { return *array_ref_last(a) = v; }
template <typename T> T   *array_try_ref_last (Array<T> *a)             { return a->count ? &a->data[a->count - 1] : 0; }
template <typename T> T    array_try_get_last (Array<T> *a)             { return a->count ? a->data[a->count - 1] : AElem(a){}; }

// =============================================================================
// Sorting:
// =============================================================================
Int c_compare (U8 *a, U8 *b)   { return (*a < *b) ? -1 : (*a > *b) ? 1 : 0; }
Int c_compare (U32 *a, U32 *b) { return (*a < *b) ? -1 : (*a > *b) ? 1 : 0; }
Int c_compare (U64 *a, U64 *b) { return (*a < *b) ? -1 : (*a > *b) ? 1 : 0; }

template <typename T>
Void array_sort_cmp (Array<T> *a, Int(*cmp)(T*, T*)) {
    Auto fn = reinterpret_cast<int(*)(const Void *, const Void *)>(cmp);
    std::qsort(a->data, a->count, sizeof(T), fn);
}

template <typename T> Void array_sort    (Array<T> *a)               { array_sort_cmp(a, c_compare); }
template <typename T> Void array_swap    (Array<T> *a, U64 i, U64 j) { T *e1=array_ref(a, i), *e2=array_ref(a, j), tmp=*e1; *e1=*e2; *e2=tmp; }
template <typename T> Void array_reverse (Array<T> *a)               { for (U64 i=0; i < a->count/2; ++i) array_swap(a, i, a->count-i-1); }
template <typename T> Void array_shuffle (Array<T> *a)               { array_iter (x, a) { x; swap(ARRAY->data[ARRAY_IDX], ARRAY->data[random_range(ARRAY_IDX, ARRAY->count)]); } }

// =============================================================================
// Removal:
// =============================================================================
template <typename T>
Void array_remove (Array<T> *a, U64 idx) {
    array_bounds_check(a, idx);
    if (idx < (a->count - 1)) {
        T *src = &a->data[idx + 1];
        T *dst = &a->data[idx];
        memmove(dst, src, sizeof(T) * (a->count - idx - 1));
    }
    a->count--;
}

template <typename T> T    array_pop         (Array<T> *a)        { T r = array_get_last(a); a->count--; return r; }
template <typename T> T    array_pop_or      (Array<T> *a, T v)   { return a->count ? array_pop(a) : v; }
template <typename T> Void array_remove_fast (Array<T> *a, U64 i) { array_set(a, i, array_get_last(a)); a->count--; }
template <typename T> Void array_swap_remove (Array<T> *a, U64 i) { array_swap(a, i, a->count-1); a->count--; }

// =============================================================================
// Search:
// =============================================================================
template <typename T>
U64 array_bsearch (Array<T> *a, T *elem, Int(*cmp)(T*, T*)) {
    Void *p = std::bsearch(elem, a->data, a->count, sizeof(T), cmp);
    return p ? (static_cast<U8*>(p) - a->data) / sizeof(T) : ARRAY_NIL_IDX;
}

template <typename T, typename F>
U64 array_find (Array<T> *a, const F &f) {
    U64 r = ARRAY_NIL_IDX;
    array_iter (it, a) if (f(it)) { r = ARRAY_IDX; break; }
    return r;
}

template <typename T, typename F>
T array_find_get (Array<T> *a, const F &f) {
    T r = {};
    array_iter (it, a) if (f(it)) { r = it; break; }
    return r;
}

template <typename T, typename F>
T *array_find_ptr (Array<T> *a, const F &f) {
    T *r = 0;
    array_iter_ptr (it, a) if (f(it)) { r = it; break; }
    return r;
}

template <typename T, typename F>
Void array_find_remove (Array<T> *a, const F &f) {
    array_iter (it, a) if (f(it)) { array_remove(a, ARRAY_IDX); break; }
}

template <typename T, typename F>
Void array_find_remove_fast (Array<T> *a, const F &f) {
    array_iter (it, a) if (f(it)) { array_remove_fast(a, ARRAY_IDX); break; }
}

template <typename T, typename F>
Void array_find_remove_all_fast (Array<T> *a, const F &f) {
    array_iter_back (it, a) if (f(it)) array_remove_fast(a, ARRAY_IDX);
}

template <typename T, typename F>
Void array_find_replace (Array<T> *a, const F &f, T r) {
    array_iter (it, a) if (f(it)) { a->data[ARRAY_IDX] = r; break; }
}

template <typename T, typename F>
Void array_find_replace_all (Array<T> *a, const F &f, T r) {
    array_iter (it, a) if (f(it)) a->data[ARRAY_IDX] = r;
}

template <typename T, typename F>
Void array_find_remove_all (Array<T> *a, const F &f) {
    U64 n = 0;
    array_iter (it, a) if (! f(it)) { a->data[n++] = it; }
    a->count = n;
}

template <typename T>
Bool array_has (Array<T> *a, T e)  {
    return !!array_find_ref(a, [&](T *it){ return e == *it; });
}

// =============================================================================
// Insertion:
// =============================================================================
#define array_push_lit(A, ...)   array_push(A, AElem(A){__VA_ARGS__})
#define array_insert_lit(A, ...) array_insert(A, AElem(A){__VA_ARGS__})

#define array_push_n(A, ...) do {\
    AElem(A) _(E)[] = {__VA_ARGS__};\
    array_push_many(A, Slice<AElem(A)>{ .data=_(E), .count=(sizeof(_(E)) / sizeof(_(E)[0])) });\
}while(0)

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

template <typename T>
Slice<T> array_insert_gap (Array<T> *a, U64 count, U64 idx, Bool zeroed) {
    idx = min(a->count, idx);
    U64 bytes_to_move = a->count - idx;
    array_increase_count(a, count, false);
    T *p = &a->data[idx];
    memmove(&p[count], p, sizeof(T) * bytes_to_move);
    Slice<T> r = { .data=p, .count=count };
    if (zeroed) std::memset(r.data, 0, sizeof(T) * count);
    return r;
}

template <typename T>
Void array_push_many (Array<T> *a, Slice<T> elems) {
    if (elems.count) {
        Slice<T> p = array_increase_count(a, elems.count, false);
        std::memcpy(p.data, elems.data, sizeof(T) * elems.count);
    }
}

template <typename T>
Void array_insert_many (Array<T> *a, Slice<T> elems, U64 idx) {
    if (elems.count) {
        Slice <T> p = array_insert_gap(a, elems.count, idx, false);
        std::memcpy(p.data, elems.data, sizeof(T) * elems.count);
    }
}

template <typename T> Void array_push   (Array<T> *a, T e) { *array_push_slot(a) = e; }
template <typename T> Void array_insert (Array<T> *a, T e) { *array_insert_slot(a) = e; }
template <typename T> Void array_push_if_unique (Array<T> *a, T e) { if (! array_has(a, e)) array_push(a, e); }

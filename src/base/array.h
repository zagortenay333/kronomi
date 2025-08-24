#pragma once

// =============================================================================
// Overview:
// ---------
//
// A dynamic array with bounds checking for POD types.
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
struct Slice {
    T *data;
    U64 count;
};

template <typename T>
struct Array {
    T *data;
    U64 count;
    U64 capacity;
    Mem *mem;
};

#define Elem(T)  Type(static_cast<T*>(0)->data[0])
#define Elemv(A) Type((A)->data[0])

const U64 ARRAY_NIL_IDX = UINT64_MAX;

// =============================================================================
// Iteration:
// =============================================================================
#define array_iter(X, A)              let1(ARRAY, A)        ARRAY_ITER(X, 0, (ARRAY_IDX < ARRAY->count), ++ARRAY_IDX)
#define array_iter_from(X, A, I)      let2(ARRAY, I_, A, I) ARRAY_ITER(X, I_, (ARRAY_IDX < ARRAY->count), ++ARRAY_IDX)
#define array_iter_back(X, A)         let1(ARRAY, A)        ARRAY_ITER(X, ARRAY->count, (ARRAY_IDX-- > 0), /**/)
#define array_iter_back_from(X, A, I) let2(ARRAY, I_, A, I) ARRAY_ITER(X, (ARRAY->count ? I_+1 : 0), (ARRAY_IDX-- > 0), /**/)
#define ARRAY_ITER(X, F, C, INC)      if (U64 ARRAY_IDX=(F); true)\
                                      for (Elemv(ARRAY) X; (C) && (X = ARRAY->data[ARRAY_IDX], true); INC)

#define array_iter_ptr(X, A)              let1(ARRAY, A)        ARRAY_ITER_PTR(X, 0, (ARRAY_IDX < ARRAY->count), ++ARRAY_IDX)
#define array_iter_ptr_from(X, A, I)      let2(ARRAY, I_, A, I) ARRAY_ITER_PTR(X, I_, (ARRAY_IDX < ARRAY->count), ++ARRAY_IDX)
#define array_iter_ptr_back(X, A)         let1(ARRAY, A)        ARRAY_ITER_PTR(X, ARRAY->count, (ARRAY_IDX-- > 0), /**/)
#define array_iter_ptr_back_from(X, A, I) let2(ARRAY, I_, A, I) ARRAY_ITER_PTR(X, (ARRAY->count ? I_+1 : 0), (ARRAY_IDX-- > 0), /**/)
#define ARRAY_ITER_PTR(X, F, C, INC)      if (U64 ARRAY_IDX=(F); true)\
                                          for (Elemv(ARRAY) *X; (C) && (X = &ARRAY->data[ARRAY_IDX], true); INC)

#define ARRAY_ITER_DONE (ARRAY_IDX == (ARRAY->count - 1))

// =============================================================================
// Memory management:
// =============================================================================
template <typename T> U64 array_elem_size (T *a) { return sizeof(Elem(T)); }
template <typename T> U64 array_byte_size (T *a) { return array_elem_size(a) * a->count; }

template <typename T>
Void array_maybe_decrease_capacity (T *a) {
    if ((a->capacity > 4) && (a->count < (safe_mul(a->capacity, 25lu) / 100))) {
        U64 new_cap = 2 * a->count;
        a->data     = mem_shrink(a->mem, Elem(T), .size=(array_elem_size(a) * new_cap), .old_ptr=a->data, .old_size=(array_elem_size(a) * a->capacity));
        a->capacity = new_cap;
    }
}

template <typename T>
Void array_ensure_capacity (T *a, U64 n) {
    assert_dbg(n);
    U64 new_cap = a->capacity ?: n;
    while ((new_cap - a->count) < n) new_cap = safe_mul(new_cap, 2lu);
    U64 dt = new_cap - a->capacity;
    if (dt) array_increase_capacity(a, dt);
}

template <typename T>
Void array_increase_capacity (T *a, U64 n) {
    assert_dbg(n);
    U64 new_cap = safe_add(a->capacity, n);
    a->data     = mem_grow(a->mem, Elem(T), .size=(array_elem_size(a) * new_cap), .old_ptr=a->data, .old_size=(array_elem_size(a) * a->capacity));
    a->capacity = new_cap;
}

template <typename T>
Void array_ensure_capacity_min (T *a, U64 n) {
    U64 unused = a->capacity - a->count;
    if (unused < n) array_increase_capacity(a, (n - unused));
}

template <typename T>
Slice<Elem(T)> array_increase_count (T *a, U64 n, Bool zeroed) {
    if (n) array_ensure_capacity(a, n);
    Slice<Elem(T)> r = { .data=&a->data[a->count], .count=n };
    a->count += n;
    if (zeroed) std::memset(r.data, 0, array_elem_size(a) * n);
    return r;
}

template <typename T>
Void array_ensure_count (T *a, U64 n, Bool zeroed) {
    if (a->count < n) array_increase_count(a, (n - a->count), zeroed);
}

// =============================================================================
// Init:
// =============================================================================
template <typename T> Void     array_init     (T *a, Mem *mem)          { *a = { .mem=mem }; }
template <typename T> Void     array_init_cap (T *a, Mem *mem, U64 cap) { array_init(a, mem); array_increase_capacity(a, cap); }
template <typename T> Array<T> array_new      (Mem *mem)                { Array<T> a; array_init(&a, mem); return a; }
template <typename T> Array<T> array_new_cap  (Mem *mem, U64 cap)       { Array<T> a; array_init_cap(&a, mem, cap); return a; }
template <typename T> Void     array_free     (T a)                     { mem_free(a->mem, .old_ptr=a->data, .old_size=array_byte_size(a)); }

// =============================================================================
// Access:
// =============================================================================
template <typename T> Void     array_bounds_check (T *a, U64 i)            { assert_always(i < a->count); }
template <typename T> Elem(T) *array_ref          (T *a, U64 i)            { array_bounds_check(a, i); return &a->data[i]; }
template <typename T> Elem(T)  array_get          (T *a, U64 i)            { return *array_ref(a, i); }
template <typename T> Elem(T)  array_set          (T *a, U64 i, Elem(T) v) { return *array_ref(a, i) = v; }
template <typename T> Elem(T) *array_try_ref      (T *a, U64 i)            { return (i < a->count) ? &a->data[i] : 0; }
template <typename T> Elem(T)  array_try_get      (T *a, U64 i)            { return (i < a->count) ? a->data[i] : Elem(T){}; }
template <typename T> Elem(T) *array_ref_last     (T *a)                   { array_bounds_check(a, 0); return &a->data[a->count - 1]; }
template <typename T> Elem(T)  array_get_last     (T *a)                   { return *array_ref_last(a); }
template <typename T> Elem(T)  array_set_last     (T *a, Elem(T) v)        { return *array_ref_last(a) = v; }
template <typename T> Elem(T) *array_try_ref_last (T *a)                   { return a->count ? &a->data[a->count - 1] : 0; }
template <typename T> Elem(T)  array_try_get_last (T *a)                   { return a->count ? a->data[a->count - 1] : Elem(T){}; }

template <typename T> Slice<Elem(T)> slice (T *a) { return { .data=a->data, .count=a->count }; }

// =============================================================================
// Sorting:
// =============================================================================
inline Int c_compare (U8 *a, U8 *b)   { return (*a < *b) ? -1 : (*a > *b) ? 1 : 0; }
inline Int c_compare (U32 *a, U32 *b) { return (*a < *b) ? -1 : (*a > *b) ? 1 : 0; }
inline Int c_compare (U64 *a, U64 *b) { return (*a < *b) ? -1 : (*a > *b) ? 1 : 0; }

template <typename T>
Void array_sort_cmp (T *a, Int(*cmp)(Elem(T)*, Elem(T)*)) {
    Auto fn = reinterpret_cast<int(*)(const Void *, const Void *)>(cmp);
    std::qsort(a->data, a->count, array_elem_size(a), fn);
}

template <typename T> Void array_sort    (T *a)               { array_sort_cmp(a, c_compare); }
template <typename T> Void array_swap    (T *a, U64 i, U64 j) { Elem(T) *e1=array_ref(a, i), *e2=array_ref(a, j), tmp=*e1; *e1=*e2; *e2=tmp; }
template <typename T> Void array_reverse (T *a)               { for (U64 i=0; i < a->count/2; ++i) array_swap(a, i, a->count-i-1); }
template <typename T> Void array_shuffle (T *a)               { array_iter (x, a) { x; swap(ARRAY->data[ARRAY_IDX], ARRAY->data[random_range(ARRAY_IDX, ARRAY->count)]); } }

// =============================================================================
// Removal:
// =============================================================================
template <typename T>
Void array_remove (T *a, U64 idx) {
    array_bounds_check(a, idx);
    if (idx < (a->count - 1)) {
        Elem(T) *src = &a->data[idx + 1];
        Elem(T) *dst = &a->data[idx];
        memmove(dst, src, array_elem_size(a) * (a->count - idx - 1));
    }
    a->count--;
}

template <typename T> T    array_pop         (T *a)            { Auto r = array_get_last(a); a->count--; return r; }
template <typename T> T    array_pop_or      (T *a, Elem(T) v) { return a->count ? array_pop(a) : v; }
template <typename T> Void array_remove_fast (T *a, U64 i)     { array_set(a, i, array_get_last(a)); a->count--; }
template <typename T> Void array_swap_remove (T *a, U64 i)     { array_swap(a, i, a->count-1); a->count--; }

// =============================================================================
// Search:
// =============================================================================
template <typename T>
U64 array_bsearch (T *a, Elem(T) *elem, Int(*cmp)(Elem(T)*, Elem(T)*)) {
    Void *p = std::bsearch(elem, a->data, a->count, array_elem_size(a), cmp);
    return p ? (static_cast<U8*>(p) - a->data) / array_elem_size(a) : ARRAY_NIL_IDX;
}

template <typename T, typename F>
U64 array_find (T *a, const F &f) {
    U64 r = ARRAY_NIL_IDX;
    array_iter (it, a) if (f(it)) { r = ARRAY_IDX; break; }
    return r;
}

template <typename T, typename F>
Elem(T) array_find_get (T *a, const F &f) {
    Elem(T) r = {};
    array_iter (it, a) if (f(it)) { r = it; break; }
    return r;
}

template <typename T, typename F>
Elem(T) *array_find_ptr (T *a, const F &f) {
    Elem(T) *r = 0;
    array_iter_ptr (it, a) if (f(it)) { r = it; break; }
    return r;
}

template <typename T, typename F>
Void array_find_remove (T *a, const F &f) {
    array_iter (it, a) if (f(it)) { array_remove(a, ARRAY_IDX); break; }
}

template <typename T, typename F>
Void array_find_remove_fast (T *a, const F &f) {
    array_iter (it, a) if (f(it)) { array_remove_fast(a, ARRAY_IDX); break; }
}

template <typename T, typename F>
Void array_find_remove_all_fast (T *a, const F &f) {
    array_iter_back (it, a) if (f(it)) array_remove_fast(a, ARRAY_IDX);
}

template <typename T, typename F>
Void array_find_replace (T *a, const F &f, T r) {
    array_iter (it, a) if (f(it)) { a->data[ARRAY_IDX] = r; break; }
}

template <typename T, typename F>
Void array_find_replace_all (T *a, const F &f, T r) {
    array_iter (it, a) if (f(it)) a->data[ARRAY_IDX] = r;
}

template <typename T, typename F>
Void array_find_remove_all (T *a, const F &f) {
    U64 n = 0;
    array_iter (it, a) if (! f(it)) { a->data[n++] = it; }
    a->count = n;
}

template <typename T>
Bool array_has (T *a, Elem(T) e)  {
    return !!array_find_ptr(a, [&](Auto it){ return e == *it; });
}

// =============================================================================
// Insertion:
// =============================================================================
#define array_push_lit(A, ...)      array_push(A, Elemv(A){__VA_ARGS__})
#define array_insert_lit(A, I, ...) array_insert(A, Elemv(A){__VA_ARGS__}, I)

#define array_push_n(A, ...) do {\
    Elemv(A) _(E)[] = {__VA_ARGS__};\
    array_push_many(A, Slice<Elemv(A)>{ .data=_(E), .count=(sizeof(_(E)) / sizeof(_(E)[0])) });\
}while(0)

template <typename T>
Elem(T) *array_push_slot (T *a) {
    if (a->count == a->capacity) {
        U64 new_cap = a->capacity ? static_cast<U64>(1.8 * a->capacity) : 2;
        assert_always(new_cap > a->capacity);
        array_increase_capacity(a, new_cap);
    }
    Elem(T) *r = &a->data[a->count];
    a->count++;
    return r;
}

template <typename T>
Elem(T) *array_insert_slot (T *a, U64 idx) {
    if (idx == a->count) return array_push_slot(a);
    array_bounds_check(a, idx);
    array_ensure_capacity(a, 1);
    Elem(T) *p = &a->data[idx];
    memmove(p+1, p, array_elem_size(a) * (a->count - idx));
    a->count++;
    return p;
}

template <typename T>
Slice<Elem(T)> array_insert_gap (T *a, U64 count, U64 idx, Bool zeroed) {
    idx = min(a->count, idx);
    U64 bytes_to_move = a->count - idx;
    array_increase_count(a, count, false);
    T *p = &a->data[idx];
    memmove(&p[count], p, array_elem_size(a) * bytes_to_move);
    Slice<Elem(T)> r = { .data=p, .count=count };
    if (zeroed) std::memset(r.data, 0, array_elem_size(a) * count);
    return r;
}

template <typename T, typename U>
Void array_push_many (T *a, U elems) {
    if (elems.count) {
        Slice<Elem(T)> p = array_increase_count(a, elems.count, false);
        std::memcpy(p.data, elems.data, array_elem_size(a) * elems.count);
    }
}

template <typename T, typename U>
Void array_insert_many (T *a, U elems, U64 idx) {
    if (elems.count) {
        Slice <T> p = array_insert_gap(a, elems.count, idx, false);
        std::memcpy(p.data, elems.data, array_elem_size(a) * elems.count);
    }
}

template <typename T> Void array_push   (T *a, Elem(T) e)          { *array_push_slot(a) = e; }
template <typename T> Void array_insert (T *a, Elem(T) e, U64 idx) { *array_insert_slot(a, idx) = e; }
template <typename T> Void array_push_if_unique (T *a, Elem(T) e)  { if (! array_has(a, e)) array_push(a, e); }

#pragma once

// =============================================================================
// Overview:
// ---------
//
// An open addressing hash table with quadratic probing
// via triangular numbers.
//
// This data structure is only suitable for POD types.
//
// Usage example:
// --------------
//
//     Auto map = map_new<U64, CString>(tm, 0);
//     map_add(&map, 42lu, "Hello world!");
//     map_add(&map, 420lu, "Foo bar baz!");
//     map_iter (e, &map) printf("hash=%lu key=%lu val=%s\n", e->hash, e->key, e->val);
//
// For custom keys either write two overloaded functions
// named "hash" and "compare" which will be used by the
// init code, or simply set the map->hash/compare fields
// manually after initialization.
// =============================================================================
#include "base/core.h"

template <typename Key>
using MapHashFn = U64 (*)(Key);

template <typename Key>
using MapCmpFn = Bool (*)(Key, Key);

template <typename Key, typename Val>
struct MapEntry {
    Key key;
    Val val;
    U64 hash;
};

template <typename Key, typename Val>
struct Map {
    Mem *mem;
    U64 count;
    U64 capacity;
    U64 tomb_count;
    MapEntry<Key, Val> *entries;
    Bool shrink_on_del;
    MapHashFn<Key> hash;
    MapCmpFn<Key> compare;
};

const U64 MAP_HASH_OF_EMPTY_ENTRY  = 0; // Marks an entry as empty.
const U64 MAP_HASH_OF_TOMB_ENTRY   = 1; // Marks an entry as a tombstone (internal use).
const U64 MAP_HASH_OF_FILLED_ENTRY = 2; // Values >= to this mark occupied entries.

const U64 MAX_LOAD     = 70;
const U64 MIN_LOAD     = 20;
const U64 MIN_CAPACITY = 16;

assert_static(MAP_HASH_OF_EMPTY_ENTRY == 0);
assert_static((MAX_LOAD < 100) && (MIN_LOAD < 100));

#define map_iter(IT, M)         let2(MAP, MAP_IDX, M, 0u) MAP_ITER(IT, MAP)
#define map_iter_from(IT, M, I) let2(MAP, MAP_IDX, M, I)  MAP_ITER(IT, MAP)
#define MAP_ITER(IT, MAP)       for (Type(*MAP->entries) *IT; (MAP_IDX < MAP->capacity) && (IT = &MAP->entries[MAP_IDX], true); MAP_IDX++)\
                                if (IT->hash >= MAP_HASH_OF_FILLED_ENTRY)

inline Bool compare (U8 a,  U8  b) { return a == b; }
inline Bool compare (U32 a, U32 b) { return a == b; }
inline Bool compare (U64 a, U64 b) { return a == b; }

// Performs quadratic probing via triangular numbers.
// Pass key == 0 to only look for empty slots.
template <typename Key, typename Val>
MapEntry<Key, Val> *map_probe (Map<Key, Val> *map, Key key, U64 hash) {
    assert_dbg(is_pow2(map->capacity));
    assert_dbg(hash >= MAP_HASH_OF_FILLED_ENTRY);

    U64 mask = map->capacity - 1;
    U64 idx  = hash & mask;
    U64 inc  = 1;

    while (true) {
        Auto entry = &map->entries[idx];
        if (entry->hash == MAP_HASH_OF_EMPTY_ENTRY) return entry;
        if (key && (hash == entry->hash) && map->compare(key, entry->key)) return entry;
        idx  = (idx + inc) & mask;
        inc += 1;
    }
}

template <typename Key, typename Val>
static Void map_rehash (Map<Key, Val> *map, U64 new_cap) {
    Auto old_map    = *map;
    map->tomb_count = 0;
    map->capacity   = new_cap;
    map->entries    = mem_alloc(map->mem, Type(*map->entries), .zeroed=true, .size=(new_cap * sizeof(MapEntry<Key, Val>)));
    map_iter (old, &old_map) *map_probe(map, 0lu, old->hash) = *old;
    mem_free(map->mem, .old_ptr=old_map.entries, .old_size=(old_map.capacity * sizeof(MapEntry<Key, Val>)));
}

template <typename Key, typename Val>
Void map_maybe_grow (Map<Key, Val> *map) {
    U64 max_load = safe_mul(map->capacity, MAX_LOAD) / 100;
    if ((map->count + map->tomb_count) > max_load) {
        map_rehash(map, (map->count > max_load) ? safe_mul(2lu, map->capacity) : map->capacity);
    }
}

template <typename Key, typename Val>
Void map_maybe_shrink (Map<Key, Val> *map) {
    if (map->capacity <= MIN_CAPACITY) return;
    U64 min_load = safe_mul(map->capacity, MIN_LOAD) / 100;
    if (map->count < min_load) map_rehash(map, map->capacity / 2);
}

template <typename Key, typename Val>
Void map_clear (Map<Key, Val> *map) {
    memset(map->entries, 0, map->capacity * sizeof(MapEntry<Key, Val>));
    map->tomb_count = 0;
    map->count = 0;
}

template <typename Key, typename Val>
Val map_get (Map<Key, Val> *map, Key key) {
    U64 hash   = max(map->hash(key), MAP_HASH_OF_FILLED_ENTRY);
    Auto entry = map_probe(map, key, hash);
    return (entry->hash < MAP_HASH_OF_FILLED_ENTRY) ? 0 : entry->val;
}

template <typename Key, typename Val>
Bool map_add (Map<Key, Val> *map, Key key, Val val) {
    map_maybe_grow(map);

    U64 hash   = max(map->hash(key), MAP_HASH_OF_FILLED_ENTRY);
    Auto entry = map_probe(map, key, hash);
    Bool found = (entry->hash >= MAP_HASH_OF_FILLED_ENTRY);

    if (! found) {
        map->count++;
        entry->hash = hash;
        entry->key  = key;
        entry->val  = val;
    }

    return found;
}

template <typename Key, typename Val>
Bool map_remove (Map<Key, Val> *map, Key key) {
    U64 hash   = max(map->hash(key), MAP_HASH_OF_FILLED_ENTRY);
    Auto entry = probe(map, key, hash);
    Bool found = (entry->hash >= MAP_HASH_OF_FILLED_ENTRY);

    if (found) {
        map->count--;
        map->tomb_count++;
        entry->hash = MAP_HASH_OF_TOMB_ENTRY;
        if (map->shrink_on_del) map_maybe_shrink(map);
    }

    return found;
}

template <typename Key, typename Val>
Void map_init (Map<Key, Val> *map, Mem *mem, U64 cap) {
    cap = max(MIN_CAPACITY, next_pow2(safe_mul(cap / MAX_LOAD, 100lu)));
    map->mem        = mem;
    map->count      = 0;
    map->capacity   = cap;
    map->tomb_count = 0;
    map->entries    = mem_alloc(mem, Type(*map->entries), .zeroed=true, .size=(cap * sizeof(MapEntry<Key, Val>)));
    map->hash       = hash;
    map->compare    = compare;
}

template <typename Key, typename Val>
Map<Key, Val> map_new (Mem *mem, U64 cap) {
    Map<Key, Val> map;
    map_init(&map, mem, cap);
    return map;
}

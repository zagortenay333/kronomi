#include <cstring>
#include "base/mem.h"

#if ASAN_ENABLED
    #include <sanitizer/asan_interface.h>
    inline U64  adjust_align (U64 x)               { return max(8u, (x ?: MAX_ALIGN)); } // Max(a, 8) for asan poisoning.
    inline Void poison_trace (U8 *a, U64 n, U8 *x) { if ((x >= a) && (x <= a+n)) print_stack_trace(); } // Disable ASLR for this.
    inline Void unpoison     (Void *a, U64 n)      { __asan_unpoison_memory_region(a, n); }
    inline Void poison       (Void *a, U64 n)      { __asan_poison_memory_region(a, n); poison_trace(cast(U8*,a), n, cast(U8*,0x0)); }
#else
    #define adjust_align(A) ((A) ?: MAX_ALIGN)
    #define unpoison(...)
    #define poison(...)
#endif

// =============================================================================
// GMem:
// =============================================================================
Void *mem_fn (GMem *ctx, MemOp op) {
    op.align = adjust_align(op.align);

    Void *result = 0;

    switch (op.tag) {
    case MEM_OP_FREE:
        free(op.old_ptr);
        break;
    case MEM_OP_ALLOC:
        assert_always(op.size);
        op.size += padding_to_align(op.size, op.align);
        result = aligned_alloc(op.align, op.size);
        assert_always(result);
        if (op.zeroed) memset(result, 0, op.size);
        break;
    case MEM_OP_GROW:
        assert_always(op.size);
        assert_always(op.size >= op.old_size);
        result = realloc(op.old_ptr, op.size);
        assert_always(result);
        if (op.zeroed) memset(static_cast<U8*>(result) + op.old_size, 0, op.size - op.old_size);
        break;
    case MEM_OP_SHRINK:
        assert_always(op.size);
        assert_always(op.old_ptr && (op.size <= op.old_size));
        result = realloc(op.old_ptr, op.size);
        assert_always(result);
        break;
    }

    return result;
}

Void *gmem_fn (Void *ctx, MemOp op) {
    return mem_fn(static_cast<GMem*>(ctx), op);
}

// =============================================================================
// Arena:
// =============================================================================
static U64 arena_push_block (Arena *arena, U64 size, U64 align) {
    size                = max(size, arena->min_block_size);
    align               = adjust_align(align);
    U64 padding         = padding_to_align(ARENA_BLOCK_HEADER, align);
    U64 capacity        = safe_add(size, safe_add(ARENA_BLOCK_HEADER, padding));
    Auto block          = mem_alloc(arena->parent, ArenaBlock, .size=capacity, .align=align);
    block->capacity     = capacity;
    block->prev         = arena->block;
    arena->block        = block;
    arena->block_count  = ARENA_BLOCK_HEADER;
    arena->total_count += ARENA_BLOCK_HEADER;
    poison(cast(U8*, block) + ARENA_BLOCK_HEADER, capacity - ARENA_BLOCK_HEADER);
    return padding;
}

Void *arena_alloc (Arena *arena, MemOp op) {
    assert_always(op.size);

    U8 *result    = reinterpret_cast<U8*>(arena->block);
    U64 size      = op.size;
    U64 align     = adjust_align(op.align);
    U64 padding   = padding_to_align(reinterpret_cast<UIntPtr>(result + arena->block_count), align);
    U64 remaining = arena->block->capacity - arena->block_count;

    if (remaining < safe_add(size, padding)) {
        arena->total_count += remaining;
        padding = arena_push_block(arena, size, align);
    }

    result = reinterpret_cast<U8*>(arena->block) + arena->block_count + padding;
    unpoison(result, size);
    if (op.zeroed) memset(result, 0, size);
    arena->block_count += (size + padding);
    arena->total_count += (size + padding);
    return result;
}

Void arena_pop_to (Arena *arena, U64 new_count) {
    assert_always(new_count <= arena->total_count);

    ArenaBlock *block = arena->block;
    U64 block_count   = arena->block_count;
    U64 amount_to_pop = arena->total_count - new_count;

    while (amount_to_pop >= block_count) {
        amount_to_pop -= block_count;
        ArenaBlock *prev = block->prev;
        mem_free(arena->parent, .old_ptr=block, .old_size=block->capacity);
        block_count = prev->capacity;
        block = prev;
    }

    arena->block       = block;
    arena->block_count = block_count - amount_to_pop;
    arena->total_count = new_count;
    assert_always(block_count >= ARENA_BLOCK_HEADER);
    poison(cast(U8*, block) + arena->block_count, amount_to_pop);
}

// Deletes all but 1 block.
Void arena_pop_all (Arena *arena) {
    ArenaBlock *block  = arena->block->prev;
    arena->block->prev = 0;
    arena->block_count = ARENA_BLOCK_HEADER;
    arena->total_count = 0;
    poison(cast(U8*, arena->block) + arena->block_count, arena->block->capacity - arena->block_count);

    while (block) {
        ArenaBlock *prev = block->prev;
        mem_free(arena->parent, .old_ptr=block, .old_size=block->capacity);
        block = prev;
    }
}

Void *arena_grow (Arena *arena, MemOp op) {
    assert_always(op.size >= op.old_size);
    Auto result = static_cast<U8*>(arena_alloc(arena, op));

    if (op.old_ptr) {
        memcpy(result, op.old_ptr, op.old_size);
        poison(op.old_ptr, op.old_size);
    }

    return result;
}

static Void arena_free (Arena *arena, MemOp op) {
    poison(op.old_ptr, op.old_size);
}

static Void *arena_shrink (Arena *arena, MemOp op) {
    assert_always(op.size <= op.old_size);
    poison(cast(U8*, op.old_ptr) + op.size, op.old_size - op.size);
    return op.old_ptr;
}

Void arena_destroy (Arena *arena) {
    arena_pop_all(arena);
    mem_free(arena->parent, .old_ptr=arena->block, .old_size=arena->block->capacity);
    mem_free(arena->parent, .old_ptr=arena, .old_size=sizeof(Arena));
}

Void arena_init (Arena *arena, Mem *mem, U64 min_block_size) {
    arena->base.fn = [](Void *ctx, MemOp op){ return mem_fn(static_cast<Arena*>(ctx), op); };
    arena->parent = mem;
    arena->min_block_size = min_block_size;
    arena_push_block(arena, min_block_size, MAX_ALIGN);
}

Arena *arena_new (Mem *mem, U64 min_block_size) {
    Arena *arena = mem_new(mem, Arena);
    arena_init(arena, mem, min_block_size);
    return arena;
}

Void *mem_fn (Arena *arena, MemOp op) {
    switch (op.tag) {
    case MEM_OP_FREE:   arena_free(arena, op); return 0;
    case MEM_OP_GROW:   return arena_grow(arena, op);
    case MEM_OP_ALLOC:  return arena_alloc(arena, op);
    case MEM_OP_SHRINK: return arena_shrink(arena, op);
    }
    badpath;
}

// =============================================================================
// TMem:
// =============================================================================
tls TMemRing tmem_ring;

Void *mem_fn (TMem *t, MemOp op) {
    assert_dbg(static_cast<TMem*>(t)->slot_idx < 8);
    Auto tm        = static_cast<TMem*>(t);
    Arena *a       = &tmem_ring.slots[tm->slot_idx];
    U64 prev_count = a->total_count;
    Void *result   = mem_fn(a, op);
    if (a->total_count > prev_count) tm->count += a->total_count - prev_count;
    return result;
}

Void *tmem_fn (Void *ctx, MemOp op) {
    return mem_fn(static_cast<TMem*>(ctx), op);
}

Void tmem_setup (Mem *mem, U64 min_size) {
    tmem_ring.slot_idx = 7;
    for (U64 i = 0; i < 8; ++i) arena_init(&tmem_ring.slots[i], mem, min_size / 8);
}

Void tmem_start (TMem *tm) {
    TMemRing *r = &tmem_ring;
    U8 slot_idx = [&]{ // Next unpinned slot or idx+1 if all slots pinned.
        U8 i = r->slot_idx + 1;
        U8 p = leading_one_bits(rotl8(r->pin_flags, i));
        r->slot_idx = (p + i) & 7;
        return r->slot_idx;
    }();
    tm->base.fn   = tmem_fn;
    tm->count     = 0;
    tm->slot_idx  = slot_idx;
    tm->arena_pos = r->slots[slot_idx].total_count;
}

Void tmem_destroy (TMem *tm) {
    assert_dbg(tm->slot_idx < 8);
    TMemRing *r  = &tmem_ring;
    Arena *arena = &r->slots[tm->slot_idx];
    r->slot_idx  = (tm->slot_idx - 1) & 7;
    Bool on_top  = (tm->arena_pos + tm->count) == arena->total_count;
    if (on_top) arena_pop_to(arena, tm->arena_pos);
    else        panic();
}

U8 tmem_pin_push (Mem *m, Bool exclusive) {
    U8 prev_pins = tmem_ring.pin_flags;
    if (m->fn == tmem_fn) tmem_ring.pin_flags = (exclusive ? 0 : prev_pins) | (0x80 >> reinterpret_cast<TMem*>(m)->slot_idx);
    return prev_pins;
}

Void tmem_pin_pop (U8 prev_flags) {
    tmem_ring.pin_flags = prev_flags;
}

Mem mem_root = { gmem_fn };


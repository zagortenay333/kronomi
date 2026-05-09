#include <freetype/freetype.h>
#include <freetype/ftmodapi.h>
#include "vendor/plutosvg/src/plutosvg.h"
#include "font/font.h"
#include "base/array.h"
#include "base/string.h"
#include "base/log.h"
#include "base/map.h"
#include "os/fs.h"
#include "os/time.h"
#include "window/window.h"

#define LOG_HEADER "Font"

AtlasSlot *font_get_atlas_slot (Font *font, GlyphInfo *info) {
    AtlasSlot *slot = map_get_ptr(&font->slot_map, info->glyph_index);

    Bool atlas_update_needed = true;

    // Remove from lru chain.
    if (slot) {
        slot->lru_next->lru_prev = slot->lru_prev;
        slot->lru_prev->lru_next = slot->lru_next;
        atlas_update_needed = false;
    }

    // See if we have a free slot.
    if (!slot && font->free_slots.count) {
        slot = array_pop(&font->free_slots);
        slot->glyph_index = info->glyph_index;
        map_add(&font->slot_map, info->glyph_index, slot);
    }

    // Evict the lru slot.
    if (! slot) {
        if (font->cache->vertex_flush_fn) font->cache->vertex_flush_fn();
        assert_dbg(font->lru.lru_prev != &font->lru);
        slot = font->lru.lru_prev;
        map_remove(&font->slot_map, slot->glyph_index);
        slot->glyph_index = info->glyph_index;
        map_add(&font->slot_map, info->glyph_index, slot);
    }

    // Mark as mru slot
    slot->lru_next = font->lru.lru_next;
    slot->lru_prev = &font->lru;
    font->lru.lru_next->lru_prev = slot;
    font->lru.lru_next = slot;

    if (atlas_update_needed) {
        tmem_new(tm);

        if (FT_Load_Glyph(font->ft_face, slot->glyph_index, FT_LOAD_RENDER | (FT_HAS_COLOR(font->ft_face) ? FT_LOAD_COLOR : 0))) {
            log_msg_fmt(LOG_ERROR, LOG_HEADER, 0, "Couldn't load/render font glyph.");
            goto done;
        }

        Auto ft_glyph = font->ft_face->glyph;
        Auto ft_bitmap = ft_glyph->bitmap;
        Auto w = ft_bitmap.width;
        Auto h = ft_bitmap.rows;

        slot->width = w;
        slot->height = h;
        slot->bearing_x = ft_glyph->bitmap_left;
        slot->bearing_y = ft_glyph->bitmap_top;
        slot->advance = (I32)(ft_glyph->advance.x >> 6);
        slot->pixel_mode = ft_bitmap.pixel_mode;

        if ((w > font->atlas_slot_size) || (h > font->atlas_slot_size)) {
            log_msg_fmt(LOG_ERROR, LOG_HEADER, 0, "Font glyph too big to fit into atlas slot.");
            goto done;
        }

        if ((w == 0) || (h == 0)) {
            goto done;
        }

        U8 *buf = mem_alloc(tm, U8, .zeroed=true, .size=(font->atlas_slot_size * font->atlas_slot_size * 4));

        switch (ft_bitmap.pixel_mode) {
        case FT_PIXEL_MODE_GRAY: {
            for (U32 y = 0; y < h; ++y) {
                U8 *src = ft_bitmap.buffer + y * abs(ft_bitmap.pitch);
                for (U32 x = 0; x < w; ++x) {
                    U8 value = src[x];
                    U32 i = (y * font->atlas_slot_size + x) * 4;
                    buf[i + 0] = 255;
                    buf[i + 1] = 255;
                    buf[i + 2] = 255;
                    buf[i + 3] = value;
                }
            }
        } break;

        case FT_PIXEL_MODE_BGRA: {
            for (U32 y = 0; y < h; ++y) {
                U8 *src = ft_bitmap.buffer + y * abs(ft_bitmap.pitch);
                for (U32 x = 0; x < w; ++x) {
                    U32 i = (y * font->atlas_slot_size + x) * 4;
                    buf[i + 0] = src[x * 4 + 2];
                    buf[i + 1] = src[x * 4 + 1];
                    buf[i + 2] = src[x * 4 + 0];
                    buf[i + 3] = src[x * 4 + 3];
                }
            }
        } break;

        default: badpath;
        }

        dr_texture_update(&font->atlas_texture, slot->x, slot->y, font->atlas_slot_size, font->atlas_slot_size, buf);

        done:;
    }

    return slot;
}

static Font *font_new (FontCache *cache, String filepath, U32 size) {
    Mem *mem = cast(Mem*, arena_new(mem_root, 1*KB));

    Auto font = mem_new(mem, Font);

    font->mem = mem;
    font->size = size;
    font->filepath = filepath;
    font->cache = cache;
    font->lru.lru_next = &font->lru;
    font->lru.lru_prev = &font->lru;
    font->atlas_slot_size = 2 * size;
    font->binary = fs_read_entire_file(mem, filepath, 0);

    AtlasSlot *slots = mem_alloc(mem, AtlasSlot, .size=(cache->atlas_size * cache->atlas_size * sizeof(AtlasSlot)));
    array_init(&font->free_slots, mem);
    map_init(&font->slot_map, mem);

    U32 x = 0;
    U32 y = 0;
    for (U32 i = 0; i < cast(U32, cache->atlas_size) * cache->atlas_size; ++i) {
        AtlasSlot *slot = &slots[i];
        slot->x = x * font->atlas_slot_size;
        slot->y = y * font->atlas_slot_size;
        array_push(&font->free_slots, slot);
        x++;
        if (x == cache->atlas_size) { x = 0; y++; }
    }

    FT_Open_Args args = { .flags=FT_OPEN_MEMORY, .memory_base=cast(U8*, font->binary.data), .memory_size=font->binary.count };
    if (FT_Open_Face(cache->ft_lib, &args, 0, &font->ft_face)) {
        arena_destroy(cast(Arena*, mem));
        return 0;
    }

    FT_Set_Pixel_Sizes(font->ft_face, 0, size);

    font->hb_face = hb_ft_face_create_referenced(font->ft_face);
    font->hb_font = hb_font_create(font->hb_face);
    I32 hb_font_size = size * 64;
    hb_font_set_scale(font->hb_font, hb_font_size, hb_font_size);

    font->atlas_texture = dr_texture_alloc(cache->atlas_size*font->atlas_slot_size, cache->atlas_size*font->atlas_slot_size);

    { // Get metrics:
        U32 glyph_index = FT_Get_Char_Index(font->ft_face, 'M');
        AtlasSlot *slot = font_get_atlas_slot(font, &(GlyphInfo){.glyph_index = glyph_index});
        font->ascent    = font->ft_face->size->metrics.ascender >> 6;
        font->descent   = -(font->ft_face->size->metrics.descender >> 6);
        font->height    = font->ft_face->size->metrics.height >> 6;
        font->width     = slot->advance;
    }

    array_push(&cache->fonts, font);
    return font;
}

static Void font_free (FontCache *cache, Font *font) {
    hb_font_destroy(font->hb_font);
    hb_face_destroy(font->hb_face);
    FT_Done_Face(font->ft_face);
    dr_texture_free(&font->atlas_texture);
    arena_destroy(cast(Arena*, font->mem));
}

Font *font_get (FontCache *cache, String filepath, U32 size) {
    if (size == 0) size = 12;

    Font *font = 0;

    array_iter (it, &cache->fonts) {
        if (str_match(it->filepath, filepath) && it->size == size) {
            font = it;
            break;
        }
    }

    if (! font) font = font_new(cache, filepath, size);
    if (! font) return font_get(cache, str("data/fonts/NotoSans-Regular.ttf"), size);
    font->last_touched = os_get_time_ms();

    if (! --cache->gc) {
        cache->gc = 200;
        U64 time = os_get_time_ms();
        U64 ttl = 1000*60*60;
        array_iter (it, &cache->fonts) {
            if (time - it->last_touched > ttl) {
                font_free(cache, it);
                array_remove_fast(&cache->fonts, ARRAY_IDX--);
            }
        }
    }

    return font;
}

FontCache *font_cache_new (Mem *mem, VertexFlushFn vertex_flush_fn, U16 atlas_size) {
    Auto cache = mem_new(mem, FontCache);
    cache->mem = mem;
    cache->gc = 200;
    cache->vertex_flush_fn = vertex_flush_fn;
    cache->atlas_size = atlas_size;
    array_init(&cache->fonts, mem);

    FT_Init_FreeType(&cache->ft_lib);

    Auto hooks = plutosvg_ft_svg_hooks();
    FT_Property_Set(cache->ft_lib, "ot-svg", "svg-hooks", hooks);

    return cache;
}

SliceGlyphInfo font_get_glyph_infos (Font *font, Mem *mem, String text) {
    ArrayGlyphInfo infos;
    array_init(&infos, mem);

    I32 cursor_x = 0;
    I32 cursor_y = 0;

    Auto buffer = hb_buffer_create();

    hb_buffer_add_utf8(buffer, text.data, text.count, 0, text.count);
    hb_buffer_guess_segment_properties(buffer);

    hb_shape(font->hb_font, buffer, 0, 0);

    Slice(hb_glyph_info_t) hb_infos;
    Slice(hb_glyph_position_t) hb_positions;

    U32 info_count;
    hb_infos.data = hb_buffer_get_glyph_infos(buffer, &info_count);
    hb_infos.count = info_count;

    U32 position_count;
    hb_positions.data = hb_buffer_get_glyph_positions(buffer, &position_count);
    hb_positions.count = position_count;

    array_iter (info, &hb_infos) {
        Auto pos = array_get(&hb_positions, ARRAY_IDX);
        UtfDecode codepoint = str_utf8_decode(str_suffix_from(text, info.cluster));

        array_push_lit(&infos,
            .x = cursor_x + (pos.x_offset >> 6),
            .y = cursor_y + (pos.y_offset >> 6),
            .x_advance = pos.x_advance >> 6,
            .y_advance = pos.y_advance >> 6,
            .glyph_index = info.codepoint, // After shaping harfbuzz sets this field to the glyph index.
            .codepoint = codepoint.codepoint,
            .byte_offset = info.cluster,
            .font = font,
        );

        cursor_x += pos.x_advance >> 6;
        cursor_y += pos.y_advance >> 6;
    }

    hb_buffer_destroy(buffer);

    return infos.as_slice;
}

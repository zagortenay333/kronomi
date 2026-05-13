#include "vendor/glad/glad.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <stdatomic.h>
#include "vendor/stb/stb_image.h"
#include "window/window.h"
#include "ui/ui.h"

istruct (Sound) {
    U8 *buf;
    U32 len;
    atomic_bool done;
    atomic_bool looping;
    SDL_AudioStream *stream;
};

#define VERTEX_MAX_BATCH_SIZE 2400

Bool running = true;
F32 prev_scale;

SDL_Window *window;
SDL_GLContext gl_ctx;
ArrayEvent events;

Int win_width  = 1000;
Int win_height = 650;

SDL_Cursor *cursors[SDL_SYSTEM_CURSOR_COUNT];

#define BLUR_SHRINK 4
U32 blur_shader;
U32 blur_VBO, blur_VAO;
U32 blur_buffer1;
U32 blur_buffer2;
U32 blur_tex1;
U32 blur_tex2;
Array(struct { Vec2 pos; }) blur_vertices;

ArrayVertex vertices;

U32 rect_shader;
U32 VBO, VAO;
Mat4 projection;
U32 framebuffer;
U32 framebuffer_tex;

U32 active_texture;

U32 screen_shader;
U32 screen_VBO, screen_VAO;
Array(struct { Vec2 pos; Vec2 tex; }) screen_vertices;

static Void set_bool  (U32 p, CString name, Bool v) { glUniform1i(glGetUniformLocation(p, name), cast(Int, v)); }
static Void set_int   (U32 p, CString name, Int v)  { glUniform1i(glGetUniformLocation(p, name), v); }
static Void set_float (U32 p, CString name, F32 v)  { glUniform1f(glGetUniformLocation(p, name), v); }
static Void set_vec2  (U32 p, CString name, Vec2 v) { glUniform2f(glGetUniformLocation(p, name), v.x, v.y); }
static Void set_vec4  (U32 p, CString name, Vec4 v) { glUniform4f(glGetUniformLocation(p, name), v.x, v.y, v.z, v.w); }
static Void set_mat4  (U32 p, CString name, Mat4 m) { glUniformMatrix4fv(glGetUniformLocation(p, name), 1, GL_FALSE, cast(F32*, &m)); }

#define ATTR(T, OFFSET, LEN, NAME) ({\
    glVertexAttribPointer(OFFSET, LEN, GL_FLOAT, GL_FALSE, sizeof(T), cast(Void*, offsetof(T, NAME)));\
    glEnableVertexAttribArray(OFFSET);\
})

Noreturn static Void error () {
    log_scope_end_all();
    panic();
}

Noreturn Fmt(1, 2) static Void error_fmt (CString fmt, ...) {
    log_msg(m, LOG_ERROR, "Win", 1);
    astr_push_fmt_vam(m, fmt);
    astr_push_byte(m, '\n');
    error();
}

static Void SDLCALL stream_callback (Void *userdata, SDL_AudioStream *stream, Int additional_amount, Int total_amount) {
    Sound *s = userdata;

    if (SDL_GetAudioStreamAvailable(stream) == 0) {
        if (atomic_load(&s->looping)) {
            SDL_PutAudioStreamData(stream, s->buf, cast(Int, s->len));
        } else {
            atomic_store(&s->done, true);
        }
    }
}

Sound *win_sound_play (Mem *mem, String path, Bool looping) {
    tmem_new(tm);
    Sound *sound = mem_new(mem, Sound);

    atomic_init(&sound->looping, looping);
    atomic_init(&sound->done, false);

    SDL_AudioSpec spec;
    if (! SDL_LoadWAV(cstr(tm, path), &spec, &sound->buf, &sound->len)) return 0;

    sound->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, stream_callback, sound);
    if (! sound->stream) { SDL_free(sound->buf); return 0; }

    SDL_PutAudioStreamData(sound->stream, sound->buf, cast(Int, sound->len));
    SDL_FlushAudioStream(sound->stream);
    SDL_ResumeAudioStreamDevice(sound->stream);

    return sound;
}

Void win_sound_cancel (Sound *sound) {
    if (! sound) return;
    atomic_store(&sound->looping, false);
    SDL_DestroyAudioStream(sound->stream);
    SDL_free(sound->buf);
}

Bool win_sound_is_playing (Sound *h) {
    return h && !atomic_load(&h->done);
}

static Void update_projection () {
    F32 h = cast(F32, win_height);
    F32 w = cast(F32, win_width);
    projection = mat_ortho(0, w, 0, h, -1.f, 1.f);
}

static U32 framebuffer_new (U32 *out_texture, U32 w, U32 h) {
    U32 r;
    glGenFramebuffers(1, &r);
    glBindFramebuffer(GL_FRAMEBUFFER, r);

    U32 texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (out_texture) *out_texture = texture;

    assert_always(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return r;
}

static U32 shader_compile (GLenum type, String filepath) {
    tmem_new(tm);

    String source = fs_read_entire_file(tm, filepath, 0);
    if (! source.data) error_fmt("Unable to read file: %.*s\n", STR(filepath));

    U32 shader = glCreateShader(type);

    glShaderSource(shader, 1, cast(const GLchar**, &source.data), 0);
    glCompileShader(shader);

    Int success; glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (! success) {
        log_msg(msg, LOG_ERROR, "", 1);
        Int count; glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &count);
        astr_push_fmt(msg, "Shader compilation error: %.*s\n  ", STR(filepath));
        U32 offset = msg->count;
        array_increase_count(msg, cast(U32, count), false);
        glGetShaderInfoLog(shader, count, 0, msg->data + offset);
        msg->count--; // Get rid of the NUL byte...
        error();
    }

    return shader;
}

static U32 shader_new (CString vshader_path, CString fshader_path) {
    U32 id      = glCreateProgram();
    U32 vshader = shader_compile(GL_VERTEX_SHADER, str(vshader_path));
    U32 fshader = shader_compile(GL_FRAGMENT_SHADER, str(fshader_path));

    glAttachShader(id, vshader);
    glAttachShader(id, fshader);
    glLinkProgram(id);

    Int success; glGetProgramiv(id, GL_LINK_STATUS, &success);
    if (! success) {
        log_msg(msg, LOG_ERROR, "", 1);
        Int count; glGetProgramiv(id, GL_INFO_LOG_LENGTH, &count);
        astr_push_cstr(msg, "Shader prog link error.\n  ");
        U32 offset = msg->count;
        array_increase_count(msg, cast(U32, count), false);
        glGetProgramInfoLog(id, count, 0, msg->data + offset);
        msg->count--; // Get rid of the NUL byte...
        error();
    }

    glDeleteShader(vshader);
    glDeleteShader(fshader);

    return id;
}

Void dr_flush_vertices () {
    glBindVertexArray(VAO);
    glUseProgram(rect_shader);
    set_mat4(rect_shader, "projection", projection);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);

    ATTR(Vertex, 0, 2, position);
    ATTR(Vertex, 1, 4, color);
    ATTR(Vertex, 2, 2, top_left);
    ATTR(Vertex, 3, 2, bottom_right);
    ATTR(Vertex, 4, 4, radius);
    ATTR(Vertex, 5, 1, edge_softness);
    ATTR(Vertex, 6, 4, border_color);
    ATTR(Vertex, 7, 4, border_widths);
    ATTR(Vertex, 8, 4, inset_shadow_color);
    ATTR(Vertex, 9, 4, outset_shadow_color);
    ATTR(Vertex, 10, 1, outset_shadow_width);
    ATTR(Vertex, 11, 1, inset_shadow_width);
    ATTR(Vertex, 12, 2, shadow_offsets);
    ATTR(Vertex, 13, 2, uv);
    ATTR(Vertex, 14, 4, text_color);
    ATTR(Vertex, 15, 1, text_is_grayscale);

    glBufferData(GL_ARRAY_BUFFER, array_size(&vertices), vertices.data, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, vertices.count);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    vertices.count = 0;
}

Vertex *dr_reserve_vertices (U32 n) {
    if (vertices.count + n >= VERTEX_MAX_BATCH_SIZE) dr_flush_vertices();
    SliceVertex slice;
    array_increase_count_o(&vertices, n, false, &slice);
    return slice.data;
}

static Void draw_rect_vertex (Vertex *v, Vec2 pos, Vec2 uv, Vec4 color, RectAttributes *a) {
    v->position            = pos;
    v->color               = color;
    v->top_left            = a->top_left;
    v->bottom_right        = a->bottom_right;
    v->radius              = a->radius;
    v->edge_softness       = a->edge_softness;
    v->border_color        = a->border_color;
    v->border_widths       = a->border_widths;
    v->inset_shadow_color  = a->inset_shadow_color;
    v->outset_shadow_color = a->outset_shadow_color;
    v->outset_shadow_width = a->outset_shadow_width;
    v->inset_shadow_width  = a->inset_shadow_width;
    v->shadow_offsets      = a->shadow_offsets;
    v->uv                  = uv;
    v->text_color          = a->text_color;
    v->text_is_grayscale   = a->text_is_grayscale;
}

SliceVertex dr_rect_fn (RectAttributes *a) {
    Vertex *p = dr_reserve_vertices(6);

    if (a->color2.x == -1.0) a->color2 = a->color;

    // We make the rect slightly bigger because the fragment
    // shader will shrink it to make room for the drop shadow.
    a->top_left.x     -= 2*a->outset_shadow_width + 2*a->edge_softness;
    a->top_left.y     -= 2*a->outset_shadow_width + 2*a->edge_softness;
    a->bottom_right.x += 2*a->outset_shadow_width + 2*a->edge_softness;
    a->bottom_right.y += 2*a->outset_shadow_width + 2*a->edge_softness;

    a->top_left.y = win_height - a->top_left.y;
    a->bottom_right.y = win_height - a->bottom_right.y;

    Vec2 bottom_left = vec2(a->top_left.x, a->bottom_right.y);
    Vec2 top_right   = vec2(a->bottom_right.x, a->top_left.y);

    Vec4 tr = a->texture_rect;

    draw_rect_vertex(&p[0], a->top_left, vec2(tr.x, tr.y), a->color, a);
    draw_rect_vertex(&p[1], bottom_left, vec2(tr.x, tr.y+tr.w), a->color2, a);
    draw_rect_vertex(&p[2], a->bottom_right, vec2(tr.x+tr.z, tr.y+tr.w), a->color2, a);
    draw_rect_vertex(&p[3], a->bottom_right, vec2(tr.x+tr.z, tr.y+tr.w), a->color2, a);
    draw_rect_vertex(&p[4], top_right, vec2(tr.x+tr.z, tr.y), a->color, a);
    draw_rect_vertex(&p[5], a->top_left, vec2(tr.x, tr.y), a->color, a);

    return (SliceVertex){p,6};
}

Void dr_line (Vec2 a, Vec2 b, Vec4 color, F32 softness, F32 thickness) {
    RectAttributes attr = {0};

    F32 half = thickness * 0.5f;
    F32 pad = half + softness;

    F32 min_x = min(a.x, b.x);
    F32 min_y = min(a.y, b.y);
    F32 max_x = max(a.x, b.x);
    F32 max_y = max(a.y, b.y);

    attr.top_left = vec2(min_x - pad, min_y - pad);
    attr.bottom_right = vec2(max_x + pad, max_y + pad);

    attr.color  = color;
    attr.color2 = vec4(-1, 0, 0, 0);
    attr.edge_softness = softness;

    attr.text_color.x = a.x;
    attr.text_color.y = win_height - a.y;
    attr.text_color.z = b.x;
    attr.text_color.w = win_height - b.y;

    attr.radius.x = half;
    attr.text_is_grayscale = -1;

    dr_rect_fn(&attr);
}

Void dr_triangle (Vec2 a, Vec2 b, Vec2 c, Vec4 color, F32 softness) {
    RectAttributes attr = {};

    attr.top_left     = vec2(min(a.x, min(b.x, c.x)), min(a.y, min(b.y, c.y)));
    attr.bottom_right = vec2(max(a.x, max(b.x, c.x)), max(a.y, max(b.y, c.y)));

    attr.color  = color;
    attr.color2 = vec4(-1, 0, 0, 0);
    attr.edge_softness = softness;

    attr.text_color.x = a.x;
    attr.text_color.y = win_height - a.y;
    attr.text_color.z = b.x;
    attr.text_color.w = win_height - b.y;
    attr.radius.x     = c.x;
    attr.radius.y     = win_height - c.y;

    attr.text_is_grayscale = -2;

    dr_rect_fn(&attr);
}
 
Void dr_circle (Vec2 c, F32 r, Vec4 color, F32 softness) {
    RectAttributes attr = {};

    attr.top_left     = vec2(c.x-r, c.y-r);
    attr.bottom_right = vec2(c.x+r, c.y+r);

    attr.radius = vec4(r, r, r, r);

    attr.color  = color;
    attr.color2 = vec4(-1, 0, 0, 0);
    attr.edge_softness = softness;
    
    dr_rect_fn(&attr);
}

Void dr_blur (Rect r, F32 strength, Vec4 corner_radius) {
    dr_flush_vertices();
    glScissor(0, 0, win_width, win_height);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blur_buffer1);
    glBlitFramebuffer(0, 0, win_width, win_height, 0, 0, win_width/BLUR_SHRINK, win_height/BLUR_SHRINK, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glViewport(0, 0, win_width/BLUR_SHRINK, win_height/BLUR_SHRINK);

    blur_vertices.count = 0;
    array_push_lit(&blur_vertices, -1.0f,  1.0f);
    array_push_lit(&blur_vertices, -1.0f, -1.0f);
    array_push_lit(&blur_vertices,  1.0f, -1.0f);
    array_push_lit(&blur_vertices, -1.0f,  1.0f);
    array_push_lit(&blur_vertices,  1.0f, -1.0f);
    array_push_lit(&blur_vertices,  1.0f,  1.0f);

    glBindVertexArray(blur_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, blur_VBO);
    ATTR(AElem(&blur_vertices), 0, 2, pos);
    glBufferData(GL_ARRAY_BUFFER, array_size(&blur_vertices), blur_vertices.data, GL_STREAM_DRAW);

    glUseProgram(blur_shader);
    set_int(blur_shader, "blur_radius", strength);
    set_bool(blur_shader, "do_blurring", true);
    set_mat4(blur_shader, "projection", mat4(1));

    for (U64 i = 0; i < 3; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, blur_buffer2);
        glBindTexture(GL_TEXTURE_2D, blur_tex1);
        set_bool(blur_shader, "horizontal", true);
        glDrawArrays(GL_TRIANGLES, 0, blur_vertices.count);

        glBindFramebuffer(GL_FRAMEBUFFER, blur_buffer1);
        glBindTexture(GL_TEXTURE_2D, blur_tex2);
        set_bool(blur_shader, "horizontal", false);
        glDrawArrays(GL_TRIANGLES, 0, blur_vertices.count);
    }

    glViewport(0, 0, win_width, win_height);
    glBindTexture(GL_TEXTURE_2D, blur_tex1);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    r.y = win_height - r.y;
    blur_vertices.count = 0;
    array_push_lit(&blur_vertices, r.x, r.y);
    array_push_lit(&blur_vertices, r.x+r.w, r.y);
    array_push_lit(&blur_vertices, r.x, r.y-r.h);
    array_push_lit(&blur_vertices, r.x, r.y-r.h);
    array_push_lit(&blur_vertices, r.x+r.w, r.y);
    array_push_lit(&blur_vertices, r.x+r.w, r.y-r.h);

    set_mat4(blur_shader, "projection", projection);
    set_bool(blur_shader, "do_blurring", false);
    set_vec2(blur_shader, "half_size", vec2(r.w/2, r.h/2));
    set_vec2(blur_shader, "center", vec2(r.x+r.w/2, r.y-r.h/2));
    set_vec4(blur_shader, "radius", corner_radius);
    set_float(blur_shader, "blur_shrink", BLUR_SHRINK);

    glBufferData(GL_ARRAY_BUFFER, array_size(&blur_vertices), blur_vertices.data, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, blur_vertices.count);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

Void dr_scissor (Rect r) {
    glScissor(r.x, r.y, r.w, r.h);
}

Texture dr_image (CString filepath, Bool flip) {
    U32 id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTextureParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_set_flip_vertically_on_load(flip);

    Int w, h, n; U8 *data = stbi_load(filepath, &w, &h, &n, 0);

    Int fmt = (n == 3) ? GL_RGB : GL_RGBA;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(data);
    return (Texture){ .id=id, .width=w, .height=h };
}

Void dr_bind_texture (Texture *texture) {
    if (active_texture != texture->id) {
        dr_flush_vertices();
        active_texture = texture->id;
    }

    glBindTexture(GL_TEXTURE_2D, texture->id);
}

Texture dr_texture_alloc (U32 width, U32 height) {
    U32 id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return (Texture){.id=id, .width=width, .height=height};
}

Void dr_texture_free (Texture *texture) {
    glDeleteTextures(1, &texture->id);
}

Void dr_texture_update (Texture *texture, U32 x, U32 y, U32 w, U32 h, U8 *buf) {
    glBindTexture(GL_TEXTURE_2D, texture->id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
}

Void win_set_cursor (MouseCursor cursor) {
    switch (cursor) {
    case MOUSE_CURSOR_DEFAULT:     SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_DEFAULT]); break;
    case MOUSE_CURSOR_TEXT:        SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_TEXT]); break;
    case MOUSE_CURSOR_WAIT:        SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_WAIT]); break;
    case MOUSE_CURSOR_CROSSHAIR:   SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_CROSSHAIR]); break;
    case MOUSE_CURSOR_PROGRESS:    SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_PROGRESS]); break;
    case MOUSE_CURSOR_NWSE_RESIZE: SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_NWSE_RESIZE]); break;
    case MOUSE_CURSOR_NESW_RESIZE: SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_NESW_RESIZE]); break;
    case MOUSE_CURSOR_EW_RESIZE:   SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_EW_RESIZE]); break;
    case MOUSE_CURSOR_NS_RESIZE:   SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_NS_RESIZE]); break;
    case MOUSE_CURSOR_MOVE:        SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_MOVE]); break;
    case MOUSE_CURSOR_NOT_ALLOWED: SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_NOT_ALLOWED]); break;
    case MOUSE_CURSOR_POINTER:     SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_POINTER]); break;
    case MOUSE_CURSOR_NW_RESIZE:   SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_NW_RESIZE]); break;
    case MOUSE_CURSOR_N_RESIZE:    SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_N_RESIZE]); break;
    case MOUSE_CURSOR_NE_RESIZE:   SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_NE_RESIZE]); break;
    case MOUSE_CURSOR_E_RESIZE:    SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_E_RESIZE]); break;
    case MOUSE_CURSOR_SE_RESIZE:   SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_SE_RESIZE]); break;
    case MOUSE_CURSOR_S_RESIZE:    SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_S_RESIZE]); break;
    case MOUSE_CURSOR_SW_RESIZE:   SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_SW_RESIZE]); break;
    case MOUSE_CURSOR_W_RESIZE:    SDL_SetCursor(cursors[SDL_SYSTEM_CURSOR_W_RESIZE]); break;
    }
}

Void win_open_file_url (String url) {
    tmem_new(tm);

    if (str_starts_with(url, str("~"))) {
        String home = fs_get_home_dir(tm);
        String suffix = str_suffix_from_first(url, '~');
        url = astr_fmt(tm, "%.*s/%.*s", STR(home), STR(suffix));
    }

    String str = astr_fmt(tm, "file:///%.*s%c", STR(url), 0);
    SDL_OpenURL(str.data);
}

Void win_open_web_url (String url) {
    tmem_new(tm);
    String str = astr_fmt(tm, "https:///%.*s%c", STR(url), 0);
    SDL_OpenURL(str.data);
}

Vec2 win_get_size () {
    return vec2(win_width, win_height);
}

SliceEvent *win_get_events () {
    return &events.as_slice;
}

Void win_set_clipboard_text (String str) {
    tmem_new(tm);
    SDL_SetClipboardText(cstr(tm, str));
}

String win_get_clipboard_text (Mem *mem) {
    CString txt = SDL_GetClipboardText();
    String result = str_copy(mem, str(txt));
    SDL_free(txt);
    return result;
}

static KeyMod convert_key_mod (SDL_Keymod sdl_mod) {
    KeyMod mod = 0;
    if (sdl_mod & SDL_KMOD_SHIFT) mod |= KEY_MOD_SHIFT;
    if (sdl_mod & SDL_KMOD_CTRL) mod |= KEY_MOD_CTRL;
    if (sdl_mod & SDL_KMOD_ALT) mod |= KEY_MOD_ALT;
    return mod;
}

static Key convert_scancode (SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_A: return KEY_A;
    case SDL_SCANCODE_B: return KEY_B;
    case SDL_SCANCODE_C: return KEY_C;
    case SDL_SCANCODE_D: return KEY_D;
    case SDL_SCANCODE_E: return KEY_E;
    case SDL_SCANCODE_F: return KEY_F;
    case SDL_SCANCODE_G: return KEY_G;
    case SDL_SCANCODE_H: return KEY_H;
    case SDL_SCANCODE_I: return KEY_I;
    case SDL_SCANCODE_J: return KEY_J;
    case SDL_SCANCODE_K: return KEY_K;
    case SDL_SCANCODE_L: return KEY_L;
    case SDL_SCANCODE_M: return KEY_M;
    case SDL_SCANCODE_N: return KEY_N;
    case SDL_SCANCODE_O: return KEY_O;
    case SDL_SCANCODE_P: return KEY_P;
    case SDL_SCANCODE_Q: return KEY_Q;
    case SDL_SCANCODE_R: return KEY_R;
    case SDL_SCANCODE_S: return KEY_S;
    case SDL_SCANCODE_T: return KEY_T;
    case SDL_SCANCODE_U: return KEY_U;
    case SDL_SCANCODE_V: return KEY_V;
    case SDL_SCANCODE_W: return KEY_W;
    case SDL_SCANCODE_X: return KEY_X;
    case SDL_SCANCODE_Y: return KEY_Y;
    case SDL_SCANCODE_Z: return KEY_Z;

    case SDL_SCANCODE_F1: return KEY_F1;
    case SDL_SCANCODE_F2: return KEY_F2;
    case SDL_SCANCODE_F3: return KEY_F3;
    case SDL_SCANCODE_F4: return KEY_F4;
    case SDL_SCANCODE_F5: return KEY_F5;
    case SDL_SCANCODE_F6: return KEY_F6;
    case SDL_SCANCODE_F7: return KEY_F7;
    case SDL_SCANCODE_F8: return KEY_F8;
    case SDL_SCANCODE_F9: return KEY_F9;
    case SDL_SCANCODE_F10: return KEY_F10;
    case SDL_SCANCODE_F11: return KEY_F11;
    case SDL_SCANCODE_F12: return KEY_F12;

    case SDL_SCANCODE_0: return KEY_0;
    case SDL_SCANCODE_1: return KEY_1;
    case SDL_SCANCODE_2: return KEY_2;
    case SDL_SCANCODE_3: return KEY_3;
    case SDL_SCANCODE_4: return KEY_4;
    case SDL_SCANCODE_5: return KEY_5;
    case SDL_SCANCODE_6: return KEY_6;
    case SDL_SCANCODE_7: return KEY_7;
    case SDL_SCANCODE_8: return KEY_8;
    case SDL_SCANCODE_9: return KEY_9;

    case SDL_SCANCODE_DELETE: return KEY_DEL;
    case SDL_SCANCODE_BACKSPACE: return KEY_BACKSPACE;
    case SDL_SCANCODE_RETURN: return KEY_RETURN;
    case SDL_SCANCODE_KP_ENTER: return KEY_RETURN;
    case SDL_SCANCODE_LSHIFT: return KEY_SHIFT;
    case SDL_SCANCODE_RSHIFT: return KEY_SHIFT;
    case SDL_SCANCODE_LCTRL: return KEY_CTRL;
    case SDL_SCANCODE_RCTRL: return KEY_CTRL;
    case SDL_SCANCODE_LALT: return KEY_ALT;
    case SDL_SCANCODE_TAB: return KEY_TAB;
    case SDL_SCANCODE_ESCAPE: return KEY_ESC;

    case SDL_SCANCODE_LEFT: return KEY_LEFT;
    case SDL_SCANCODE_RIGHT: return KEY_RIGHT;
    case SDL_SCANCODE_UP: return KEY_UP;
    case SDL_SCANCODE_DOWN: return KEY_DOWN;

    default: return KEY_UNKNOWN;
    }
}

static Uint32 SDLCALL on_tick (Void *userdata, SDL_TimerID id, Uint32 interval) {
    SDL_Event e = { .type = SDL_EVENT_USER };
    SDL_PushEvent(&e);
    return interval;
}

TickId win_tick_start (U32 ms) {
    return SDL_AddTimer(ms, on_tick, NULL);
}

Void win_tick_end (TickId tick) {
    SDL_RemoveTimer(tick);
}

F32 win_get_display_scale () {
    return SDL_GetWindowDisplayScale(window);
}

Void win_minimize () {
    SDL_MinimizeWindow(window);
}

Void win_toggle_maximize () {
    SDL_WindowFlags flags = SDL_GetWindowFlags(window);

    if (flags & SDL_WINDOW_MAXIMIZED) {
        SDL_RestoreWindow(window);
    } else {
        SDL_MaximizeWindow(window);
    }
}

Bool win_is_pinned () {
    SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    return flags & SDL_WINDOW_ALWAYS_ON_TOP;
}

Void win_toggle_pin () {
    SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    Bool top = !!(flags & SDL_WINDOW_ALWAYS_ON_TOP);
    SDL_SetWindowAlwaysOnTop(window, !top);
}

Void win_close () {
    running = false;
}

Bool win_is_tiled () {
    SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    if (flags & SDL_WINDOW_MAXIMIZED) return true;

    int wx, wy;
    SDL_GetWindowPosition(window, &wx, &wy);

    SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    SDL_Rect bounds;
    SDL_GetDisplayUsableBounds(display, &bounds);

    Bool left   = wx == bounds.x;
    Bool top    = wy == bounds.y;
    Bool right  = (wx + win_width)  == (bounds.x + bounds.w);
    Bool bottom = (wy + win_height) == (bounds.y + bounds.h);

    return left || top || right || bottom;
}

SDL_HitTestResult win_hit_test (SDL_Window *win, const SDL_Point *area, Void *data) {
    if (! ui) return SDL_HITTEST_NORMAL;

    Int w, h;
    SDL_GetWindowSize(win, &w, &h);

    I32 border = 5;

    if (area->y < border) {
        if (area->x < border) return SDL_HITTEST_RESIZE_TOPLEFT;
        if (area->x > w - border) return SDL_HITTEST_RESIZE_TOPRIGHT;
        return SDL_HITTEST_RESIZE_TOP;
    } else if (area->y > h - border) {
        if (area->x < border) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (area->x > w - border) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        return SDL_HITTEST_RESIZE_BOTTOM;
    } else if (area->x < border) {
        return SDL_HITTEST_RESIZE_LEFT;
    } else if (area->x > w - border) {
        return SDL_HITTEST_RESIZE_RIGHT;
    } else if (ui->hovered == ui->titlebar) {
        return SDL_HITTEST_DRAGGABLE;
    }

    return SDL_HITTEST_NORMAL;
}

static Void process_event (SDL_Event *event, Bool *running) {
    switch (event->type) {
    case SDL_EVENT_QUIT: {
        *running = false;
    } break;

    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
        Auto e = array_push_slot(&events);
        e->tag = EVENT_DUMMY;
        F32 s = win_get_display_scale();
        ui_scale(1/prev_scale);
        ui_scale(s);
        prev_scale = s;
    } break;

    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
        Int width  = event->window.data1;
        Int height = event->window.data2;

        win_width  = width;
        win_height = height;

        update_projection();
        glViewport(0, 0, width, height);

        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &framebuffer_tex);
        glDeleteFramebuffers(1, &blur_buffer1);
        glDeleteTextures(1, &blur_tex1);
        glDeleteFramebuffers(1, &blur_buffer2);
        glDeleteTextures(1, &blur_tex2);

        framebuffer = framebuffer_new(&framebuffer_tex, win_width, win_height);
        blur_buffer1 = framebuffer_new(&blur_tex1, floor(win_width  / BLUR_SHRINK), floor(win_height / BLUR_SHRINK));
        blur_buffer2 = framebuffer_new(&blur_tex2, floor(win_width  / BLUR_SHRINK), floor(win_height / BLUR_SHRINK));

        glScissor(0, 0, width, height);

        Auto e = array_push_slot(&events);
        e->tag = EVENT_WINDOW_SIZE;
    } break;

    case SDL_EVENT_MOUSE_WHEEL: {
        Auto e = array_push_slot(&events);
        e->tag = EVENT_SCROLL;
        e->x = event->wheel.x;
        e->y = event->wheel.y;
    } break;

    case SDL_EVENT_MOUSE_MOTION: {
        Auto e = array_push_slot(&events);
        e->tag = EVENT_MOUSE_MOVE;
        F32 scale = win_get_display_scale();
        e->x = event->motion.x * scale;
        e->y = event->motion.y * scale;
    } break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        Auto e  = array_push_slot(&events);
        e->tag  = (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? EVENT_KEY_PRESS : EVENT_KEY_RELEASE;
        e->key  = SDL_BUTTON_LEFT ? KEY_MOUSE_LEFT :
                  SDL_BUTTON_MIDDLE ? KEY_MOUSE_MIDDLE :
                  SDL_BUTTON_RIGHT ? KEY_MOUSE_RIGHT :
                  KEY_UNKNOWN;
        e->mods = convert_key_mod(SDL_GetModState());
    } break;

    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        Auto e = array_push_slot(&events);
        e->tag = (event->type == SDL_EVENT_KEY_UP) ? EVENT_KEY_RELEASE : EVENT_KEY_PRESS;
        e->key = convert_scancode(event->key.scancode);
        e->mods = convert_key_mod(event->key.mod);
    } break;

    case SDL_EVENT_TEXT_INPUT: {
        Auto e = array_push_slot(&events);
        e->tag = EVENT_TEXT_INPUT;
        e->text = str(cast(Char*, event->text.text));
    } break;

    case SDL_EVENT_USER: {
        Auto e = array_push_slot(&events);
        e->tag = EVENT_DUMMY;
    } break;
    }
}

Void win_init (CString title) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    window = SDL_CreateWindow(title, win_width, win_height, SDL_WINDOW_TRANSPARENT|SDL_WINDOW_BORDERLESS|SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
    gl_ctx = SDL_GL_CreateContext(window);
    gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
    SDL_StartTextInput(window);
    SDL_SetWindowHitTest(window, win_hit_test, 0);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    prev_scale = win_get_display_scale();
    for (U32 c = SDL_SYSTEM_CURSOR_DEFAULT; c < SDL_SYSTEM_CURSOR_COUNT; ++c) {
        cursors[c] = SDL_CreateSystemCursor(c);
    }

    framebuffer   = framebuffer_new(&framebuffer_tex, win_width, win_height);
    blur_buffer1  = framebuffer_new(&blur_tex1, floor(win_width/BLUR_SHRINK), floor(win_height/BLUR_SHRINK));
    blur_buffer2  = framebuffer_new(&blur_tex2, floor(win_width/BLUR_SHRINK), floor(win_height/BLUR_SHRINK));
    rect_shader   = shader_new("data/shaders/rect_vs.glsl", "data/shaders/rect_fs.glsl");
    screen_shader = shader_new("data/shaders/screen_vs.glsl", "data/shaders/screen_fs.glsl");
    blur_shader   = shader_new("data/shaders/blur_vs.glsl", "data/shaders/blur_fs.glsl");

    { // Screen quad init:
        array_init(&screen_vertices, mem_root);
        array_push_lit(&screen_vertices, .pos={-1.0f,  1.0f},  .tex={0.0f, 1.0f});
        array_push_lit(&screen_vertices, .pos={-1.0f, -1.0f},  .tex={0.0f, 0.0f});
        array_push_lit(&screen_vertices, .pos={ 1.0f, -1.0f},  .tex={1.0f, 0.0f});
        array_push_lit(&screen_vertices, .pos={-1.0f,  1.0f},  .tex={0.0f, 1.0f});
        array_push_lit(&screen_vertices, .pos={ 1.0f, -1.0f},  .tex={1.0f, 0.0f});
        array_push_lit(&screen_vertices, .pos={ 1.0f,  1.0f},  .tex={1.0f, 1.0f});

        glGenVertexArrays(1, &screen_VAO);
        glGenBuffers(1, &screen_VBO);
        glBindVertexArray(screen_VAO);
        glBindBuffer(GL_ARRAY_BUFFER, screen_VBO);
        glBufferData(GL_ARRAY_BUFFER, array_size(&screen_vertices), screen_vertices.data, GL_STATIC_DRAW);
        ATTR(AElem(&screen_vertices), 0, 2, pos);
        ATTR(AElem(&screen_vertices), 1, 2, tex);

        glUseProgram(screen_shader);
        set_int(screen_shader, "tex", 0);
    }

    array_init(&blur_vertices, mem_root);
    glGenVertexArrays(1, &blur_VAO);
    glBindVertexArray(blur_VAO);
    glGenBuffers(1, &blur_VBO);
    ATTR(AElem(&blur_vertices), 0, 2, pos);
    glBindVertexArray(0);

    array_init(&vertices, mem_root);
    array_init(&events, mem_root);
    update_projection();
}

Void win_run (Void (*frame)(F64 dt)) {
    F64 dt   = 0;
    U64 now  = SDL_GetPerformanceCounter();
    U64 last = 0;

    I32 poll_events = 4;

    while (running) {
        last = now;
        now  =  SDL_GetPerformanceCounter();
        dt   = (now - last) / cast(F64, SDL_GetPerformanceFrequency());

        log_scope(ls, 1);

        #if 0
            static U64 fps_last_counter = 0;
            static U64 fps_frame_count  = 0;

            fps_frame_count++;
            if (fps_last_counter == 0) fps_last_counter = now;
            F64 elapsed = (now - fps_last_counter) / cast(F64, SDL_GetPerformanceFrequency());

            if (elapsed >= 0.5) {
                tmem_new(tm);
                F64 fps = cast(F64, fps_frame_count) / elapsed;
                SDL_SetWindowTitle(window, astr_fmt(tm, "fps: %.1f%c", fps, 0).data);
                fps_frame_count  = 0;
                fps_last_counter = now;
            }
        #endif

        SDL_Event event;
        if (poll_events > 0 || ui_is_animating()) {
            while (SDL_PollEvent(&event)) process_event(&event, &running);
            poll_events--;
        } else {
            SDL_WaitEvent(&event);
            do process_event(&event, &running); while (SDL_PollEvent(&event));
            poll_events = 4;
            now = SDL_GetPerformanceCounter();
        }

        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        if (events.count == 0) array_push_lit(&events, .tag=EVENT_DUMMY);

        if (events.count > 1 && !array_find_ref(&events, IT->tag != EVENT_MOUSE_MOVE)) {
            Event e = array_pop(&events);
            events.count = 0;
            array_push(&events, e);
        }

        frame(dt);
        events.count = 0;
        if (vertices.count) dr_flush_vertices();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(screen_shader);
        glBindVertexArray(screen_VAO);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, framebuffer_tex);
        glDrawArrays(GL_TRIANGLES, 0, screen_vertices.count);

        SDL_GL_SwapWindow(window);
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(rect_shader);
    glDeleteProgram(screen_shader);
    SDL_GL_DestroyContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

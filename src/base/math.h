#pragma once

// =============================================================================
// Conventions:
// ------------
//
// - Right-handed coordinates with +y=up -z=forward +x=right.
// - Column vectors.
// - Column-major matrix order: matrix[col][row].
// =============================================================================
#include <tgmath.h>
#include "base/array.h"
#include "base/string.h"

// =============================================================================
// Scalars
// =============================================================================
#define PI  3.1415926535897f
#define TAU (2*PI)

typedef F32 Rad;

#define deg2rad(D) ((D)*PI/180)

F32 lerp_f32 (F32 a, F32 b, F32 t);

// =============================================================================
// Vectors:
// =============================================================================
iunion (Vec2) { struct { F32 x, y; }; F32 v[2]; };
iunion (Vec3) { struct { F32 x, y, z; }; F32 v[3]; };
iunion (Vec4) {
    struct { F32 x, y, z, w; };
    Vec3 xyz;
    F32 v[4];
};

array_typedef(Vec2, Vec2);
array_typedef(Vec3, Vec3);
array_typedef(Vec4, Vec4);

#define vec2(x, y)       ((Vec2){x, y})
#define vec3(x, y, z)    ((Vec3){x, y, z})
#define vec4(x, y, z, w) ((Vec4){x, y, z, w})

Vec3 add_v3       (Vec3, Vec3);
Vec3 sub_v3       (Vec3, Vec3);
Vec3 cross        (Vec3, Vec3);
Vec3 normcross    (Vec3, Vec3);
Vec3 mul_f32_v3   (F32, Vec3);
Vec3 mul_v3_v3    (Vec3, Vec3);
F32  dot_v3       (Vec3, Vec3);
F32  len_v3       (Vec3);
Vec3 normalize_v3 (Vec3);
Void print_v3     (Vec3, AString *, CString prefix, CString suffix);
Vec3 lerp_v3      (Vec3, Vec3, F32);

Vec2 add_v2       (Vec2, Vec2);
Vec2 sub_v2       (Vec2, Vec2);
F32  len_v2       (Vec2);
Vec2 mul_f32_v2   (F32, Vec2);
Vec2 normalize_v2 (Vec2);

Vec4 lerp_v4      (Vec4, Vec4, F32);

// =============================================================================
// Matrices:
// =============================================================================
istruct (Mat4) { F32 v[4][4]; };

array_typedef(Mat4, Mat4);

Mat4 mat4            (F32 diagonal);
Mat4 mat_translate   (Vec3 delta);
Mat4 mat_rotate      (Rad angle, Vec3 axis);
Mat4 mat_scale       (Vec3);
Mat4 mat_uscale      (F32);
Mat4 mat_ortho       (F32 left, F32 right, F32 bottom, F32 top, F32 znear, F32 zfar);
Mat4 mat_perspective (Rad fovy, F32 aspect, F32 znear, F32 zfar);
Mat4 mat_look_at     (Vec3 pos, Vec3 target, Vec3 up);
Mat4 mul_m4          (Mat4 a, Mat4 b);
Void print_m4        (Mat4, AString *, CString prefix, CString suffix);

// =============================================================================
// Overloads:
// =============================================================================
#define mul(a, b) typematch2(a, b,\
    Void(*)(Mat4, Mat4): mul_m4,\
    Void(*)(Vec3, Vec3): mul_v3_v3,\
    Void(*)(F32,  Vec3): mul_f32_v3,\
    Void(*)(F32,  Vec2): mul_f32_v2\
)(a, b)

#define add(a, b)         typematch(a, Vec2:add_v2, Vec3:add_v3)(a, b)
#define sub(a, b)         typematch(a, Vec2:sub_v2, Vec3:sub_v3)(a, b)
#define dot(a, b)         typematch(a, Vec3:dot_v3)(a, b)
#define len(a)            typematch(a, Vec2:len_v2, Vec3:len_v3)(a)
#define normalize(a)      typematch(a, Vec2:normalize_v2, Vec3:normalize_v3)(a)
#define lerp(a, b, t)     typematch(a, Vec3:lerp_v3, Vec4:lerp_v4)(a, b, t)
#define mat_print(v, a)   typematch(v, Mat4:print_m4)(v, a, #v":\n", "")
#define mat_println(v, a) typematch(v, Mat4:print_m4)(v, a, #v":\n", "")
#define vec_print(v, a)   typematch(v, Vec3:print_v3)(v, a, #v":", "")
#define vec_println(v, a) typematch(v, Vec3:print_v3)(v, a, #v":", "\n")

// =============================================================================
// Colors:
// =============================================================================
Vec3 rgb_to_hsv      (Vec3 rgb);
Vec3 hsv_to_rgb      (Vec3 hsv);
Vec4 rgba_to_hsva    (Vec4 rgba);
Vec4 hsva_to_rgba    (Vec4 hsva);
Vec3 srgb_to_linear  (Vec3 srgb);
Vec3 linear_to_srgb  (Vec3 linear);
Vec4 srgba_to_linear (Vec4 srgba);
Vec4 linear_to_srgba (Vec4 linear);

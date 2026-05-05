#version 450 core

layout (location = 0) in vec2  v_position;
layout (location = 1) in vec4  v_color;
layout (location = 2) in vec2  v_bottom_right;
layout (location = 3) in vec2  v_top_left;
layout (location = 4) in vec4 v_radius;
layout (location = 5) in float v_edge_softness;
layout (location = 6) in vec4  v_border_color;
layout (location = 7) in vec4 v_border_widths;
layout (location = 8) in vec4 v_inset_shadow_color;
layout (location = 9) in vec4 v_outset_shadow_color;
layout (location = 10) in float v_outset_shadow_width;
layout (location = 11) in float v_inset_shadow_width;
layout (location = 12) in vec2 v_shadow_offsets;
layout (location = 13) in vec2 v_uv;
layout (location = 14) in vec4 v_text_color;
layout (location = 15) in float v_text_is_grayscale;

out vec4 color;
flat out vec4 radius;
flat out float edge_softness;
flat out vec4 border_color;
flat out vec4 border_widths;
flat out vec4 inset_shadow_color;
flat out vec4 outset_shadow_color;
flat out float outset_shadow_width;
flat out float inset_shadow_width;
flat out vec2 shadow_offsets;
flat out vec2 center;
flat out vec2 half_size;
flat out vec2 top_left;
flat out vec2 bottom_right;
flat out vec4 text_color;
flat out float text_is_grayscale;
out vec2 uv;

uniform mat4 projection;

void main () {
    gl_Position         = projection * vec4(v_position, 0, 1.0);
    color               = v_color;
    radius              = v_radius;
    edge_softness       = v_edge_softness;
    border_color        = v_border_color;
    border_widths       = v_border_widths;
    inset_shadow_color  = v_inset_shadow_color;
    outset_shadow_color = v_outset_shadow_color;
    outset_shadow_width = v_outset_shadow_width;
    inset_shadow_width  = v_inset_shadow_width;
    shadow_offsets      = v_shadow_offsets;
    center              = (v_top_left + v_bottom_right) * 0.5;
    half_size           = abs(v_top_left - v_bottom_right) * 0.5 - 2*v_outset_shadow_width - 2*v_edge_softness;
    top_left            = v_top_left;
    bottom_right        = v_bottom_right;
    text_color          = v_text_color;
    text_is_grayscale   = v_text_is_grayscale;
    uv                  = v_uv;
}

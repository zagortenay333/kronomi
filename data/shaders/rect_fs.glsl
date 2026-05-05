#version 450 core

out vec4 frag_color;

in vec4 color;
flat in vec4 radius;
flat in float edge_softness;
flat in vec4 border_color;
flat in vec4 border_widths;
flat in vec4 inset_shadow_color;
flat in vec4 outset_shadow_color;
flat in float outset_shadow_width;
flat in float inset_shadow_width;
flat in vec2 shadow_offsets;
flat in vec2 center;
flat in vec2 half_size;
flat in vec2 top_left;
flat in vec2 bottom_right;
flat in vec4 text_color;
flat in float text_is_grayscale;
in vec2 uv;

const float pi = 3.141592653589793;

uniform sampler2D tex;

// A standard gaussian function, used for weighting samples
float gaussian (float x, float sigma) {
    return exp(-(x * x) / (2.0 * sigma * sigma)) / (sqrt(2.0 * pi) * sigma);
}

// This approximates the error function, needed for the gaussian integral
vec2 erf (vec2 x) {
    vec2 s = sign(x), a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}

// Return the blurred mask along the x dimension
float box_shadow_x (float x, float y, float sigma, float corner, vec2 halfSize) {
    float delta = min(halfSize.y - corner - abs(y), 0.0);
    float curved = halfSize.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    vec2 integral = 0.5 + 0.5 * erf((x + vec2(-curved, curved)) * (sqrt(0.5) / sigma));
    return integral.y - integral.x;
}

// Box shadow code by Evan Wallace in public domain.
// Return the mask for the shadow of a box from lower to upper.
float box_shadow (vec2 halfSize, vec2 point, float sigma, float corner) {
    sigma = max(sigma, .001);

    // The signal is only non-zero in a limited range, so don't waste samples
    float low = point.y - halfSize.y;
    float high = point.y + halfSize.y;
    float start = clamp(-3.0 * sigma, low, high);
    float end = clamp(3.0 * sigma, low, high);

    // Accumulate samples (we can get away with surprisingly few samples)
    float step = (end - start) / 4.0;
    float y = start + step * 0.5;
    float value = 0.0;
    for (int i = 0; i < 4; i++) {
        value += box_shadow_x(point.x, point.y - y, sigma, corner, halfSize) * gaussian(y, sigma) * step;
        y += step;
    }

    return value;
}

float rect_sdf (vec2 frag_pos, vec2 center, vec2 half_size, float radius) {
    return length(max(abs(frag_pos - center) - half_size + radius, 0.0)) - radius;
}

float select_border_width (vec2 p, vec2 half_size, vec4 borders) {
    if (p.y > (half_size.y - borders.y)) return borders.y;
    if (p.y < (-half_size.y + borders.w)) return borders.w;
    if (p.x > (half_size.x - borders.x)) return borders.x;
    if (p.x < (-half_size.x + borders.z)) return borders.z;
    return 0;
}

void draw_rect () {
    frag_color = color;
    vec2 frag_pos = gl_FragCoord.xy;

    if (text_color.w > 0) {
        frag_color = texture(tex, uv/textureSize(tex, 0));
        if (text_is_grayscale > 0) frag_color *= text_color;
    }

    vec2 fpc = frag_pos - center;
    float r = (fpc.x > 0.0) ? ((fpc.y > 0.0) ? radius.x : radius.z) : ((fpc.y > 0.0) ? radius.y : radius.w);

    float dist_outer = rect_sdf(frag_pos, center, half_size, r);
    float dist_outer_smooth = 1.0 - smoothstep(0.0, edge_softness, dist_outer);
    frag_color.a *= dist_outer_smooth;

    if (inset_shadow_width > 0.001) {
        vec4 ic = inset_shadow_color;
        ic.a *= 1 - box_shadow(half_size, fpc, inset_shadow_width, r);
        float shadow_alpha = 1.0 - pow(1.0 - ic.a, 8.0);
        frag_color = mix(frag_color, vec4(ic.rgb, 1.0), shadow_alpha);
        frag_color.a *= dist_outer_smooth;
    }

    if (border_widths.x > 0.001 || border_widths.y > 0.001 || border_widths.z > 0.001 || border_widths.w > 0.001) {
        float b = select_border_width(fpc, half_size - r, border_widths);
        float dist_inner = rect_sdf(frag_pos, center, half_size - b, max(r - b, 0));
        float dist_inner_smooth = smoothstep(0.0, edge_softness, dist_inner);
        frag_color = mix(frag_color, border_color, dist_inner_smooth);
        frag_color.a *= dist_outer_smooth;
    }

    if (outset_shadow_width > 0.001) {
        vec4 oc = outset_shadow_color;
        oc.a *= box_shadow(half_size + edge_softness, fpc - shadow_offsets, outset_shadow_width, r);
        frag_color = mix(frag_color, oc, 1.0 - dist_outer_smooth);
    }

    frag_color.rgb *= frag_color.a;
}

float line_sdf (vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float t = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * t);
}

void draw_line() {
    frag_color = color;
    vec2 frag_pos = gl_FragCoord.xy;
    vec2 a = text_color.xy;
    vec2 b = text_color.zw;
    float half_thickness = radius.x;

    float dist_outer = line_sdf(frag_pos, a, b) - half_thickness;
    float dist_outer_smooth = 1.0 - smoothstep(0.0, edge_softness, dist_outer);
    frag_color.a *= dist_outer_smooth;

    frag_color.rgb *= frag_color.a;
}

void main () {
    if (text_is_grayscale == -1) {
        draw_line();
    } else {
        draw_rect();
    }
}

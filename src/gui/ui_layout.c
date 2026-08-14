#include "ui_layout.h"

fb_rect_t ui_pm(int x_pm, int y_pm, int w_pm, int h_pm) {
    int fw = fb_width();
    int fh = fb_height();

    fb_rect_t r;
    r.x = (fw * x_pm) / 1000;
    r.y = (fh * y_pm) / 1000;
    r.w = (fw * w_pm) / 1000;
    r.h = (fh * h_pm) / 1000;
    return r;
}

fb_rect_t ui_pm_right(int right_pm, int top_pm, int w_pm, int h_pm) {
    int fw = fb_width();
    int fh = fb_height();

    fb_rect_t r;
    r.w = (fw * w_pm) / 1000;
    r.h = (fh * h_pm) / 1000;
    r.x = fw - ((fw * right_pm) / 1000) - r.w;
    r.y = (fh * top_pm) / 1000;
    return r;
}

fb_rect_t ui_pm_bottom(int left_pm, int bottom_pm, int w_pm, int h_pm) {
    int fw = fb_width();
    int fh = fb_height();

    fb_rect_t r;
    r.w = (fw * w_pm) / 1000;
    r.h = (fh * h_pm) / 1000;
    r.x = (fw * left_pm) / 1000;
    r.y = fh - ((fh * bottom_pm) / 1000) - r.h;
    return r;
}

fb_rect_t ui_pm_bottom_right(int right_pm, int bottom_pm, int w_pm, int h_pm) {
    int fw = fb_width();
    int fh = fb_height();

    fb_rect_t r;
    r.w = (fw * w_pm) / 1000;
    r.h = (fh * h_pm) / 1000;
    r.x = fw - ((fw * right_pm) / 1000) - r.w;
    r.y = fh - ((fh * bottom_pm) / 1000) - r.h;
    return r;
}

fb_rect_t ui_pm_bottom_center(int bottom_pm, int w_pm, int h_pm) {
    int fw = fb_width();
    int fh = fb_height();

    fb_rect_t r;
    r.w = (fw * w_pm) / 1000;
    r.h = (fh * h_pm) / 1000;
    r.x = (fw - r.w) / 2;
    r.y = fh - ((fh * bottom_pm) / 1000) - r.h;
    return r;
}

int ui_scale_pm(void) {
    int fw = fb_width();
    int fh = fb_height();

    if (fw <= 0 || fh <= 0)
        return 1000;

    int sx = (fw * 1000) / UI_DESIGN_W;
    int sy = (fh * 1000) / UI_DESIGN_H;
    int s = sx < sy ? sx : sy;

    if (s < 600)
        s = 600;
    if (s > 1600)
        s = 1600;

    return s;
}

int ui_scaled(int base_px) {
    int v = (base_px * ui_scale_pm()) / 1000;
    return v < 1 ? 1 : v;
}

int ui_font_scale(void) {
    return ui_scale_pm() >= 1250 ? 2 : 1;
}

static int clamp255(int v) {
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return v;
}

color_t ui_blend(color_t bg, color_t fg, int alpha_pm) {
    if (alpha_pm < 0)
        alpha_pm = 0;
    if (alpha_pm > 1000)
        alpha_pm = 1000;

    color_t out;
    out.r = (uint8_t)clamp255(bg.r + (((int)fg.r - (int)bg.r) * alpha_pm) / 1000);
    out.g = (uint8_t)clamp255(bg.g + (((int)fg.g - (int)bg.g) * alpha_pm) / 1000);
    out.b = (uint8_t)clamp255(bg.b + (((int)fg.b - (int)bg.b) * alpha_pm) / 1000);
    return out;
}

static int rounded_mask(int dx, int dy, int w, int h, int r) {
    if (r <= 0)
        return 1;

    if (dx < r && dy < r) {
        int ox = r - 1 - dx;
        int oy = r - 1 - dy;
        return ox * ox + oy * oy <= r * r;
    }
    if (dx >= w - r && dy < r) {
        int ox = dx - (w - r);
        int oy = r - 1 - dy;
        return ox * ox + oy * oy <= r * r;
    }
    if (dx < r && dy >= h - r) {
        int ox = r - 1 - dx;
        int oy = dy - (h - r);
        return ox * ox + oy * oy <= r * r;
    }
    if (dx >= w - r && dy >= h - r) {
        int ox = dx - (w - r);
        int oy = dy - (h - r);
        return ox * ox + oy * oy <= r * r;
    }
    return 1;
}

void ui_fill_glass(int x, int y, int w, int h, color_t tint, int alpha_pm) {
    if (w <= 0 || h <= 0)
        return;

    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            color_t under = fb_get_pixel(x + dx, y + dy);
            fb_put_pixel(x + dx, y + dy, ui_blend(under, tint, alpha_pm));
        }
    }
}

void ui_fill_rounded_glass(int x, int y, int w, int h, int radius, color_t tint, int alpha_pm) {
    if (w <= 0 || h <= 0)
        return;

    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            if (!rounded_mask(dx, dy, w, h, radius))
                continue;

            color_t under = fb_get_pixel(x + dx, y + dy);
            fb_put_pixel(x + dx, y + dy, ui_blend(under, tint, alpha_pm));
        }
    }
}

void ui_draw_rounded_border(int x, int y, int w, int h, int radius, color_t color) {
    if (w <= 0 || h <= 0)
        return;

    for (int dx = 0; dx < w; dx++) {
        if (rounded_mask(dx, 0, w, h, radius))
            fb_put_pixel(x + dx, y, color);
        if (rounded_mask(dx, h - 1, w, h, radius))
            fb_put_pixel(x + dx, y + h - 1, color);
    }

    for (int dy = 0; dy < h; dy++) {
        if (rounded_mask(0, dy, w, h, radius))
            fb_put_pixel(x, y + dy, color);
        if (rounded_mask(w - 1, dy, w, h, radius))
            fb_put_pixel(x + w - 1, y + dy, color);
    }
}

void ui_draw_rounded_border_alpha(int x, int y, int w, int h, int radius, color_t color, int alpha_pm) {
    if (w <= 0 || h <= 0)
        return;

    for (int dx = 0; dx < w; dx++) {
        if (rounded_mask(dx, 0, w, h, radius)) {
            color_t under = fb_get_pixel(x + dx, y);
            fb_put_pixel(x + dx, y, ui_blend(under, color, alpha_pm));
        }
        if (rounded_mask(dx, h - 1, w, h, radius)) {
            color_t under = fb_get_pixel(x + dx, y + h - 1);
            fb_put_pixel(x + dx, y + h - 1, ui_blend(under, color, alpha_pm));
        }
    }

    for (int dy = 0; dy < h; dy++) {
        if (rounded_mask(0, dy, w, h, radius)) {
            color_t under = fb_get_pixel(x, y + dy);
            fb_put_pixel(x, y + dy, ui_blend(under, color, alpha_pm));
        }
        if (rounded_mask(w - 1, dy, w, h, radius)) {
            color_t under = fb_get_pixel(x + w - 1, y + dy);
            fb_put_pixel(x + w - 1, y + dy, ui_blend(under, color, alpha_pm));
        }
    }
}

void ui_draw_glow_border(int x, int y, int w, int h, int radius, color_t color) {
    ui_draw_rounded_border(x - 1, y - 1, w + 2, h + 2, radius + 1, ui_blend(UI_BG_VOID, color, 250));
    ui_draw_rounded_border(x, y, w, h, radius, color);
}

void ui_draw_glow_border_alpha(int x, int y, int w, int h, int radius, color_t color, int alpha_pm) {
    int outer_alpha = (250 * alpha_pm) / 1000;
    ui_draw_rounded_border_alpha(x - 1, y - 1, w + 2, h + 2, radius + 1, color, outer_alpha);
    ui_draw_rounded_border_alpha(x, y, w, h, radius, color, alpha_pm);
}

void ui_draw_top_accent(int x, int y, int w, color_t color, int thickness) {
    if (thickness < 1)
        thickness = 1;

    fb_fill_rect(x, y, w, thickness, color);
}

void ui_fill_circle(int cx, int cy, int r, color_t c) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r)
                fb_put_pixel(cx + dx, cy + dy, c);
        }
    }
}

void ui_draw_progress(int x, int y, int w, int h, int percent, color_t fill, color_t track) {
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;

    int radius = h / 2;

    ui_fill_rounded_glass(x, y, w, h, radius, track, 1000);

    int fw = (w * percent) / 100;
    if (fw > 0)
        ui_fill_rounded_glass(x, y, fw, h, radius, fill, 1000);
}
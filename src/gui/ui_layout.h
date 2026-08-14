#pragma once

#include "../framebuffer.h"

#define UI_DESIGN_W 1440
#define UI_DESIGN_H 900

fb_rect_t ui_pm(int x_pm, int y_pm, int w_pm, int h_pm);
fb_rect_t ui_pm_right(int right_pm, int top_pm, int w_pm, int h_pm);
fb_rect_t ui_pm_bottom(int left_pm, int bottom_pm, int w_pm, int h_pm);
fb_rect_t ui_pm_bottom_right(int right_pm, int bottom_pm, int w_pm, int h_pm);
fb_rect_t ui_pm_bottom_center(int bottom_pm, int w_pm, int h_pm);

int ui_scale_pm(void);
int ui_scaled(int base_px);
int ui_font_scale(void);

color_t ui_blend(color_t bg, color_t fg, int alpha_pm);

void ui_fill_glass(int x, int y, int w, int h, color_t tint, int alpha_pm);
void ui_fill_rounded_glass(int x, int y, int w, int h, int radius, color_t tint, int alpha_pm);
void ui_draw_rounded_border(int x, int y, int w, int h, int radius, color_t color);
void ui_draw_rounded_border_alpha(int x, int y, int w, int h, int radius, color_t color, int alpha_pm);
void ui_draw_glow_border(int x, int y, int w, int h, int radius, color_t color);
void ui_draw_glow_border_alpha(int x, int y, int w, int h, int radius, color_t color, int alpha_pm);
void ui_draw_top_accent(int x, int y, int w, color_t color, int thickness);
void ui_fill_circle(int cx, int cy, int r, color_t c);
void ui_draw_progress(int x, int y, int w, int h, int percent, color_t fill, color_t track);

#define UI_BG_VOID     RGB(5, 5, 16)
#define UI_GLASS_TINT  RGB(14, 10, 30)
#define UI_NEON_PURPLE RGB(150, 70, 230)
#define UI_NEON_MAGENTA RGB(230, 70, 190)
#define UI_ELECTRIC_BLUE RGB(80, 150, 255)
#define UI_TEXT_WHITE  RGB(235, 235, 245)
#define UI_TEXT_LAVENDER RGB(195, 175, 225)
#define UI_TEXT_DIM    RGB(135, 125, 155)
#define UI_OK_GREEN    RGB(70, 210, 150)
#define UI_WARN_AMBER  RGB(235, 170, 70)
#define UI_DANGER_RED  RGB(225, 80, 100)
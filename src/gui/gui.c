#include "gui.h"
#include "../drivers/mouse/mouse.h"
#include "../drivers/timer/timer.h"
#include "../framebuffer.h"
#include "../terminal/terminal.h"
#include "window.h"

#define TITLEBAR_H     24
#define TASKBAR_H      28
#define CURSOR_W        8
#define CURSOR_H        8
#define BG_COLOR        RGB(20, 20, 20)
#define CONTENT_MARGIN   8
#define BTN_SIZE        16
#define BTN_GAP          6

static int prev_left    = 0;
static int gui_dirty    = 1;

static int      prev_cursor_x = -1;
static int      prev_cursor_y = -1;
static color_t  cursor_bg[CURSOR_W * CURSOR_H];

static int prev_win_x, prev_win_y, prev_win_w, prev_win_h;
static int prev_visible = 1;
static int first_draw = 1;

static const color_t *bg_pixels = 0;
static int bg_width = 0;
static int bg_height = 0;
static color_t solid_bg_color = BG_COLOR;

void gui_set_background(const color_t *pixels, int width, int height) {
    bg_pixels = pixels;
    bg_width = width;
    bg_height = height;
    gui_dirty = 1;
}

void gui_set_bg_color(color_t color) {
    bg_pixels = 0;
    solid_bg_color = color;
    gui_dirty = 1;
}

static void draw_background_full(void) {
    if (bg_pixels && bg_width > 0 && bg_height > 0)
        fb_draw_image(0, 0, bg_width, bg_height, bg_pixels);
    else
        fb_clear(solid_bg_color);
}

static void draw_background_rect(int x, int y, int w, int h) {
    if (bg_pixels && bg_width > 0 && bg_height > 0) {
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > bg_width)  w = bg_width  - x;
        if (y + h > bg_height) h = bg_height - y;

        for (int dy = 0; dy < h; dy++) {
            const color_t *row = bg_pixels + (uint32_t)(y + dy) * (uint32_t)bg_width + x;

            for (int dx = 0; dx < w; dx++)
                fb_put_pixel(x + dx, y + dy, row[dx]);
        }
    } else {
        fb_fill_rect(x, y, w, h, solid_bg_color);
    }
}

static window_t terminal_window = {
    .x = 200, .y = 120, .width = 500, .height = 300
};

static int window_visible(void) {
    return !terminal_window.minimized && !terminal_window.closed;
}

static void button_rects(fb_rect_t *close_r, fb_rect_t *max_r, fb_rect_t *min_r) {
    int y = terminal_window.y + (TITLEBAR_H - BTN_SIZE) / 2;
    int x = terminal_window.x + terminal_window.width - BTN_SIZE - 8;

    close_r->x = x;
    close_r->y = y;
    close_r->w = BTN_SIZE;
    close_r->h = BTN_SIZE;

    x -= BTN_SIZE + BTN_GAP;
    max_r->x = x;
    max_r->y = y;
    max_r->w = BTN_SIZE;
    max_r->h = BTN_SIZE;

    x -= BTN_SIZE + BTN_GAP;
    min_r->x = x;
    min_r->y = y;
    min_r->w = BTN_SIZE;
    min_r->h = BTN_SIZE;
}

static int point_in_rect(int px, int py, fb_rect_t r) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

static void draw_window(void) {
    fb_fill_rect(terminal_window.x, terminal_window.y,
                 terminal_window.width, terminal_window.height,
                 RGB(40, 40, 40));
    fb_fill_rect(terminal_window.x, terminal_window.y,
                 terminal_window.width, TITLEBAR_H,
                 RGB(0, 100, 255));
    fb_draw_string(terminal_window.x + 8, terminal_window.y + 8,
                   "Terminal", RGB(255,255,255), RGB(0,100,255));

    fb_rect_t close_r, max_r, min_r;
    button_rects(&close_r, &max_r, &min_r);

    fb_fill_rect(min_r.x, min_r.y, min_r.w, min_r.h, RGB(230, 200, 60));
    fb_draw_char(min_r.x + 4, min_r.y + 3, '_', RGB(0, 0, 0), RGB(230, 200, 60));

    fb_fill_rect(max_r.x, max_r.y, max_r.w, max_r.h, RGB(60, 180, 80));
    fb_draw_char(max_r.x + 4, max_r.y + 3, terminal_window.maximized ? 'o' : '#', RGB(0, 0, 0), RGB(60, 180, 80));

    fb_fill_rect(close_r.x, close_r.y, close_r.w, close_r.h, RGB(220, 60, 60));
    fb_draw_char(close_r.x + 4, close_r.y + 3, 'x', RGB(0, 0, 0), RGB(220, 60, 60));
}

static void draw_taskbar(void) {
    int y = fb_height() - TASKBAR_H;

    fb_fill_rect(0, y, fb_width(), TASKBAR_H, RGB(15, 15, 20));
    fb_fill_rect(0, y, fb_width(), 2, RGB(0, 100, 255));
    fb_draw_string(10, y + 9, "FuturisticOS", RGB(200, 200, 200), RGB(15, 15, 20));

    if (!window_visible())
        fb_draw_string(fb_width() - 90, y + 9, "Terminal", RGB(150, 150, 150), RGB(15, 15, 20));
}

static void terminal_content_rect(int *x, int *y, int *w, int *h) {
    *x = terminal_window.x + CONTENT_MARGIN;
    *y = terminal_window.y + TITLEBAR_H + CONTENT_MARGIN;
    *w = terminal_window.width - CONTENT_MARGIN * 2;
    *h = terminal_window.height - TITLEBAR_H - CONTENT_MARGIN * 2;
}

static void toggle_maximize(void) {
    if (terminal_window.maximized) {
        terminal_window.x = terminal_window.restore_x;
        terminal_window.y = terminal_window.restore_y;
        terminal_window.width = terminal_window.restore_w;
        terminal_window.height = terminal_window.restore_h;
        terminal_window.maximized = 0;
    } else {
        terminal_window.restore_x = terminal_window.x;
        terminal_window.restore_y = terminal_window.y;
        terminal_window.restore_w = terminal_window.width;
        terminal_window.restore_h = terminal_window.height;

        terminal_window.x = 0;
        terminal_window.y = 0;
        terminal_window.width = fb_width();
        terminal_window.height = fb_height() - TASKBAR_H;
        terminal_window.maximized = 1;
    }
}

void gui_restore_terminal(void) {
    terminal_window.minimized = 0;
    terminal_window.closed = 0;
    gui_dirty = 1;
}

void gui_draw(void) {
    mouse_state_t mouse = mouse_get_state();

    fb_rect_t dirty[6];
    int       ndirty = 0;

    if (gui_dirty) {
        gui_dirty = 0;

        if (first_draw) {
            first_draw = 0;
            draw_background_full();
            draw_taskbar();

            if (window_visible()) {
                draw_window();

                int cx, cy, cw, ch;
                terminal_content_rect(&cx, &cy, &cw, &ch);
                terminal_set_viewport(cx, cy, cw, ch);

                prev_win_x = terminal_window.x;
                prev_win_y = terminal_window.y;
                prev_win_w = terminal_window.width;
                prev_win_h = terminal_window.height;
            }

            prev_visible = window_visible();
            dirty[ndirty++] = (fb_rect_t){ 0, 0, fb_width(), fb_height() };
        } else {
            if (prev_visible) {
                draw_background_rect(prev_win_x, prev_win_y, prev_win_w, prev_win_h);
                dirty[ndirty++] = (fb_rect_t){
                    prev_win_x, prev_win_y, prev_win_w, prev_win_h
                };
            }

            int visible_now = window_visible();

            if (visible_now) {
                draw_window();
                dirty[ndirty++] = (fb_rect_t){
                    terminal_window.x, terminal_window.y,
                    terminal_window.width, terminal_window.height
                };

                int cx, cy, cw, ch;
                terminal_content_rect(&cx, &cy, &cw, &ch);

                if (!prev_visible || prev_win_w != terminal_window.width || prev_win_h != terminal_window.height)
                    terminal_set_viewport(cx, cy, cw, ch);
                else
                    terminal_move_viewport(cx, cy);

                prev_win_x = terminal_window.x;
                prev_win_y = terminal_window.y;
                prev_win_w = terminal_window.width;
                prev_win_h = terminal_window.height;
            }

            prev_visible = visible_now;

            draw_taskbar();
            dirty[ndirty++] = (fb_rect_t){ 0, fb_height() - TASKBAR_H, fb_width(), TASKBAR_H };
        }

        prev_cursor_x = -1;
    }

    if (prev_cursor_x >= 0) {
        for (int dy = 0; dy < CURSOR_H; dy++)
            for (int dx = 0; dx < CURSOR_W; dx++)
                fb_put_pixel(prev_cursor_x + dx, prev_cursor_y + dy,
                             cursor_bg[dy * CURSOR_W + dx]);
        dirty[ndirty++] = (fb_rect_t){
            prev_cursor_x, prev_cursor_y, CURSOR_W, CURSOR_H
        };
    }

    for (int dy = 0; dy < CURSOR_H; dy++)
        for (int dx = 0; dx < CURSOR_W; dx++)
            cursor_bg[dy * CURSOR_W + dx] =
                fb_get_pixel(mouse.x + dx, mouse.y + dy);

    fb_fill_rect(mouse.x, mouse.y, CURSOR_W, CURSOR_H, RGB(255,255,255));
    dirty[ndirty++] = (fb_rect_t){ mouse.x, mouse.y, CURSOR_W, CURSOR_H };

    prev_cursor_x = mouse.x;
    prev_cursor_y = mouse.y;

    fb_present_rects(dirty, ndirty);
}

void gui_update(void) {
    mouse_state_t mouse = mouse_get_state();
    int clicked = !prev_left && mouse.left;

    if (window_visible()) {
        fb_rect_t close_r, max_r, min_r;
        button_rects(&close_r, &max_r, &min_r);

        if (clicked && point_in_rect(mouse.x, mouse.y, close_r)) {
            terminal_window.closed = 1;
            terminal_window.dragging = 0;
            gui_dirty = 1;
        } else if (clicked && point_in_rect(mouse.x, mouse.y, max_r)) {
            toggle_maximize();
            terminal_window.dragging = 0;
            gui_dirty = 1;
        } else if (clicked && point_in_rect(mouse.x, mouse.y, min_r)) {
            terminal_window.minimized = 1;
            terminal_window.dragging = 0;
            gui_dirty = 1;
        } else {
            int inside_titlebar =
                mouse.x >= terminal_window.x &&
                mouse.x <  terminal_window.x + terminal_window.width &&
                mouse.y >= terminal_window.y &&
                mouse.y <  terminal_window.y + TITLEBAR_H;

            if (clicked && inside_titlebar && !terminal_window.maximized) {
                terminal_window.dragging      = 1;
                terminal_window.drag_offset_x = mouse.x - terminal_window.x;
                terminal_window.drag_offset_y = mouse.y - terminal_window.y;
            }
        }
    }

    if (!mouse.left)
        terminal_window.dragging = 0;

    if (terminal_window.dragging) {
        int new_x = mouse.x - terminal_window.drag_offset_x;
        int new_y = mouse.y - terminal_window.drag_offset_y;

        if (new_x != terminal_window.x || new_y != terminal_window.y) {
            terminal_window.x = new_x;
            terminal_window.y = new_y;
            gui_dirty = 1;
        }
    }

    int scroll = mouse_get_scroll();
    if (scroll && window_visible() && terminal_contains(mouse.x, mouse.y))
        terminal_scroll(scroll);

    prev_left = mouse.left;
}

void gui_init(void)       { gui_dirty = 1; }
void gui_invalidate(void) { gui_dirty = 1; }
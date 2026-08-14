#include "gui.h"
#include "../drivers/mouse/mouse.h"
#include "../drivers/timer/timer.h"
#include "../framebuffer.h"
#include "../terminal/terminal.h"
#include "window.h"

#define TITLEBAR_H     24
#define CURSOR_W        8
#define CURSOR_H        8
#define BG_COLOR        RGB(20, 20, 20)
#define CONTENT_MARGIN   8

static int prev_left    = 0;
static int gui_dirty    = 1;

static int      prev_cursor_x = -1;
static int      prev_cursor_y = -1;
static color_t  cursor_bg[CURSOR_W * CURSOR_H];

// previous window rect so we can erase just that region
static int prev_win_x, prev_win_y, prev_win_w, prev_win_h;
static int first_draw = 1;

// Desktop background. NULL means "flat color fill" - solid_bg_color, which
// defaults to BG_COLOR but can be swapped by gui_set_bg_color(). Either way,
// gui_set_background()/gui_set_bg_color() just change the pixel source; none
// of the drawing logic below needs to know which mode it's in.
static const color_t *bg_pixels = 0;
static int bg_width = 0;
static int bg_height = 0;
static color_t solid_bg_color = BG_COLOR;

void gui_set_background(const color_t *pixels, int width, int height) {
    bg_pixels = pixels;
    bg_width = width;
    bg_height = height;
    gui_dirty = 1; // force a full redraw with the new background
}

void gui_set_bg_color(color_t color) {
    bg_pixels = 0; // solid color mode takes over from any image background
    solid_bg_color = color;
    gui_dirty = 1; // force a full redraw with the new background
}

// Paint the whole background (image if one is set, else flat solid_bg_color).
static void draw_background_full(void) {
    if (bg_pixels && bg_width > 0 && bg_height > 0)
        fb_draw_image(0, 0, bg_width, bg_height, bg_pixels);
    else
        fb_clear(solid_bg_color);
}

// Repaint just one rectangle of background - used to erase whatever used
// to be at the window's old position before redrawing it at the new one.
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

static void draw_window(void) {
    fb_fill_rect(terminal_window.x, terminal_window.y,
                 terminal_window.width, terminal_window.height,
                 RGB(40, 40, 40));
    fb_fill_rect(terminal_window.x, terminal_window.y,
                 terminal_window.width, TITLEBAR_H,
                 RGB(0, 100, 255));
    fb_draw_string(terminal_window.x + 8, terminal_window.y + 8,
                   "Terminal", RGB(255,255,255), RGB(0,100,255));
}

// The rectangle inside the window, below the titlebar and inset by a small
// margin, that the shell's terminal is allowed to draw into.
static void terminal_content_rect(int *x, int *y, int *w, int *h) {
    *x = terminal_window.x + CONTENT_MARGIN;
    *y = terminal_window.y + TITLEBAR_H + CONTENT_MARGIN;
    *w = terminal_window.width - CONTENT_MARGIN * 2;
    *h = terminal_window.height - TITLEBAR_H - CONTENT_MARGIN * 2;
}

void gui_draw(void) {
    mouse_state_t mouse = mouse_get_state();

    fb_rect_t dirty[4];
    int       ndirty = 0;

    if (gui_dirty) {
        gui_dirty = 0;

        int cx, cy, cw, ch;
        terminal_content_rect(&cx, &cy, &cw, &ch);

        if (first_draw) {
            // very first frame — full clear needed
            first_draw = 0;
            draw_background_full();
            draw_window();
            dirty[ndirty++] = (fb_rect_t){ 0, 0, fb_width(), fb_height() };

            // Up to now the shell's terminal has been drawing straight onto
            // the raw framebuffer at (0,0) - that's how the boot log and
            // first prompt get on screen before the desktop exists. Now
            // that the window is actually up, hand it the window's content
            // rect instead so further output (and the cursor) lands inside
            // the window instead of clashing with the compositor.
            terminal_set_viewport(cx, cy, cw, ch);
        } else {
            // erase old window position with the background
            draw_background_rect(prev_win_x, prev_win_y, prev_win_w, prev_win_h);
            dirty[ndirty++] = (fb_rect_t){
                prev_win_x, prev_win_y, prev_win_w, prev_win_h
            };

            // draw window at new position
            draw_window();
            dirty[ndirty++] = (fb_rect_t){
                terminal_window.x, terminal_window.y,
                terminal_window.width, terminal_window.height
            };

            // window may have been dragged - keep the terminal's content
            // glued to the same spot inside it
            terminal_move_viewport(cx, cy);
        }

        // save new window position for next erase
        prev_win_x = terminal_window.x;
        prev_win_y = terminal_window.y;
        prev_win_w = terminal_window.width;
        prev_win_h = terminal_window.height;

        prev_cursor_x = -1; // cursor bg is stale after redraw
    }

    // restore old cursor area
    if (prev_cursor_x >= 0) {
        for (int dy = 0; dy < CURSOR_H; dy++)
            for (int dx = 0; dx < CURSOR_W; dx++)
                fb_put_pixel(prev_cursor_x + dx, prev_cursor_y + dy,
                             cursor_bg[dy * CURSOR_W + dx]);
        dirty[ndirty++] = (fb_rect_t){
            prev_cursor_x, prev_cursor_y, CURSOR_W, CURSOR_H
        };
    }

    // save pixels under new cursor
    for (int dy = 0; dy < CURSOR_H; dy++)
        for (int dx = 0; dx < CURSOR_W; dx++)
            cursor_bg[dy * CURSOR_W + dx] =
                fb_get_pixel(mouse.x + dx, mouse.y + dy);

    // draw cursor
    fb_fill_rect(mouse.x, mouse.y, CURSOR_W, CURSOR_H, RGB(255,255,255));
    dirty[ndirty++] = (fb_rect_t){ mouse.x, mouse.y, CURSOR_W, CURSOR_H };

    prev_cursor_x = mouse.x;
    prev_cursor_y = mouse.y;

    fb_present_rects(dirty, ndirty);
}

void gui_update(void) {
    mouse_state_t mouse = mouse_get_state();

    int inside_titlebar =
        mouse.x >= terminal_window.x &&
        mouse.x <  terminal_window.x + terminal_window.width &&
        mouse.y >= terminal_window.y &&
        mouse.y <  terminal_window.y + TITLEBAR_H;

    if (!prev_left && mouse.left && inside_titlebar) {
        terminal_window.dragging      = 1;
        terminal_window.drag_offset_x = mouse.x - terminal_window.x;
        terminal_window.drag_offset_y = mouse.y - terminal_window.y;
    }

    if (!mouse.left)
        terminal_window.dragging = 0;

    if (terminal_window.dragging) {
        terminal_window.x = mouse.x - terminal_window.drag_offset_x;
        terminal_window.y = mouse.y - terminal_window.drag_offset_y;
        gui_dirty = 1;
    }

    prev_left = mouse.left;
}

void gui_init(void)       { gui_dirty = 1; }
void gui_invalidate(void) { gui_dirty = 1; }
#include "gui.h"
#include "../drivers/mouse/mouse.h"
#include "../drivers/timer/timer.h"
#include "../fs/fs.h"
#include "../framebuffer.h"
#include "../memory/pmm.h"
#include "../process/process.h"
#include "../system/clock.h"
#include "../terminal/shell.h"
#include "../terminal/terminal.h"
#include "window.h"

#define TITLEBAR_H     24
#define TASKBAR_H      24
#define TOPBAR_H       34
#define SIDEBAR_W     220
#define PANEL_W       220
#define PANEL_GAP      12
#define CURSOR_W        8
#define CURSOR_H        8
#define BG_COLOR        RGB(20, 20, 20)
#define CONTENT_MARGIN   8
#define BTN_SIZE        16
#define BTN_GAP          6
#define MAX_WINDOWS      8

#define ACCENT_BLUE     RGB(0, 100, 255)
#define ACCENT_PURPLE   RGB(160, 60, 220)
#define PANEL_BG        RGB(20, 15, 30)
#define PANEL_BG2       RGB(15, 15, 20)
#define TEXT_DIM        RGB(120, 120, 130)
#define TEXT_BRIGHT     RGB(210, 210, 220)

static int prev_left    = 0;
static int gui_dirty    = 1;

static int      prev_cursor_x = -1;
static int      prev_cursor_y = -1;
static color_t  cursor_bg[CURSOR_W * CURSOR_H];

static int first_draw = 1;
static int last_clock_total = -1;

static const color_t *bg_pixels = 0;
static int bg_width = 0;
static int bg_height = 0;
static color_t solid_bg_color = BG_COLOR;

static window_t windows[MAX_WINDOWS];
static int window_count = 0;
static window_t *terminal_window = 0;

static const char *nav_items[] = {
    "Terminal",
    "Files",
    "Browser",
    "Settings",
    "Apps",
    "System Monitor",
    "Music",
    "Gallery"
};
#define NAV_ITEM_COUNT ((int)(sizeof(nav_items) / sizeof(nav_items[0])))
#define NAV_ITEM_H 34
#define NAV_START_Y 100

#define STATUS_START_Y 400
#define STATUS_ROW_H 40

#define POWER_ROW_Y 545
#define POWER_BTN_W 56
#define POWER_BTN_H 40

static const char *quick_access_items[] = {
    "Documents", "Downloads", "Pictures",
    "Videos", "Music", "Projects"
};
#define QUICK_ACCESS_COLS 2
#define QUICK_ACCESS_ROWS 3
#define QUICK_TILE_W 96
#define QUICK_TILE_H 60
#define QUICK_TILE_GAP 8

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

static void fb_fill_circle(int cx, int cy, int r, color_t c) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r)
                fb_put_pixel(cx + dx, cy + dy, c);
        }
    }
}

static window_t *window_create(window_kind_t kind, const char *title, int x, int y, int w, int h) {
    if (window_count >= MAX_WINDOWS)
        return 0;

    window_t *win = &windows[window_count++];

    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->dragging = 0;
    win->minimized = 0;
    win->closed = 0;
    win->maximized = 0;
    win->used = 1;
    win->kind = kind;
    win->prev_visible = 0;

    int i = 0;
    while (title[i] && i < 31) {
        win->title[i] = title[i];
        i++;
    }
    win->title[i] = '\0';

    return win;
}

static int window_visible(window_t *w) {
    return w->used && !w->minimized && !w->closed;
}

static void button_rects(window_t *w, fb_rect_t *close_r, fb_rect_t *max_r, fb_rect_t *min_r) {
    int y = w->y + (TITLEBAR_H - BTN_SIZE) / 2;
    int x = w->x + w->width - BTN_SIZE - 8;

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

static void draw_window(window_t *w) {
    fb_fill_rect(w->x, w->y, w->width, w->height, RGB(40, 40, 40));
    fb_fill_rect(w->x, w->y, w->width, TITLEBAR_H, ACCENT_BLUE);
    fb_draw_string(w->x + 8, w->y + 8, w->title, RGB(255, 255, 255), ACCENT_BLUE);

    fb_rect_t close_r, max_r, min_r;
    button_rects(w, &close_r, &max_r, &min_r);

    fb_fill_rect(min_r.x, min_r.y, min_r.w, min_r.h, RGB(230, 200, 60));
    fb_draw_char(min_r.x + 4, min_r.y + 3, '_', RGB(0, 0, 0), RGB(230, 200, 60));

    fb_fill_rect(max_r.x, max_r.y, max_r.w, max_r.h, RGB(60, 180, 80));
    fb_draw_char(max_r.x + 4, max_r.y + 3, w->maximized ? 'o' : '#', RGB(0, 0, 0), RGB(60, 180, 80));

    fb_fill_rect(close_r.x, close_r.y, close_r.w, close_r.h, RGB(220, 60, 60));
    fb_draw_char(close_r.x + 4, close_r.y + 3, 'x', RGB(0, 0, 0), RGB(220, 60, 60));
}

static void format_clock(char *buf, int h, int m, int s) {
    buf[0] = '0' + (h / 10);
    buf[1] = '0' + (h % 10);
    buf[2] = ':';
    buf[3] = '0' + (m / 10);
    buf[4] = '0' + (m % 10);
    buf[5] = ':';
    buf[6] = '0' + (s / 10);
    buf[7] = '0' + (s % 10);
    buf[8] = '\0';
}

static const char *topbar_menu[] = { "Machine", "View", "System", "Tools", "Help" };
#define TOPBAR_MENU_COUNT ((int)(sizeof(topbar_menu) / sizeof(topbar_menu[0])))

static void draw_topbar(void) {
    fb_fill_rect(0, 0, fb_width(), TOPBAR_H, PANEL_BG2);
    fb_fill_rect(0, TOPBAR_H - 2, fb_width(), 2, ACCENT_BLUE);
    fb_draw_string(10, 13, "FuturisticOS", TEXT_BRIGHT, PANEL_BG2);

    int mx = 150;
    for (int i = 0; i < TOPBAR_MENU_COUNT; i++) {
        fb_draw_string(mx, 13, topbar_menu[i], TEXT_DIM, PANEL_BG2);
        int len = 0;
        while (topbar_menu[i][len])
            len++;
        mx += len * 8 + 20;
    }

    int h, m, s;
    clock_get(&h, &m, &s);

    char buf[9];
    format_clock(buf, h, m, s);

    int cw = 8 * 8;
    int cx = fb_width() - cw - 16;

    fb_fill_rect(cx - 8, 4, cw + 16, TOPBAR_H - 8, RGB(30, 20, 45));
    fb_draw_string(cx, 13, buf, RGB(210, 180, 255), RGB(30, 20, 45));

    int tray_x = cx - 8 - 8 - 3 * 32;
    const char *tray_labels[] = { "D", "A", "N" };

    for (int i = 0; i < 3; i++) {
        int bx = tray_x + i * 32;
        fb_fill_rect(bx, 6, 24, TOPBAR_H - 12, PANEL_BG);
        fb_draw_char(bx + 8, 13, tray_labels[i][0], TEXT_DIM, PANEL_BG);
    }
}

static void draw_sidebar_header(void) {
    int cx = 16 + 22;
    int cy = TOPBAR_H + 16 + 22;

    fb_fill_circle(cx, cy, 22, RGB(90, 60, 200));
    fb_draw_char(cx - 4, cy - 4, 'F', RGB(255, 255, 255), RGB(90, 60, 200));

    fb_draw_string(76, TOPBAR_H + 22, "FuturisticOS", TEXT_BRIGHT, PANEL_BG2);
    fb_draw_string(76, TOPBAR_H + 38, "v1.0.0", TEXT_DIM, PANEL_BG2);

    fb_fill_rect(0, TOPBAR_H + 90, SIDEBAR_W, 2, RGB(40, 40, 50));
}

static void draw_sidebar_nav(void) {
    int y0 = TOPBAR_H + NAV_START_Y;

    for (int i = 0; i < NAV_ITEM_COUNT; i++) {
        int iy = y0 + i * NAV_ITEM_H;
        int active = (i == 0);
        color_t bg = active ? ACCENT_BLUE : PANEL_BG2;
        color_t fg = active ? RGB(255, 255, 255) : RGB(140, 140, 150);

        if (active)
            fb_fill_rect(8, iy, SIDEBAR_W - 16, NAV_ITEM_H - 6, bg);

        fb_draw_string(20, iy + 8, nav_items[i], fg, bg);
    }

    fb_fill_rect(0, y0 + NAV_ITEM_COUNT * NAV_ITEM_H + 8, SIDEBAR_W, 2, RGB(40, 40, 50));
}

static void draw_status_bar(int x, int y, int w, int percent, color_t fg) {
    fb_fill_rect(x, y, w, 6, RGB(35, 35, 45));

    int filled = (w * percent) / 100;
    if (filled > w) filled = w;
    if (filled > 0)
        fb_fill_rect(x, y, filled, 6, fg);
}

static void draw_sidebar_status(void) {
    int y0 = TOPBAR_H + STATUS_START_Y;

    fb_draw_string(16, y0, "SYSTEM STATUS", TEXT_DIM, PANEL_BG2);

    uint64_t total_ticks = timer_get_ticks();
    uint64_t busy_ticks = process_get_busy_ticks();
    int cpu_percent = total_ticks ? (int)((busy_ticks * 100) / total_ticks) : 0;

    uint64_t total_pages = pmm_get_total_pages();
    uint64_t free_pages = pmm_get_free_pages();
    uint64_t used_pages = total_pages - free_pages;
    int ram_percent = total_pages ? (int)((used_pages * 100) / total_pages) : 0;

    uint32_t used_sectors, total_sectors;
    fs_disk_stats(&used_sectors, &total_sectors);
    int disk_percent = total_sectors ? (int)(((uint64_t)used_sectors * 100) / total_sectors) : 0;

    const char *labels[3]     = { "CPU Usage", "RAM Usage", "Storage" };
    int         percents[3]   = { cpu_percent, ram_percent, disk_percent };
    color_t     colors[3]     = { ACCENT_BLUE, RGB(60, 200, 140), RGB(230, 160, 60) };

    for (int i = 0; i < 3; i++) {
        int ry = y0 + 20 + i * STATUS_ROW_H;

        fb_draw_string(16, ry, labels[i], TEXT_BRIGHT, PANEL_BG2);
        fb_draw_int(SIDEBAR_W - 48, ry, percents[i], TEXT_DIM, PANEL_BG2);
        fb_draw_char(SIDEBAR_W - 24, ry, '%', TEXT_DIM, PANEL_BG2);

        draw_status_bar(16, ry + 16, SIDEBAR_W - 32, percents[i], colors[i]);
    }

    fb_fill_rect(0, y0 + 20 + 3 * STATUS_ROW_H, SIDEBAR_W, 2, RGB(40, 40, 50));
}

static void power_button_rects(fb_rect_t *restart_r, fb_rect_t *lock_r, fb_rect_t *off_r) {
    int y = TOPBAR_H + POWER_ROW_Y;

    restart_r->x = 16;
    restart_r->y = y;
    restart_r->w = POWER_BTN_W;
    restart_r->h = POWER_BTN_H;

    lock_r->x = 16 + POWER_BTN_W + 14;
    lock_r->y = y;
    lock_r->w = POWER_BTN_W;
    lock_r->h = POWER_BTN_H;

    off_r->x = 16 + (POWER_BTN_W + 14) * 2;
    off_r->y = y;
    off_r->w = POWER_BTN_W;
    off_r->h = POWER_BTN_H;
}

static void draw_sidebar_power(void) {
    fb_rect_t restart_r, lock_r, off_r;
    power_button_rects(&restart_r, &lock_r, &off_r);

    fb_draw_string(16, restart_r.y - 18, "POWER", TEXT_DIM, PANEL_BG2);

    fb_fill_rect(restart_r.x, restart_r.y, restart_r.w, restart_r.h, PANEL_BG);
    fb_draw_char(restart_r.x + restart_r.w / 2 - 4, restart_r.y + restart_r.h / 2 - 4, 'R', TEXT_BRIGHT, PANEL_BG);

    fb_fill_rect(lock_r.x, lock_r.y, lock_r.w, lock_r.h, PANEL_BG);
    fb_draw_char(lock_r.x + lock_r.w / 2 - 4, lock_r.y + lock_r.h / 2 - 4, 'L', TEXT_DIM, PANEL_BG);

    fb_fill_rect(off_r.x, off_r.y, off_r.w, off_r.h, PANEL_BG);
    fb_draw_char(off_r.x + off_r.w / 2 - 4, off_r.y + off_r.h / 2 - 4, 'O', RGB(220, 80, 80), PANEL_BG);
}

static void draw_sidebar(void) {
    int h = fb_height() - TOPBAR_H;

    fb_fill_rect(0, TOPBAR_H, SIDEBAR_W, h, PANEL_BG2);
    fb_fill_rect(SIDEBAR_W, TOPBAR_H, 2, h, ACCENT_BLUE);

    draw_sidebar_header();
    draw_sidebar_nav();
    draw_sidebar_status();
    draw_sidebar_power();
}

static void draw_now_playing(int x, int y, int w, int h) {
    fb_fill_rect(x, y, w, h, PANEL_BG);
    fb_fill_rect(x, y, w, 2, ACCENT_PURPLE);
    fb_draw_string(x + 10, y + 10, "NOW PLAYING", RGB(190, 150, 230), PANEL_BG);
    fb_draw_string(x + 10, y + 40, "Nothing playing", TEXT_DIM, PANEL_BG);
}

static void draw_quick_access(int x, int y, int w, int h) {
    fb_fill_rect(x, y, w, h, PANEL_BG);
    fb_fill_rect(x, y, w, 2, ACCENT_PURPLE);
    fb_draw_string(x + 10, y + 10, "QUICK ACCESS", RGB(190, 150, 230), PANEL_BG);

    int gx0 = x + 10;
    int gy0 = y + 30;

    for (int i = 0; i < QUICK_ACCESS_ROWS * QUICK_ACCESS_COLS; i++) {
        int col = i % QUICK_ACCESS_COLS;
        int row = i / QUICK_ACCESS_COLS;
        int tx = gx0 + col * (QUICK_TILE_W + QUICK_TILE_GAP);
        int ty = gy0 + row * (QUICK_TILE_H + QUICK_TILE_GAP);

        fb_fill_rect(tx, ty, QUICK_TILE_W, QUICK_TILE_H, PANEL_BG2);
        fb_draw_string(tx + 8, ty + QUICK_TILE_H - 18, quick_access_items[i], TEXT_DIM, PANEL_BG2);
    }
}

static void draw_notifications(int x, int y, int w, int h) {
    fb_fill_rect(x, y, w, h, PANEL_BG);
    fb_fill_rect(x, y, w, 2, ACCENT_PURPLE);
    fb_draw_string(x + 10, y + 10, "NOTIFICATIONS", RGB(190, 150, 230), PANEL_BG);

    fb_fill_circle(x + 16, y + 42, 4, RGB(60, 200, 140));
    fb_draw_string(x + 28, y + 38, "System ready", TEXT_BRIGHT, PANEL_BG);
}

static int panel_layout(int *now_h, int *quick_h, int *notif_h) {
    *now_h = 90;
    *quick_h = 30 + QUICK_ACCESS_ROWS * QUICK_TILE_H + (QUICK_ACCESS_ROWS - 1) * QUICK_TILE_GAP + 10;
    *notif_h = 70;
    return *now_h + *quick_h + *notif_h + PANEL_GAP * 3;
}

static void draw_side_panels(void) {
    int x = fb_width() - PANEL_W - PANEL_GAP;
    int y = TOPBAR_H + PANEL_GAP;

    int now_h, quick_h, notif_h;
    panel_layout(&now_h, &quick_h, &notif_h);

    draw_now_playing(x, y, PANEL_W, now_h);
    y += now_h + PANEL_GAP;

    draw_quick_access(x, y, PANEL_W, quick_h);
    y += quick_h + PANEL_GAP;

    draw_notifications(x, y, PANEL_W, notif_h);
}

static void draw_taskbar(void) {
    int y = fb_height() - TASKBAR_H;

    fb_fill_rect(0, y, fb_width(), TASKBAR_H, PANEL_BG2);
    fb_fill_rect(0, y, fb_width(), 2, ACCENT_BLUE);

    int tx = fb_width() - 10;

    for (int i = 0; i < window_count; i++) {
        window_t *w = &windows[i];

        if (!w->used || window_visible(w))
            continue;

        int len = 0;
        while (w->title[len])
            len++;

        tx -= len * 8;
        fb_draw_string(tx, y + 8, w->title, TEXT_DIM, PANEL_BG2);
        tx -= 20;
    }
}

static void terminal_content_rect(window_t *w, int *x, int *y, int *cw, int *ch) {
    *x = w->x + CONTENT_MARGIN;
    *y = w->y + TITLEBAR_H + CONTENT_MARGIN;
    *cw = w->width - CONTENT_MARGIN * 2;
    *ch = w->height - TITLEBAR_H - CONTENT_MARGIN * 2;
}

static void toggle_maximize(window_t *w) {
    if (w->maximized) {
        w->x = w->restore_x;
        w->y = w->restore_y;
        w->width = w->restore_w;
        w->height = w->restore_h;
        w->maximized = 0;
    } else {
        w->restore_x = w->x;
        w->restore_y = w->y;
        w->restore_w = w->width;
        w->restore_h = w->height;

        w->x = SIDEBAR_W;
        w->y = TOPBAR_H;
        w->width = fb_width() - SIDEBAR_W - PANEL_W - PANEL_GAP * 2;
        w->height = fb_height() - TOPBAR_H - TASKBAR_H;
        w->maximized = 1;
    }
}

static int sidebar_nav_hit_test(int mx, int my) {
    int y0 = TOPBAR_H + NAV_START_Y;

    if (mx < 8 || mx >= SIDEBAR_W - 8)
        return -1;

    for (int i = 0; i < NAV_ITEM_COUNT; i++) {
        int iy = y0 + i * NAV_ITEM_H;

        if (my >= iy && my < iy + NAV_ITEM_H - 6)
            return i;
    }

    return -1;
}

void gui_restore_terminal(void) {
    if (!terminal_window)
        return;

    terminal_window->minimized = 0;
    terminal_window->closed = 0;
    gui_dirty = 1;
}

void gui_draw(void) {
    mouse_state_t mouse = mouse_get_state();

    fb_rect_t dirty[MAX_WINDOWS * 2 + 8];
    int       ndirty = 0;

    if (gui_dirty) {
        gui_dirty = 0;

        if (first_draw) {
            first_draw = 0;
            draw_background_full();
            draw_topbar();
            draw_sidebar();
            draw_side_panels();
            draw_taskbar();

            for (int i = 0; i < window_count; i++) {
                window_t *w = &windows[i];
                int visible = window_visible(w);

                if (visible) {
                    draw_window(w);

                    if (w->kind == WINDOW_KIND_TERMINAL) {
                        int cx, cy, cw, ch;
                        terminal_content_rect(w, &cx, &cy, &cw, &ch);
                        terminal_set_viewport(cx, cy, cw, ch);
                    }
                }

                w->prev_x = w->x;
                w->prev_y = w->y;
                w->prev_w = w->width;
                w->prev_h = w->height;
                w->prev_visible = visible;
            }

            dirty[ndirty++] = (fb_rect_t){ 0, 0, fb_width(), fb_height() };
        } else {
            for (int i = 0; i < window_count; i++) {
                window_t *w = &windows[i];

                if (w->prev_visible) {
                    draw_background_rect(w->prev_x, w->prev_y, w->prev_w, w->prev_h);
                    dirty[ndirty++] = (fb_rect_t){ w->prev_x, w->prev_y, w->prev_w, w->prev_h };
                }
            }

            draw_topbar();
            dirty[ndirty++] = (fb_rect_t){ 0, 0, fb_width(), TOPBAR_H };

            draw_sidebar();
            dirty[ndirty++] = (fb_rect_t){ 0, TOPBAR_H, SIDEBAR_W + 2, fb_height() - TOPBAR_H };

            draw_side_panels();

            int now_h, quick_h, notif_h;
            int total_h = panel_layout(&now_h, &quick_h, &notif_h);
            dirty[ndirty++] = (fb_rect_t){ fb_width() - PANEL_W - PANEL_GAP, TOPBAR_H, PANEL_W + PANEL_GAP, total_h + PANEL_GAP };

            draw_taskbar();
            dirty[ndirty++] = (fb_rect_t){ 0, fb_height() - TASKBAR_H, fb_width(), TASKBAR_H };

            for (int i = 0; i < window_count; i++) {
                window_t *w = &windows[i];
                int visible = window_visible(w);

                if (visible) {
                    draw_window(w);
                    dirty[ndirty++] = (fb_rect_t){ w->x, w->y, w->width, w->height };

                    if (w->kind == WINDOW_KIND_TERMINAL) {
                        int cx, cy, cw, ch;
                        terminal_content_rect(w, &cx, &cy, &cw, &ch);

                        if (!w->prev_visible || w->prev_w != w->width || w->prev_h != w->height)
                            terminal_set_viewport(cx, cy, cw, ch);
                        else
                            terminal_move_viewport(cx, cy);
                    }
                }

                w->prev_x = w->x;
                w->prev_y = w->y;
                w->prev_w = w->width;
                w->prev_h = w->height;
                w->prev_visible = visible;
            }
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

    int ch, cm, cs;
    clock_get(&ch, &cm, &cs);
    int total = ch * 3600 + cm * 60 + cs;

    if (total != last_clock_total) {
        last_clock_total = total;
        gui_dirty = 1;
    }

    if (clicked) {
        int hit = sidebar_nav_hit_test(mouse.x, mouse.y);

        if (hit == 0)
            gui_restore_terminal();

        fb_rect_t restart_r, lock_r, off_r;
        power_button_rects(&restart_r, &lock_r, &off_r);

        if (point_in_rect(mouse.x, mouse.y, restart_r))
            reboot_system();
        else if (point_in_rect(mouse.x, mouse.y, off_r))
            shutdown_system();
    }

    for (int i = window_count - 1; i >= 0; i--) {
        window_t *w = &windows[i];

        if (!window_visible(w))
            continue;

        fb_rect_t close_r, max_r, min_r;
        button_rects(w, &close_r, &max_r, &min_r);

        if (clicked && point_in_rect(mouse.x, mouse.y, close_r)) {
            w->closed = 1;
            w->dragging = 0;
            gui_dirty = 1;
        } else if (clicked && point_in_rect(mouse.x, mouse.y, max_r)) {
            toggle_maximize(w);
            w->dragging = 0;
            gui_dirty = 1;
        } else if (clicked && point_in_rect(mouse.x, mouse.y, min_r)) {
            w->minimized = 1;
            w->dragging = 0;
            gui_dirty = 1;
        } else {
            int inside_titlebar =
                mouse.x >= w->x &&
                mouse.x <  w->x + w->width &&
                mouse.y >= w->y &&
                mouse.y <  w->y + TITLEBAR_H;

            if (clicked && inside_titlebar && !w->maximized) {
                w->dragging      = 1;
                w->drag_offset_x = mouse.x - w->x;
                w->drag_offset_y = mouse.y - w->y;
            }
        }

        if (!mouse.left)
            w->dragging = 0;

        if (w->dragging) {
            int new_x = mouse.x - w->drag_offset_x;
            int new_y = mouse.y - w->drag_offset_y;

            if (new_x != w->x || new_y != w->y) {
                w->x = new_x;
                w->y = new_y;
                gui_dirty = 1;
            }
        }

        if (w->kind == WINDOW_KIND_TERMINAL) {
            int scroll = mouse_get_scroll();

            if (scroll && terminal_contains(mouse.x, mouse.y))
                terminal_scroll(scroll);
        }

        break;
    }

    prev_left = mouse.left;
}

void gui_init(void) {
    clock_init();
    terminal_window = window_create(WINDOW_KIND_TERMINAL, "Terminal",
                                     SIDEBAR_W + 40, TOPBAR_H + 20, 500, 300);
    gui_dirty = 1;
}

void gui_invalidate(void) { gui_dirty = 1; }
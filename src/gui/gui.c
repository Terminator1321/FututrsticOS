#include "gui.h"
#include "../drivers/mouse/mouse.h"
#include "../drivers/timer/timer.h"
#include "../fs/fs.h"
#include "../framebuffer.h"
#include "../memory/pmm.h"
#include "../process/process.h"
#include "../system/clock.h"
#include "../system/system.h"
#include "../terminal/shell.h"
#include "../terminal/terminal.h"
#include "ui_layout.h"
#include "window.h"

#define CURSOR_W 8
#define CURSOR_H 8
#define MAX_WINDOWS 8
#define MAX_NOTIFICATIONS 4

static int prev_left = 0;
static int gui_dirty = 1;

static int prev_cursor_x = -1;
static int prev_cursor_y = -1;
static color_t cursor_bg[CURSOR_W * CURSOR_H];

static int first_draw = 1;
static int last_clock_total = -1;

static const color_t *bg_pixels = 0;
static int bg_width = 0;
static int bg_height = 0;
static color_t solid_bg_color = UI_BG_VOID;

static int bg_scale_pm = 1000;
static int bg_off_x = 0;
static int bg_off_y = 0;

static window_t windows[MAX_WINDOWS];
static int window_count = 0;
static window_t *terminal_window = 0;

typedef struct {
    char text[40];
} notification_t;

static notification_t notifications[MAX_NOTIFICATIONS];
static int notification_count = 0;

static int dock_hover = -1;
static int quick_hover = -1;

static const char *nav_items[] = {
    "Terminal", "Files", "Browser", "Settings",
    "Apps", "System Monitor", "Music", "Gallery"
};
#define NAV_ITEM_COUNT ((int)(sizeof(nav_items) / sizeof(nav_items[0])))

static const char *quick_access_items[] = {
    "Documents", "Downloads", "Pictures", "Videos", "Music", "Projects"
};
#define QUICK_COLS 2
#define QUICK_ROWS 3

static const char *dock_items[] = {
    "Terminal", "Files", "Browser", "Settings", "Apps", "Gallery"
};
#define DOCK_ITEM_COUNT ((int)(sizeof(dock_items) / sizeof(dock_items[0])))

typedef struct {
    fb_rect_t sidebar;
    fb_rect_t clock;
    fb_rect_t music;
    fb_rect_t quick;
    fb_rect_t notif;
    fb_rect_t dock;
    fb_rect_t wsw;
} desktop_layout_t;

static desktop_layout_t g_layout;

static void recompute_layout(void) {
    int fw = fb_width();
    int fh = fb_height();
    int gap = ui_scaled(14);

    g_layout.sidebar.x = (fw * 10) / 1000;
    g_layout.sidebar.y = (fh * 40) / 1000;
    g_layout.sidebar.w = (fw * 150) / 1000;
    g_layout.sidebar.h = fh - g_layout.sidebar.y - (fh * 50) / 1000;

    g_layout.clock = ui_pm_right(30, 50, 210, 105);

    g_layout.music = g_layout.clock;
    g_layout.music.w = (fw * 230) / 1000;
    g_layout.music.h = (fh * 165) / 1000;
    g_layout.music.x = fw - ((fw * 30) / 1000) - g_layout.music.w;
    g_layout.music.y = g_layout.clock.y + g_layout.clock.h + gap;

    g_layout.quick = g_layout.music;
    g_layout.quick.h = (fh * 220) / 1000;
    g_layout.quick.y = g_layout.music.y + g_layout.music.h + gap;

    g_layout.notif = g_layout.quick;
    g_layout.notif.h = (fh * 150) / 1000;
    g_layout.notif.y = g_layout.quick.y + g_layout.quick.h + gap;

    int notif_bottom_limit = fh - (fh * 30) / 1000;
    if (g_layout.notif.y + g_layout.notif.h > notif_bottom_limit)
        g_layout.notif.y = notif_bottom_limit - g_layout.notif.h;

    g_layout.dock = ui_pm_bottom_center(28, 400, 68);
    g_layout.wsw = ui_pm_bottom(15, 28, 180, 46);
}

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

static void recompute_wallpaper_fit(void) {
    if (!bg_pixels || bg_width <= 0 || bg_height <= 0)
        return;

    int fw = fb_width();
    int fh = fb_height();

    int sx = (fw * 1000) / bg_width;
    int sy = (fh * 1000) / bg_height;
    bg_scale_pm = sx > sy ? sx : sy;

    if (bg_scale_pm < 1)
        bg_scale_pm = 1000;

    int scaled_w = (bg_width * bg_scale_pm) / 1000;
    int scaled_h = (bg_height * bg_scale_pm) / 1000;

    bg_off_x = (fw - scaled_w) / 2;
    bg_off_y = (fh - scaled_h) / 2;
}

static void draw_background_rect(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0)
        return;

    if (!bg_pixels || bg_width <= 0 || bg_height <= 0) {
        fb_fill_rect(x, y, w, h, solid_bg_color);
        return;
    }

    for (int dy = 0; dy < h; dy++) {
        int sy = ((y + dy - bg_off_y) * 1000) / bg_scale_pm;
        if (sy < 0) sy = 0;
        if (sy >= bg_height) sy = bg_height - 1;

        const color_t *row = bg_pixels + (uint32_t)sy * (uint32_t)bg_width;

        for (int dx = 0; dx < w; dx++) {
            int sx = ((x + dx - bg_off_x) * 1000) / bg_scale_pm;
            if (sx < 0) sx = 0;
            if (sx >= bg_width) sx = bg_width - 1;

            fb_put_pixel(x + dx, y + dy, row[sx]);
        }
    }
}

static void draw_background_full(void) {
    recompute_wallpaper_fit();
    draw_background_rect(0, 0, fb_width(), fb_height());
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
    win->fading = 1;
    win->fade_start_tick = timer_get_ticks();

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

static int titlebar_height(void) {
    return ui_scaled(30);
}

static int window_btn_size(void) {
    return ui_scaled(11);
}

static void button_rects(window_t *w, fb_rect_t *close_r, fb_rect_t *max_r, fb_rect_t *min_r) {
    int th = titlebar_height();
    int bs = window_btn_size();
    int gap = ui_scaled(9);

    int y = w->y + th / 2 - bs / 2;
    int x = w->x + w->width - bs - ui_scaled(14);

    close_r->x = x;
    close_r->y = y;
    close_r->w = bs;
    close_r->h = bs;

    x -= bs + gap;
    max_r->x = x;
    max_r->y = y;
    max_r->w = bs;
    max_r->h = bs;

    x -= bs + gap;
    min_r->x = x;
    min_r->y = y;
    min_r->w = bs;
    min_r->h = bs;
}

static int point_in_rect(int px, int py, fb_rect_t r) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

static int window_opacity_pm(window_t *w) {
    if (!w->fading)
        return 1000;

    if (system_timer_hz == 0) {
        w->fading = 0;
        return 1000;
    }

    uint64_t now = timer_get_ticks();
    uint64_t elapsed = now - w->fade_start_tick;
    uint64_t duration = system_timer_hz / 4;

    if (duration == 0)
        duration = 1;

    if (elapsed >= duration) {
        w->fading = 0;
        return 1000;
    }

    return (int)((elapsed * 1000) / duration);
}

static void draw_window(window_t *w) {
    int th = titlebar_height();
    int radius = ui_scaled(14);
    int opacity = window_opacity_pm(w);

    int glass_alpha = (780 * opacity) / 1000;
    ui_fill_rounded_glass(w->x, w->y, w->width, w->height, radius, UI_GLASS_TINT, glass_alpha);
    ui_fill_glass(w->x + radius / 2, w->y, w->width - radius, ui_scaled(2), UI_NEON_PURPLE, opacity);
    ui_draw_glow_border_alpha(w->x, w->y, w->width, w->height, radius, UI_NEON_PURPLE, opacity);

    if (opacity < 700)
        return;

    int fs = ui_font_scale();
    fb_draw_string_ex(w->x + ui_scaled(16), w->y + th / 2 - 4 * fs, w->title, UI_TEXT_WHITE, UI_BG_VOID, fs, 1);

    fb_rect_t close_r, max_r, min_r;
    button_rects(w, &close_r, &max_r, &min_r);

    int br = min_r.w / 2;
    ui_fill_circle(min_r.x + br, min_r.y + br, br, UI_WARN_AMBER);

    br = max_r.w / 2;
    ui_fill_circle(max_r.x + br, max_r.y + br, br, UI_OK_GREEN);

    br = close_r.w / 2;
    ui_fill_circle(close_r.x + br, close_r.y + br, br, UI_DANGER_RED);
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

static void draw_sidebar_header(fb_rect_t s) {
    int pad = ui_scaled(16);
    int r = ui_scaled(22);
    int cx = s.x + pad + r;
    int cy = s.y + pad + r;

    ui_fill_circle(cx, cy, r, UI_NEON_PURPLE);

    int fs = ui_font_scale();
    fb_draw_char_ex(cx - 4 * fs, cy - 4 * fs, 'Q', UI_TEXT_WHITE, UI_BG_VOID, fs, 1);

    int tx = cx + r + ui_scaled(10);
    fb_draw_string_ex(tx, s.y + pad, "QEMO OS", UI_TEXT_WHITE, UI_BG_VOID, fs, 1);
    fb_draw_string(tx, s.y + pad + 8 * fs + ui_scaled(4), "v1.0.0", UI_TEXT_DIM, UI_BG_VOID);

    fb_fill_rect(s.x + pad, cy + r + ui_scaled(14), s.w - pad * 2, ui_scaled(1), UI_TEXT_DIM);
}

static int nav_start_y(fb_rect_t s) {
    return s.y + ui_scaled(90);
}

static int nav_item_h(void) {
    return ui_scaled(36);
}

static void draw_sidebar_nav(fb_rect_t s) {
    int y0 = nav_start_y(s);
    int ih = nav_item_h();
    int pad = ui_scaled(8);

    for (int i = 0; i < NAV_ITEM_COUNT; i++) {
        int iy = y0 + i * ih;
        int active = (i == 0);

        if (active)
            ui_fill_rounded_glass(s.x + pad, iy, s.w - pad * 2, ih - ui_scaled(6), ui_scaled(10), UI_NEON_PURPLE, 550);

        color_t fg = active ? UI_TEXT_WHITE : UI_TEXT_LAVENDER;
        fb_draw_string(s.x + pad + ui_scaled(12), iy + ih / 2 - 6, nav_items[i], fg, UI_BG_VOID);
    }

    int sep_y = y0 + NAV_ITEM_COUNT * ih + ui_scaled(6);
    fb_fill_rect(s.x + pad, sep_y, s.w - pad * 2, ui_scaled(1), UI_TEXT_DIM);
}

static int status_start_y(fb_rect_t s) {
    return nav_start_y(s) + NAV_ITEM_COUNT * nav_item_h() + ui_scaled(24);
}

static int status_row_h(void) {
    return ui_scaled(42);
}

static void draw_sidebar_status(fb_rect_t s) {
    int y0 = status_start_y(s);
    int pad = ui_scaled(16);

    fb_draw_string(s.x + pad, y0, "SYSTEM STATUS", UI_TEXT_DIM, UI_BG_VOID);

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

    const char *labels[3] = { "CPU Usage", "RAM Usage", "Storage" };
    int percents[3] = { cpu_percent, ram_percent, disk_percent };
    color_t colors[3] = { UI_ELECTRIC_BLUE, UI_OK_GREEN, UI_NEON_MAGENTA };

    int rh = status_row_h();

    for (int i = 0; i < 3; i++) {
        int ry = y0 + ui_scaled(22) + i * rh;

        fb_draw_string(s.x + pad, ry, labels[i], UI_TEXT_LAVENDER, UI_BG_VOID);
        fb_draw_int(s.x + s.w - pad - ui_scaled(32), ry, percents[i], UI_TEXT_DIM, UI_BG_VOID);
        fb_draw_char(s.x + s.w - pad - ui_scaled(10), ry, '%', UI_TEXT_DIM, UI_BG_VOID);

        ui_draw_progress(s.x + pad, ry + ui_scaled(16), s.w - pad * 2, ui_scaled(6), percents[i], colors[i], RGB(35, 30, 50));
    }
}

static void power_button_rects(fb_rect_t s, fb_rect_t *restart_r, fb_rect_t *lock_r, fb_rect_t *off_r) {
    int pad = ui_scaled(16);
    int bw = (s.w - pad * 2 - ui_scaled(24)) / 3;
    int bh = ui_scaled(36);
    int y = s.y + s.h - bh - ui_scaled(14);

    restart_r->x = s.x + pad;
    restart_r->y = y;
    restart_r->w = bw;
    restart_r->h = bh;

    lock_r->x = restart_r->x + bw + ui_scaled(12);
    lock_r->y = y;
    lock_r->w = bw;
    lock_r->h = bh;

    off_r->x = lock_r->x + bw + ui_scaled(12);
    off_r->y = y;
    off_r->w = bw;
    off_r->h = bh;
}

static void draw_sidebar_power(fb_rect_t s) {
    fb_rect_t restart_r, lock_r, off_r;
    power_button_rects(s, &restart_r, &lock_r, &off_r);

    fb_draw_string(restart_r.x, restart_r.y - ui_scaled(18), "POWER", UI_TEXT_DIM, UI_BG_VOID);

    int radius = ui_scaled(8);

    ui_fill_rounded_glass(restart_r.x, restart_r.y, restart_r.w, restart_r.h, radius, UI_GLASS_TINT, 700);
    fb_draw_char(restart_r.x + restart_r.w / 2 - 4, restart_r.y + restart_r.h / 2 - 4, 'R', UI_TEXT_LAVENDER, UI_BG_VOID);

    ui_fill_rounded_glass(lock_r.x, lock_r.y, lock_r.w, lock_r.h, radius, UI_GLASS_TINT, 700);
    fb_draw_char(lock_r.x + lock_r.w / 2 - 4, lock_r.y + lock_r.h / 2 - 4, 'L', UI_TEXT_LAVENDER, UI_BG_VOID);

    ui_fill_rounded_glass(off_r.x, off_r.y, off_r.w, off_r.h, radius, UI_GLASS_TINT, 700);
    fb_draw_char(off_r.x + off_r.w / 2 - 4, off_r.y + off_r.h / 2 - 4, 'O', UI_DANGER_RED, UI_BG_VOID);
}

static void draw_sidebar(void) {
    fb_rect_t s = g_layout.sidebar;
    int radius = ui_scaled(18);

    ui_fill_rounded_glass(s.x, s.y, s.w, s.h, radius, UI_GLASS_TINT, 720);
    ui_draw_glow_border(s.x, s.y, s.w, s.h, radius, UI_NEON_PURPLE);

    draw_sidebar_header(s);
    draw_sidebar_nav(s);
    draw_sidebar_status(s);
    draw_sidebar_power(s);
}

static void draw_panel_frame(fb_rect_t r, const char *title) {
    int radius = ui_scaled(16);

    ui_fill_rounded_glass(r.x, r.y, r.w, r.h, radius, UI_GLASS_TINT, 740);
    ui_draw_top_accent(r.x + radius / 2, r.y, r.w - radius, UI_NEON_MAGENTA, ui_scaled(2));
    ui_draw_glow_border(r.x, r.y, r.w, r.h, radius, UI_NEON_MAGENTA);

    fb_draw_string(r.x + ui_scaled(14), r.y + ui_scaled(14), title, UI_TEXT_LAVENDER, UI_BG_VOID);
}

static void draw_clock_widget(void) {
    fb_rect_t r = g_layout.clock;
    int fs = ui_font_scale() + 1;

    int h, m, s;
    clock_get(&h, &m, &s);

    char hm[6];
    hm[0] = '0' + (h / 10);
    hm[1] = '0' + (h % 10);
    hm[2] = ':';
    hm[3] = '0' + (m / 10);
    hm[4] = '0' + (m % 10);
    hm[5] = '\0';

    char sec[3];
    sec[0] = '0' + (s / 10);
    sec[1] = '0' + (s % 10);
    sec[2] = '\0';

    int radius = ui_scaled(18);
    ui_fill_rounded_glass(r.x, r.y, r.w, r.h, radius, UI_GLASS_TINT, 740);
    ui_draw_glow_border(r.x, r.y, r.w, r.h, radius, UI_ELECTRIC_BLUE);

    int pad = ui_scaled(16);
    fb_draw_string_ex(r.x + pad, r.y + pad, hm, UI_TEXT_WHITE, UI_BG_VOID, fs, 1);

    int sx = r.x + pad + fb_text_width(hm, fs) + ui_scaled(6);
    fb_draw_string_ex(sx, r.y + pad, sec, UI_TEXT_DIM, UI_BG_VOID, ui_font_scale(), 1);

    char datebuf[16];
    format_clock(datebuf, h, m, s);
    fb_draw_string(r.x + pad, r.y + r.h - ui_scaled(22), "SYSTEM TIME", UI_TEXT_DIM, UI_BG_VOID);
}

static void draw_music_widget(void) {
    fb_rect_t r = g_layout.music;
    draw_panel_frame(r, "NOW PLAYING");

    int pad = ui_scaled(14);
    int art = ui_scaled(48);
    int ay = r.y + ui_scaled(38);

    ui_fill_rounded_glass(r.x + pad, ay, art, art, ui_scaled(8), RGB(40, 20, 60), 900);
    fb_draw_string(r.x + pad + art + ui_scaled(10), ay + ui_scaled(6), "No media", UI_TEXT_LAVENDER, UI_BG_VOID);
    fb_draw_string(r.x + pad + art + ui_scaled(10), ay + ui_scaled(20), "playing", UI_TEXT_DIM, UI_BG_VOID);

    int by = ay + art + ui_scaled(14);
    ui_draw_progress(r.x + pad, by, r.w - pad * 2, ui_scaled(4), 0, UI_NEON_PURPLE, RGB(35, 30, 50));

    fb_draw_string(r.x + pad, by + ui_scaled(10), "--:--", UI_TEXT_DIM, UI_BG_VOID);
    fb_draw_string(r.x + r.w - pad - ui_scaled(40), by + ui_scaled(10), "--:--", UI_TEXT_DIM, UI_BG_VOID);

    int cy = by + ui_scaled(30);
    int cx = r.x + r.w / 2;
    int cr = ui_scaled(12);

    ui_fill_circle(cx - ui_scaled(36), cy, ui_scaled(8), UI_TEXT_DIM);
    ui_fill_circle(cx, cy, cr, UI_NEON_PURPLE);
    fb_draw_char(cx - 3, cy - 4, '>', UI_TEXT_WHITE, UI_NEON_PURPLE);
    ui_fill_circle(cx + ui_scaled(36), cy, ui_scaled(8), UI_TEXT_DIM);
}

static void quick_tile_rect(fb_rect_t r, int index, fb_rect_t *out) {
    int pad = ui_scaled(14);
    int gap = ui_scaled(8);
    int gx0 = r.x + pad;
    int gy0 = r.y + ui_scaled(38);

    int tile_w = (r.w - pad * 2 - gap * (QUICK_COLS - 1)) / QUICK_COLS;
    int tile_h = (r.h - ui_scaled(38) - pad - gap * (QUICK_ROWS - 1)) / QUICK_ROWS;

    int col = index % QUICK_COLS;
    int row = index / QUICK_COLS;

    out->x = gx0 + col * (tile_w + gap);
    out->y = gy0 + row * (tile_h + gap);
    out->w = tile_w;
    out->h = tile_h;
}

static void draw_quick_access(void) {
    fb_rect_t r = g_layout.quick;
    draw_panel_frame(r, "QUICK ACCESS");

    for (int i = 0; i < QUICK_ROWS * QUICK_COLS; i++) {
        fb_rect_t tile;
        quick_tile_rect(r, i, &tile);

        int hovered = (i == quick_hover);
        color_t fill = hovered ? RGB(55, 30, 85) : RGB(30, 20, 45);
        int fill_alpha = hovered ? 880 : 780;
        color_t border = hovered ? UI_NEON_MAGENTA : ui_blend(UI_BG_VOID, UI_NEON_PURPLE, 500);

        ui_fill_rounded_glass(tile.x, tile.y, tile.w, tile.h, ui_scaled(10), fill, fill_alpha);

        if (hovered)
            ui_draw_glow_border(tile.x, tile.y, tile.w, tile.h, ui_scaled(10), border);
        else
            ui_draw_rounded_border(tile.x, tile.y, tile.w, tile.h, ui_scaled(10), border);

        color_t label = hovered ? UI_TEXT_WHITE : UI_TEXT_LAVENDER;
        fb_draw_string(tile.x + ui_scaled(8), tile.y + tile.h - ui_scaled(18), quick_access_items[i], label, UI_BG_VOID);
    }
}

static void draw_notifications(void) {
    fb_rect_t r = g_layout.notif;
    draw_panel_frame(r, "NOTIFICATIONS");

    int pad = ui_scaled(14);
    int y = r.y + ui_scaled(38);

    if (notification_count == 0) {
        fb_draw_string(r.x + pad, y, "No new notifications", UI_TEXT_DIM, UI_BG_VOID);
        return;
    }

    for (int i = 0; i < notification_count && i < MAX_NOTIFICATIONS; i++) {
        ui_fill_circle(r.x + pad + ui_scaled(4), y + ui_scaled(6), ui_scaled(4), UI_OK_GREEN);
        fb_draw_string(r.x + pad + ui_scaled(16), y, notifications[i].text, UI_TEXT_WHITE, UI_BG_VOID);
        y += ui_scaled(22);
    }
}

void gui_notify(const char *text) {
    if (notification_count < MAX_NOTIFICATIONS) {
        int i = 0;
        while (text[i] && i < 39) {
            notifications[notification_count].text[i] = text[i];
            i++;
        }
        notifications[notification_count].text[i] = '\0';
        notification_count++;
    }
    gui_dirty = 1;
}

static void dock_item_rect(fb_rect_t dock, int index, fb_rect_t *out) {
    int pad = ui_scaled(10);
    int gap = ui_scaled(6);
    int size = dock.h - pad * 2;
    int total_w = DOCK_ITEM_COUNT * size + (DOCK_ITEM_COUNT - 1) * gap;
    int x0 = dock.x + (dock.w - total_w) / 2;

    out->x = x0 + index * (size + gap);
    out->y = dock.y + pad;
    out->w = size;
    out->h = size;
}

static void draw_dock(void) {
    fb_rect_t d = g_layout.dock;
    int radius = d.h / 2;

    ui_fill_rounded_glass(d.x, d.y, d.w, d.h, radius, UI_GLASS_TINT, 780);
    ui_draw_glow_border(d.x, d.y, d.w, d.h, radius, UI_NEON_MAGENTA);

    for (int i = 0; i < DOCK_ITEM_COUNT; i++) {
        fb_rect_t item;
        dock_item_rect(d, i, &item);

        int hovered = (i == dock_hover);

        if (hovered) {
            int grow = ui_scaled(3);
            item.x -= grow;
            item.y -= grow;
            item.w += grow * 2;
            item.h += grow * 2;
        }

        color_t fill = hovered ? RGB(70, 35, 105) : RGB(35, 20, 55);
        int fill_alpha = hovered ? 900 : 800;

        ui_fill_rounded_glass(item.x, item.y, item.w, item.h, ui_scaled(10), fill, fill_alpha);

        if (hovered)
            ui_draw_glow_border(item.x, item.y, item.w, item.h, ui_scaled(10), UI_NEON_MAGENTA);

        color_t glyph = hovered ? UI_TEXT_WHITE : UI_TEXT_LAVENDER;
        fb_draw_char(item.x + item.w / 2 - 4, item.y + item.h / 2 - 4, dock_items[i][0], glyph, UI_BG_VOID);
    }
}

static void draw_workspace_switcher(void) {
    fb_rect_t w = g_layout.wsw;
    int radius = w.h / 2;

    ui_fill_rounded_glass(w.x, w.y, w.w, w.h, radius, UI_GLASS_TINT, 780);
    ui_draw_glow_border(w.x, w.y, w.w, w.h, radius, UI_NEON_PURPLE);

    int count = 5;
    int pad = ui_scaled(6);
    int gap = ui_scaled(4);
    int size = w.h - pad * 2;
    int x = w.x + pad;

    for (int i = 0; i < count; i++) {
        int active = (i == 0);
        color_t fill = active ? UI_NEON_PURPLE : RGB(30, 25, 45);

        ui_fill_rounded_glass(x, w.y + pad, size, size, ui_scaled(6), fill, active ? 950 : 700);

        if (i < 4)
            fb_draw_char(x + size / 2 - 4, w.y + pad + size / 2 - 4, '1' + i, UI_TEXT_WHITE, UI_BG_VOID);
        else
            fb_draw_char(x + size / 2 - 4, w.y + pad + size / 2 - 4, '+', UI_TEXT_WHITE, UI_BG_VOID);

        x += size + gap;
    }
}

static void terminal_content_rect(window_t *w, int *x, int *y, int *cw, int *ch) {
    int margin = ui_scaled(10);
    int th = titlebar_height();

    *x = w->x + margin;
    *y = w->y + th + margin;
    *cw = w->width - margin * 2;
    *ch = w->height - th - margin * 2;
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

        int gap = ui_scaled(14);

        w->x = g_layout.sidebar.x + g_layout.sidebar.w + gap;
        w->y = gap;
        w->width = fb_width() - w->x - ui_scaled(230) - gap;
        w->height = g_layout.dock.y - gap * 2;
        w->maximized = 1;
    }
}

static int sidebar_nav_hit_test(int mx, int my) {
    fb_rect_t s = g_layout.sidebar;

    if (mx < s.x || mx >= s.x + s.w)
        return -1;

    int y0 = nav_start_y(s);
    int ih = nav_item_h();

    for (int i = 0; i < NAV_ITEM_COUNT; i++) {
        int iy = y0 + i * ih;

        if (my >= iy && my < iy + ih - ui_scaled(6))
            return i;
    }

    return -1;
}

static int dock_hit_test(int mx, int my) {
    fb_rect_t d = g_layout.dock;

    if (!point_in_rect(mx, my, d))
        return -1;

    for (int i = 0; i < DOCK_ITEM_COUNT; i++) {
        fb_rect_t item;
        dock_item_rect(d, i, &item);

        if (point_in_rect(mx, my, item))
            return i;
    }

    return -1;
}

static int quick_hit_test(int mx, int my) {
    fb_rect_t r = g_layout.quick;

    if (!point_in_rect(mx, my, r))
        return -1;

    for (int i = 0; i < QUICK_ROWS * QUICK_COLS; i++) {
        fb_rect_t tile;
        quick_tile_rect(r, i, &tile);

        if (point_in_rect(mx, my, tile))
            return i;
    }

    return -1;
}

void gui_restore_terminal(void) {
    if (!terminal_window)
        return;

    terminal_window->minimized = 0;
    terminal_window->closed = 0;
    terminal_window->fading = 1;
    terminal_window->fade_start_tick = timer_get_ticks();
    gui_dirty = 1;
}

static fb_rect_t sidebar_dirty_rect(void) {
    fb_rect_t s = g_layout.sidebar;
    fb_rect_t r = { s.x - 2, 0, s.w + 4, fb_height() };
    return r;
}

static fb_rect_t right_column_dirty_rect(void) {
    int right_edge = fb_width();
    int left_edge = g_layout.clock.x - ui_scaled(10);
    fb_rect_t r = { left_edge, 0, right_edge - left_edge, fb_height() };
    return r;
}

void gui_draw(void) {
    mouse_state_t mouse = mouse_get_state();

    fb_rect_t dirty[MAX_WINDOWS * 2 + 12];
    int ndirty = 0;

    recompute_layout();

    if (gui_dirty) {
        gui_dirty = 0;

        if (first_draw) {
            first_draw = 0;
            draw_background_full();
            draw_sidebar();
            draw_clock_widget();
            draw_music_widget();
            draw_quick_access();
            draw_notifications();
            draw_dock();
            draw_workspace_switcher();

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

            draw_sidebar();
            dirty[ndirty++] = sidebar_dirty_rect();

            draw_clock_widget();
            draw_music_widget();
            draw_quick_access();
            draw_notifications();
            dirty[ndirty++] = right_column_dirty_rect();

            draw_dock();
            dirty[ndirty++] = (fb_rect_t){ g_layout.dock.x - 2, g_layout.dock.y - 2, g_layout.dock.w + 4, g_layout.dock.h + 4 };

            draw_workspace_switcher();
            dirty[ndirty++] = (fb_rect_t){ g_layout.wsw.x - 2, g_layout.wsw.y - 2, g_layout.wsw.w + 4, g_layout.wsw.h + 4 };

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
                fb_put_pixel(prev_cursor_x + dx, prev_cursor_y + dy, cursor_bg[dy * CURSOR_W + dx]);

        dirty[ndirty++] = (fb_rect_t){ prev_cursor_x, prev_cursor_y, CURSOR_W, CURSOR_H };
    }

    for (int dy = 0; dy < CURSOR_H; dy++)
        for (int dx = 0; dx < CURSOR_W; dx++)
            cursor_bg[dy * CURSOR_W + dx] = fb_get_pixel(mouse.x + dx, mouse.y + dy);

    fb_fill_rect(mouse.x, mouse.y, CURSOR_W, CURSOR_H, RGB(255, 255, 255));
    dirty[ndirty++] = (fb_rect_t){ mouse.x, mouse.y, CURSOR_W, CURSOR_H };

    prev_cursor_x = mouse.x;
    prev_cursor_y = mouse.y;

    fb_present_rects(dirty, ndirty);
}

void gui_update(void) {
    recompute_layout();

    mouse_state_t mouse = mouse_get_state();
    int clicked = !prev_left && mouse.left;

    int ch, cm, cs;
    clock_get(&ch, &cm, &cs);
    int total = ch * 3600 + cm * 60 + cs;

    if (total != last_clock_total) {
        last_clock_total = total;
        gui_dirty = 1;
    }

    int new_dock_hover = dock_hit_test(mouse.x, mouse.y);
    if (new_dock_hover != dock_hover) {
        dock_hover = new_dock_hover;
        gui_dirty = 1;
    }

    int new_quick_hover = quick_hit_test(mouse.x, mouse.y);
    if (new_quick_hover != quick_hover) {
        quick_hover = new_quick_hover;
        gui_dirty = 1;
    }

    for (int i = 0; i < window_count; i++) {
        if (windows[i].fading)
            gui_dirty = 1;
    }

    if (clicked) {
        int hit = sidebar_nav_hit_test(mouse.x, mouse.y);

        if (hit == 0)
            gui_restore_terminal();

        int dock_hit = dock_hit_test(mouse.x, mouse.y);

        if (dock_hit == 0)
            gui_restore_terminal();

        fb_rect_t restart_r, lock_r, off_r;
        power_button_rects(g_layout.sidebar, &restart_r, &lock_r, &off_r);

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
            int th = titlebar_height();
            int inside_titlebar =
                mouse.x >= w->x &&
                mouse.x < w->x + w->width &&
                mouse.y >= w->y &&
                mouse.y < w->y + th;

            if (clicked && inside_titlebar && !w->maximized) {
                w->dragging = 1;
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
    recompute_layout();

    int fw = fb_width();
    int fh = fb_height();

    int tx = (fw * 175) / 1000;
    int ty = (fh * 60) / 1000;
    int tw = (fw * 290) / 1000;
    int th = (fh * 340) / 1000;

    terminal_window = window_create(WINDOW_KIND_TERMINAL, "Terminal", tx, ty, tw, th);
    gui_notify("System Ready");
    gui_dirty = 1;
}

void gui_invalidate(void) { gui_dirty = 1; }
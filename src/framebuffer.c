#include "framebuffer.h"
#include "font8x8.h"
#include "io.h"
#include "memory/kmalloc.h"
#include "multiboot2.h"
#include <stdint.h>

static uint8_t *fb_addr = 0;
static uint32_t fb_pitch = 0;
static uint32_t fb_w = 0;
static uint32_t fb_h = 0;
static uint8_t fb_bpp = 0;
static uint8_t r_pos, g_pos, b_pos;

/* Two RAM buffers: CPU always draws into draw_buf.
   present_rect() blits only changed regions to display_buf,
   then to the real framebuffer. No full-screen blit ever. */
static uint32_t *draw_buf;    // what the CPU draws into
static uint32_t *display_buf; // what was last sent to hardware

static inline void memcpy32(uint32_t *dst, uint32_t *src, uint32_t n) {
    while (n--)
        *dst++ = *src++;
}

/* ── init ──────────────────────────────────────────────────── */

int fb_init(void *mb2_info) {
    mb2_tag_fb_t *fb = (mb2_tag_fb_t *)mb2_find_tag((mb2_info_t *)mb2_info, MB2_TAG_FB);
    if (!fb || fb->fb_type != 1)
        return -1;
    fb_addr = (uint8_t *)(uintptr_t)fb->addr;
    fb_pitch = fb->pitch;
    fb_w = fb->width;
    fb_h = fb->height;
    fb_bpp = fb->bpp;
    r_pos = fb->red_pos;
    g_pos = fb->green_pos;
    b_pos = fb->blue_pos;
    return 0;
}

// void fb_enable_backbuffer(void) {
//     if (draw_buf)
//         return;
//     uint32_t total = fb_w * fb_h;
//     draw_buf = kmalloc(total * sizeof(uint32_t));
//     display_buf = kmalloc(total * sizeof(uint32_t));
//     if (!draw_buf || !display_buf)
//         return;
//     for (uint32_t i = 0; i < total; i++)
//         draw_buf[i] = display_buf[i] = 0;
// }

void fb_enable_backbuffer(void)
{
    if (draw_buf)
        return;

    uint32_t total = fb_w * fb_h;
    uint64_t bytes = (uint64_t)total * sizeof(uint32_t);

    uint32_t *new_draw = kmalloc(bytes);
    uint32_t *new_display = kmalloc(bytes);

    if (!new_draw || !new_display)
    {
        draw_buf = 0;
        display_buf = 0;
        return;
    }

    draw_buf = new_draw;
    display_buf = new_display;

    for (uint32_t i = 0; i < total; i++)
    {
        draw_buf[i] = 0;
        display_buf[i] = 0;
    }
}

int fb_width(void) { return (int)fb_w; }
int fb_height(void) { return (int)fb_h; }

/* ── vsync ─────────────────────────────────────────────────── */

void fb_wait_vsync(void) {
    while (inb(0x3DA) & 0x08)
        ; // wait for vblank to END
    while (!inb(0x3DA) & 0x08)
        ; // wait for vblank to START
}

/* ── pixel primitives (write into draw_buf only) ───────────── */

void fb_put_pixel(int x, int y, color_t c) {
    if ((unsigned)x >= fb_w || (unsigned)y >= fb_h)
        return;
    uint32_t px = ((uint32_t)c.r << r_pos) | ((uint32_t)c.g << g_pos) | ((uint32_t)c.b << b_pos);
    if (draw_buf) {
        draw_buf[y * fb_w + x] = px;
        return;
    }
    uint8_t *p = fb_addr + y * fb_pitch + x * (fb_bpp / 8);
    for (int i = 0; i < fb_bpp / 8; i++)
        p[i] = (uint8_t)(px >> (8 * i));
}

color_t fb_get_pixel(int x, int y) {
    if ((unsigned)x >= fb_w || (unsigned)y >= fb_h)
        return RGB(0, 0, 0);
    if (draw_buf) {
        uint32_t px = draw_buf[y * fb_w + x];
        return (color_t){(px >> r_pos) & 0xFF, (px >> g_pos) & 0xFF, (px >> b_pos) & 0xFF};
    }
    uint8_t *p = fb_addr + y * fb_pitch + x * (fb_bpp / 8);
    uint32_t px = 0;
    for (int i = 0; i < fb_bpp / 8; i++)
        px |= (uint32_t)p[i] << (8 * i);
    return (color_t){(px >> r_pos) & 0xFF, (px >> g_pos) & 0xFF, (px >> b_pos) & 0xFF};
}

void fb_clear(color_t c) {
    uint32_t px = ((uint32_t)c.r << r_pos) | ((uint32_t)c.g << g_pos) | ((uint32_t)c.b << b_pos);
    if (draw_buf) {
        uint32_t total = fb_w * fb_h;
        for (uint32_t i = 0; i < total; i++)
            draw_buf[i] = px;
        return;
    }
    for (uint32_t y = 0; y < fb_h; y++)
        for (uint32_t x = 0; x < fb_w; x++)
            fb_put_pixel((int)x, (int)y, c);
}

void fb_fill_rect(int x, int y, int w, int h, color_t c) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            fb_put_pixel(x + dx, y + dy, c);
}

void fb_draw_char(int x, int y, char ch, color_t fg, color_t bg) {
    int idx = (unsigned char)ch;
    if (idx < 32 || idx > 127)
        idx = '?';
    const char *glyph = font8x8_basic[idx];
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            fb_put_pixel(x + col, y + row, (glyph[row] & (1 << col)) ? fg : bg);
}

void fb_draw_string(int x, int y, const char *s, color_t fg, color_t bg) {
    for (int i = 0; s[i]; i++)
        fb_draw_char(x + i * 8, y, s[i], fg, bg);
}

void fb_draw_int(int x, int y, int n, color_t fg, color_t bg) {
    char buf[24];
    int i = 23;
    buf[i] = '\0';
    unsigned int u = (n < 0) ? (unsigned int)(-n) : (unsigned int)n;
    if (u == 0) {
        buf[--i] = '0';
    } else {
        while (u) {
            buf[--i] = '0' + (u % 10);
            u /= 10;
        }
    }
    if (n < 0)
        buf[--i] = '-';
    fb_draw_string(x, y, &buf[i], fg, bg);
}

/* ── present: blit ONE rectangle from draw_buf → hardware ─── */
/*    Called with vsync already waited for by fb_present_rects  */

static void blit_rect(int x, int y, int w, int h) {
    if (!draw_buf || !display_buf)
        return;

    /* clamp */
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if ((uint32_t)(x + w) > fb_w)
        w = (int)fb_w - x;
    if ((uint32_t)(y + h) > fb_h)
        h = (int)fb_h - y;
    if (w <= 0 || h <= 0)
        return;

    for (int row = y; row < y + h; row++) {
        uint32_t *src = &draw_buf[row * fb_w + x];
        uint32_t *cmp = &display_buf[row * fb_w + x];
        uint8_t *dst = fb_addr + row * fb_pitch + x * (fb_bpp / 8);

        if (fb_bpp == 32) {
            /* fast path: write only if pixel changed */
            uint32_t *dst32 = (uint32_t *)dst;
            for (int col = 0; col < w; col++) {
                if (src[col] != cmp[col]) {
                    dst32[col] = src[col];
                    cmp[col] = src[col];
                }
            }
        } else {
            /* 24bpp fallback */
            int bpp = fb_bpp / 8;
            for (int col = 0; col < w; col++) {
                if (src[col] != cmp[col]) {
                    cmp[col] = src[col];
                    uint8_t *p = dst + col * bpp;
                    for (int b = 0; b < bpp; b++)
                        p[b] = (uint8_t)(src[col] >> (8 * b));
                }
            }
        }
    }
}

void fb_present_rects(fb_rect_t *rects, int count) {
    if (!draw_buf) return;
    // no vsync — timer already gates frame rate
    for (int i = 0; i < count; i++)
        blit_rect(rects[i].x, rects[i].y, rects[i].w, rects[i].h);
}

/* Legacy full-screen present (kept so nothing else breaks) */
void fb_present(void) {
    if (!draw_buf)
        return;
    fb_rect_t full = {0, 0, (int)fb_w, (int)fb_h};
    fb_present_rects(&full, 1);
}

uint64_t fb_physical_address(void)
{
    return (uint64_t)(uintptr_t)fb_addr;
}

uint64_t fb_memory_size(void)
{
    return (uint64_t)fb_pitch * fb_h;
}
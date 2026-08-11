#pragma once
#include <stdint.h>

typedef struct { uint8_t r, g, b; } color_t;

#define RGB(r,g,b)      ((color_t){(r),(g),(b)})
#define COLOR_BLACK     RGB(0,0,0)
#define COLOR_WHITE     RGB(255,255,255)
#define COLOR_GRAY      RGB(120,120,130)
#define COLOR_DARK      RGB(18,18,28)
#define COLOR_RED       RGB(210,50,50)
#define COLOR_GREEN     RGB(50,200,80)
#define COLOR_BLUE      RGB(40,100,220)
#define COLOR_YELLOW    RGB(240,200,0)
#define COLOR_CYAN      RGB(0,200,220)
#define COLOR_PURPLE    RGB(160,60,220)
#define COLOR_ORANGE    RGB(240,130,30)

/* dirty rectangle — tell fb_present_rects which regions changed */
typedef struct { int x, y, w, h; } fb_rect_t;

int     fb_init(void *mb2_info);
void    fb_enable_backbuffer(void);
int     fb_width(void);
int     fb_height(void);

void    fb_put_pixel(int x, int y, color_t c);
color_t fb_get_pixel(int x, int y);
void    fb_clear(color_t c);
void    fb_fill_rect(int x, int y, int w, int h, color_t c);
void    fb_draw_char(int x, int y, char ch, color_t fg, color_t bg);
void    fb_draw_string(int x, int y, const char *s, color_t fg, color_t bg);
void    fb_draw_int(int x, int y, int n, color_t fg, color_t bg);

/* blit only the listed rectangles — call this instead of fb_present */
void    fb_present_rects(fb_rect_t *rects, int count);
void    fb_present(void);          // legacy full-screen blit
void    fb_wait_vsync(void);

uint64_t fb_physical_address(void);
uint64_t fb_memory_size(void);
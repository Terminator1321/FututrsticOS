#include "terminal.h"
#include "../framebuffer.h"

#define COL_BG RGB(0, 0, 0)
#define COL_TEXT RGB(255, 255, 255)

#define CHAR_W 8
#define CHAR_H 10

#define MAX_ROWS 64
#define MAX_COLS 120

static int W, H;
static int cols, rows;

// Where the terminal's (0,0) lands on the framebuffer, and the pixel size
// of the area it's allowed to draw into. Defaults to the whole screen
// (set by terminal_init) until something - the GUI, once its window is
// up - narrows it with terminal_set_viewport()/terminal_move_viewport().
static int origin_x = 0;
static int origin_y = 0;

static char screen_buf[MAX_ROWS][MAX_COLS];

static int buf_row = 0;
static int buf_col = 0;
static int scroll_top = 0;

static int blink_counter = 0;
static int cursor_visible = 1;

static void redraw_terminal(void) {
    fb_fill_rect(origin_x, origin_y, W, H, COL_BG);

    for (int r = 0; r < rows; r++) {
        int buf_r = scroll_top + r;

        if (buf_r >= MAX_ROWS)
            break;

        for (int c = 0; c < cols; c++) {
            char ch = screen_buf[buf_r][c];

            if (!ch)
                break;

            fb_draw_char(origin_x + c * CHAR_W, origin_y + r * CHAR_H, ch, COL_TEXT, COL_BG);
        }
    }
}

static void scroll_down(void) {
    buf_row++;
    buf_col = 0;

    if (buf_row >= MAX_ROWS) {
        buf_row = MAX_ROWS - 1;

        for (int r = 0; r < MAX_ROWS - 1; r++) {
            for (int c = 0; c < MAX_COLS; c++) {
                screen_buf[r][c] = screen_buf[r + 1][c];
            }
        }

        for (int c = 0; c < MAX_COLS; c++) {
            screen_buf[buf_row][c] = 0;
        }
    }

    if (buf_row >= rows)
        scroll_top = buf_row - rows + 1;
    else
        scroll_top = 0;

    redraw_terminal();
}

void terminal_putchar(char c) {
    if (c == '\n') {
        scroll_down();
        fb_present();
        return;
    }

    if (c == '\b') {
        if (buf_col > 0) {
            buf_col--;

            screen_buf[buf_row][buf_col] = 0;

            fb_fill_rect(origin_x + buf_col * CHAR_W, origin_y + (buf_row - scroll_top) * CHAR_H, CHAR_W, CHAR_H, COL_BG);
            fb_present();
        }

        return;
    }

    if (buf_col >= cols)
        scroll_down();

    screen_buf[buf_row][buf_col] = c;

    fb_draw_char(origin_x + buf_col * CHAR_W, origin_y + (buf_row - scroll_top) * CHAR_H, c, COL_TEXT, COL_BG);

    buf_col++;

    /* fb_enable_backbuffer() means every draw above only touches the
     * off-screen buffer. Nothing outside a handful of debug call sites
     * (pmm.c/kmalloc.c/idt.c) ever blitted it to the screen, so boot
     * text stopped updating right after those debug prints ended, and
     * keystrokes (which land here via shell_input -> terminal_putchar)
     * never appeared at all - it *looked* frozen even though the kernel
     * kept running. Presenting after every visible change fixes that. */
    fb_present();
}

void terminal_print(const char *s) {
    while (*s)
        terminal_putchar(*s++);
}

void terminal_clear(void) {
    for (int r = 0; r < MAX_ROWS; r++) {
        for (int c = 0; c < MAX_COLS; c++) {
            screen_buf[r][c] = 0;
        }
    }

    buf_row = 0;
    buf_col = 0;
    scroll_top = 0;

    // Was fb_clear(COL_BG), which blanks the *entire* framebuffer - fine
    // back when the terminal always owned the whole screen, but wrong once
    // it's confined to a window's content area, where it must only touch
    // its own rectangle and leave the desktop/window chrome alone.
    fb_fill_rect(origin_x, origin_y, W, H, COL_BG);
    fb_present();
}

void terminal_hide_cursor(void) {
    int x = origin_x + buf_col * CHAR_W;
    int y = origin_y + (buf_row - scroll_top) * CHAR_H;

    fb_fill_rect(x, y + CHAR_H - 2, CHAR_W, 2, COL_BG);
    fb_present();
}

void terminal_draw_cursor(void) {
    int x = origin_x + buf_col * CHAR_W;
    int y = origin_y + (buf_row - scroll_top) * CHAR_H;

    fb_fill_rect(x, y + CHAR_H - 2, CHAR_W, 2, COL_TEXT);
    fb_present();
}

void terminal_tick(void) {
    blink_counter++;

    if (blink_counter >= 3000) {
        blink_counter = 0;

        cursor_visible = !cursor_visible;

        if (cursor_visible)
            terminal_draw_cursor();
        else
            terminal_hide_cursor();
    }
}

void terminal_init(int width, int height) {
    origin_x = 0;
    origin_y = 0;

    W = width;
    H = height;

    cols = W / CHAR_W;
    rows = H / CHAR_H;

    // screen_buf is a fixed MAX_ROWS x MAX_COLS array. On any framebuffer
    // bigger than 960x640 (120*8 x 64*10), cols/rows computed above exceed
    // that size, and terminal_putchar()/scroll_down() would then index and
    // write past the end of screen_buf's rows - corrupting adjacent rows
    // and the globals declared after it (buf_row, buf_col, scroll_top).
    // That's what produced the overlapping/garbled boot text and left the
    // terminal's row/col state garbage afterward. Clamp to what the buffer
    // can actually hold.
    if (cols > MAX_COLS)
        cols = MAX_COLS;

    if (rows > MAX_ROWS)
        rows = MAX_ROWS;

    terminal_clear();
    redraw_terminal();
    terminal_draw_cursor();
}

static void clamp_cursor_to_grid(void) {
    // Called after cols/rows shrink (switching into a smaller window
    // viewport). Existing content/state was produced against the old,
    // larger grid, so pull the cursor back onto the new one - otherwise
    // it would draw outside the viewport until the next keystroke
    // happened to trigger a scroll.
    if (buf_col > cols)
        buf_col = cols;

    if (buf_row >= MAX_ROWS)
        buf_row = MAX_ROWS - 1;

    scroll_top = (buf_row >= rows) ? (buf_row - rows + 1) : 0;

    if (scroll_top < 0)
        scroll_top = 0;
}

void terminal_set_viewport(int x, int y, int w, int h) {
    origin_x = x;
    origin_y = y;
    W = w;
    H = h;

    cols = W / CHAR_W;
    rows = H / CHAR_H;

    if (cols > MAX_COLS)
        cols = MAX_COLS;

    if (rows > MAX_ROWS)
        rows = MAX_ROWS;

    // Note: this only re-homes the existing screen_buf onto the new grid,
    // it doesn't reflow lines that no longer fit within the narrower
    // `cols` - they're just truncated on screen (the buffered characters
    // themselves are untouched). Good enough for confining an existing
    // full-screen console into a window; a real reflow isn't needed here.
    clamp_cursor_to_grid();

    redraw_terminal();
    terminal_draw_cursor();
}

void terminal_move_viewport(int x, int y) {
    origin_x = x;
    origin_y = y;

    redraw_terminal();
    terminal_draw_cursor();
}

void terminal_scroll(int lines) {
    int max_scroll = (buf_row >= rows) ? (buf_row - rows + 1) : 0;

    scroll_top -= lines;

    if (scroll_top < 0)
        scroll_top = 0;
    if (scroll_top > max_scroll)
        scroll_top = max_scroll;

    redraw_terminal();
    terminal_draw_cursor();
}

int terminal_contains(int x, int y) {
    return x >= origin_x && x < origin_x + W &&
           y >= origin_y && y < origin_y + H;
}
#pragma once

void terminal_init(int width, int height);

void terminal_putchar(char c);
void terminal_print(const char *s);

void terminal_clear(void);
void terminal_draw_cursor(void);
void terminal_hide_cursor(void);
void terminal_tick(void);

// Confine the terminal to a sub-rectangle of the framebuffer (e.g. a GUI
// window's content area) instead of the full screen. Recomputes cols/rows
// for the new area (clamped to the fixed screen_buf capacity) and redraws
// the existing scrollback into it - the buffer itself isn't cleared or
// reflowed, so rows wider than the new area are simply truncated on screen.
void terminal_set_viewport(int x, int y, int w, int h);

// Move an already-established viewport (same size, new position) and
// redraw in place - used when the GUI window it lives in is dragged.
void terminal_move_viewport(int x, int y);

void terminal_scroll(int lines);
int terminal_contains(int x, int y);
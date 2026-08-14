#pragma once

typedef enum {
    WINDOW_KIND_TERMINAL
} window_kind_t;

typedef struct {
    int x;
    int y;

    int width;
    int height;

    int dragging;

    int drag_offset_x;
    int drag_offset_y;

    int minimized;
    int closed;
    int maximized;

    int restore_x;
    int restore_y;
    int restore_w;
    int restore_h;

    int used;
    window_kind_t kind;
    char title[32];

    int prev_x;
    int prev_y;
    int prev_w;
    int prev_h;
    int prev_visible;
} window_t;
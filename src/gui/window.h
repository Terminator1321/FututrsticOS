#pragma once

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
} window_t;
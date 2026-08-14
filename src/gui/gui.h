#pragma once

#include "../framebuffer.h"

void gui_init(void);
void gui_update(void);
void gui_draw(void);
void gui_invalidate(void);
void gui_set_background(const color_t *pixels, int width, int height);
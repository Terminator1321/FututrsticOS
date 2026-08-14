#pragma once

#include <stdint.h>

extern volatile uint64_t mouse_irq_count;
typedef struct {
    int x;
    int y;

    uint8_t left;
    uint8_t right;
    uint8_t middle;
} mouse_state_t;

mouse_state_t mouse_get_state(void);

void mouse_init(void);
void mouse_handler(void);
int mouse_tick(void);
int mouse_get_x(void);
int mouse_get_y(void);

int mouse_left(void);
int mouse_right(void);
int mouse_middle(void);
int mouse_get_scroll(void);
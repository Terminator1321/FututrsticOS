#include "mouse.h"
#include "../../framebuffer.h"
#include "../../gui/gui.h"
#include "../../io.h"
#include "../../terminal/terminal.h"

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64

#define PS2_TIMEOUT 100000

#define CURSOR_W 8
#define CURSOR_H 8

volatile uint64_t mouse_irq_count = 0;

static const color_t CURSOR_COLOR = RGB(255, 255, 255);
static const color_t CURSOR_BG_COLOR = RGB(0, 0, 0);

static int mouse_x = 0;
static int mouse_y = 0;

static int prev_x = -1;
static int prev_y = -1;

static uint8_t packet[4];
static uint8_t packet_index = 0;
static uint8_t packet_ready = 0;
static int packet_size = 3;
static int scroll_accum = 0;

static uint8_t buttons = 0;

static int mouse_wait_write(void) {
    for (int i = 0; i < PS2_TIMEOUT; i++)
        if (!(inb(PS2_STATUS) & 0x02))
            return 0;
    return -1;
}

static int mouse_wait_read(void) {
    for (int i = 0; i < PS2_TIMEOUT; i++)
        if (inb(PS2_STATUS) & 0x01)
            return 0;
    return -1;
}

static int mouse_write(uint8_t data) {
    if (mouse_wait_write() != 0)
        return -1;
    outb(PS2_COMMAND, 0xD4);
    if (mouse_wait_write() != 0)
        return -1;
    outb(PS2_DATA, data);
    return 0;
}

static int mouse_read(uint8_t *out) {
    if (mouse_wait_read() != 0)
        return -1;
    *out = inb(PS2_DATA);
    return 0;
}

void mouse_init(void) {
    uint8_t ack;

    // Enable the second PS/2 port (the mouse port on the controller)
    if (mouse_wait_write() != 0)
        return;
    outb(PS2_COMMAND, 0xA8);

    // Write the controller command byte directly instead of reading it
    // back first (some controllers don't return a byte for 0x20, which
    // would otherwise hang here). 0x47 = enable IRQ1 + IRQ12, enable both
    // clocks, no scancode translation, both ports active.
    if (mouse_wait_write() != 0)
        return;
    outb(PS2_COMMAND, 0x60);
    if (mouse_wait_write() != 0)
        return;
    outb(PS2_DATA, 0x47);

    if (mouse_write(0xF6) != 0)
        return;
    if (mouse_read(&ack) != 0)
        return;

    mouse_write(0xF3);
    mouse_read(&ack);
    mouse_write(200);
    mouse_read(&ack);

    mouse_write(0xF3);
    mouse_read(&ack);
    mouse_write(100);
    mouse_read(&ack);

    mouse_write(0xF3);
    mouse_read(&ack);
    mouse_write(80);
    mouse_read(&ack);

    mouse_write(0xF2);
    mouse_read(&ack);

    uint8_t device_id = 0;
    if (mouse_read(&device_id) == 0 && device_id == 3)
        packet_size = 4;

    if (mouse_write(0xF4) != 0)
        return;
    if (mouse_read(&ack) != 0)
        return;

    uint8_t slave_mask = inb(0xA1);
    outb(0xA1, slave_mask & ~(1 << 4)); // unmask IRQ12 on slave PIC

    uint8_t master_mask = inb(0x21);
    outb(0x21, master_mask & ~(1 << 2)); // unmask IRQ2 (cascade) on master PIC

    mouse_x = fb_width() / 2;
    mouse_y = fb_height() / 2;
}

void mouse_handler(void) {
    mouse_irq_count++;

    uint8_t byte = inb(PS2_DATA);

    if (packet_index == 0 && !(byte & 0x08)) {
        return;
    }

    packet[packet_index++] = byte;

    if (packet_index == packet_size) {
        packet_index = 0;
        packet_ready = 1;
    }
}

int mouse_tick(void) {
    if (!packet_ready)
        return 0;
    packet_ready = 0;

    uint8_t new_buttons = packet[0] & 0x07;

    int dx = (int8_t)packet[1];
    int dy = -(int8_t)packet[2];
    if (packet[0] & 0x40)
        dx = 0;
    if (packet[0] & 0x80)
        dy = 0;

    int new_x = mouse_x + dx;
    int new_y = mouse_y + dy;

    if (new_x < 0)
        new_x = 0;
    if (new_y < 0)
        new_y = 0;
    if (new_x >= fb_width())
        new_x = fb_width() - 1;
    if (new_y >= fb_height())
        new_y = fb_height() - 1;

    int dz = 0;
    if (packet_size == 4)
        dz = (int8_t)packet[3];

    if (dz)
        scroll_accum += dz;

    if (new_x == mouse_x && new_y == mouse_y && new_buttons == buttons && dz == 0)
        return 0;

    mouse_x = new_x;
    mouse_y = new_y;
    buttons = new_buttons;

    return 1;
}

int mouse_get_scroll(void) {
    int value = scroll_accum;
    scroll_accum = 0;
    return value;
}

mouse_state_t mouse_get_state(void) {
    mouse_state_t state;

    state.x = mouse_x;
    state.y = mouse_y;

    state.left = mouse_left();
    state.right = mouse_right();
    state.middle = mouse_middle();

    return state;
}

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
int mouse_left(void) { return buttons & 1; }
int mouse_right(void) { return buttons & 2; }
int mouse_middle(void) { return buttons & 4; }
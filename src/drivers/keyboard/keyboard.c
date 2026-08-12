#include "keyboard.h"
#include "../../io.h"
#include "../../terminal/shell.h"
#include <stdint.h>

#define PS2_DATA 0x60
#define PS2_STATUS 0x64

// Scancode set 1 -> ASCII  (index = scancode byte)
// 0x00 = no mapping
static const char sc_ascii[128] = {
    0,    0,   '1', '2', '3', '4', '5', '6', '7', '8', '9',  '0', '-', '=',  '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',  '[', ']', '\n', 0,
    'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z',
    'x',  'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,   '*',  0,   ' ', 0,
    // F1-F10, numlock, scrolllock etc - no ASCII mapping
};

static const char sc_ascii_shift[128] = {
    0,    0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',  '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A',  'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|',  'Z',
    'X',  'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,
};

// Simple ring buffer
#define BUF_SIZE 256
static volatile char buf[BUF_SIZE];
static volatile uint8_t buf_head = 0; // write index
static volatile uint8_t buf_tail = 0; // read  index
volatile uint64_t keyboard_irq_count = 0;
static void buf_push(char c) {
    uint8_t next = (buf_head + 1) % BUF_SIZE;
    if (next != buf_tail) { // drop if full
        buf[buf_head] = c;
        buf_head = next;
    }
}

char keyboard_getchar(void) {
    if (buf_tail == buf_head)
        return 0; // empty
    char c = buf[buf_tail];
    buf_tail = (buf_tail + 1) % BUF_SIZE;
    return c;
}

// Modifier state
static int shift = 0;
static int caps = 0;

void keyboard_handler(void)
{
    keyboard_irq_count++;

    uint8_t sc = inb(PS2_DATA);

    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    vga[0] = 0x0F00 | 'K';

    if (sc & 0x80) {
        uint8_t released = sc & 0x7F;

        if (released == 0x2A || released == 0x36)
            shift = 0;

        return;
    }

    switch (sc) {
    case 0x2A:
    case 0x36:
        shift = 1;
        return;

    case 0x3A:
        caps = !caps;
        return;

    default:
        break;
    }

    if (sc >= 128)
        return;

    char c;
    int upper = shift ^ caps;

    c = upper ? sc_ascii_shift[sc] : sc_ascii[sc];

    if (c)
        shell_input(c);
}

// Unmask IRQ1 in the PIC
void keyboard_init(void) {
    // read current mask, clear bit 1 (IRQ1 = keyboard)
    uint8_t mask = inb(0x21);
    outb(0x21, mask & ~(1 << 1));
}

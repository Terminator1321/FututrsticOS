#include "timer.h"
#include "../../io.h"
#include "../../pic.h"

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40
#define PIT_BASE_FREQ 1193182
#include "../../terminal/terminal.h"
#include "../../framebuffer.h"
#include "../../io.h"
#include "timer.h"
volatile uint64_t timer_ticks = 0;

void timer_init(uint32_t frequency) {
    uint16_t divisor = PIT_BASE_FREQ / frequency;

    outb(PIT_COMMAND, 0x36);

    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, divisor >> 8);

    // Unmask IRQ0
    uint8_t mask = inb(PIC1_DATA);
    outb(PIC1_DATA, mask & ~(1 << 0));
}
void timer_handler(void) {
    timer_ticks++;
}
uint64_t timer_get_ticks(void) {
    return timer_ticks;
}
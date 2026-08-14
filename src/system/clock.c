#include "clock.h"
#include "../drivers/timer/timer.h"

static long long clock_offset = 0;

void clock_init(void) {
    clock_offset = 0;
}

void clock_get(int *h, int *m, int *s) {
    long long total = (long long)(timer_get_ticks() / 100) + clock_offset;

    total %= 86400;
    if (total < 0)
        total += 86400;

    *h = (int)(total / 3600);
    *m = (int)((total % 3600) / 60);
    *s = (int)(total % 60);
}

void clock_set(int h, int m, int s) {
    if (h < 0) h = 0;
    if (h > 23) h = 23;
    if (m < 0) m = 0;
    if (m > 59) m = 59;
    if (s < 0) s = 0;
    if (s > 59) s = 59;

    long long target = (long long)h * 3600 + (long long)m * 60 + (long long)s;
    long long now = (long long)(timer_get_ticks() / 100);

    clock_offset = target - now;
}
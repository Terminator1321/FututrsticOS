#include <stdint.h>

#define SYS_WRITE 2
#define SYS_EXIT  1

static inline uint64_t syscall1(uint64_t number, uint64_t arg1)
{
    uint64_t result;

    __asm__ volatile (
        "int $0x80"
        : "=a"(result)
        : "a"(number),
          "D"(arg1)
        : "memory"
    );

    return result;
}

int main(void)
{
    syscall1(SYS_WRITE, 'H');
    syscall1(SYS_WRITE, 'e');
    syscall1(SYS_WRITE, 'l');
    syscall1(SYS_WRITE, 'l');
    syscall1(SYS_WRITE, 'o');
    syscall1(SYS_WRITE, ' ');
    syscall1(SYS_WRITE, 'f');
    syscall1(SYS_WRITE, 'r');
    syscall1(SYS_WRITE, 'o');
    syscall1(SYS_WRITE, 'm');
    syscall1(SYS_WRITE, ' ');
    syscall1(SYS_WRITE, 'R');
    syscall1(SYS_WRITE, 'i');
    syscall1(SYS_WRITE, 'n');
    syscall1(SYS_WRITE, 'g');
    syscall1(SYS_WRITE, ' ');
    syscall1(SYS_WRITE, '3');
    syscall1(SYS_WRITE, '\n');

    syscall1(SYS_EXIT, 0);

    for (;;)
        ;
}
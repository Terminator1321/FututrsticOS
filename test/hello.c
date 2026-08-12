#include <stdint.h>

#define SYS_EXIT 1
#define SYS_WRITE 2

static inline uint64_t syscall2(uint64_t number, uint64_t arg1, uint64_t arg2) {
    uint64_t result;

    __asm__ volatile("int $0x80" : "=a"(result) : "a"(number), "D"(arg1), "S"(arg2) : "memory");

    return result;
}

int main(void) {
    const char message[] = "Hello from Ring 3!\n";

    syscall2(SYS_WRITE, (uint64_t)(uintptr_t)message, sizeof(message) - 1);

    syscall2(SYS_EXIT, 0, 0);

    for (;;)
        ;
}
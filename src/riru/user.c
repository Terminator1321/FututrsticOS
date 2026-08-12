#include "user.h"

__attribute__((noreturn)) void riru_enter_user(uint64_t entry, uint64_t stack) {
    __asm__ volatile("pushq $0x23\n"
                     "pushq %0\n"
                     "pushfq\n"
                     "pushq $0x1B\n"
                     "pushq %1\n"
                     "iretq\n"
                     :
                     : "r"(stack), "r"(entry)
                     : "memory");

    __builtin_unreachable();
}
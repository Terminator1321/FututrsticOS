static inline long syscall1(
    long number,
    long arg1)
{
    long result;

    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number),
          "D"(arg1)
        : "memory"
    );

    return result;
}

#define SYS_EXIT 1

int main(void)
{
    syscall1(SYS_EXIT, 42);

    for (;;)
        ;
}
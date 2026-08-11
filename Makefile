ASM     = nasm
CC      = gcc
LD      = ld

CFLAGS  = -ffreestanding -O2 -Wall -Wextra \
           -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -g

LDFLAGS = -m elf_x86_64 -T linker.ld -nostdlib

QEMU    = qemu-system-x86_64
BUILD   = build
SRC     = src

# Find all C and ASM files
C_SRCS   := $(shell find $(SRC) -name '*.c')
ASM_SRCS := $(shell find $(SRC) -name '*.s')

# Convert source paths to object paths
C_OBJS   := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(C_SRCS))
ASM_OBJS := $(patsubst $(SRC)/%.s,$(BUILD)/%.o,$(ASM_SRCS))

OBJS := $(C_OBJS) $(ASM_OBJS)

all: $(BUILD)/os.iso

# Compile C
$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile ASM
$(BUILD)/%.o: $(SRC)/%.s
	@mkdir -p $(dir $@)
	$(ASM) -f elf64 $< -o $@

$(BUILD)/kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

GRUB_MKRESCUE := $(shell command -v grub-mkrescue 2>/dev/null || command -v grub2-mkrescue 2>/dev/null)

$(BUILD)/os.iso: $(BUILD)/kernel.elf grub.cfg
	mkdir -p $(BUILD)/iso/boot/grub
	cp $(BUILD)/kernel.elf $(BUILD)/iso/boot/
	cp grub.cfg $(BUILD)/iso/boot/grub/
	$(GRUB_MKRESCUE) -o $@ $(BUILD)/iso

run: all disk.img
	$(QEMU) -cdrom $(BUILD)/os.iso -drive file=disk.img,format=raw,if=ide -m 512M

disk.img:
	dd if=/dev/zero of=disk.img bs=1M count=64

clean:
	rm -rf $(BUILD)

rebuild: clean all

.PHONY: all run clean rebuild

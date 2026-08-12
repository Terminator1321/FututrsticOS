ASM = nasm
CC = gcc
LD = ld

CFLAGS = -ffreestanding -O2 -Wall -Wextra \
-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -g

HOST_CFLAGS = -O2 -Wall -Wextra

LDFLAGS = -m elf_x86_64 -T linker.ld -nostdlib

QEMU = qemu-system-x86_64

BUILD = build
SRC = src

KERNEL = $(BUILD)/kernel.elf
ISO = $(BUILD)/os.iso
DISK = disk.img

RIRU_PACK = $(SRC)/tools/riru_pack
NANOFS_IMAGE = $(SRC)/tools/nanofs_image

GRUB_MKRESCUE := $(shell command -v grub-mkrescue 2>/dev/null || command -v grub2-mkrescue 2>/dev/null)

C_SRCS := $(shell find $(SRC) -name '*.c' ! -path '$(SRC)/tools/*')
ASM_SRCS := $(shell find $(SRC) -name '*.s')

C_OBJS := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(C_SRCS))
ASM_OBJS := $(patsubst $(SRC)/%.s,$(BUILD)/%.o,$(ASM_SRCS))

OBJS := $(C_OBJS) $(ASM_OBJS)

all: $(ISO) $(DISK)

$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.s
	@mkdir -p $(dir $@)
	$(ASM) -f elf64 $< -o $@

$(KERNEL): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

$(RIRU_PACK): $(SRC)/tools/riru_pack.c
	$(CC) $(HOST_CFLAGS) $< -o $@

$(NANOFS_IMAGE): $(SRC)/tools/nanofs_img.c
	$(CC) $(HOST_CFLAGS) $< -o $@

test/hello.o: test/hello.c
	@mkdir -p test
	$(CC) -m64 -O2 -ffreestanding -nostdlib -c $< -o $@

test/hello.elf: test/hello.o
	$(LD) -m elf_x86_64 -e main -o $@ $<

test/hello.riru: test/hello.elf $(RIRU_PACK)
	$(RIRU_PACK) test/hello.elf test/hello.riru

$(DISK): $(NANOFS_IMAGE) test/hello.riru
	$(NANOFS_IMAGE) test/hello.riru $(DISK)

$(ISO): $(KERNEL) grub.cfg
	@mkdir -p $(BUILD)/iso/boot/grub
	cp $(KERNEL) $(BUILD)/iso/boot/kernel.elf
	cp grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $@ $(BUILD)/iso

run: all
	$(QEMU) \
	-cdrom $(ISO) \
	-drive file=$(DISK),format=raw,if=ide \
	-m 512M

clean:
	rm -rf $(BUILD)

rebuild: clean
	rm -f $(DISK)
	rm -f test/hello.o
	rm -f test/hello.elf
	rm -f test/hello.riru
	rm -f $(RIRU_PACK)
	rm -f $(NANOFS_IMAGE)
	$(MAKE) all

kernel: $(KERNEL)

riru: test/hello.riru

filesystem: $(DISK)

iso: $(ISO)

.PHONY: all run clean rebuild kernel riru filesystem iso
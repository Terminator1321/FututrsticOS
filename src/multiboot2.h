#pragma once
#include <stdint.h>

typedef struct {
    uint32_t total_size;
    uint32_t reserved;
} mb2_info_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} mb2_tag_t;

// type 8 - framebuffer
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t size;
    uint64_t addr;
    uint32_t pitch; // bytes per scanline
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t fb_type; // 1 = RGB direct color
    uint16_t reserved;
    // RGB channel layout (only present when fb_type == 1)
    uint8_t red_pos;
    uint8_t red_mask;
    uint8_t green_pos;
    uint8_t green_mask;
    uint8_t blue_pos;
    uint8_t blue_mask;
} mb2_tag_fb_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t size;
    uint8_t rsdp[];
} mb2_tag_acpi_t;

#define MB2_TAG_END 0
#define MB2_TAG_FB 8
#define MB2_TAG_ACPI_OLD 14
#define MB2_TAG_ACPI_NEW 15

static inline mb2_tag_t *mb2_find_tag(mb2_info_t *info, uint32_t type) {
    mb2_tag_t *tag = (mb2_tag_t *)((uint8_t *)info + 8);
    while (tag->type != MB2_TAG_END) {
        if (tag->type == type)
            return tag;
        tag = (mb2_tag_t *)((uint8_t *)tag + ((tag->size + 7) & ~7u));
    }
    return 0;
}
#pragma once

// On-disk layout for a desktop wallpaper image, stored as raw, headerless
// RGB888 pixel data (one 3-byte {R,G,B} triplet per pixel, row-major - the
// same format gui_set_background()/fb_draw_image() already expect in
// memory, just sitting on disk instead).
//
// This lives well past anything NanoFS2 (src/fs/fs.c) could ever allocate:
// its inode table caps out at FS_MAX_INODES(72) * FS_MAX_BLOCKS(4) = 288
// data blocks, i.e. sectors ~9-297. Sector 100000 (of the 64 MiB / 131072
// sector disk) is comfortably out of that range, so the two never collide,
// and the wallpaper survives `format`/normal filesystem use.
//
// This header is intentionally freestanding-safe (stdint.h only) so it can
// be shared as-is between the kernel (src/gui/wallpaper.c) and the host
// packing tool (src/tools/wallpaper_pack.c) - same pattern as the manual
// struct duplication in src/tools/nanofs_img.c.

#include <stdint.h>

#define WALLPAPER_MAGIC "WPBMP01"     // 8 bytes with the NUL, like fs_superblock_t's magic
#define WALLPAPER_HEADER_SECTOR 100000u
#define WALLPAPER_DATA_START_SECTOR (WALLPAPER_HEADER_SECTOR + 1u)

#define WALLPAPER_DISK_SECTORS 131072u // must match nanofs_img.c's TOTAL_SECTORS (64 MiB disk)
#define WALLPAPER_MAX_BYTES ((WALLPAPER_DISK_SECTORS - WALLPAPER_DATA_START_SECTOR) * 512u)

typedef struct __attribute__((packed)) {
    char magic[8];
    uint32_t width;
    uint32_t height;
    uint32_t size_bytes; // width * height * 3, i.e. raw RGB888 payload length
} wallpaper_header_t;
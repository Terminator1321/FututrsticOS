#include "wallpaper.h"

#include "../drivers/ata/ata.h"
#include "../framebuffer.h"
#include "../libc/string.h"
#include "../memory/kmalloc.h"
#include "../wallpaper_layout.h"
#include "gui.h"

#define SECTOR_SIZE 512

int wallpaper_load(void) {
    uint8_t header_sector[SECTOR_SIZE];

    if (ata_read_sector(WALLPAPER_HEADER_SECTOR, header_sector) != 0)
        return -1;

    wallpaper_header_t hdr;
    memcpy(&hdr, header_sector, sizeof(hdr));

    if (memcmp(hdr.magic, WALLPAPER_MAGIC, sizeof(WALLPAPER_MAGIC) - 1) != 0)
        return -1; // nothing packed onto disk yet

    if (hdr.width == 0 || hdr.height == 0)
        return -2;

    if ((uint64_t)hdr.width * hdr.height * 3 != hdr.size_bytes)
        return -2;

    if (hdr.size_bytes == 0 || hdr.size_bytes > WALLPAPER_MAX_BYTES)
        return -2;

    uint8_t *pixels = kmalloc(hdr.size_bytes);

    if (!pixels)
        return -2;

    uint32_t sector = WALLPAPER_DATA_START_SECTOR;
    uint32_t offset = 0;

    while (offset < hdr.size_bytes) {
        uint8_t buffer[SECTOR_SIZE];

        if (ata_read_sector(sector, buffer) != 0) {
            kfree(pixels);
            return -2;
        }

        uint32_t chunk = hdr.size_bytes - offset;
        if (chunk > SECTOR_SIZE)
            chunk = SECTOR_SIZE;

        memcpy(pixels + offset, buffer, chunk);
        offset += chunk;
        sector++;
    }

    gui_set_background((const color_t *)pixels, (int)hdr.width, (int)hdr.height);
    return 0;
}
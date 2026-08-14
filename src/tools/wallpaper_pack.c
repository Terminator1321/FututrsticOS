// Writes a raw RGB888 image (see ../wallpaper_layout.h for the on-disk
// format) into the fixed wallpaper sectors of an *existing* disk.img,
// without touching anything NanoFS2 owns.
//
// Usage: wallpaper_pack <raw-rgb-file> <width> <height> <disk-image>
//
// <raw-rgb-file> must be exactly width*height*3 bytes: one {R,G,B} byte
// triplet per pixel, row-major, no header. To produce that from a normal
// photo with ImageMagick, cropping/scaling to fill 1024x768 (the screen
// resolution this OS boots at - see grub.cfg/src/boot.s):
//
//   convert input.jpg -resize 1024x768^ -gravity center -extent 1024x768 -depth 8 RGB:wallpaper.rgb
//
// then:
//
//   ./src/tools/wallpaper_pack wallpaper.rgb 1024 768 disk.img

#include "../wallpaper_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR_SIZE 512

static void write_sector(FILE *disk, uint32_t sector, const void *data) {
    if (fseek(disk, (long)sector * SECTOR_SIZE, SEEK_SET) != 0) {
        perror("fseek");
        exit(1);
    }

    if (fwrite(data, 1, SECTOR_SIZE, disk) != SECTOR_SIZE) {
        perror("fwrite");
        exit(1);
    }
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <raw-rgb-file> <width> <height> <disk-image>\n", argv[0]);
        fprintf(stderr, "  <raw-rgb-file> must be exactly width*height*3 bytes (RGB888, no header)\n");
        return 1;
    }

    const char *rgb_path = argv[1];
    long width = strtol(argv[2], NULL, 10);
    long height = strtol(argv[3], NULL, 10);
    const char *disk_path = argv[4];

    if (width <= 0 || height <= 0) {
        fprintf(stderr, "Width and height must be positive\n");
        return 1;
    }

    uint32_t expected_size = (uint32_t)(width * height * 3);

    if (expected_size > WALLPAPER_MAX_BYTES) {
        fprintf(stderr, "Image too large for the reserved wallpaper region\n");
        fprintf(stderr, "Maximum: %u bytes, requested: %u bytes\n", WALLPAPER_MAX_BYTES, expected_size);
        return 1;
    }

    FILE *rgb_file = fopen(rgb_path, "rb");
    if (!rgb_file) {
        perror(rgb_path);
        return 1;
    }

    uint8_t *pixels = malloc(expected_size);
    if (!pixels) {
        fprintf(stderr, "Out of memory\n");
        fclose(rgb_file);
        return 1;
    }

    size_t got = fread(pixels, 1, expected_size, rgb_file);

    // A trailing byte still present after reading the expected amount means
    // the file is bigger than width*height*3, i.e. dimensions don't match.
    int extra = fgetc(rgb_file) != EOF;
    fclose(rgb_file);

    if (got != expected_size || extra) {
        fprintf(stderr, "%s is not exactly %u bytes (%ldx%ld @ 3 bytes/pixel) - read %zu bytes\n",
                rgb_path, expected_size, width, height, got);
        fprintf(stderr, "Check the image was converted at the right resolution.\n");
        free(pixels);
        return 1;
    }

    FILE *disk = fopen(disk_path, "r+b");
    if (!disk) {
        perror(disk_path);
        fprintf(stderr, "(disk image must already exist - build it with `make filesystem` first)\n");
        free(pixels);
        return 1;
    }

    uint8_t header_sector[SECTOR_SIZE];
    memset(header_sector, 0, sizeof(header_sector));
    wallpaper_header_t *hdr = (wallpaper_header_t *)header_sector;
    memset(hdr->magic, 0, sizeof(hdr->magic));
    memcpy(hdr->magic, WALLPAPER_MAGIC, strlen(WALLPAPER_MAGIC));
    hdr->width = (uint32_t)width;
    hdr->height = (uint32_t)height;
    hdr->size_bytes = expected_size;
    write_sector(disk, WALLPAPER_HEADER_SECTOR, header_sector);

    uint32_t offset = 0;
    uint32_t sector = WALLPAPER_DATA_START_SECTOR;

    while (offset < expected_size) {
        uint8_t buffer[SECTOR_SIZE];
        memset(buffer, 0, sizeof(buffer));
        uint32_t chunk = expected_size - offset;
        if (chunk > SECTOR_SIZE)
            chunk = SECTOR_SIZE;
        memcpy(buffer, pixels + offset, chunk);
        write_sector(disk, sector, buffer);
        offset += chunk;
        sector++;
    }

    fclose(disk);
    free(pixels);

    printf("Wallpaper written: %ldx%ld, %u bytes, sectors %u-%u\n",
           width, height, expected_size, WALLPAPER_HEADER_SECTOR, sector - 1);
    return 0;
}
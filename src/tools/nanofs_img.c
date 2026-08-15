#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR_SIZE 512

#define DISK_SIZE_MB 4096
#define TOTAL_SECTORS ((DISK_SIZE_MB * 1024 * 1024) / SECTOR_SIZE)

#define FS_MAGIC "NANOFS2"
#define FS_VERSION 2

#define FS_MAX_INODES 72
#define FS_INODE_TABLE_START 1
#define FS_INODE_TABLE_SECTORS 8
#define FS_DATA_START 9
#define FS_MAX_BLOCKS 4
#define FS_NAME_MAX 28

typedef struct __attribute__((packed)) {
    char name[FS_NAME_MAX];
    uint32_t parent;
    uint32_t blocks[FS_MAX_BLOCKS];
    uint32_t size;
    uint8_t is_directory;
    uint8_t _pad[1];
} fs_inode_t;

typedef struct __attribute__((packed)) {
    char magic[8];
    uint32_t version;
    uint32_t total_sectors;
    uint32_t inode_count;
} fs_superblock_t;

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

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");

    if (!file) {
        perror(path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long file_size = ftell(file);

    if (file_size < 0) {
        fclose(file);
        return NULL;
    }

    rewind(file);
    uint8_t *buffer = malloc((size_t)file_size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    if (fread(buffer, 1, (size_t)file_size, file) != (size_t)file_size) {
        free(buffer);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)file_size;
    return buffer;
}

static void create_empty_disk(FILE *disk) {
    uint8_t zero[SECTOR_SIZE];
    memset(zero, 0, sizeof(zero));
    for (uint32_t sector = 0; sector < TOTAL_SECTORS; sector++) {
        write_sector(disk, sector, zero);
    }
}

static void create_superblock(FILE *disk) {
    uint8_t sector[SECTOR_SIZE];
    memset(sector, 0, sizeof(sector));
    fs_superblock_t *sb = (fs_superblock_t *)sector;
    memset(sb->magic, 0, sizeof(sb->magic));
    memcpy(sb->magic, FS_MAGIC, strlen(FS_MAGIC));
    sb->version = FS_VERSION;
    sb->total_sectors = TOTAL_SECTORS;
    sb->inode_count = 1;
    write_sector(disk, 0, sector);
}

static void create_inode_table(FILE *disk, const uint8_t *riru, size_t riru_size) {
    uint8_t sector[SECTOR_SIZE];
    memset(sector, 0, sizeof(sector));
    fs_inode_t *inode = (fs_inode_t *)sector;
    memset(inode, 0, sizeof(fs_inode_t));
    strncpy(inode->name, "hello.riru", FS_NAME_MAX - 1);
    inode->parent = 0;
    inode->blocks[0] = FS_DATA_START;
    inode->size = (uint32_t)riru_size;
    inode->is_directory = 0;
    write_sector(disk, FS_INODE_TABLE_START, sector);
}

static void write_riru(FILE *disk, const uint8_t *riru, size_t riru_size) {
    if (riru_size == 0) {
        fprintf(stderr, "RIRU file is empty\n");
        exit(1);
    }

    if (riru_size > FS_MAX_BLOCKS * 511) {
        fprintf(stderr, "RIRU file is too large\n");
        fprintf(stderr, "Maximum: %u bytes\n", FS_MAX_BLOCKS * 511);
        exit(1);
    }

    size_t offset = 0;

    for (uint32_t block = 0; block < FS_MAX_BLOCKS && offset < riru_size; block++) {
        uint8_t sector[SECTOR_SIZE];
        memset(sector, 0, sizeof(sector));
        size_t remaining = riru_size - offset;
        size_t chunk = remaining > 511 ? 511 : remaining;
        memcpy(sector, riru + offset, chunk);
        write_sector(disk, FS_DATA_START + block, sector);
        offset += chunk;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <riru-file> <disk-image>\n", argv[0]);

        return 1;
    }

    const char *riru_path = argv[1];
    const char *disk_path = argv[2];
    size_t riru_size = 0;
    uint8_t *riru = read_file(riru_path, &riru_size);

    if (!riru)
        return 1;

    printf("RIRU size: %zu bytes\n", riru_size);
    FILE *disk = fopen(disk_path, "wb+");
    if (!disk) {
        perror(disk_path);
        free(riru);
        return 1;
    }

    printf("Creating %d MiB NANOFS2 image...\n", DISK_SIZE_MB);
    create_empty_disk(disk);
    create_superblock(disk);
    create_inode_table(disk, riru, riru_size);
    write_riru(disk, riru, riru_size);
    fflush(disk);
    fclose(disk);
    free(riru);
    printf("NANOFS2 image created successfully.\n");
    printf("File: hello.riru\n");
    printf("Data block: %u\n", FS_DATA_START);
    printf("Size: %zu bytes\n", riru_size);
    return 0;
}
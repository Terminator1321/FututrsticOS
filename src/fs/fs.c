#include "fs.h"

#include "../drivers/ata/ata.h"
#include "../libc/string.h"

static uint32_t current_dir = 0;
static uint32_t mounted_total_sectors = 131072;
#define INODES_PER_SECTOR (512 / sizeof(fs_inode_t))

static void inode_read(uint32_t id, fs_inode_t *out) {
    if (id == 0 || id > FS_MAX_INODES) {
        for (uint32_t i = 0; i < sizeof(fs_inode_t); i++) {
            ((uint8_t *)out)[i] = 0;
        }

        return;
    }

    uint32_t index = id - 1;
    uint32_t sector = FS_INODE_TABLE_START + index / INODES_PER_SECTOR;
    uint32_t slot = index % INODES_PER_SECTOR;
    uint8_t buffer[512];
    ata_read_sector(sector, buffer);
    fs_inode_t *table = (fs_inode_t *)buffer;
    *out = table[slot];
}

static void inode_write(uint32_t id, const fs_inode_t *in) {
    if (id == 0 || id > FS_MAX_INODES)
        return;

    uint32_t index = id - 1;
    uint32_t sector = FS_INODE_TABLE_START + index / INODES_PER_SECTOR;
    uint32_t slot = index % INODES_PER_SECTOR;
    uint8_t buffer[512];
    ata_read_sector(sector, buffer);
    fs_inode_t *table = (fs_inode_t *)buffer;
    table[slot] = *in;
    ata_write_sector(sector, buffer);
}

static uint32_t inode_alloc(void) {
    for (uint32_t id = 1; id <= FS_MAX_INODES; id++) {
        fs_inode_t node;
        inode_read(id, &node);
        if (node.name[0] == '\0')
            return id;
    }
    return 0;
}

static uint32_t block_alloc(void) {
    uint32_t highest = FS_DATA_START - 1;
    for (uint32_t id = 1; id <= FS_MAX_INODES; id++) {
        fs_inode_t node;
        inode_read(id, &node);

        if (node.name[0] == '\0')
            continue;

        for (int b = 0; b < FS_MAX_BLOCKS; b++) {
            if (node.blocks[b] > highest)
                highest = node.blocks[b];
        }
    }
    return highest + 1;
}

static uint32_t lookup(const char *name, uint32_t parent_dir) {
    for (uint32_t id = 1; id <= FS_MAX_INODES; id++) {
        fs_inode_t node;
        inode_read(id, &node);

        if (node.name[0] == '\0')
            continue;

        if (node.parent != parent_dir)
            continue;

        if (strcmp(node.name, name) == 0)
            return id;
    }
    return 0;
}

void fs_format(void) {
    uint8_t sector[512] = {0};
    fs_superblock_t *sb = (fs_superblock_t *)sector;
    strncpy(sb->magic, FS_MAGIC, sizeof(sb->magic));
    sb->magic[sizeof(sb->magic) - 1] = '\0';
    sb->version = FS_VERSION;
    sb->total_sectors = 131072;
    sb->inode_count = 0;
    ata_write_sector(0, sector);
    uint8_t empty[512] = {0};

    for (int s = 0; s < FS_INODE_TABLE_SECTORS; s++) {
        ata_write_sector(FS_INODE_TABLE_START + s, empty);
    }
    current_dir = 0;
    mounted_total_sectors = sb->total_sectors;
}

int fs_mount(void) {
    uint8_t sector[512];

    if (ata_read_sector(0, sector) != 0) {
        return 0;
    }

    fs_superblock_t *sb = (fs_superblock_t *)sector;

    if (strcmp(sb->magic, FS_MAGIC) != 0) {
        return 0;
    }

    if (sb->version != FS_VERSION)
        return 0;
    current_dir = 0;
    mounted_total_sectors = sb->total_sectors;
    return 1;
}

int fs_create(const char *name) {
    if (!name)
        return -1;

    if (lookup(name, current_dir)) {
        return -1;
    }

    uint32_t id = inode_alloc();

    if (id == 0)
        return -2;

    fs_inode_t node;
    memset(&node, 0, sizeof(node));
    strncpy(node.name, name, FS_NAME_MAX);
    node.name[FS_NAME_MAX - 1] = '\0';
    node.parent = current_dir;
    node.is_directory = 0;
    node.size = 0;
    node.blocks[0] = block_alloc();
    inode_write(id, &node);
    return 0;
}

int fs_write(const char *name, const void *data) {
    if (!name || !data)
        return -1;

    uint32_t id = lookup(name, current_dir);

    if (id == 0)
        return -1;

    fs_inode_t node;
    inode_read(id, &node);
    if (node.is_directory)
        return -2;

    const char *text = (const char *)data;
    uint32_t len = (uint32_t)strlen(text);
    uint32_t max_size = FS_MAX_BLOCKS * 511;

    if (len > max_size)
        len = max_size;

    uint32_t blocks_needed = (len + 510) / 511;

    if (blocks_needed < 1)
        blocks_needed = 1;

    for (uint32_t b = 0; b < blocks_needed; b++) {
        if (node.blocks[b] == 0) {
            node.blocks[b] = block_alloc();
        }
    }

    uint32_t written = 0;

    for (uint32_t b = 0; b < blocks_needed && written < len; b++) {
        uint8_t buffer[512] = {0};
        uint32_t chunk = len - written;

        if (chunk > 511)
            chunk = 511;

        for (uint32_t j = 0; j < chunk; j++) {
            buffer[j] = ((const uint8_t *)data)[written + j];
        }
        ata_write_sector(node.blocks[b], buffer);
        written += chunk;
    }
    node.size = len;
    inode_write(id, &node);
    return 0;
}

int fs_write_binary(const char *name, const void *data, uint32_t size) {
    if (!name || !data)
        return -1;

    uint32_t id = lookup(name, current_dir);

    if (id == 0)
        return -1;

    fs_inode_t node;
    inode_read(id, &node);

    if (node.is_directory)
        return -2;

    uint32_t max_size = FS_MAX_BLOCKS * 511;

    if (size > max_size)
        return -3;

    uint32_t blocks_needed = (size + 510) / 511;

    if (blocks_needed == 0)
        blocks_needed = 1;

    for (uint32_t b = 0; b < blocks_needed; b++) {
        if (node.blocks[b] == 0) {
            node.blocks[b] = block_alloc();
        }
    }

    uint32_t written = 0;

    for (uint32_t b = 0; b < blocks_needed; b++) {
        uint8_t buffer[512] = {0};
        uint32_t chunk = size - written;
        if (chunk > 511)
            chunk = 511;

        if (chunk > 0) {
            for (uint32_t j = 0; j < chunk; j++) {
                buffer[j] = ((const uint8_t *)data)[written + j];
            }
        }

        ata_write_sector(node.blocks[b], buffer);
        written += chunk;

        if (written >= size)
            break;
    }
    node.size = size;
    inode_write(id, &node);
    return 0;
}

int fs_read(const char *name, void *buffer, uint32_t buf_size) {
    if (!name || !buffer)
        return -1;

    uint32_t id = lookup(name, current_dir);

    if (id == 0)
        return -1;

    fs_inode_t node;
    inode_read(id, &node);

    if (node.is_directory)
        return -2;

    uint32_t to_read = node.size;

    if (to_read > buf_size)
        to_read = buf_size;

    uint32_t pos = 0;

    for (uint32_t b = 0; b < FS_MAX_BLOCKS && pos < to_read; b++) {
        if (node.blocks[b] == 0)
            break;

        uint8_t sector[512];
        ata_read_sector(node.blocks[b], sector);
        uint32_t chunk = to_read - pos;

        if (chunk > 511)
            chunk = 511;

        for (uint32_t j = 0; j < chunk; j++) {
            ((uint8_t *)buffer)[pos + j] = sector[j];
        }

        pos += chunk;
    }
    return (int)pos;
}

int fs_append(const char *name, const void *data) {
    if (!name || !data)
        return -1;

    uint32_t id = lookup(name, current_dir);

    if (id == 0)
        return -1;
    fs_inode_t node;
    inode_read(id, &node);
    if (node.is_directory)
        return -2;

    uint32_t extra = (uint32_t)strlen((const char *)data);
    uint32_t new_size = node.size + extra;
    uint32_t max_size = FS_MAX_BLOCKS * 511;

    if (new_size > max_size)
        new_size = max_size;
    extra = new_size - node.size;

    if (extra == 0)
        return 0;
    uint8_t scratch[FS_MAX_BLOCKS * 512];
    uint32_t pos = 0;

    for (uint32_t b = 0; b < FS_MAX_BLOCKS && pos < node.size; b++) {
        if (node.blocks[b] == 0)
            break;
        uint8_t sector[512];
        ata_read_sector(node.blocks[b], sector);
        uint32_t chunk = node.size - pos;
        if (chunk > 511)
            chunk = 511;
        for (uint32_t j = 0; j < chunk; j++) {
            scratch[pos + j] = sector[j];
        }
        pos += chunk;
    }
    for (uint32_t i = 0; i < extra; i++) {
        scratch[node.size + i] = ((const uint8_t *)data)[i];
    }
    return fs_write_binary(name, scratch, new_size);
}

int fs_delete(const char *name) {
    uint32_t id = lookup(name, current_dir);
    if (id == 0)
        return -1;
    fs_inode_t empty;
    memset(&empty, 0, sizeof(empty));
    inode_write(id, &empty);
    return 0;
}

int fs_rename(const char *old_name, const char *new_name) {
    uint32_t id = lookup(old_name, current_dir);
    if (id == 0)
        return -1;
    if (lookup(new_name, current_dir)) {
        return -2;
    }
    fs_inode_t node;
    inode_read(id, &node);
    strncpy(node.name, new_name, FS_NAME_MAX);
    node.name[FS_NAME_MAX - 1] = '\0';
    inode_write(id, &node);
    return 0;
}

int fs_mkdir(const char *name) {
    if (lookup(name, current_dir)) {
        return -1;
    }
    uint32_t id = inode_alloc();
    if (id == 0)
        return -2;

    fs_inode_t node;
    memset(&node, 0, sizeof(node));
    strncpy(node.name, name, FS_NAME_MAX);
    node.name[FS_NAME_MAX - 1] = '\0';
    node.parent = current_dir;
    node.is_directory = 1;
    node.size = 0;
    inode_write(id, &node);
    return 0;
}

int fs_change_dir(const char *name) {
    if (name[0] == '/' && name[1] == '\0') {
        current_dir = 0;
        return 0;
    }

    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        if (current_dir != 0) {
            fs_inode_t node;
            inode_read(current_dir, &node);
            current_dir = node.parent;
        }
        return 0;
    }
    if (name[0] == '.' && name[1] == '\0') {
        return 0;
    }
    uint32_t id = lookup(name, current_dir);
    if (id == 0)
        return -1;
    fs_inode_t node;
    inode_read(id, &node);
    if (!node.is_directory)
        return -2;
    current_dir = id;
    return 0;
}

int fs_list(fs_inode_t *out, int max_out) {
    int count = 0;
    for (uint32_t id = 1; id <= FS_MAX_INODES && count < max_out; id++) {
        fs_inode_t node;
        inode_read(id, &node);
        if (node.name[0] == '\0')
            continue;
        if (node.parent != current_dir)
            continue;
        out[count++] = node;
    }
    return count;
}

int fs_stat(const char *name, fs_inode_t *out) {
    if (!out)
        return -1;
    uint32_t id = lookup(name, current_dir);
    if (id == 0)
        return -1;
    inode_read(id, out);
    return 0;
}

const char *fs_get_pwd(void) {
    static char path[256];
    static char tmp[256];
    if (current_dir == 0)
        return "/";
    int pos = sizeof(tmp) - 1;
    tmp[pos] = '\0';
    uint32_t dir = current_dir;
    while (dir != 0) {
        fs_inode_t node;
        inode_read(dir, &node);
        int len = (int)strlen(node.name);
        for (int i = len - 1; i >= 0; i--) {
            tmp[--pos] = node.name[i];
        }
        tmp[--pos] = '/';
        dir = node.parent;
    }
    strncpy(path, &tmp[pos], sizeof(path));
    path[sizeof(path) - 1] = '\0';
    return path;
}

void fs_disk_stats(uint32_t *used_sectors, uint32_t *total_sectors) {
    uint32_t used = FS_DATA_START;

    for (uint32_t id = 1; id <= FS_MAX_INODES; id++) {
        fs_inode_t node;
        inode_read(id, &node);

        if (node.name[0] == '\0')
            continue;

        for (int b = 0; b < FS_MAX_BLOCKS; b++) {
            if (node.blocks[b] >= used)
                used = node.blocks[b] + 1;
        }
    }

    if (used_sectors)
        *used_sectors = used;
    if (total_sectors)
        *total_sectors = mounted_total_sectors;
}
#include "ata.h"
#include "../../io.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECCOUNT0   0x1F2
#define ATA_LBA0        0x1F3
#define ATA_LBA1        0x1F4
#define ATA_LBA2        0x1F5
#define ATA_HDDEVSEL    0x1F6
#define ATA_COMMAND     0x1F7
#define ATA_STATUS      0x1F7

#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_FLUSH   0xE7

#define ATA_SR_ERR      0x01
#define ATA_SR_DRQ      0x08
#define ATA_SR_DF       0x20
#define ATA_SR_BSY      0x80

#define ATA_TIMEOUT     1000000


static void ata_400ns_delay(void)
{
    inb(ATA_STATUS);
    inb(ATA_STATUS);
    inb(ATA_STATUS);
    inb(ATA_STATUS);
}


static int ata_wait_bsy(void)
{
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = inb(ATA_STATUS);

        if (status & ATA_SR_ERR)
            return -2;

        if (status & ATA_SR_DF)
            return -3;

        if (!(status & ATA_SR_BSY))
            return 0;
    }

    return -1;
}

static int ata_wait_drq(void)
{
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = inb(ATA_STATUS);

        if (status & ATA_SR_ERR)
            return -2;

        if (status & ATA_SR_DF)
            return -3;

        if (status & ATA_SR_DRQ)
            return 0;
    }

    return -1;
}

static int ata_flush(void)
{
    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    return ata_wait_bsy();
}


int ata_read_sector(uint32_t lba, void *buffer)
{
    uint16_t *buf = (uint16_t *)buffer;
    outb(ATA_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    ata_400ns_delay();
    outb(ATA_SECCOUNT0, 1);
    outb(ATA_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_READ);

    int result = ata_wait_bsy();

    if (result != 0)
        return result;

    result = ata_wait_drq();

    if (result != 0)
        return result;

    for (int i = 0; i < 256; i++)
        buf[i] = inw(ATA_DATA);

    return 0;
}


int ata_write_sector(uint32_t lba, const void *buffer)
{
    const uint16_t *buf = (const uint16_t *)buffer;

    outb(ATA_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));

    ata_400ns_delay();

    outb(ATA_SECCOUNT0, 1);

    outb(ATA_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (uint8_t)((lba >> 16) & 0xFF));

    outb(ATA_COMMAND, ATA_CMD_WRITE);

    int result = ata_wait_bsy();

    if (result != 0)
        return -10 + result;   // -11 timeout, -12 error, etc.

    result = ata_wait_drq();

    if (result != 0)
        return -20 + result;   // -21 timeout, -22 error

    for (int i = 0; i < 256; i++)
        outw(ATA_DATA, buf[i]);

    result = ata_flush();

    if (result != 0)
        return -60 + result;   // -61 timeout, -62 error

    return 0;
}
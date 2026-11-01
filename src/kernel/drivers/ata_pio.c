#include "ata_pio.h"

#include "../kklibc/kklibc.h"
#include "lowlevel_io.h"

#define ATA_CONTROL 0x3f6
#define ATA_TIMEOUT 1000000
ata_disk_info_t ata_disks[2];

static void ata_delay(void) {
    for (int i = 0; i < 4; ++i) {
        port_byte_in(ATA_CONTROL);
    }
}

static int ata_poll(int data) {
    for (u32 i = 0; i < ATA_TIMEOUT; ++i) {
        u8 s = port_byte_in(ATA_PRIMARY_STATUS);
        if (s == 0 || s == 0xff) {
            return -3;
        }
        if (s & ATA_SR_BSY) {
            continue;
        }
        if (s & (ATA_SR_ERR | ATA_SR_DF)) {
            return -1;
        }
        if (!data || (s & ATA_SR_DRQ)) {
            return 0;
        }
    }
    return -2;
}

static void ata_select(u8 drive, u32 lba) {
    port_byte_out(ATA_PRIMARY_DRIVE_SEL, drive | 0x40 | ((lba >> 24) & 15));
    ata_delay();
}

int ata_pio_wait(void) {
    return ata_poll(0);
}

int ata_pio_identify(u8 drive, ata_disk_info_t* info) {
    if (!info || (drive != ATA_MASTER && drive != ATA_SLAVE)) {
        return -1;
    }
    memset(info, 0, sizeof(*info));
    port_byte_out(ATA_PRIMARY_DRIVE_SEL, drive);
    ata_delay();
    port_byte_out(ATA_PRIMARY_SECTOR_CNT, 0);
    port_byte_out(ATA_PRIMARY_LBA_LOW, 0);
    port_byte_out(ATA_PRIMARY_LBA_MID, 0);
    port_byte_out(ATA_PRIMARY_LBA_HIGH, 0);
    port_byte_out(ATA_PRIMARY_CMD, ATA_CMD_IDENTIFY);
    ata_delay();
    if (ata_poll(1) != 0) {
        return -1; /* also rejects ATAPI */
    }
    u16 words[256];
    for (int i = 0; i < 256; ++i) {
        words[i] = port_word_in(ATA_PRIMARY_DATA);
    }
    ata_delay();
    if (!(words[49] & 0x200)) {
        return -1; /* LBA required */
    }
    info->signature = words[0];
    info->capabilities = words[49];
    info->command_sets = (u32)words[82] | ((u32)words[83] << 16);
    info->size = (u32)words[60] | ((u32)words[61] << 16);
    info->type = ATA_DISK_PATA;
    for (int i = 0; i < 20; ++i) {
        info->model[2 * i] = words[27 + i] >> 8;
        info->model[2 * i + 1] = words[27 + i] & 255;
    }
    info->model[40] = 0;
    return 0;
}

void ata_pio_init(void) {
    /* Polling driver: do not generate unhandled ATA interrupts. */
    port_byte_out(ATA_CONTROL, 2);
    for (int i = 0; i < 2; ++i) {
        if (ata_pio_identify(i ? ATA_SLAVE : ATA_MASTER, &ata_disks[i]) == 0) {
            printf("Drive %d: %s %d MB\n", i, ata_disks[i].model, ata_disks[i].size / 2048);
        } else {
            printf("Drive %d: no ATA disk\n", i);
        }
    }
}

static int ata_transfer(u8 drive, u32 lba, u8 num, u16* buffer, int write) {
    if (!num) {
        return 0;
    }
    if (!buffer || (drive != ATA_MASTER && drive != ATA_SLAVE)) {
        return -1;
    }
    ata_disk_info_t* disk = &ata_disks[drive == ATA_SLAVE];
    if (!disk->size || lba >= 0x10000000 || num > 0x10000000 - lba || lba >= disk->size
        || num > disk->size - lba) {
        return -1;
    }
    ata_select(drive, lba);
    /* ERR can describe the previous command (e.g. probing an absent slave).
     * Before issuing a command only BSY matters; check ERR after issuing it. */
    u32 timeout = ATA_TIMEOUT;
    while (port_byte_in(ATA_PRIMARY_STATUS) & ATA_SR_BSY) {
        if (!--timeout) {
            return -2;
        }
    }
    port_byte_out(ATA_PRIMARY_SECTOR_CNT, num);
    port_byte_out(ATA_PRIMARY_LBA_LOW, lba & 255);
    port_byte_out(ATA_PRIMARY_LBA_MID, (lba >> 8) & 255);
    port_byte_out(ATA_PRIMARY_LBA_HIGH, (lba >> 16) & 255);
    port_byte_out(ATA_PRIMARY_CMD, write ? ATA_CMD_WRITE_PIO : ATA_CMD_READ_PIO);
    ata_delay();
    for (u32 sector = 0; sector < num; ++sector) {
        int result = ata_poll(1);
        if (result) {
            return result;
        }
        for (int i = 0; i < 256; ++i) {
            if (write) {
                port_word_out(ATA_PRIMARY_DATA, buffer[sector * 256 + i]);
            } else {
                buffer[sector * 256 + i] = port_word_in(ATA_PRIMARY_DATA);
            }
        }
        ata_delay();
    }
    if (ata_poll(0)) {
        return -1;
    }
    if (write) {
        port_byte_out(ATA_PRIMARY_CMD, ATA_CMD_CACHE_FLUSH);
        ata_delay();
        return ata_poll(0);
    }
    return 0;
}

int ata_pio_read_sectors(u8 drive, u32 lba, u8 num, u16* buffer) {
    return ata_transfer(drive, lba, num, buffer, 0);
}

int ata_pio_write_sectors(u8 drive, u32 lba, u8 num, u16* buffer) {
    return ata_transfer(drive, lba, num, buffer, 1);
}

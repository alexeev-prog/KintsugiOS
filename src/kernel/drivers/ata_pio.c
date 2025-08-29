#include "ata_pio.h"

#include "lowlevel_io.h"
#include "screen.h"

// Источник: https://wiki.osdev.org/ATA_PIO_Mode

static void ata_wait_ready() {
    while (port_byte_in(ATA_REG_STATUS) & ATA_SR_BSY)
        ;
}

static void ata_cache_flush() {
    ata_wait_ready();
    port_byte_out(ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_ready();
}

void ata_read_sectors(u32 lba, u32 sector_count, const u8* buffer) {
    ata_wait_ready();

    // Выбор диска (Master)
    port_byte_out(ATA_REG_DRIVE_SELECT, 0xE0 | ((lba >> 24) & 0x0F));

    // Задержка для стабилизации контроллера
    port_byte_out(ATA_REG_ERROR, 0x00);

    port_byte_out(ATA_REG_SECCOUNT, sector_count);    // Количество секторов для чтения
    port_byte_out(ATA_REG_LBA0, (u8)(lba & 0xFF));
    port_byte_out(ATA_REG_LBA1, (u8)((lba >> 8) & 0xFF));
    port_byte_out(ATA_REG_LBA2, (u8)((lba >> 16) & 0xFF));
    port_byte_out(ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

    ata_wait_ready();

    for (u8 sector = 0; sector < sector_count; sector++) {
        while (!(port_byte_in(ATA_REG_STATUS) & ATA_SR_DRQ))
            ;
        // ПРИМЕЧАНИЕ: При чтении слов в little-endian порядок байт сохраняется корректно
        rep_insw(
            ATA_REG_DATA, (void*)(&buffer[sector * SECTOR_BYTES]), SECTOR_WORDS);    // 256 слов (512 байт)
    }
}

void ata_write_sectors(u32 lba, u32 sector_count, const u8* buffer) {
    ata_wait_ready();

    // Выбор диска (Master)
    port_byte_out(ATA_REG_DRIVE_SELECT, 0xE0 | ((lba >> 24) & 0x0F));

    // Задержка для стабилизации контроллера
    port_byte_out(ATA_REG_ERROR, 0x00);

    port_byte_out(ATA_REG_SECCOUNT, (u8)sector_count);    // Количество секторов для записи
    port_byte_out(ATA_REG_LBA0, (u8)(lba & 0xFF));
    port_byte_out(ATA_REG_LBA1, (u8)((lba >> 8) & 0xFF));
    port_byte_out(ATA_REG_LBA2, (u8)((lba >> 16) & 0xFF));
    port_byte_out(ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);

    for (u8 sector = 0; sector < sector_count; sector++) {
        while (!(port_byte_in(ATA_REG_STATUS) & ATA_SR_DRQ))
            ;
        for (u32 sw = 0; sw < SECTOR_WORDS; sw++) {
            // Преобразование в big-endian для контроллера
            outsw(ATA_REG_DATA,
                  ((u16)buffer[sector * SECTOR_BYTES + sw * 2 + 1]) << 8
                      | (u16)(buffer[sector * SECTOR_BYTES + sw * 2]));
        }
    }

    ata_cache_flush();
}

void ata_select_drive(int is_master) {
    port_byte_out(ATA_REG_DRIVE_SELECT, is_master ? 0xA0 : 0xB0);
}

void ata_identify() {
    ata_select_drive(1);    // Выбор master-диска

    port_byte_out(ATA_REG_SECCOUNT, 0);
    port_byte_out(ATA_REG_LBA0, 0);
    port_byte_out(ATA_REG_LBA1, 0);
    port_byte_out(ATA_REG_LBA2, 0);
    port_byte_out(ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
}

int ata_wait() {
    while (1) {
        u8 status = port_byte_in(ATA_REG_STATUS);
        if (!(status & 0x80)) {
            return 1;    // Проверка бита BSY (занято)
        }
    }
}

void ata_read_buffer(u16* buffer) {
    // Чтение всего сектора: 256 слов, 512 байт
    for (int i = 0; i < 256; i++) {
        buffer[i] = port_word_in(ATA_REG_DATA);
    }
}

u32 ata_get_disk_size() {
    // 1024^2 = 1048576 (1 МиБ)
    u16 ata_buffer[256];
    ata_identify();
    ata_wait();
    ata_read_buffer(ata_buffer);
    u32 total_sectors = ((u32)ata_buffer[61] << 16) | ata_buffer[60];
    return (u32)total_sectors * 512;    // Конвертация в байты
}

void ata_read_blocks(u32 block_num, const u8* buffer, u32 count) {
    ata_read_sectors(block_num * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK * count, buffer);
}

// неэффективные методы: запись происходит поверх буфера без учета длины
void ata_write_blocks(u32 block_num, const u8* buffer, u32 count) {
    ata_write_sectors(block_num * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK * count, buffer);
}

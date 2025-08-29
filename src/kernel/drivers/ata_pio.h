#ifndef ATA_PIO_H
#define ATA_PIO_H

#include "../kklibc/ctypes.h"

#define SECTOR_BYTES 512
#define SECTOR_WORDS 256

// ATA I/O Порты
#define ATA_PRIMARY_IO_BASE 0x1F0
#define ATA_PRIMARY_CTRL_BASE 0x3F6
#define ATA_REG_DATA (ATA_PRIMARY_IO_BASE + 0)
#define ATA_REG_ERROR (ATA_PRIMARY_IO_BASE + 1)
#define ATA_REG_SECCOUNT (ATA_PRIMARY_IO_BASE + 2)
#define ATA_REG_LBA0 (ATA_PRIMARY_IO_BASE + 3)
#define ATA_REG_LBA1 (ATA_PRIMARY_IO_BASE + 4)
#define ATA_REG_LBA2 (ATA_PRIMARY_IO_BASE + 5)
#define ATA_REG_DRIVE_SELECT (ATA_PRIMARY_IO_BASE + 6)
#define ATA_REG_COMMAND (ATA_PRIMARY_IO_BASE + 7)
#define ATA_REG_STATUS (ATA_PRIMARY_IO_BASE + 7)
#define ATA_REG_ALTSTATUS (ATA_PRIMARY_CTRL_BASE + 0)
#define ATA_REG_CONTROL (ATA_PRIMARY_CTRL_BASE + 0)

// ATA Команда
#define ATA_CMD_READ_SECTORS 0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY 0xEC

// Статус флаги
#define ATA_SR_BSY 0x80    // Занятость
#define ATA_SR_DRDY 0x40    // Готовность драйва
#define ATA_SR_DRQ 0x08    // Запрос на готовность данных

// Типы драйвов
#define ATA_MASTER 0xA0
#define ATA_SLAVE 0xB0

#define SECTORS_PER_BLOCK 0x8    // для ФС блоки по 4KB
#define BLOCK_BYTES SECTORS_PER_BLOCK* SECTOR_BYTES

/**
 * @brief Чтение секторов
 *
 * @param lba LBA
 * @param sector_count количество секторов
 * @param buffer буфер
 **/
void ata_read_sectors(u32 lba, u32 sector_count, const u8* buffer);

/**
 * @brief Запись секторов
 *
 * @param lba LBA
 * @param sector_count количество секторов
 * @param buffer буфер
 **/
void ata_write_sectors(u32 lba, u32 sector_count, const u8* buffer);

/**
 * @brief Чтение буфера
 *
 * @param buffer буфер
 **/
void ata_read_buffer(u16* buffer);

/**
 * @brief Получение размера диска
 *
 * @return u32
 **/
u32 ata_get_disk_size();

/**
 * @brief Чтение блоков
 *
 * @param block_num количество блоков
 * @param buffer буфер
 * @param count число
 **/
void ata_read_blocks(u32 block_num, const u8* buffer, u32 count);

/**
 * @brief Запись блоков
 *
 * @param block_num количество блоков
 * @param buffer буфер
 * @param count число
 **/
void ata_write_blocks(u32 block_num, const u8* buffer, u32 count);

#endif    // ATA_PIO_H

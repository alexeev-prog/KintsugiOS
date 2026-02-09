/*------------------------------------------------------------------------------
 *  Kintsugi OS Drivers source code
 *  File: kernel/drivers/ata_pio.h
 *  Title: Заголовочный файл драйвера ATA PIO
 *  Author: alexeev-prog
 *  License: MIT License
 * ------------------------------------------------------------------------------
 *  Description: Драйвер для работы с жесткими дисками через PIO mode.
 * ---------------------------------------------------------------------------*/

#ifndef ATA_PIO_H
#define ATA_PIO_H

#include "../kklibc/ctypes.h"

// Порт Primary ATA канала
#define ATA_PRIMARY_DATA 0x1F0
#define ATA_PRIMARY_ERROR 0x1F1
#define ATA_PRIMARY_SECTOR_CNT 0x1F2
#define ATA_PRIMARY_LBA_LOW 0x1F3
#define ATA_PRIMARY_LBA_MID 0x1F4
#define ATA_PRIMARY_LBA_HIGH 0x1F5
#define ATA_PRIMARY_DRIVE_SEL 0x1F6
#define ATA_PRIMARY_STATUS 0x1F7
#define ATA_PRIMARY_CMD 0x1F7

// Статусные биты регистра STATUS
#define ATA_SR_BSY 0x80    // Drive busy
#define ATA_SR_DRDY 0x40    // Drive ready
#define ATA_SR_DF 0x20    // Drive write fault
#define ATA_SR_DSC 0x10    // Drive seek complete
#define ATA_SR_DRQ 0x08    // Data request ready
#define ATA_SR_CORR 0x04    // Corrected data
#define ATA_SR_IDX 0x02    // Index
#define ATA_SR_ERR 0x01    // Error

// Типы устройств
#define ATA_MASTER 0xA0
#define ATA_SLAVE 0xB0

// Команды
#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_READ_PIO_EXT 0x24
#define ATA_CMD_READ_DMA 0xC8
#define ATA_CMD_READ_DMA_EXT 0x25
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_WRITE_PIO_EXT 0x34
#define ATA_CMD_WRITE_DMA 0xCA
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA
#define ATA_CMD_IDENTIFY 0xEC

// Типы дисков
#define ATA_DISK_UNKNOWN 0
#define ATA_DISK_PATA 1
#define ATA_DISK_SATA 2
#define ATA_DISK_PATAPI 3
#define ATA_DISK_SATAPI 4

/* Структура для информации о диске */
typedef struct {
    u16 type;
    u16 signature;
    u16 capabilities;
    u32 command_sets;
    u32 size;    // в секторах
    char model[41];
} ata_disk_info_t;

/**
 * @brief Инициализация драйвера ATA PIO
 */
void ata_pio_init();

/**
 * @brief Определение типа и параметров диска
 * @param drive Тип диска (ATA_MASTER/ATA_SLAVE)
 * @param info Указатель на структуру для информации
 * @return 0 в случае успеха, код ошибки в противном случае
 */
int ata_pio_identify(u8 drive, ata_disk_info_t* info);

/**
 * @brief Чтение секторов с диска
 * @param drive Тип диска (ATA_MASTER/ATA_SLAVE)
 * @param lba Начальный LBA адрес
 * @param num Количество секторов для чтения
 * @param buffer Буфер для данных
 * @return 0 в случае успеха, код ошибки в противном случае
 */
int ata_pio_read_sectors(u8 drive, u32 lba, u8 num, u16* buffer);

/**
 * @brief Запись секторов на диск
 * @param drive Тип диска (ATA_MASTER/ATA_SLAVE)
 * @param lba Начальный LBA адрес
 * @param num Количество секторов для записи
 * @param buffer Буфер с данными
 * @return 0 в случае успеха, код ошибки в противном случае
 */
int ata_pio_write_sectors(u8 drive, u32 lba, u8 num, u16* buffer);

/**
 * @brief Ожидание готовности диска
 * @return 0 если диск готов, код ошибки в противном случае
 */
int ata_pio_wait();

#endif    // ATA_PIO_H

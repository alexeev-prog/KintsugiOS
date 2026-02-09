/*------------------------------------------------------------------------------
 *  Kintsugi OS Drivers source code
 *  File: kernel/drivers/ata_pio.c
 *  Title: Драйвер ATA PIO
 *  Author: alexeev-prog
 *  License: MIT License
 * ------------------------------------------------------------------------------
 *  Description: Реализация драйвера для работы с жесткими дисками
 * через PIO mode. ATA PIO (Programmed input/output - программный ввод/вывод) -
 * это режим в интерфейсе ATA, который определяет скорость обмена данными
 * с винчестером. В этом режиме управлением считыванием данных с диска
 * занимается центральный процессор, что приводит к повышенной нагрузке на него.
 * ATA (AT Attachment) - интерфейс, используемый для подключения жёстких
 * дисков и оптических накопителей к компьютерам.
 * PIO (Programmed Input/Output) — режим программируемого ввода-вывода, который
 * определяет скорость обмена данными с жёстким диском. Существует несколько
 * режимов PIO, которые различаются максимальными скоростями пакетной передачи
 * данных. Например, для PIO mode 4 скорость обмена достигает 16,6 МБ/с.
 * В Kintsugi OS как можно увидеть из bootloader/diskload.asm используется LBA.
 * LBA (Logical Block Adressing) — линейная адресация через логический адрес
 * блока. Режим LBA использует линейную адресацию секторов, начиная с сектора
 * 1, головки 0, цилиндра 0 (LBA 0) и заканчивая последним физическим
 * сектором диска.
 * ---------------------------------------------------------------------------*/

#include "ata_pio.h"

#include "../kklibc/kklibc.h"
#include "lowlevel_io.h"
#include "screen.h"

// внутреннее API ядра
static void ata_pio_select_drive(u8 drive);
static void ata_pio_set_lba(u32 lba, u8 drive);

// глобал переменные для хранения информации о дисках
ata_disk_info_t ata_disks[2];    // 0 - master, 1 - slave

// внешнее api ядра

void ata_pio_init() {
    for (int i = 0; i < 2; i++) {
        // Если все ОК, то при запуске в QEMU мы увидим мастер драйв и не увидим slave-драйв.
        u8 drive = (i == 0) ? ATA_MASTER : ATA_SLAVE;

        // чекаем наличие устройства
        ata_pio_select_drive(drive);
        u8 status = port_byte_in(ATA_PRIMARY_STATUS);

        if (status == 0xFF) {
            printf("Drive %d: no device connected\n", i);
            continue;
        }

        int result = ata_pio_identify(drive, &ata_disks[i]);

        if (result == 0) {
            printf("Drive %d: %s %d MB\n", i, ata_disks[i].model, (ata_disks[i].size * 512) / (1024 * 1024));
        } else {
            printf("Drive %d: identification failed (error %d)\n", i, result);
        }
    }
}

static void ata_pio_select_drive(u8 drive) {
    // выюор диска (master/slave) и режима LBA
    port_byte_out(ATA_PRIMARY_DRIVE_SEL, drive | 0x40);    // LBA mode
    // задержка для стабильности
    for (int i = 0; i < 4; i++) {
        port_byte_in(ATA_PRIMARY_STATUS);
    }
}

static void ata_pio_set_lba(u32 lba, u8 drive) {
    // установочка LBA адреса
    port_byte_out(ATA_PRIMARY_LBA_LOW, (u8)(lba & 0xFF));
    port_byte_out(ATA_PRIMARY_LBA_MID, (u8)((lba >> 8) & 0xFF));
    port_byte_out(ATA_PRIMARY_LBA_HIGH, (u8)((lba >> 16) & 0xFF));
    port_byte_out(ATA_PRIMARY_DRIVE_SEL, drive | 0x40 | ((lba >> 24) & 0x0F));
}

int ata_pio_wait() {
    // ожидаем снятия флага BSY и установки флага DRDY
    int timeout = 100000;    // таймаут для предотвращения зависания

    while (timeout-- > 0) {
        u8 status = port_byte_in(ATA_PRIMARY_STATUS);

        // коли BSY снят и DRDY установлен - диск готов
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) {
            return 0;
        }

        // произошла ошибка
        if (status & ATA_SR_ERR) {
            return -1;
        }
    }

    return -2;    // таймаут
}

int ata_pio_identify(u8 drive, ata_disk_info_t* info) {
    ata_pio_select_drive(drive);

    u8 status = port_byte_in(ATA_PRIMARY_STATUS);
    if (status == 0xFF) {
        return -3;
    }

    if (ata_pio_wait() != 0) {
        return -1;
    }

    // отправляем IDENTIFY
    port_byte_out(ATA_PRIMARY_CMD, ATA_CMD_IDENTIFY);

    if (ata_pio_wait() != 0) {
        return -2;
    }

    // процесс чтения данных (256 слов = 512 байт)
    u16 buffer[256];
    for (int i = 0; i < 256; i++) {
        buffer[i] = port_word_in(ATA_PRIMARY_DATA);
    }

    // анализируем полученные данные
    info->signature = buffer[0];
    info->capabilities = buffer[49];
    info->command_sets = *((u32*)&buffer[83]);

    // получаем модель диска
    for (int i = 0; i < 20; i++) {
        info->model[i * 2] = (char)(buffer[27 + i] >> 8);
        info->model[i * 2 + 1] = (char)(buffer[27 + i] & 0xFF);
    }
    info->model[40] = '\0';

    // размер диска в секторах
    if (info->command_sets & 0x40000000) {
        // 48-bit LBA
        info->size = *((u32*)&buffer[100]);
    } else {
        // 28-bit LBA
        info->size = *((u32*)&buffer[60]);
    }

    // тип диска
    if (buffer[0] == 0x8489 || buffer[0] == 0x8449) {
        info->type = ATA_DISK_PATAPI;
    } else {
        info->type = ATA_DISK_PATA;
    }

    return 0;
}

int ata_pio_read_sectors(u8 drive, u32 lba, u8 num, u16* buffer) {
    if (num == 0) {
        return 0;
    }

    // выбор диск
    ata_pio_select_drive(drive);

    port_byte_out(ATA_PRIMARY_SECTOR_CNT, num);

    // сетаем LBA адрес
    ata_pio_set_lba(lba, drive);

    // сендим команду чтения
    port_byte_out(ATA_PRIMARY_CMD, ATA_CMD_READ_PIO);

    // Читаем сектора
    for (int sector = 0; sector < num; sector++) {
        // Ждем готовности данных
        if (ata_pio_wait() != 0) {
            return -1;
        }

        // Читаем сектор (256 слов = 512 байт)
        for (int i = 0; i < 256; i++) {
            buffer[sector * 256 + i] = port_word_in(ATA_PRIMARY_DATA);
        }

        // ждем между секторами
        for (int i = 0; i < 4; i++) {
            port_byte_in(ATA_PRIMARY_STATUS);
        }
    }

    return 0;
}

int ata_pio_write_sectors(u8 drive, u32 lba, u8 num, u16* buffer) {
    if (num == 0) {
        return 0;
    }

    // диск
    ata_pio_select_drive(drive);

    // количество секторов
    port_byte_out(ATA_PRIMARY_SECTOR_CNT, num);

    // LBA адрес
    ata_pio_set_lba(lba, drive);

    // команда записи
    port_byte_out(ATA_PRIMARY_CMD, ATA_CMD_WRITE_PIO);

    // врайтим сектора
    for (int sector = 0; sector < num; sector++) {
        // ждем готовности к приему данных
        if (ata_pio_wait() != 0) {
            return -1;
        }

        // сектор (256 слов = 512 байт)
        for (int i = 0; i < 256; i++) {
            port_word_out(ATA_PRIMARY_DATA, buffer[sector * 256 + i]);
        }

        // оиждаем завершения записи
        if (ata_pio_wait() != 0) {
            return -2;
        }

        port_byte_out(ATA_PRIMARY_CMD, ATA_CMD_CACHE_FLUSH);
        ata_pio_wait();    //
    }

    return 0;
}

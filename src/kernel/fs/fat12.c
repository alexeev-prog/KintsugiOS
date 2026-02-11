/*------------------------------------------------------------------------------
 *  Kintsugi OS FileSystems source code
 *  File: fs/fat12.c
 *  Title: Заголовочный файл fat12.c
 *  Last Change Date: 30 October 2023, 12:28 (UTC)
 *  Author: alexeev-prog
 *  License: GNU GPL v3
 * ------------------------------------------------------------------------------
 *	Description: Файл исходного кода файловой системы Fat12
 * ----------------------------------------------------------------------------*/

#include "fat12.h"

#include "../drivers/ata_pio.h"
#include "../drivers/screen.h"
#include "../kklibc/mem.h"
#include "../kklibc/stdio.h"
#include "../kklibc/stdlib.h"

// TODO: Сейчас Fat12 в режиме ReadOnly, реализовать Read+Write

static fat12_context_t ctx;
static fat12_boot_sector_t boot_sector;

/* Вспомогательные функции */
static void format_filename(const char* input, char* output);
static u16 fat12_get_fat_entry(u32 cluster);
static void fat12_set_fat_entry(u32 cluster, u16 value);
static u16 fat12_find_free_cluster(void);
static void fat12_free_cluster_chain(u16 start_cluster);
static int fat12_find_free_dir_entry(u32* sector_out, u32* offset_out);
static void fat12_sync_fat(void);

/* -------------------------------------------------------------------------- */
/* ИНИЦИАЛИЗАЦИЯ И ОЧИСТКА                                                   */
/* -------------------------------------------------------------------------- */

void fat12_cleanup(void) {
    if (ctx.fat_buffer) {
        // Перед освобождением, если FAT была изменена, нужно записать на диск
        if (ctx.fat_buffer_loaded == 2) {    // 2 = изменена
            fat12_sync_fat();
        }
        kfree(ctx.fat_buffer);
        ctx.fat_buffer = NULL;
        ctx.fat_buffer_loaded = 0;
    }
}

void fat12_init(void) {
    printf("Initializing FAT12...\n");

    if (ata_pio_read_sectors(ATA_MASTER, 0, 1, (u16*)&boot_sector) != 0) {
        printf_colored("Cannot read boot sector!\n", RED_ON_BLACK);
        return;
    }

    u16* sig = (u16*)((u8*)&boot_sector + 510);
    if (*sig != 0xAA55) {
        printf_colored("Invalid boot signature\n", RED_ON_BLACK);
        return;
    }

    ctx.fat_start_sector = boot_sector.reserved_sectors;
    ctx.fat_size_sectors = boot_sector.sectors_per_fat;
    ctx.root_dir_start_sector = ctx.fat_start_sector + (boot_sector.fat_count * ctx.fat_size_sectors);

    ctx.root_dir_size_sectors = (boot_sector.root_entries * 32 + 511) / 512;
    ctx.data_start_sector = ctx.root_dir_start_sector + ctx.root_dir_size_sectors;

    u32 total_data_sectors = boot_sector.total_sectors - ctx.data_start_sector;
    ctx.total_clusters = total_data_sectors / boot_sector.sectors_per_cluster;

    ctx.fat_buffer = NULL;
    ctx.fat_buffer_loaded = 0;

    printf(
        "FAT12 loaded: sectors %d-%d (size: %d sectors)\n",
        ctx.fat_start_sector,
        ctx.fat_start_sector + ctx.fat_size_sectors - 1,
        ctx.fat_size_sectors);
}

/* -------------------------------------------------------------------------- */
/* ИНФОРМАЦИЯ И СПИСОК ФАЙЛОВ                                                */
/* -------------------------------------------------------------------------- */

void print_fat12_info(void) {
    printf("FAT12 Boot Sector Info:\n");
    printf("  Bytes per sector: %d\n", boot_sector.bytes_per_sector);
    printf("  Sectors per cluster: %d\n", boot_sector.sectors_per_cluster);
    printf("  Reserved sectors: %d\n", boot_sector.reserved_sectors);
    printf("  FAT count: %d\n", boot_sector.fat_count);
    printf("  Root entries: %d\n", boot_sector.root_entries);
    printf("  Total sectors: %d\n", boot_sector.total_sectors);
    printf("  Sectors per FAT: %d\n", boot_sector.sectors_per_fat);

    printf("\nFAT12 Calculated Offsets:\n");
    printf(
        "  FAT: sectors %d-%d (size: %d sectors)\n",
        ctx.fat_start_sector,
        ctx.fat_start_sector + ctx.fat_size_sectors - 1,
        ctx.fat_size_sectors);
    printf(
        "  Root: sectors %d-%d (size: %d sectors)\n",
        ctx.root_dir_start_sector,
        ctx.root_dir_start_sector + ctx.root_dir_size_sectors - 1,
        ctx.root_dir_size_sectors);
    printf("  Data: starts at sector %d\n", ctx.data_start_sector);
    printf("  Total clusters: %d\n", ctx.total_clusters);
    printf("  Total data sectors: %d\n", boot_sector.total_sectors - ctx.data_start_sector);
}

void fat12_list_root(void) {
    u32 buffer_size = ctx.root_dir_size_sectors * 512;
    u8* buffer = (u8*)kmalloc(buffer_size);

    if (!buffer) {
        printf("No memory for root dir\n");
        return;
    }

    if (ata_pio_read_sectors(ATA_MASTER, ctx.root_dir_start_sector, ctx.root_dir_size_sectors, (u16*)buffer)
        != 0) {
        printf("Cannot read root dir\n");
        kfree(buffer);
        return;
    }

    printf("Root directory:\n");
    printf("===============\n");

    for (int i = 0; i < boot_sector.root_entries; i++) {
        fat12_dir_entry_t* entry = (fat12_dir_entry_t*)(buffer + i * 32);

        if (entry->filename[0] == 0x00) {
            break;
        }
        if (entry->filename[0] == 0xE5) {
            continue;
        }
        if (entry->attributes & 0x08) {
            continue;
        }

        char name[13] = { 0 };
        int pos = 0;

        for (int j = 0; j < 8 && entry->filename[j] != ' '; j++) {
            name[pos++] = entry->filename[j];
        }

        if (entry->extension[0] != ' ') {
            name[pos++] = '.';
            for (int j = 0; j < 3 && entry->extension[j] != ' '; j++) {
                name[pos++] = entry->extension[j];
            }
        }

        printf("%-12s  %6d bytes  cluster: %d\n", name, entry->file_size, entry->first_cluster);
    }

    kfree(buffer);
}

/* -------------------------------------------------------------------------- */
/* ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ                                                   */
/* -------------------------------------------------------------------------- */

static void format_filename(const char* input, char* output) {
    // Форматируем имя файла в формат 8.3
    memset(output, ' ', 11);    // Заполняем пробелами

    const char* dot = strchr(input, '.');

    if (dot) {
        // Копируем имя (до точки)
        int name_len = dot - input;
        if (name_len > 8) {
            name_len = 8;
        }
        for (int i = 0; i < name_len; i++) {
            output[i] = toupper(input[i]);
        }

        // Копируем расширение (после точки)
        const char* ext = dot + 1;
        int ext_len = strlen(ext);
        if (ext_len > 3) {
            ext_len = 3;
        }
        for (int i = 0; i < ext_len; i++) {
            output[8 + i] = toupper(ext[i]);
        }
    } else {
        // Без расширения
        int name_len = strlen(input);
        if (name_len > 8) {
            name_len = 8;
        }
        for (int i = 0; i < name_len; i++) {
            output[i] = toupper(input[i]);
        }
    }
}

static void format_filename_from_entry(fat12_dir_entry_t* entry, char* out) {
    int i = 0;

    for (int j = 0; j < 8 && entry->filename[j] != ' '; j++) {
        out[i++] = entry->filename[j];
    }

    if (entry->extension[0] != ' ') {
        out[i++] = '.';
        for (int j = 0; j < 3 && entry->extension[j] != ' '; j++) {
            out[i++] = entry->extension[j];
        }
    }

    out[i] = '\0';
}

static void load_fat_if_needed(void) {
    if (!ctx.fat_buffer_loaded) {
        u32 fat_size = ctx.fat_size_sectors * 512;
        ctx.fat_buffer = (u8*)kmalloc(fat_size);

        if (!ctx.fat_buffer) {
            printf("No memory for FAT\n");
            return;
        }

        if (ata_pio_read_sectors(ATA_MASTER, ctx.fat_start_sector, ctx.fat_size_sectors, (u16*)ctx.fat_buffer)
            != 0) {
            kfree(ctx.fat_buffer);
            ctx.fat_buffer = NULL;
            printf("Cannot read FAT\n");
            return;
        }

        ctx.fat_buffer_loaded = 1;    // 1 = загружена, не изменена
    }
}

static u16 fat12_get_fat_entry(u32 cluster) {
    if (cluster < 2) {
        return 0xFFF;
    }

    if (cluster - 2 >= ctx.total_clusters) {
        return 0xFFF;
    }

    load_fat_if_needed();

    if (!ctx.fat_buffer) {
        return 0xFFF;
    }

    u32 offset = cluster * 3 / 2;

    u32 fat_size = ctx.fat_size_sectors * 512;
    if (offset + 1 >= fat_size) {
        return 0xFFF;
    }

    u16 entry = *((u16*)(ctx.fat_buffer + offset));

    if (cluster & 1) {
        entry >>= 4;
    } else {
        entry &= 0x0FFF;
    }

    return entry;
}

static void fat12_set_fat_entry(u32 cluster, u16 value) {
    if (cluster < 2) {
        return;
    }

    if (cluster - 2 >= ctx.total_clusters) {
        return;
    }

    load_fat_if_needed();

    if (!ctx.fat_buffer) {
        return;
    }

    u32 offset = cluster * 3 / 2;
    u16* fat_entry = (u16*)(ctx.fat_buffer + offset);

    if (cluster & 1) {
        // Нечетный кластер: младшие 4 бита остаются, старшие 12 меняем
        *fat_entry = (*fat_entry & 0x000F) | (value << 4);
    } else {
        // Четный кластер: старшие 4 бита остаются, младшие 12 меняем
        *fat_entry = (*fat_entry & 0xF000) | (value & 0x0FFF);
    }

    // Помечаем FAT как измененную
    ctx.fat_buffer_loaded = 2;    // 2 = изменена
}

static u16 fat12_find_free_cluster(void) {
    load_fat_if_needed();

    if (!ctx.fat_buffer) {
        return 0;
    }

    // Начинаем с кластера 2 (первые два кластера зарезервированы)
    for (u16 cluster = 2; cluster < ctx.total_clusters + 2; cluster++) {
        u16 entry = fat12_get_fat_entry(cluster);
        if (entry == 0x000) {
            return cluster;
        }
    }

    return 0;    // Нет свободных кластеров
}

static void fat12_free_cluster_chain(u16 start_cluster) {
    u16 current = start_cluster;

    while (current >= 2 && current < 0xFF8) {
        u16 next = fat12_get_fat_entry(current);
        fat12_set_fat_entry(current, 0x000);    // Освобождаем кластер
        current = next;
    }
}

static void fat12_sync_fat(void) {
    if (!ctx.fat_buffer || ctx.fat_buffer_loaded != 2) {
        return;    // FAT не изменена
    }

    // Записываем обе копии FAT
    for (int i = 0; i < boot_sector.fat_count; i++) {
        u32 fat_sector = ctx.fat_start_sector + i * ctx.fat_size_sectors;

        if (ata_pio_write_sectors(ATA_MASTER, fat_sector, ctx.fat_size_sectors, (u16*)ctx.fat_buffer) != 0) {
            printf_colored("Error writing FAT copy %d\n", RED_ON_BLACK, i + 1);
            return;
        }
    }

    printf("FAT synced to disk\n");
    ctx.fat_buffer_loaded = 1;    // Возвращаем в состояние "загружена, не изменена"
}

static int fat12_find_free_dir_entry(u32* sector_out, u32* offset_out) {
    u32 buffer_size = ctx.root_dir_size_sectors * 512;
    u8* buffer = (u8*)kmalloc(buffer_size);

    if (!buffer) {
        printf("No memory for root dir\n");
        return -1;
    }

    if (ata_pio_read_sectors(ATA_MASTER, ctx.root_dir_start_sector, ctx.root_dir_size_sectors, (u16*)buffer)
        != 0) {
        printf("Cannot read root dir\n");
        kfree(buffer);
        return -1;
    }

    // Ищем свободную запись (0x00 или 0xE5)
    for (int i = 0; i < boot_sector.root_entries; i++) {
        fat12_dir_entry_t* entry = (fat12_dir_entry_t*)(buffer + i * 32);

        if (entry->filename[0] == 0x00 || entry->filename[0] == 0xE5) {
            *sector_out = ctx.root_dir_start_sector + (i * 32) / 512;
            *offset_out = (i * 32) % 512;

            kfree(buffer);
            return 0;    // Нашли свободное место
        }
    }

    kfree(buffer);
    return -1;    // Нет свободного места
}

/* -------------------------------------------------------------------------- */
/* ПОИСК И ЧТЕНИЕ ФАЙЛОВ                                                     */
/* -------------------------------------------------------------------------- */

int fat12_find_file(const char* filename, fat12_dir_entry_t* result) {
    char formatted_name[12];
    format_filename(filename, formatted_name);

    u32 buffer_size = ctx.root_dir_size_sectors * 512;
    u8* buffer = (u8*)kmalloc(buffer_size);

    if (!buffer) {
        return 0;
    }

    if (ata_pio_read_sectors(ATA_MASTER, ctx.root_dir_start_sector, ctx.root_dir_size_sectors, (u16*)buffer)
        != 0) {
        kfree(buffer);
        return 0;
    }

    for (int i = 0; i < boot_sector.root_entries; i++) {
        fat12_dir_entry_t* entry = (fat12_dir_entry_t*)(buffer + i * 32);

        if (entry->filename[0] == 0x00) {
            break;
        }
        if (entry->filename[0] == 0xE5) {
            continue;
        }
        if (entry->attributes & 0x08) {
            continue;
        }

        // Сравниваем отформатированное имя
        if (memcmp(entry->filename, formatted_name, 11) == 0) {
            memcpy(result, entry, sizeof(fat12_dir_entry_t));
            kfree(buffer);
            return 1;
        }
    }

    kfree(buffer);
    return 0;
}

int fat12_read_file(const char* filename, u8* buffer) {
    fat12_dir_entry_t entry;
    if (!fat12_find_file(filename, &entry)) {
        printf_colored("File not found: %s\n", RED_ON_BLACK, filename);
        return -1;
    }

    if (entry.file_size == 0) {
        return 0;
    }

    u32 current_cluster = entry.first_cluster;
    u32 bytes_read = 0;
    u32 sectors_per_cluster = boot_sector.sectors_per_cluster;
    u32 bytes_per_sector = boot_sector.bytes_per_sector;

    while (current_cluster >= 2 && current_cluster < 0xFF8) {
        u32 sector = ctx.data_start_sector + (current_cluster - 2) * sectors_per_cluster;

        if (ata_pio_read_sectors(ATA_MASTER, sector, sectors_per_cluster, (u16*)(buffer + bytes_read)) != 0) {
            printf_colored("Read error at cluster %d\n", RED_ON_BLACK, current_cluster);
            return -1;
        }

        bytes_read += sectors_per_cluster * bytes_per_sector;

        if (bytes_read >= entry.file_size) {
            break;
        }

        u16 next_cluster = fat12_get_fat_entry(current_cluster);

        if (next_cluster == 0) {
            printf_colored("Invalid cluster chain\n", RED_ON_BLACK);
            return -1;
        }

        current_cluster = next_cluster;
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
/* НОВЫЕ ФУНКЦИИ ДЛЯ ЗАПИСИ                                                   */
/* -------------------------------------------------------------------------- */

int fat12_create_file(const char* filename) {
    // Проверяем, существует ли уже файл
    fat12_dir_entry_t existing;
    if (fat12_find_file(filename, &existing)) {
        printf("File already exists: %s\n", filename);
        return -1;
    }

    // Ищем свободное место в корневом каталоге
    u32 sector, offset;
    if (fat12_find_free_dir_entry(&sector, &offset) != 0) {
        printf("No space in root directory\n");
        return -1;
    }

    // Форматируем имя файла
    char formatted_name[12];
    format_filename(filename, formatted_name);

    // Подготавливаем запись
    fat12_dir_entry_t new_entry;
    memset(&new_entry, 0, sizeof(fat12_dir_entry_t));

    // Копируем имя и расширение
    memcpy(new_entry.filename, formatted_name, 11);

    // Устанавливаем атрибуты (обычный файл)
    new_entry.attributes = 0x20;

    // Время и дата (пока что фиксированные)
    new_entry.time_created = 0x0000;
    new_entry.date_created = 0x0000;

    // Первый кластер = 0 (пока файл пустой)
    new_entry.first_cluster = 0;

    // Размер файла = 0
    new_entry.file_size = 0;

    // Читаем сектор, куда будем писать
    u8 sector_buffer[512];
    if (ata_pio_read_sectors(ATA_MASTER, sector, 1, (u16*)sector_buffer) != 0) {
        printf("Cannot read directory sector\n");
        return -1;
    }

    // Копируем запись в буфер
    memcpy(sector_buffer + offset, &new_entry, sizeof(fat12_dir_entry_t));

    // Записываем сектор обратно
    if (ata_pio_write_sectors(ATA_MASTER, sector, 1, (u16*)sector_buffer) != 0) {
        printf("Cannot write directory sector\n");
        return -1;
    }

    return 0;
}

int fat12_delete_file(const char* filename) {
    // Находим файл
    fat12_dir_entry_t entry;
    if (!fat12_find_file(filename, &entry)) {
        printf("File not found: %s\n", filename);
        return -1;
    }

    // Находим запись в каталоге (нужно знать ее положение)
    char formatted_name[12];
    format_filename(filename, formatted_name);

    u32 buffer_size = ctx.root_dir_size_sectors * 512;
    u8* buffer = (u8*)kmalloc(buffer_size);

    if (!buffer) {
        printf("No memory for root dir\n");
        return -1;
    }

    if (ata_pio_read_sectors(ATA_MASTER, ctx.root_dir_start_sector, ctx.root_dir_size_sectors, (u16*)buffer)
        != 0) {
        printf("Cannot read root dir\n");
        kfree(buffer);
        return -1;
    }

    // Ищем файл и помечаем как удаленный
    for (int i = 0; i < boot_sector.root_entries; i++) {
        fat12_dir_entry_t* dir_entry = (fat12_dir_entry_t*)(buffer + i * 32);

        if (dir_entry->filename[0] == 0x00) {
            break;
        }

        if (memcmp(dir_entry->filename, formatted_name, 11) == 0) {
            // Помечаем как удаленный (первый байт = 0xE5)
            dir_entry->filename[0] = 0xE5;

            // Освобождаем кластеры файла
            if (entry.first_cluster >= 2) {
                fat12_free_cluster_chain(entry.first_cluster);
                fat12_sync_fat();
            }

            // Записываем измененный каталог обратно
            u32 sector = ctx.root_dir_start_sector + (i * 32) / 512;
            u32 sector_offset = (i * 32) % 512;

            u8 sector_buffer[512];
            if (ata_pio_read_sectors(ATA_MASTER, sector, 1, (u16*)sector_buffer) != 0) {
                printf("Cannot read directory sector for write\n");
                kfree(buffer);
                return -1;
            }

            // Копируем измененную запись
            memcpy(sector_buffer + sector_offset, dir_entry, sizeof(fat12_dir_entry_t));

            if (ata_pio_write_sectors(ATA_MASTER, sector, 1, (u16*)sector_buffer) != 0) {
                printf("Cannot write directory sector\n");
                kfree(buffer);
                return -1;
            }

            kfree(buffer);
            return 0;
        }
    }

    kfree(buffer);
    printf("File not found in directory scan: %s\n", filename);
    return -1;
}

int fat12_write_file(const char* filename, u8* data, u32 size) {
    // Проверяем, существует ли файл
    fat12_dir_entry_t existing_entry;
    int file_exists = fat12_find_file(filename, &existing_entry);

    if (!file_exists) {
        // Создаем новый файл
        if (fat12_create_file(filename) != 0) {
            printf("Cannot create file: %s\n", filename);
            return -1;
        }

        // Снова ищем, чтобы получить структуру
        if (!fat12_find_file(filename, &existing_entry)) {
            printf("Failed to find newly created file: %s\n", filename);
            return -1;
        }
    } else {
        // Файл существует - освобождаем старые кластеры
        if (existing_entry.first_cluster >= 2) {
            fat12_free_cluster_chain(existing_entry.first_cluster);
        }
    }

    // Если файл пустой (size == 0)
    if (size == 0) {
        // Просто обновляем размер в записи каталога
        existing_entry.file_size = 0;
        existing_entry.first_cluster = 0;

        // Находим и обновляем запись в каталоге
        char formatted_name[12];
        format_filename(filename, formatted_name);

        u32 buffer_size = ctx.root_dir_size_sectors * 512;
        u8* buffer = (u8*)kmalloc(buffer_size);

        if (!buffer) {
            printf("No memory for root dir\n");
            return -1;
        }

        if (ata_pio_read_sectors(
                ATA_MASTER, ctx.root_dir_start_sector, ctx.root_dir_size_sectors, (u16*)buffer)
            != 0) {
            printf("Cannot read root dir\n");
            kfree(buffer);
            return -1;
        }

        // Ищем файл и обновляем его запись
        for (int i = 0; i < boot_sector.root_entries; i++) {
            fat12_dir_entry_t* dir_entry = (fat12_dir_entry_t*)(buffer + i * 32);

            if (dir_entry->filename[0] == 0x00) {
                break;
            }

            if (memcmp(dir_entry->filename, formatted_name, 11) == 0) {
                // Обновляем запись
                dir_entry->file_size = 0;
                dir_entry->first_cluster = 0;

                // Записываем обратно
                u32 sector = ctx.root_dir_start_sector + (i * 32) / 512;
                u32 sector_offset = (i * 32) % 512;

                u8 sector_buffer[512];
                if (ata_pio_read_sectors(ATA_MASTER, sector, 1, (u16*)sector_buffer) != 0) {
                    printf("Cannot read directory sector for write\n");
                    kfree(buffer);
                    return -1;
                }

                memcpy(sector_buffer + sector_offset, dir_entry, sizeof(fat12_dir_entry_t));

                if (ata_pio_write_sectors(ATA_MASTER, sector, 1, (u16*)sector_buffer) != 0) {
                    printf("Cannot write directory sector\n");
                    kfree(buffer);
                    return -1;
                }

                kfree(buffer);
                printf("File cleared: %s\n", filename);
                return 0;
            }
        }

        kfree(buffer);
        return -1;
    }

    // Вычисляем сколько кластеров нужно
    u32 bytes_per_cluster = boot_sector.sectors_per_cluster * boot_sector.bytes_per_sector;
    u32 clusters_needed = (size + bytes_per_cluster - 1) / bytes_per_cluster;

    if (clusters_needed == 0) {
        clusters_needed = 1;
    }

    // Находим свободные кластеры
    u16* cluster_chain = (u16*)kmalloc(clusters_needed * sizeof(u16));
    if (!cluster_chain) {
        printf("No memory for cluster chain\n");
        return -1;
    }

    u16 prev_cluster = 0;
    u16 first_cluster = 0;

    for (u32 i = 0; i < clusters_needed; i++) {
        u16 free_cluster = fat12_find_free_cluster();
        if (free_cluster == 0) {
            printf("No free clusters available\n");
            kfree(cluster_chain);
            return -1;
        }

        cluster_chain[i] = free_cluster;

        if (i == 0) {
            first_cluster = free_cluster;
        } else {
            // Связываем с предыдущим кластером
            fat12_set_fat_entry(prev_cluster, free_cluster);
        }

        prev_cluster = free_cluster;
    }

    // Последний кластер помечаем как конец цепочки
    fat12_set_fat_entry(prev_cluster, 0xFFF);

    // Записываем данные в кластеры
    u32 bytes_written = 0;
    for (u32 i = 0; i < clusters_needed; i++) {
        u32 sector = ctx.data_start_sector + (cluster_chain[i] - 2) * boot_sector.sectors_per_cluster;
        u32 bytes_to_write = size - bytes_written;

        if (bytes_to_write > bytes_per_cluster) {
            bytes_to_write = bytes_per_cluster;
        }

        // Подготавливаем буфер для сектора
        u8* write_buffer = (u8*)kmalloc(bytes_per_cluster);
        if (!write_buffer) {
            printf("No memory for write buffer\n");
            kfree(cluster_chain);
            return -1;
        }

        // Копируем данные
        memcpy(write_buffer, data + bytes_written, bytes_to_write);

        // Заполняем остаток нулями
        if (bytes_to_write < bytes_per_cluster) {
            memset(write_buffer + bytes_to_write, 0, bytes_per_cluster - bytes_to_write);
        }

        // Записываем кластер
        if (ata_pio_write_sectors(ATA_MASTER, sector, boot_sector.sectors_per_cluster, (u16*)write_buffer)
            != 0) {
            printf("Write error at cluster %d\n", cluster_chain[i]);
            kfree(write_buffer);
            kfree(cluster_chain);
            return -1;
        }

        kfree(write_buffer);
        bytes_written += bytes_to_write;
    }

    // Обновляем запись в каталоге
    char formatted_name[12];
    format_filename(filename, formatted_name);

    u32 buffer_size = ctx.root_dir_size_sectors * 512;
    u8* buffer = (u8*)kmalloc(buffer_size);

    if (!buffer) {
        printf("No memory for root dir\n");
        kfree(cluster_chain);
        return -1;
    }

    if (ata_pio_read_sectors(ATA_MASTER, ctx.root_dir_start_sector, ctx.root_dir_size_sectors, (u16*)buffer)
        != 0) {
        printf("Cannot read root dir\n");
        kfree(buffer);
        kfree(cluster_chain);
        return -1;
    }

    // Ищем файл и обновляем его запись
    for (int i = 0; i < boot_sector.root_entries; i++) {
        fat12_dir_entry_t* dir_entry = (fat12_dir_entry_t*)(buffer + i * 32);

        if (dir_entry->filename[0] == 0x00) {
            break;
        }

        if (memcmp(dir_entry->filename, formatted_name, 11) == 0) {
            // Обновляем запись
            dir_entry->file_size = size;
            dir_entry->first_cluster = first_cluster;

            // Обновляем время/дату модификации (пока что фиксированные)
            dir_entry->time_created = 0x0000;
            dir_entry->date_created = 0x0000;

            // Записываем обратно
            u32 sector = ctx.root_dir_start_sector + (i * 32) / 512;
            u32 sector_offset = (i * 32) % 512;

            u8 sector_buffer[512];
            if (ata_pio_read_sectors(ATA_MASTER, sector, 1, (u16*)sector_buffer) != 0) {
                printf("Cannot read directory sector for write\n");
                kfree(buffer);
                kfree(cluster_chain);
                return -1;
            }

            memcpy(sector_buffer + sector_offset, dir_entry, sizeof(fat12_dir_entry_t));

            if (ata_pio_write_sectors(ATA_MASTER, sector, 1, (u16*)sector_buffer) != 0) {
                printf("Cannot write directory sector\n");
                kfree(buffer);
                kfree(cluster_chain);
                return -1;
            }

            // Синхронизируем FAT
            fat12_sync_fat();

            kfree(buffer);
            kfree(cluster_chain);

            printf("File written: %s (%d bytes, %d clusters)\n", filename, size, clusters_needed);
            return 0;
        }
    }

    kfree(buffer);
    kfree(cluster_chain);
    printf("Failed to update directory entry: %s\n", filename);
    return -1;
}

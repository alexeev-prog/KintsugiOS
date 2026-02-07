#include "fat12.h"

#include "../drivers/ata_pio.h"
#include "../kklibc/mem.h"
#include "../kklibc/stdio.h"
#include "../kklibc/stdlib.h"

static fat12_context_t ctx;
static fat12_boot_sector_t boot_sector;

void fat12_init(void) {
    printf("Initializing FAT12...\n");

    if (ata_pio_read_sectors(ATA_MASTER, 0, 1, (u16*)&boot_sector) != 0) {
        printf("Cannot read boot sector!\n");
        return;
    }

    u16* sig = (u16*)((u8*)&boot_sector + 510);
    if (*sig != 0xAA55) {
        printf("Invalid boot signature\n");
        return;
    }

    ctx.fat_start_sector = boot_sector.reserved_sectors;
    ctx.fat_size_sectors = boot_sector.sectors_per_fat;
    ctx.root_dir_start_sector = ctx.fat_start_sector + (boot_sector.fat_count * ctx.fat_size_sectors);

    ctx.root_dir_size_sectors = (boot_sector.root_entries * 32 + 511) / 512;
    ctx.data_start_sector = ctx.root_dir_start_sector + ctx.root_dir_size_sectors;

    u32 total_data_sectors = boot_sector.total_sectors - ctx.data_start_sector;
    ctx.total_clusters = total_data_sectors / boot_sector.sectors_per_cluster;

    printf(
        "FAT12 loaded: sectors %d-%d (size: %d sectors)\n",
        ctx.fat_start_sector,
        ctx.fat_start_sector + ctx.fat_size_sectors - 1,
        ctx.fat_size_sectors);
}

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

static u16 fat12_get_fat_entry(u32 cluster) {
    static u8* fat_buffer = NULL;

    if (cluster < 2) {
        return 0xFFF;
    }

    if (cluster - 2 >= ctx.total_clusters) {
        return 0xFFF;
    }

    if (!fat_buffer) {
        u32 fat_size = ctx.fat_size_sectors * 512;
        fat_buffer = (u8*)kmalloc(fat_size);

        if (!fat_buffer) {
            return 0xFFF;
        }

        if (ata_pio_read_sectors(ATA_MASTER, ctx.fat_start_sector, ctx.fat_size_sectors, (u16*)fat_buffer)
            != 0) {
            kfree(fat_buffer);
            fat_buffer = NULL;
            return 0xFFF;
        }
    }

    u32 offset = cluster * 3 / 2;

    u32 fat_size = ctx.fat_size_sectors * 512;
    if (offset + 1 >= fat_size) {
        return 0xFFF;
    }

    u16 entry = *((u16*)(fat_buffer + offset));

    if (cluster & 1) {
        entry >>= 4;
    } else {
        entry &= 0x0FFF;
    }

    // Проверяем специальные значения
    if (entry >= 0xFF8) {
        return 0xFFF;
    }
    if (entry == 0xFF7) {
        return 0xFFF;
    }
    if (entry == 0x000) {
        return 0xFFF;
    }

    return entry;
}

static void format_filename(fat12_dir_entry_t* entry, char* out) {
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

int fat12_find_file(const char* filename, fat12_dir_entry_t* result) {
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

        char entry_name[13];
        format_filename(entry, entry_name);

        if (strcmp(entry_name, filename) == 0) {
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
        printf("File not found: %s\n", filename);
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
            printf("Read error at cluster %d\n", current_cluster);
            return -1;
        }

        bytes_read += sectors_per_cluster * bytes_per_sector;

        if (bytes_read >= entry.file_size) {
            break;
        }

        u16 next_cluster = fat12_get_fat_entry(current_cluster);

        if (next_cluster == 0) {
            printf("Invalid cluster chain\n");
            return -1;
        }

        current_cluster = next_cluster;
    }

    return 0;
}

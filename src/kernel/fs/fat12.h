#ifndef FS_FAT12_H
#define FS_FAT12_H

#include "../kklibc/ctypes.h"

typedef struct {
    u8 jmp[3];
    char oem[8];
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    u8 fat_count;
    u16 root_entries;
    u16 total_sectors;
    u8 media_type;
    u16 sectors_per_fat;
    u16 sectors_per_track;
    u16 head_count;
    u32 hidden_sectors;
    u32 large_sector_count;

    // расширенный BPB
    u8 drive_number;
    u8 reserved;
    u8 boot_signature;
    u32 volume_id;
    char volume_label[11];
    char fs_type[8];
} __attribute__((packed)) fat12_boot_sector_t;

// запись в директории
typedef struct {
    char filename[8];
    char extension[3];
    u8 attributes;
    u8 reserved[10];
    u16 time_created;
    u16 date_created;
    u16 first_cluster;
    u32 file_size;
} __attribute__((packed)) fat12_dir_entry_t;

// контекст FAT12 (отвечает за хранение вычисленных значений)
typedef struct {
    u32 fat_start_sector;
    u32 fat_size_sectors;
    u32 root_dir_start_sector;
    u32 root_dir_size_sectors;
    u32 data_start_sector;
    u32 total_clusters;
} fat12_context_t;

void fat12_init(void);
int fat12_read_boot_sector(fat12_boot_sector_t* boot);
int fat12_find_file(const char* filename, fat12_dir_entry_t* result);
void fat12_list_root(void);
int fat12_read_file(const char* filename, u8* buffer);
void print_fat12_info(void);

#endif

#include "fat12.h"

#include "../drivers/ata_pio.h"
#include "../kklibc/kklibc.h"

/* FAT12 superfloppy volumes on the primary ATA master, 512-byte sectors,
 * short names in the root directory. No partition table or subdirectories. */
static fat12_context_t ctx;
static fat12_boot_sector_t boot_sector;
static int mounted;
extern ata_disk_info_t ata_disks[2];

static int read_sector(u32 sector, void* buffer) {
    return ata_pio_read_sectors(ATA_MASTER, sector, 1, (u16*)buffer);
}

static int write_sector(u32 sector, void* buffer) {
    return ata_pio_write_sectors(ATA_MASTER, sector, 1, (u16*)buffer);
}

int fat12_read_boot_sector(fat12_boot_sector_t* boot) {
    u16 sector[256];
    if (!boot) {
        return -1;
    }
    memset(sector, 0, sizeof(sector));
    int result = read_sector(0, sector);
    if (result || sector[255] != 0xaa55) {
        printf("FAT12 boot read: result=%d signature=%x\n", result, sector[255]);
        return -1;
    }
    memcpy(boot, sector, sizeof(*boot));
    return 0;
}

void fat12_cleanup(void) {
    mounted = 0;
    if (ctx.fat_buffer) {
        kfree(ctx.fat_buffer);
    }
    memset(&ctx, 0, sizeof(ctx));
    memset(&boot_sector, 0, sizeof(boot_sector));
}

void fat12_init(void) {
    fat12_cleanup();
    if (fat12_read_boot_sector(&boot_sector)) {
        goto invalid;
    }
    u32 spc = boot_sector.sectors_per_cluster;
    u32 total = boot_sector.total_sectors ? boot_sector.total_sectors : boot_sector.large_sector_count;
    if (boot_sector.bytes_per_sector != 512 || !spc || spc > 128 || (spc & (spc - 1))
        || !boot_sector.reserved_sectors || !boot_sector.sectors_per_fat || !boot_sector.fat_count
        || boot_sector.fat_count > 2 || !boot_sector.root_entries || total > ata_disks[0].size) {
        goto invalid;
    }
    ctx.fat_start_sector = boot_sector.reserved_sectors;
    ctx.fat_size_sectors = boot_sector.sectors_per_fat;
    ctx.root_dir_start_sector = ctx.fat_start_sector + boot_sector.fat_count * ctx.fat_size_sectors;
    ctx.root_dir_size_sectors = (boot_sector.root_entries * 32u + 511) / 512;
    ctx.data_start_sector = ctx.root_dir_start_sector + ctx.root_dir_size_sectors;
    if (total <= ctx.data_start_sector) {
        goto invalid;
    }
    ctx.total_clusters = (total - ctx.data_start_sector) / spc;
    if (!ctx.total_clusters || ctx.total_clusters >= 4085
        || ((ctx.total_clusters + 2) * 3 + 1) / 2 > ctx.fat_size_sectors * 512) {
        goto invalid;
    }
    ctx.fat_buffer = kmalloc(ctx.fat_size_sectors * 512);
    if (!ctx.fat_buffer) {
        goto invalid;
    }
    for (u32 i = 0; i < ctx.fat_size_sectors; ++i) {
        if (read_sector(ctx.fat_start_sector + i, ctx.fat_buffer + i * 512)) {
            goto invalid;
        }
    }
    if (ctx.fat_buffer[0] != boot_sector.media_type || ctx.fat_buffer[1] != 255 || ctx.fat_buffer[2] != 255) {
        goto invalid;
    }
    mounted = 1;
    printf("FAT12 mounted: %d clusters\n", ctx.total_clusters);
    return;
invalid:
    fat12_cleanup();
    printf("FAT12 unavailable: missing disk or invalid/unsupported volume\n");
}

static int short_name(const char* input, char out[11]) {
    if (!input || !*input) {
        return -1;
    }
    memset(out, ' ', 11);
    u32 pos = 0, ext = 0;
    for (; *input; ++input) {
        unsigned char c = *input;
        if (c == '.') {
            if (!pos || ext) {
                return -1;
            }
            ext = 1;
            pos = 8;
            continue;
        }
        if (c <= 32 || c >= 127 || strchr("\"*+,/:;<=>?[\\]|", c)) {
            return -1;
        }
        if ((!ext && pos >= 8) || pos >= 11) {
            return -1;
        }
        out[pos++] = toupper(c);
    }
    return ext && pos == 8 ? -1 : 0;
}

static int valid_cluster(u32 c) {
    return c >= 2 && c < ctx.total_clusters + 2 && c < 0xff0;
}

static u16 fat_get(u32 c) {
    u32 off = c * 3 / 2;
    u16 v = ctx.fat_buffer[off] | ((u16)ctx.fat_buffer[off + 1] << 8);
    return c & 1 ? v >> 4 : v & 0xfff;
}

static void fat_set(u32 c, u16 value) {
    u32 off = c * 3 / 2;
    u16 v = ctx.fat_buffer[off] | ((u16)ctx.fat_buffer[off + 1] << 8);
    v = c & 1 ? (v & 15) | ((value & 0xfff) << 4) : (v & 0xf000) | (value & 0xfff);
    ctx.fat_buffer[off] = v & 255;
    ctx.fat_buffer[off + 1] = v >> 8;
}

static int sync_fat(void) {
    for (u32 copy = 0; copy < boot_sector.fat_count; ++copy) {
        for (u32 i = 0; i < ctx.fat_size_sectors; ++i) {
            if (write_sector(
                    ctx.fat_start_sector + copy * ctx.fat_size_sectors + i, ctx.fat_buffer + i * 512)) {
                return -1;
            }
        }
    }
    return 0;
}

static void free_chain(u16 c) {
    for (u32 n = 0; valid_cluster(c) && n < ctx.total_clusters; ++n) {
        u16 next = fat_get(c);
        fat_set(c, 0);
        c = next;
    }
}

/* Returns 1 found, 0 absent, -1 invalid/I/O. slot is an entry index. */
static int lookup(const char name[11], fat12_dir_entry_t* out, u32* slot, u32* free_slot) {
    u16 raw[256];
    if (!mounted) {
        return -1;
    }
    if (free_slot) {
        *free_slot = 0xffffffff;
    }
    for (u32 i = 0; i < boot_sector.root_entries; ++i) {
        if (!(i % 16) && read_sector(ctx.root_dir_start_sector + i / 16, raw)) {
            return -1;
        }
        fat12_dir_entry_t* e = (fat12_dir_entry_t*)((u8*)raw + (i % 16) * 32);
        u8 first = (u8)e->filename[0];
        if (first == 0 || first == 0xe5) {
            if (free_slot && *free_slot == 0xffffffff) {
                *free_slot = i;
            }
            if (!first) {
                return 0;
            }
            continue;
        }
        if (e->attributes == 0x0f) {
            continue;
        }
        if ((e->attributes & 0x18) && !memcmp(e, name, 11)) {
            return -1;
        }
        if (e->attributes & 0x18) {
            continue;
        }
        if (!memcmp(e, name, 11)) {
            if (out) {
                memcpy(out, e, sizeof(*out));
            }
            if (slot) {
                *slot = i;
            }
            return 1;
        }
    }
    return 0;
}

static int store_entry(u32 slot, const fat12_dir_entry_t* entry) {
    u16 raw[256];
    if (read_sector(ctx.root_dir_start_sector + slot / 16, raw)) {
        return -1;
    }
    memcpy((u8*)raw + (slot % 16) * 32, entry, sizeof(*entry));
    return write_sector(ctx.root_dir_start_sector + slot / 16, raw);
}

int fat12_find_file(const char* filename, fat12_dir_entry_t* result) {
    char name[11];
    if (!result || short_name(filename, name)) {
        return 0;
    }
    return lookup(name, result, NULL, NULL) == 1;
}

int fat12_read_file(const char* filename, u8* buffer) {
    fat12_dir_entry_t e;
    if (!fat12_find_file(filename, &e) || (!buffer && e.file_size)) {
        return -1;
    }
    u32 left = e.file_size, c = e.first_cluster;
    u16 raw[256];
    u8 visited[512];
    memset(visited, 0, sizeof(visited));
    for (u32 n = 0; left && n < ctx.total_clusters; ++n) {
        if (!valid_cluster(c)) {
            return -1;
        }
        if (visited[c / 8] & (1u << (c % 8))) {
            return -1;
        }
        visited[c / 8] |= 1u << (c % 8);
        for (u32 s = 0; s < boot_sector.sectors_per_cluster && left; ++s) {
            if (read_sector(ctx.data_start_sector + (c - 2) * boot_sector.sectors_per_cluster + s, raw)) {
                return -1;
            }
            u32 bytes = left > 512 ? 512 : left;
            memcpy(buffer, raw, bytes);
            buffer += bytes;
            left -= bytes;
        }
        c = fat_get(c);
    }
    return left ? -1 : 0;
}

void fat12_list_root(void) {
    if (!mounted) {
        printf("FAT12 not mounted\n");
        return;
    }
    u16 raw[256];
    printf("Root directory:\n");
    for (u32 i = 0; i < boot_sector.root_entries; ++i) {
        if (!(i % 16) && read_sector(ctx.root_dir_start_sector + i / 16, raw)) {
            printf("Directory read error\n");
            return;
        }
        fat12_dir_entry_t* e = (fat12_dir_entry_t*)((u8*)raw + (i % 16) * 32);
        if (!e->filename[0]) {
            break;
        }
        if ((u8)e->filename[0] == 0xe5 || (e->attributes & 8)) {
            continue;
        }
        char name[13];
        u32 p = 0;
        for (int j = 0; j < 8 && e->filename[j] != ' '; ++j) {
            name[p++] = e->filename[j];
        }
        if (e->extension[0] != ' ') {
            name[p++] = '.';
            for (int j = 0; j < 3 && e->extension[j] != ' '; ++j) {
                name[p++] = e->extension[j];
            }
        }
        name[p] = 0;
        printf("%-12s %d bytes\n", name, e->file_size);
    }
}

void print_fat12_info(void) {
    if (!mounted) {
        printf("FAT12 not mounted\n");
        return;
    }
    printf(
        "FAT12: 512 bytes/sector, %d sectors/cluster, %d clusters\n",
        boot_sector.sectors_per_cluster,
        ctx.total_clusters);
    printf(
        "FAT at %d, root at %d, data at %d\n",
        ctx.fat_start_sector,
        ctx.root_dir_start_sector,
        ctx.data_start_sector);
}

int fat12_create_file(const char* filename) {
    char name[11];
    u32 slot;
    if (short_name(filename, name) || lookup(name, NULL, NULL, &slot) != 0 || slot == 0xffffffff) {
        return -1;
    }
    fat12_dir_entry_t e;
    memset(&e, 0, sizeof(e));
    memcpy(&e, name, 11);
    e.attributes = 0x20;
    return store_entry(slot, &e);
}

int fat12_delete_file(const char* filename) {
    char name[11];
    u32 slot;
    fat12_dir_entry_t e;
    if (short_name(filename, name) || lookup(name, &e, &slot, NULL) != 1) {
        return -1;
    }
    u16 old = e.first_cluster;
    e.filename[0] = (char)0xe5;
    if (store_entry(slot, &e)) {
        return -1;
    }
    free_chain(old);
    return sync_fat();
}

int fat12_write_file(const char* filename, u8* data, u32 size) {
    char name[11];
    u32 slot, free_slot;
    fat12_dir_entry_t e;
    if (short_name(filename, name) || (!data && size)) {
        return -1;
    }
    int found = lookup(name, &e, &slot, &free_slot);
    if (found < 0 || (!found && free_slot == 0xffffffff)) {
        return -1;
    }
    if (!found) {
        memset(&e, 0, sizeof(e));
        memcpy(&e, name, 11);
        e.attributes = 0x20;
        slot = free_slot;
    }
    u32 cluster_bytes = boot_sector.sectors_per_cluster * 512u;
    u32 needed = size / cluster_bytes + (size % cluster_bytes != 0);
    if (needed > ctx.total_clusters) {
        return -1;
    }
    u16 first = 0, prev = 0;
    u32 allocated = 0;
    /* Reserve immediately so a cluster cannot be allocated twice. Keep the
     * original chain intact until the new contents and directory are saved. */
    for (u32 c = 2; valid_cluster(c) && allocated < needed; ++c) {
        if (!fat_get(c)) {
            fat_set(c, 0xfff);
            if (prev) {
                fat_set(prev, c);
            } else {
                first = c;
            }
            prev = c;
            ++allocated;
        }
    }
    if (allocated != needed) {
        free_chain(first);
        return -1;
    }
    u16 raw[256];
    u32 left = size;
    u16 c = first;
    while (left) {
        for (u32 s = 0; s < boot_sector.sectors_per_cluster; ++s) {
            u32 bytes = left > 512 ? 512 : left;
            memset(raw, 0, sizeof(raw));
            if (bytes) {
                memcpy(raw, data, bytes);
            }
            if (write_sector(ctx.data_start_sector + (c - 2) * boot_sector.sectors_per_cluster + s, raw)) {
                goto rollback;
            }
            data += bytes;
            left -= bytes;
        }
        c = fat_get(c);
    }
    if (sync_fat()) {
        goto rollback;
    }
    u16 old = e.first_cluster;
    e.first_cluster = first;
    e.file_size = size;
    if (store_entry(slot, &e)) {
        goto rollback;
    }
    free_chain(old);
    return sync_fat();
rollback:
    free_chain(first);
    if (sync_fat()) {
        mounted = 0;
        printf("FAT12 I/O failure; remount required\n");
    }
    return -1;
}

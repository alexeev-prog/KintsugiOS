#include "fat12.h"

#include "../drivers/ata_pio.h"
#include "../kernel/log.h"
#include "../kklibc/kklibc.h"
#include "vfs.h"

/* FAT12 superfloppy, ATA master, 512-byte sectors, short 8.3 names. */
static fat12_context_t ctx;
static fat12_boot_sector_t boot_sector;
static int mounted;
static const vfs_ops_t fat_ops;
static int check_volume(void);

int fat12_is_mounted(void) {
    return mounted;
}

extern ata_disk_info_t ata_disks[2];

static int read_sector(u32 sector, void* buffer) {
    return ata_pio_read_sectors(ATA_MASTER, sector, 1, (u16*)buffer);
}

static int write_sector(u32 sector, void* buffer) {
    int result = ata_pio_write_sectors(ATA_MASTER, sector, 1, (u16*)buffer);
    if (result) {
        log_event(
            "ERROR", "fat12", "Sector write failed lba=%u error=%d; mutations disabled", sector, result);
        mounted = 0; /* Ambiguous writes must never be followed by more mutations. */
    }
    return result;
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
    vfs_mount(NULL);
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
    log_event(
        "DEBUG",
        "fat12",
        "BPB bytes/sector=%u sectors/cluster=%u total=%u FAT copies=%u root entries=%u",
        boot_sector.bytes_per_sector,
        spc,
        total,
        boot_sector.fat_count,
        boot_sector.root_entries);
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
    for (u32 copy = 1; copy < boot_sector.fat_count; ++copy) {
        u16 raw[256];
        for (u32 i = 0; i < ctx.fat_size_sectors; ++i) {
            if (read_sector(ctx.fat_start_sector + copy * ctx.fat_size_sectors + i, raw)
                || memcmp(raw, ctx.fat_buffer + i * 512, 512)) {
                goto invalid;
            }
        }
    }
    if (ctx.fat_buffer[0] != boot_sector.media_type || ctx.fat_buffer[1] != 255 || ctx.fat_buffer[2] != 255) {
        goto invalid;
    }
    mounted = 1;
    if (check_volume()) {
        goto invalid;
    }
    vfs_mount(&fat_ops);
    log_event(
        "INFO",
        "fat12",
        "Mounted / clusters=%u FAT start=%u data start=%u; integrity check passed",
        ctx.total_clusters,
        ctx.fat_start_sector,
        ctx.data_start_sector);
    printf("FAT12 mounted: %d clusters\n", ctx.total_clusters);
    return;
invalid:
    log_event("ERROR", "fat12", "Mount rejected: unavailable disk, invalid BPB/FAT or integrity failure");
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

/* VFS driver implementation is below the legacy compatibility API. */

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

typedef struct {
    fat12_dir_entry_t entry;
    u16 parent;
    u32 slot;
} fat_node_t;

/* Validate complete chains before accessing or freeing their clusters. */
static int chain_length(u16 first, u32* length) {
    u8 seen[512];
    memset(seen, 0, sizeof(seen));
    *length = 0;
    if (!first) {
        return 0;
    }
    u32 c = first;
    for (;;) {
        if (!valid_cluster(c) || (seen[c / 8] & (1u << (c % 8)))) {
            return VFS_ECORRUPT;
        }
        seen[c / 8] |= 1u << (c % 8);
        ++*length;
        c = fat_get(c);
        if (c >= 0xff8) {
            return 0;
        }
    }
}

static int directory_sector(u16 directory, u32 index, u32* sector) {
    if (!directory) {
        if (index >= boot_sector.root_entries) {
            return 0;
        }
        *sector = ctx.root_dir_start_sector + index / 16;
        return 1;
    }
    u32 count;
    int result = chain_length(directory, &count);
    if (result) {
        return result;
    }
    u32 per_cluster = boot_sector.sectors_per_cluster * 16u;
    if (index / per_cluster >= count) {
        return 0;
    }
    u32 c = directory;
    for (u32 n = index / per_cluster; n; --n) {
        c = fat_get(c);
    }
    *sector = ctx.data_start_sector + (c - 2) * boot_sector.sectors_per_cluster + (index % per_cluster) / 16;
    return 1;
}

static int directory_entry(u16 directory, u32 index, fat12_dir_entry_t* entry, int store) {
    u32 sector;
    int result = directory_sector(directory, index, &sector);
    if (result <= 0) {
        return result;
    }
    u16 raw[256];
    if (read_sector(sector, raw)) {
        return VFS_EIO;
    }
    void* address = (u8*)raw + (index % 16) * 32;
    if (!store) {
        memcpy(entry, address, 32);
    } else {
        memcpy(address, entry, 32);
        if (write_sector(sector, raw)) {
            return VFS_EIO;
        }
    }
    return 1;
}

static int directory_lookup(u16 directory, const char name[11], fat_node_t* node, u32* vacant) {
    if (!mounted) {
        return VFS_EIO;
    }
    if (vacant) {
        *vacant = 0xffffffffu;
    }
    for (u32 index = 0;; ++index) {
        fat12_dir_entry_t entry;
        int result = directory_entry(directory, index, &entry, 0);
        if (result <= 0) {
            return result < 0 ? result : VFS_ENOENT;
        }
        if (!entry.filename[0] || (u8)entry.filename[0] == 0xe5) {
            if (vacant && *vacant == 0xffffffffu) {
                *vacant = index;
            }
            if (!entry.filename[0]) {
                return VFS_ENOENT;
            }
            continue;
        }
        if (entry.attributes & 8) {
            continue;
        }
        if (!memcmp(&entry, name, 11)) {
            if (node) {
                *node = (fat_node_t){ .entry = entry, .parent = directory, .slot = index };
            }
            return 0;
        }
    }
}

static int resolve_node(const char* path, fat_node_t* node, u16* parent, char leaf[11]) {
    if (!mounted) {
        return VFS_EIO;
    }
    if (!path || *path != '/') {
        return VFS_EINVAL;
    }
    u16 directory = 0;
    ++path;
    if (!*path) {
        if (parent) {
            return VFS_EINVAL;
        }
        memset(node, 0, sizeof(*node));
        node->entry.attributes = 0x10;
        return 0;
    }
    for (;;) {
        char component[13], name[11];
        u32 n = 0;
        while (*path && *path != '/') {
            if (n == 12) {
                return VFS_EINVAL;
            }
            component[n++] = *path++;
        }
        component[n] = 0;
        if (short_name(component, name)) {
            return VFS_EINVAL;
        }
        if (!*path && parent) {
            *parent = directory;
            memcpy(leaf, name, 11);
            return 0;
        }
        fat_node_t found;
        int result = directory_lookup(directory, name, &found, NULL);
        if (result) {
            return result;
        }
        if (!*path) {
            *node = found;
            return 0;
        }
        if (!(found.entry.attributes & 0x10)) {
            return VFS_ENOTDIR;
        }
        if (parent && (found.entry.attributes & 1)) {
            return VFS_EROFS;
        }
        if (!valid_cluster(found.entry.first_cluster)) {
            return VFS_ECORRUPT;
        }
        directory = found.entry.first_cluster;
        ++path;
    }
}

static int reserve_chain(u32 count, u16* first) {
    *first = 0;
    u16 last = 0;
    for (u32 c = 2; valid_cluster(c) && count; ++c) {
        if (fat_get(c)) {
            continue;
        }
        fat_set(c, 0xfff);
        if (last) {
            fat_set(last, c);
        } else {
            *first = c;
        }
        last = c;
        --count;
    }
    if (!count) {
        return 0;
    }
    free_chain(*first);
    *first = 0;
    return VFS_ENOSPC;
}

static int zero_chain(u16 first) {
    u16 raw[256];
    memset(raw, 0, sizeof(raw));
    for (u32 c = first; valid_cluster(c); c = fat_get(c)) {
        for (u32 s = 0; s < boot_sector.sectors_per_cluster; ++s) {
            if (write_sector(ctx.data_start_sector + (c - 2) * boot_sector.sectors_per_cluster + s, raw)) {
                return VFS_EIO;
            }
        }
    }
    return 0;
}

static int available_slot(u16 directory, const char name[11], u32* slot) {
    int result = directory_lookup(directory, name, NULL, slot);
    if (!result) {
        return VFS_EEXIST;
    }
    if (result != VFS_ENOENT) {
        return result;
    }
    if (*slot != 0xffffffffu) {
        return 0;
    }
    if (!directory) {
        return VFS_ENOSPC;
    }
    u32 count;
    result = chain_length(directory, &count);
    if (result) {
        return result;
    }
    u16 extra;
    result = reserve_chain(1, &extra);
    if (result) {
        return result;
    }
    result = zero_chain(extra);
    if (result) {
        return result;
    }
    u16 last = directory;
    while (fat_get(last) < 0xff8) {
        last = fat_get(last);
    }
    fat_set(last, extra);
    if (sync_fat()) {
        return VFS_EIO;
    }
    *slot = count * boot_sector.sectors_per_cluster * 16u;
    return 0;
}

static int driver_stat(const char* path, vfs_stat_t* stat) {
    fat_node_t node;
    int result = resolve_node(path, &node, NULL, NULL);
    if (result) {
        return result;
    }
    *stat = (vfs_stat_t){ .size = node.entry.file_size,
                          .directory = !!(node.entry.attributes & 0x10),
                          .readonly = !!(node.entry.attributes & 1) };
    return 0;
}

static int driver_read(const char* path, u32 offset, void* buffer, u32 capacity) {
    fat_node_t node;
    int result = resolve_node(path, &node, NULL, NULL);
    if (result) {
        return result;
    }
    if (node.entry.attributes & 0x10) {
        return VFS_EISDIR;
    }
    u32 count, bytes = boot_sector.sectors_per_cluster * 512u;
    result = chain_length(node.entry.first_cluster, &count);
    if (result) {
        return result;
    }
    if (node.entry.file_size / bytes + (node.entry.file_size % bytes != 0) != count) {
        return VFS_ECORRUPT;
    }
    if (offset >= node.entry.file_size) {
        return 0;
    }
    u32 length = node.entry.file_size - offset;
    if (length > capacity) {
        length = capacity;
    }
    u32 c = node.entry.first_cluster;
    for (u32 n = offset / bytes; n; --n) {
        c = fat_get(c);
    }
    u32 position = offset % bytes, done = 0;
    u16 raw[256];
    while (done < length) {
        u32 sector = ctx.data_start_sector + (c - 2) * boot_sector.sectors_per_cluster + position / 512;
        if (read_sector(sector, raw)) {
            return VFS_EIO;
        }
        u32 n = 512 - position % 512;
        if (n > length - done) {
            n = length - done;
        }
        memcpy((u8*)buffer + done, (u8*)raw + position % 512, n);
        done += n;
        position += n;
        if (position == bytes) {
            c = fat_get(c);
            position = 0;
        }
    }
    return done;
}

static int driver_replace(const char* path, const void* data, u32 size) {
    u16 parent;
    char name[11];
    fat_node_t node;
    int result = resolve_node(path, NULL, &parent, name);
    if (result) {
        return result;
    }
    result = directory_lookup(parent, name, &node, NULL);
    if (result && result != VFS_ENOENT) {
        return result;
    }
    if (!result) {
        if (node.entry.attributes & 0x10) {
            return VFS_EISDIR;
        }
        if (node.entry.attributes & 1) {
            return VFS_EROFS;
        }
        u32 count;
        result = chain_length(node.entry.first_cluster, &count);
        if (result) {
            return result;
        }
        u32 bytes = boot_sector.sectors_per_cluster * 512u;
        if (count != node.entry.file_size / bytes + (node.entry.file_size % bytes != 0)) {
            return VFS_ECORRUPT;
        }
    } else {
        memset(&node, 0, sizeof(node));
        result = available_slot(parent, name, &node.slot);
        if (result) {
            return result;
        }
        memcpy(&node.entry, name, 11);
        node.entry.attributes = 0x20;
    }
    u32 bytes = boot_sector.sectors_per_cluster * 512u;
    u32 needed = size / bytes + (size % bytes != 0);
    u16 first;
    result = reserve_chain(needed, &first);
    if (result) {
        return result;
    }
    u16 raw[256];
    u32 left = size;
    const u8* source = data;
    for (u32 c = first; valid_cluster(c); c = fat_get(c)) {
        for (u32 s = 0; s < boot_sector.sectors_per_cluster; ++s) {
            u32 n = left > 512 ? 512 : left;
            memset(raw, 0, sizeof(raw));
            if (n) {
                memcpy(raw, source, n);
            }
            if (write_sector(ctx.data_start_sector + (c - 2) * boot_sector.sectors_per_cluster + s, raw)) {
                return VFS_EIO;
            }
            if (n) {
                source += n;
            }
            left -= n;
        }
    }
    if (sync_fat()) {
        return VFS_EIO;
    }
    u16 old = node.entry.first_cluster;
    node.entry.first_cluster = first;
    node.entry.file_size = size;
    result = directory_entry(parent, node.slot, &node.entry, 1);
    if (result < 0) {
        return result;
    }
    free_chain(old);
    return sync_fat() ? VFS_EIO : 0;
}

static int driver_create(const char* path, int directory) {
    u16 parent;
    char name[11];
    int result = resolve_node(path, NULL, &parent, name);
    if (result) {
        return result;
    }
    u32 slot;
    result = available_slot(parent, name, &slot);
    if (result) {
        return result;
    }
    fat12_dir_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    memcpy(&entry, name, 11);
    entry.attributes = directory ? 0x10 : 0x20;
    if (directory) {
        u16 first;
        result = reserve_chain(1, &first);
        if (result) {
            return result;
        }
        result = zero_chain(first);
        if (result) {
            return result;
        }
        entry.first_cluster = first;
        fat12_dir_entry_t dot;
        memset(&dot, 0, sizeof(dot));
        memset(&dot, ' ', 11);
        dot.filename[0] = '.';
        dot.attributes = 0x10;
        dot.first_cluster = first;
        result = directory_entry(first, 0, &dot, 1);
        if (result < 0) {
            return result;
        }
        dot.filename[1] = '.';
        dot.first_cluster = parent;
        result = directory_entry(first, 1, &dot, 1);
        if (result < 0) {
            return result;
        }
        if (sync_fat()) {
            return VFS_EIO;
        }
    }
    result = directory_entry(parent, slot, &entry, 1);
    return result < 0 ? result : 0;
}

static void entry_name(const fat12_dir_entry_t* entry, char name[13]) {
    u32 n = 0;
    for (u32 i = 0; i < 8 && entry->filename[i] != ' '; ++i) {
        name[n++] = entry->filename[i];
    }
    if (entry->extension[0] != ' ') {
        name[n++] = '.';
        for (u32 i = 0; i < 3 && entry->extension[i] != ' '; ++i) {
            name[n++] = entry->extension[i];
        }
    }
    name[n] = 0;
}

static int driver_readdir(const char* path, u32* cursor, vfs_dirent_t* output) {
    fat_node_t node;
    int result = resolve_node(path, &node, NULL, NULL);
    if (result) {
        return result;
    }
    if (!(node.entry.attributes & 0x10)) {
        return VFS_ENOTDIR;
    }
    if (strcmp(path, "/") && !valid_cluster(node.entry.first_cluster)) {
        return VFS_ECORRUPT;
    }
    for (;;) {
        fat12_dir_entry_t entry;
        result = directory_entry(node.entry.first_cluster, *cursor, &entry, 0);
        if (result <= 0) {
            return result;
        }
        ++*cursor;
        if (!entry.filename[0]) {
            return 0;
        }
        if ((u8)entry.filename[0] == 0xe5 || (entry.attributes & 8) || entry.filename[0] == '.') {
            continue;
        }
        entry_name(&entry, output->name);
        output->stat = (vfs_stat_t){ .size = entry.file_size,
                                     .directory = !!(entry.attributes & 0x10),
                                     .readonly = !!(entry.attributes & 1) };
        return 1;
    }
}

static int driver_remove(const char* path, int directory) {
    if (!strcmp(path, "/")) {
        return VFS_EBUSY;
    }
    fat_node_t node;
    int result = resolve_node(path, &node, NULL, NULL);
    if (result) {
        return result;
    }
    if (!!(node.entry.attributes & 0x10) != !!directory) {
        return directory ? VFS_ENOTDIR : VFS_EISDIR;
    }
    if (node.entry.attributes & 1) {
        return VFS_EROFS;
    }
    u16 checked_parent;
    char checked_name[11];
    result = resolve_node(path, NULL, &checked_parent, checked_name);
    if (result) {
        return result;
    }
    u32 count;
    result = chain_length(node.entry.first_cluster, &count);
    if (result) {
        return result;
    }
    if (directory) {
        u32 cursor = 0;
        vfs_dirent_t entry;
        result = driver_readdir(path, &cursor, &entry);
        if (result) {
            return result < 0 ? result : VFS_ENOTEMPTY;
        }
    }
    u16 first = node.entry.first_cluster;
    node.entry.filename[0] = (char)0xe5;
    result = directory_entry(node.parent, node.slot, &node.entry, 1);
    if (result < 0) {
        return result;
    }
    free_chain(first);
    return sync_fat() ? VFS_EIO : 0;
}

static int driver_rename(const char* source, const char* target) {
    if (!strcmp(source, "/")) {
        return VFS_EBUSY;
    }
    fat_node_t node;
    u16 parent;
    char name[11];
    int result = resolve_node(source, &node, NULL, NULL);
    if (result) {
        return result;
    }
    if (!strcmp(source, target)) {
        return 0;
    }
    if (node.entry.attributes & 1) {
        return VFS_EROFS;
    }
    u16 checked_parent;
    char checked_name[11];
    result = resolve_node(source, NULL, &checked_parent, checked_name);
    if (result) {
        return result;
    }
    u32 n = strlen(source);
    if ((node.entry.attributes & 0x10) && (u32)strlen(target) > n && !memcmp(source, target, n)
        && target[n] == '/') {
        return VFS_EINVAL;
    }
    result = resolve_node(target, NULL, &parent, name);
    if (result) {
        return result;
    }
    u32 slot;
    result = directory_lookup(parent, name, NULL, NULL);
    if (!result) {
        return VFS_EEXIST;
    }
    if (result != VFS_ENOENT) {
        return result;
    }
    if (parent == node.parent) {
        memcpy(&node.entry, name, 11);
        result = directory_entry(parent, node.slot, &node.entry, 1);
        return result < 0 ? result : 0;
    }
    result = available_slot(parent, name, &slot);
    if (result) {
        return result;
    }
    fat12_dir_entry_t moved = node.entry;
    memcpy(&moved, name, 11);
    result = directory_entry(parent, slot, &moved, 1);
    if (result < 0) {
        return result;
    }
    if (node.entry.attributes & 0x10) {
        fat12_dir_entry_t dotdot;
        result = directory_entry(node.entry.first_cluster, 1, &dotdot, 0);
        if (result <= 0) {
            mounted = 0; /* Destination already exists; do not allow further mutation. */
            return result < 0 ? result : VFS_ECORRUPT;
        }
        dotdot.first_cluster = parent;
        result = directory_entry(node.entry.first_cluster, 1, &dotdot, 1);
        if (result < 0) {
            return result;
        }
    }
    node.entry.filename[0] = (char)0xe5;
    result = directory_entry(node.parent, node.slot, &node.entry, 1);
    return result < 0 ? result : 0;
}

static const vfs_ops_t fat_ops = { .stat = driver_stat,
                                   .read = driver_read,
                                   .replace = driver_replace,
                                   .create = driver_create,
                                   .remove = driver_remove,
                                   .rename = driver_rename,
                                   .readdir = driver_readdir };

/* Reject cross-links before allowing writes: a local chain check alone cannot
 * prevent deleting clusters owned by another file. Orphans are rejected too. */
static int check_volume(void) {
    typedef struct {
        u16 cluster, parent;
    } pending_t;

    pending_t* queue = kmalloc((ctx.total_clusters + 1) * sizeof(pending_t));
    if (!queue) {
        return VFS_ENOMEM;
    }
    u8 owned[512];
    memset(owned, 0, sizeof(owned));
    u32 head = 0, tail = 1;
    queue[0] = (pending_t){ 0, 0 };
    int error = 0;
    while (head < tail && !error) {
        pending_t current = queue[head++];
        if (current.cluster) {
            for (u32 dot_index = 0; dot_index < 2; ++dot_index) {
                fat12_dir_entry_t dot;
                int result = directory_entry(current.cluster, dot_index, &dot, 0);
                char expected[11];
                memset(expected, ' ', sizeof(expected));
                expected[0] = '.';
                if (dot_index) {
                    expected[1] = '.';
                }
                if (result != 1 || memcmp(&dot, expected, 11) || !(dot.attributes & 0x10)
                    || dot.first_cluster != (dot_index ? current.parent : current.cluster)) {
                    error = result < 0 ? result : VFS_ECORRUPT;
                    break;
                }
            }
            if (error) {
                break;
            }
        }
        for (u32 index = 0;; ++index) {
            fat12_dir_entry_t entry;
            int result = directory_entry(current.cluster, index, &entry, 0);
            if (result < 0) {
                error = result;
                break;
            }
            if (!result || !entry.filename[0]) {
                break;
            }
            if ((u8)entry.filename[0] == 0xe5 || (entry.attributes & 8)) {
                continue;
            }
            if (entry.filename[0] == '.') {
                int parent = entry.filename[1] == '.';
                if (!current.cluster || index != (u32)parent || !(entry.attributes & 0x10)
                    || entry.first_cluster != (parent ? current.parent : current.cluster)) {
                    error = VFS_ECORRUPT;
                }
                if (error) {
                    break;
                }
                continue;
            }
            u32 count;
            error = chain_length(entry.first_cluster, &count);
            if (error) {
                break;
            }
            int directory = !!(entry.attributes & 0x10);
            u32 bytes = boot_sector.sectors_per_cluster * 512u;
            if ((directory && (!count || entry.file_size))
                || (!directory && count != entry.file_size / bytes + (entry.file_size % bytes != 0))) {
                error = VFS_ECORRUPT;
                break;
            }
            for (u32 c = entry.first_cluster; valid_cluster(c); c = fat_get(c)) {
                if (owned[c / 8] & (1u << (c % 8))) {
                    error = VFS_ECORRUPT;
                    break;
                }
                owned[c / 8] |= 1u << (c % 8);
            }
            if (error) {
                break;
            }
            if (directory) {
                if (tail > ctx.total_clusters) {
                    error = VFS_ECORRUPT;
                    break;
                }
                queue[tail++] = (pending_t){ entry.first_cluster, current.cluster };
            }
        }
    }
    if (!error) {
        for (u32 c = 2; valid_cluster(c); ++c) {
            u16 value = fat_get(c);
            if (value && value != 0xff7 && !(owned[c / 8] & (1u << (c % 8)))) {
                error = VFS_ECORRUPT;
                break;
            }
        }
    }
    kfree(queue);
    return error;
}

int fat12_check(void) {
    return mounted ? check_volume() : VFS_EIO;
}

void fat12_usage(u32* sectors, u32* bytes, u32* clusters, u32* free_clusters) {
    *sectors = *bytes = *clusters = *free_clusters = 0;
    if (!mounted) {
        return;
    }
    *sectors = boot_sector.total_sectors ? boot_sector.total_sectors : boot_sector.large_sector_count;
    *bytes = boot_sector.sectors_per_cluster * 512u;
    *clusters = ctx.total_clusters;
    for (u32 c = 2; valid_cluster(c); ++c) {
        if (!fat_get(c)) {
            ++*free_clusters;
        }
    }
}

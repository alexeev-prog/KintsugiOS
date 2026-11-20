#include "block.h"

#include "../drivers/ata_pio.h"
#include "../kklibc/kklibc.h"

#define BLOCK_CACHE_ENTRIES 8

typedef struct {
    u32 sector;
    u32 age;
    u8 valid, dirty;
    u8 data[FS_BLOCK_SIZE];
} cache_entry_t;

static cache_entry_t cache[BLOCK_CACHE_ENTRIES];
static u32 clock_hand;

static int flush_entry(cache_entry_t* entry) {
    if (!entry->valid || !entry->dirty) return 0;
    if (ata_pio_write_sectors(ATA_MASTER, entry->sector, 1, (u16*)entry->data)) return -1;
    entry->dirty = 0;
    return 0;
}

static cache_entry_t* find(u32 sector) {
    for (u32 i = 0; i < BLOCK_CACHE_ENTRIES; ++i) {
        if (cache[i].valid && cache[i].sector == sector) {
            cache[i].age = ++clock_hand;
            return &cache[i];
        }
    }
    return NULL;
}

static cache_entry_t* obtain(u32 sector) {
    cache_entry_t* entry = find(sector);
    if (entry) return entry;
    entry = &cache[0];
    for (u32 i = 1; i < BLOCK_CACHE_ENTRIES; ++i) {
        if (!cache[i].valid) { entry = &cache[i]; break; }
        if (cache[i].age < entry->age) entry = &cache[i];
    }
    if (flush_entry(entry) || ata_pio_read_sectors(ATA_MASTER, sector, 1, (u16*)entry->data)) {
        entry->valid = 0;
        return NULL;
    }
    entry->sector = sector;
    entry->valid = 1;
    entry->dirty = 0;
    entry->age = ++clock_hand;
    return entry;
}

int block_read(u32 sector, void* buffer) {
    cache_entry_t* entry;
    if (!buffer || !(entry = obtain(sector))) return -1;
    memcpy(buffer, entry->data, FS_BLOCK_SIZE);
    return 0;
}

int block_write(u32 sector, const void* buffer) {
    cache_entry_t* entry;
    if (!buffer || !(entry = obtain(sector))) return -1;
    memcpy(entry->data, buffer, FS_BLOCK_SIZE);
    entry->dirty = 1;
    return 0;
}

int block_sync(void) {
    for (u32 i = 0; i < BLOCK_CACHE_ENTRIES; ++i) {
        if (flush_entry(&cache[i])) return -1;
    }
    return ata_pio_flush() ? -1 : 0;
}

void block_invalidate(void) {
    memset(cache, 0, sizeof(cache));
    clock_hand = 0;
}

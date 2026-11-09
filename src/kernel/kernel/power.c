#include "power.h"

#include "../drivers/ata_pio.h"
#include "../drivers/lowlevel_io.h"
#include "../drivers/terminal.h"
#include "../fs/vfs.h"
#include "../kklibc/kklibc.h"
#include "../mm/memory.h"
#include "../user/process.h"
#include "log.h"

static u32 word32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int checksum(const u8* p, u32 n) {
    u8 sum = 0;
    for (u32 i = 0; i < n; ++i) {
        sum += p[i];
    }
    return !sum;
}

static const u8* table(u32 address, const char* signature) {
    if (address < 0x1000 || address > PMM_LIMIT - 36) {
        return NULL;
    }
    const u8* p = (const u8*)address;
    u32 n = word32(p + 4);
    if (memcmp(p, signature, 4) || n < 36 || n > 1024 * 1024 || n > PMM_LIMIT - address || !checksum(p, n)) {
        return NULL;
    }
    return p;
}

static const u8* find_rsdp(u32 start, u32 end) {
    for (u32 at = start; at + 20 <= end; at += 16) {
        const u8* p = (const u8*)at;
        if (!memcmp(p, "RSD PTR ", 8) && checksum(p, 20)) {
            return p;
        }
    }
    return NULL;
}

/* Deliberately small ACPI subset: legacy PM1 I/O blocks and a literal _S5 package.
 * Unsupported firmware is halted safely, not guessed using emulator-only ports. */
static int acpi_poweroff(void) {
    /* Keep the null page unmapped; access BIOS data through a temporary alias. */
    if (!vmm_map_page(0xd0000000u, 0, 0)) {
        return 0;
    }
    u32 ebda = *(volatile u16*)0xd000040e << 4;
    vmm_unmap_page(0xd0000000u);
    const u8* rsdp = ebda >= 0x80000 && ebda <= 0x9fc00 ? find_rsdp(ebda, ebda + 1024) : NULL;
    if (!rsdp) {
        rsdp = find_rsdp(0xe0000, 0x100000);
    }
    if (!rsdp) {
        return 0;
    }
    const u8* rsdt = table(word32(rsdp + 16), "RSDT");
    if (!rsdt) {
        return 0;
    }
    const u8* fadt = NULL;
    for (u32 i = 36; i + 4 <= word32(rsdt + 4); i += 4) {
        fadt = table(word32(rsdt + i), "FACP");
        if (fadt) {
            break;
        }
    }
    if (!fadt || word32(fadt + 4) < 90 || fadt[89] < 2) {
        return 0;
    }
    u32 pm1a = word32(fadt + 64), pm1b = word32(fadt + 68), smi = word32(fadt + 48);
    if (!pm1a || pm1a > 65535 || pm1b > 65535 || smi > 65535) {
        return 0;
    }
    const u8* dsdt = table(word32(fadt + 40), "DSDT");
    if (!dsdt) {
        return 0;
    }
    u32 type_a = 0, type_b = 0;
    int found = 0;
    for (u32 i = 37; i + 8 < word32(dsdt + 4); ++i) {
        if (memcmp(dsdt + i, "_S5_", 4) || dsdt[i + 4] != 0x12
            || !(dsdt[i - 1] == 0x08 || (i >= 2 && dsdt[i - 1] == '\\' && dsdt[i - 2] == 0x08))) {
            continue;
        }
        u32 at = i + 5, extra = dsdt[at] >> 6;
        if (at + extra + 7 >= word32(dsdt + 4)) {
            continue;
        }
        at += extra + 1;
        if (dsdt[at++] < 2) {
            continue;
        }
        if (dsdt[at] == 0x0a) {
            ++at;
        }
        type_a = dsdt[at++];
        if (dsdt[at] == 0x0a) {
            ++at;
        }
        type_b = dsdt[at];
        if (type_a > 7 || type_b > 7) {
            continue;
        }
        found = 1;
        break;
    }
    if (!found) {
        return 0;
    }
    if (!(port_word_in(pm1a) & 1)) {
        if (!smi || !fadt[52]) {
            return 0;
        }
        port_byte_out(smi, fadt[52]);
        u32 tries = 1000000;
        while (tries && !(port_word_in(pm1a) & 1)) {
            --tries;
        }
        if (!tries) {
            return 0;
        }
    }
    log_event("INFO", "power", "ACPI S5 power-off requested");
    port_word_out(pm1a, (port_word_in(pm1a) & ~0x3c00u) | (type_a << 10) | 0x2000);
    if (pm1b) {
        port_word_out(pm1b, (port_word_in(pm1b) & ~0x3c00u) | (type_b << 10) | 0x2000);
    }
    return 1;
}

int system_sync(void) {
    return ata_pio_flush() ? VFS_EIO : 0;
}

void system_shutdown(void) {
    __asm__ volatile("cli" ::: "memory");
    log_event("INFO", "power", "Shutdown: stopping processes and closing descriptors");
    process_shutdown();
    int result = system_sync();
    log_event(
        result ? "ERROR" : "INFO",
        "power",
        result ? "Disk flush failed; data may be lost" : "Disk caches synchronized");
    printf(result ? "Shutdown: disk flush failed.\n" : "Shutdown: disks synchronized.\n");
    terminal_refresh();
    acpi_poweroff();
    printf("System halted. You may turn off the power.\n");
    terminal_refresh();
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

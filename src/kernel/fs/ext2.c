#include "ext2.h"
#include "block.h"
#include "vfs.h"
#include "../kklibc/kklibc.h"
#include "../kernel/log.h"
#include "../drivers/ata_pio.h"

/* Second Extended Filesystem driver for KintsugiOS.
 * Supportedon-disk features: 1 KiB blocks, 128-byte inodes, direct/single/
 * double indirect block maps, regular files and directories. Extended
 * attributes, dir_index, 64-bit and checksum features are rejected at mount. */
#define EXT2_MAGIC 0xEF53u
#define EXT2_BLOCK 1024u
#define EXT2_SECTORS (EXT2_BLOCK / 512u)
#define EXT2_MAX_GROUPS 16u
#define EXT2_ROOT_INO 2u
#define EXT2_S_IFMT 0xF000u
#define EXT2_S_IFDIR 0x4000u
#define EXT2_S_IFREG 0x8000u
#define EXT2_FT_REG 1u
#define EXT2_FT_DIR 2u
#define EXT2_NAME_MAX 252u

typedef struct __attribute__((packed)) {
    u32 inodes_count, blocks_count, reserved_blocks, free_blocks, free_inodes;
    u32 first_data_block, log_block_size, log_cluster_size;
    u32 blocks_per_group, clusters_per_group, inodes_per_group;
    u32 mtime, wtime;
    u16 mnt_count, max_mnt_count, magic, state, errors, minor_rev;
    u32 lastcheck, checkinterval, creator_os, rev_level, def_resuid, def_resgid;
    u16 inode_size;
    u16 reserved_pad;
    u32 feature_compat, feature_incompat, feature_ro_compat;
} ext2_super;

typedef struct __attribute__((packed)) {
    u32 block_bitmap, inode_bitmap, inode_table;
    u16 free_blocks, free_inodes, used_dirs;
    u16 reserved[5];
} ext2_group;

typedef struct __attribute__((packed)) {
    u16 mode, uid;
    u32 size, atime, ctime, mtime, dtime;
    u16 gid, links_count;
    u32 flags, blocks, osd1;
    u32 block[15];
    u32 generation, file_acl, dir_acl, faddr;
    u8 osd2[12];
} ext2_inode;

typedef struct __attribute__((packed)) {
    u32 inode;
    u16 rec_len;
    u8 name_len, file_type;
} ext2_dirent;

static int mounted;
static ext2_super super;
static ext2_group groups[EXT2_MAX_GROUPS];
static u32 group_count, desc_size = 32;
static const vfs_ops_t ext2_ops;

static int block_read_at(u32 block, void* buffer) {
    u8* out = buffer;
    for (u32 i = 0; i < EXT2_SECTORS; ++i) {
        if (block_read(block * EXT2_SECTORS + i, out + i * 512)) return VFS_EIO;
    }
    return 0;
}

static int block_write_at(u32 block, const void* buffer) {
    const u8* in = buffer;
    for (u32 i = 0; i < EXT2_SECTORS; ++i) {
        if (block_write(block * EXT2_SECTORS + i, in + i * 512)) return VFS_EIO;
    }
    return 0;
}

static int super_write(void) {
    u8 buffer[EXT2_BLOCK];
    if (block_read_at(1, buffer)) return VFS_EIO;
    memcpy(buffer, &super, sizeof(super));
    return block_write_at(1, buffer) ? VFS_EIO : 0;
}

static int group_write(u32 group) {
    u8 buffer[EXT2_BLOCK];
    u32 byte = group * desc_size;
    u32 block = super.first_data_block + 1 + byte / EXT2_BLOCK;
    u32 offset = byte % EXT2_BLOCK;
    if (block_read_at(block, buffer)) return VFS_EIO;
    memcpy(buffer + offset, &groups[group], sizeof(ext2_group));
    return block_write_at(block, buffer) ? VFS_EIO : 0;
}

static u32 inode_block(u32 ino) {
    u32 group = (ino - 1) / super.inodes_per_group;
    u32 index = (ino - 1) % super.inodes_per_group;
    if (group >= group_count) return 0;
    u32 byte = index * super.inode_size;
    return groups[group].inode_table + (u32)(byte / EXT2_BLOCK);
}

static int inode_read(u32 ino, ext2_inode* out) {
    if (ino < 1 || ino > super.inodes_count) return VFS_EINVAL;
    u8 buffer[EXT2_BLOCK];
    u32 block = inode_block(ino);
    if (!block || block_read_at(block, buffer)) return VFS_EIO;
    u32 index = (ino - 1) % super.inodes_per_group;
    u32 offset = (index * super.inode_size) % EXT2_BLOCK;
    memcpy(out, buffer + offset, sizeof(*out));
    return 0;
}

static int inode_write(u32 ino, const ext2_inode* in) {
    if (ino < 1 || ino > super.inodes_count) return VFS_EINVAL;
    u8 buffer[EXT2_BLOCK];
    u32 block = inode_block(ino);
    if (!block || block_read_at(block, buffer)) return VFS_EIO;
    u32 index = (ino - 1) % super.inodes_per_group;
    u32 offset = (index * super.inode_size) % EXT2_BLOCK;
    memcpy(buffer + offset, in, sizeof(*in));
    return block_write_at(block, buffer) ? VFS_EIO : 0;
}

/* ---- allocation -------------------------------------------------------- */

static int bitmap_flip(u32 bitmap_block, u32 bit, int used) {
    u8 buffer[EXT2_BLOCK];
    if (block_read_at(bitmap_block, buffer)) return VFS_EIO;
    u8 mask = (u8)(1u << (bit & 7));
    if (used) buffer[bit / 8] |= mask;
    else buffer[bit / 8] &= (u8)~mask;
    return block_write_at(bitmap_block, buffer) ? VFS_EIO : 0;
}

static int block_alloc(u32* out) {
    for (u32 g = 0; g < group_count; ++g) {
        if (!groups[g].free_blocks) continue;
        u8 buffer[EXT2_BLOCK];
        if (block_read_at(groups[g].block_bitmap, buffer)) return VFS_EIO;
        for (u32 bit = 0; bit < super.blocks_per_group; ++bit) {
            if (buffer[bit / 8] & (1u << (bit & 7))) continue;
            u32 block = super.first_data_block + g * super.blocks_per_group + bit;
            if (!block || block >= super.blocks_count) continue;
            int result = bitmap_flip(groups[g].block_bitmap, bit, 1);
            if (result) return result;
            groups[g].free_blocks--;
            if (super.free_blocks) super.free_blocks--;
            if (group_write(g) || super_write()) return VFS_EIO;
            memset(buffer, 0, EXT2_BLOCK);
            result = block_write_at(block, buffer);
            if (result) return result;
            *out = block;
            return 0;
        }
    }
    return VFS_ENOSPC;
}

static void block_free(u32 block) {
    if (!block) return;
    u32 relative = block - super.first_data_block;
    u32 g = relative / super.blocks_per_group;
    u32 bit = relative % super.blocks_per_group;
    if (g >= group_count) return;
    if (bitmap_flip(groups[g].block_bitmap, bit, 0)) return;
    groups[g].free_blocks++;
    if (super.free_blocks < super.blocks_count) super.free_blocks++;
    group_write(g);
    super_write();
}

static int inode_alloc(u32* out) {
    for (u32 g = 0; g < group_count; ++g) {
        if (!groups[g].free_inodes) continue;
        u8 buffer[EXT2_BLOCK];
        if (block_read_at(groups[g].inode_bitmap, buffer)) return VFS_EIO;
        for (u32 bit = 0; bit < super.inodes_per_group; ++bit) {
            if (bit == 0 || (buffer[bit / 8] & (1u << (bit & 7)))) continue;
            int result = bitmap_flip(groups[g].inode_bitmap, bit, 1);
            if (result) return result;
            groups[g].free_inodes--;
            if (super.free_inodes) super.free_inodes--;
            if (group_write(g) || super_write()) return VFS_EIO;
            *out = g * super.inodes_per_group + bit + 1;
            return 0;
        }
    }
    return VFS_ENOSPC;
}

static void inode_free(u32 ino) {
    u32 g = (ino - 1) / super.inodes_per_group;
    u32 bit = (ino - 1) % super.inodes_per_group;
    if (g >= group_count || !bit) return;
    if (bitmap_flip(groups[g].inode_bitmap, bit, 0)) return;
    groups[g].free_inodes++;
    if (super.free_inodes < super.inodes_count) super.free_inodes++;
    group_write(g);
    super_write();
}

/* ---- block mapping ----------------------------------------------------- */

static int indirect_slot(u32* pointers_block, u32 index, int alloc, u32* out) {
    u8 buffer[EXT2_BLOCK];
    u32* slots = (u32*)buffer;
    if (block_read_at(*pointers_block, buffer)) return VFS_EIO;
    u32 value = slots[index];
    if (!value && alloc) {
        int result = block_alloc(&value);
        if (result) return result;
        slots[index] = value;
        if (block_write_at(*pointers_block, buffer)) return VFS_EIO;
    }
    *out = value;
    return 0;
}

/* Maps a logical 1 KiB block to a physical block. With alloc=1, missing
 * indirect blocks and data blocks are allocated and the inode is updated. */
static int inode_bmap(ext2_inode* inode, u32 logical, int alloc, u32* out) {
    *out = 0;
    if (logical < 12) {
        u32 value = inode->block[logical];
        if (!value && alloc) {
            int result = block_alloc(&value);
            if (result) return result;
            inode->block[logical] = value;
            inode->blocks += EXT2_BLOCK / 512;
        }
        *out = value;
        return 0;
    }
    u32 index = logical - 12;
    if (index < 256) {
        u32 pointer = inode->block[12];
        if (!pointer && alloc) {
            int result = block_alloc(&pointer);
            if (result) return result;
            inode->block[12] = pointer;
            inode->blocks += EXT2_BLOCK / 512;
        }
        if (!pointer) return 0;
        return indirect_slot(&pointer, index, alloc, out);
    }
    index -= 256;
    if (index < 65536) {
        u32 pointer = inode->block[13];
        if (!pointer && alloc) {
            int result = block_alloc(&pointer);
            if (result) return result;
            inode->block[13] = pointer;
            inode->blocks += EXT2_BLOCK / 512;
        }
        if (!pointer) return 0;
        u32 first;
        int result = indirect_slot(&pointer, index >> 8, alloc, &first);
        if (result) return result;
        if (!first) return 0;
        return indirect_slot(&first, index & 255, alloc, out);
    }
    index -= 65536;
    if (index < 0x1000000u) {
        u32 pointer = inode->block[14];
        if (!pointer && alloc) {
            int result = block_alloc(&pointer);
            if (result) return result;
            inode->block[14] = pointer;
            inode->blocks += EXT2_BLOCK / 512;
        }
        if (!pointer) return 0;
        u32 first, second;
        int result = indirect_slot(&pointer, index >> 16, alloc, &first);
        if (result) return result;
        if (!first) return 0;
        result = indirect_slot(&first, (index >> 8) & 255, alloc, &second);
        if (result) return result;
        if (!second) return 0;
        return indirect_slot(&second, index & 255, alloc, out);
    }
    return VFS_EINVAL;
}

static void inode_free_blocks(ext2_inode* inode) {
    for (u32 i = 0; i < 12; ++i) {
        block_free(inode->block[i]);
        inode->block[i] = 0;
    }
    for (int level = 12; level <= 14; ++level) {
        if (!inode->block[level]) continue;
        u8 buffer[EXT2_BLOCK];
        u32* slots = (u32*)buffer;
        if (block_read_at(inode->block[level], buffer)) continue;
        if (level == 12) {
            for (u32 i = 0; i < 256; ++i) block_free(slots[i]);
        } else {
            for (u32 i = 0; i < 256; ++i) {
                if (!slots[i]) continue;
                u8 nested[EXT2_BLOCK];
                u32* inner = (u32*)nested;
                if (block_read_at(slots[i], nested)) continue;
                if (level == 13) {
                    for (u32 j = 0; j < 256; ++j) block_free(inner[j]);
                } else {
                    for (u32 j = 0; j < 256; ++j) {
                        if (!inner[j]) continue;
                        u8 leaf[EXT2_BLOCK];
                        u32* entries = (u32*)leaf;
                        if (block_read_at(inner[j], leaf)) continue;
                        for (u32 k = 0; k < 256; ++k) block_free(entries[k]);
                        block_free(inner[j]);
                    }
                }
                block_free(slots[i]);
            }
        }
        block_free(inode->block[level]);
        inode->block[level] = 0;
    }
    inode->blocks = 0;
}

/* ---- directory helpers ------------------------------------------------- */

static u32 name_length(const char* name) {
    u32 n = 0;
    while (name[n] && n < EXT2_NAME_MAX) ++n;
    return n;
}

static int name_equal(const char* a, u32 length, const char* b) {
    if (name_length(b) != length) return 0;
    for (u32 i = 0; i < length; ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
    }
    return 1;
}

static int dir_find(u32 dir_ino, const char* name, u32* found, u32* entry_offset) {
    ext2_inode inode;
    int result = inode_read(dir_ino, &inode);
    if (result) return result;
    if (!(inode.mode & EXT2_S_IFDIR)) return VFS_ENOTDIR;
    u32 blocks = (inode.size + EXT2_BLOCK - 1) / EXT2_BLOCK;
    for (u32 b = 0; b < blocks; ++b) {
        u32 physical;
        result = inode_bmap(&inode, b, 0, &physical);
        if (result) return result;
        if (!physical) continue;
        u8 buffer[EXT2_BLOCK];
        if (block_read_at(physical, buffer)) return VFS_EIO;
        u32 offset = 0;
        while (offset + 8 <= EXT2_BLOCK) {
            ext2_dirent* entry = (ext2_dirent*)(buffer + offset);
            if (!entry->rec_len || offset + entry->rec_len > EXT2_BLOCK) break;
            if (entry->inode && entry->name_len && entry->name_len <= EXT2_NAME_MAX) {
                if (name_equal((char*)(entry + 1), entry->name_len, name)) {
                    if (found) *found = entry->inode;
                    if (entry_offset) *entry_offset = b * EXT2_BLOCK + offset;
                    return 0;
                }
            }
            offset += entry->rec_len;
        }
    }
    return VFS_ENOENT;
}

static u32 align4(u32 value) { return (value + 3u) & ~3u; }

static int dir_add(u32 dir_ino, const char* name, u32 child, u8 file_type) {
    u32 length = name_length(name);
    if (!length || length > EXT2_NAME_MAX) return VFS_EINVAL;
    u32 needed = align4(8 + length);
    ext2_inode inode;
    int result = inode_read(dir_ino, &inode);
    if (result) return result;
    u32 blocks = (inode.size + EXT2_BLOCK - 1) / EXT2_BLOCK;
    for (u32 b = 0; b < blocks; ++b) {
        u32 physical;
        result = inode_bmap(&inode, b, 0, &physical);
        if (result) return result;
        if (!physical) continue;
        u8 buffer[EXT2_BLOCK];
        if (block_read_at(physical, buffer)) return VFS_EIO;
        u32 offset = 0;
        while (offset + 8 <= EXT2_BLOCK) {
            ext2_dirent* entry = (ext2_dirent*)(buffer + offset);
            if (!entry->rec_len || offset + entry->rec_len > EXT2_BLOCK) break;
            if (entry->inode && entry->name_len == length
                && name_equal((char*)(entry + 1), length, name)) return VFS_EEXIST;
            if (!entry->inode && entry->rec_len >= needed) {
                entry->inode = child;
                entry->name_len = (u8)length;
                entry->file_type = file_type;
                memcpy(entry + 1, name, length);
                if (block_write_at(physical, buffer)) return VFS_EIO;
                return inode_write(dir_ino, &inode);
            }
            if (offset + entry->rec_len == EXT2_BLOCK) {
                u32 start = offset + align4(8 + entry->name_len);
                if (start + needed <= EXT2_BLOCK) {
                    entry->rec_len = (u16)(start - offset);
                    ext2_dirent* fresh = (ext2_dirent*)(buffer + start);
                    fresh->inode = child;
                    fresh->rec_len = (u16)(EXT2_BLOCK - start);
                    fresh->name_len = (u8)length;
                    fresh->file_type = file_type;
                    memcpy(fresh + 1, name, length);
                    if (block_write_at(physical, buffer)) return VFS_EIO;
                    return inode_write(dir_ino, &inode);
                }
            }
            offset += entry->rec_len;
        }
    }
    /* Append a fresh block holding a single entry. */
    u32 physical;
    result = inode_bmap(&inode, blocks, 1, &physical);
    if (result) return result;
    u8 buffer[EXT2_BLOCK];
    memset(buffer, 0, sizeof(buffer));
    ext2_dirent* fresh = (ext2_dirent*)buffer;
    fresh->inode = child;
    fresh->rec_len = EXT2_BLOCK;
    fresh->name_len = (u8)length;
    fresh->file_type = file_type;
    memcpy(fresh + 1, name, length);
    if (block_write_at(physical, buffer)) return VFS_EIO;
    inode.size += EXT2_BLOCK;
    return inode_write(dir_ino, &inode);
}

static int dir_remove(u32 dir_ino, const char* name) {
    u32 entry_offset;
    int result = dir_find(dir_ino, name, 0, &entry_offset);
    if (result) return result;
    ext2_inode inode;
    result = inode_read(dir_ino, &inode);
    if (result) return result;
    u32 physical;
    result = inode_bmap(&inode, entry_offset / EXT2_BLOCK, 0, &physical);
    if (result) return result;
    if (!physical) return VFS_EIO;
    u8 buffer[EXT2_BLOCK];
    if (block_read_at(physical, buffer)) return VFS_EIO;
    ext2_dirent* entry = (ext2_dirent*)(buffer + entry_offset % EXT2_BLOCK);
    entry->inode = 0;
    return block_write_at(physical, buffer) ? VFS_EIO : 0;
}

/* ---- path resolution --------------------------------------------------- */

static int split_path(const char* path, char parent[VFS_PATH_MAX], char name[EXT2_NAME_MAX + 1]) {
    if (!path || path[0] != '/') return VFS_EINVAL;
    u32 length = 0;
    while (path[length]) ++length;
    while (length > 1 && path[length - 1] == '/') --length;
    u32 slash = length;
    while (slash > 1 && path[slash - 1] != '/') --slash;
    u32 n = 0;
    while (slash + n < length && n < EXT2_NAME_MAX) { name[n] = path[slash + n]; ++n; }
    name[n] = 0;
    if (!n) return VFS_EINVAL;
    u32 parent_len = slash > 1 ? slash - 1 : 1;
    for (u32 i = 0; i < parent_len && i + 1 < VFS_PATH_MAX; ++i) parent[i] = path[i];
    parent[parent_len] = 0;
    return 0;
}

static int lookup(const char* path, u32* ino) {
    if (!path || path[0] != '/') return VFS_EINVAL;
    u32 current = EXT2_ROOT_INO;
    const char* p = path;
    while (*p) {
        while (*p == '/') ++p;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != '/') ++p;
        u32 length = (u32)(p - start);
        if (length == 1 && start[0] == '.') continue;
        char component[EXT2_NAME_MAX + 1];
        u32 n = length < EXT2_NAME_MAX ? length : EXT2_NAME_MAX;
        for (u32 i = 0; i < n; ++i) component[i] = start[i];
        component[n] = 0;
        int result = dir_find(current, component, &current, 0);
        if (result) return result;
    }
    *ino = current;
    return 0;
}

static int free_file(u32 ino) {
    ext2_inode inode;
    if (inode_read(ino, &inode)) return VFS_EIO;
    inode_free_blocks(&inode);
    memset(&inode, 0, sizeof(inode));
    if (inode_write(ino, &inode)) return VFS_EIO;
    inode_free(ino);
    return 0;
}

/* ---- VFS operations ---------------------------------------------------- */

static int stat_op(const char* path, vfs_stat_t* stat) {
    u32 ino;
    int result = lookup(path, &ino);
    if (result) return result;
    ext2_inode inode;
    result = inode_read(ino, &inode);
    if (result) return result;
    stat->size = inode.size;
    stat->directory = (inode.mode & EXT2_S_IFDIR) ? 1 : 0;
    stat->readonly = (inode.mode & 0222) ? 0 : 1;
    return 0;
}

static int read_op(const char* path, u32 offset, void* buffer, u32 size) {
    u32 ino;
    int result = lookup(path, &ino);
    if (result) return result;
    ext2_inode inode;
    result = inode_read(ino, &inode);
    if (result) return result;
    if (inode.mode & EXT2_S_IFDIR) return VFS_EISDIR;
    if (offset >= inode.size) return 0;
    if (size > inode.size - offset) size = inode.size - offset;
    u8* out = buffer;
    u32 done = 0;
    while (done < size) {
        u32 logical = (offset + done) / EXT2_BLOCK;
        u32 within = (offset + done) % EXT2_BLOCK;
        u32 physical;
        result = inode_bmap(&inode, logical, 0, &physical);
        if (result) return result;
        u32 chunk = size - done;
        if (chunk > EXT2_BLOCK - within) chunk = EXT2_BLOCK - within;
        if (!physical) memset(out + done, 0, chunk);
        else {
            u8 block[EXT2_BLOCK];
            if (block_read_at(physical, block)) return VFS_EIO;
            memcpy(out + done, block + within, chunk);
        }
        done += chunk;
    }
    return (int)done;
}

static int write_op(const char* path, u32 offset, const void* buffer, u32 size) {
    u32 ino;
    int result = lookup(path, &ino);
    if (result) return result;
    ext2_inode inode;
    result = inode_read(ino, &inode);
    if (result) return result;
    if (inode.mode & EXT2_S_IFDIR) return VFS_EISDIR;
    const u8* in = buffer;
    u32 done = 0;
    while (done < size) {
        u32 logical = (offset + done) / EXT2_BLOCK;
        u32 within = (offset + done) % EXT2_BLOCK;
        u32 physical;
        result = inode_bmap(&inode, logical, 1, &physical);
        if (result) return result;
        if (!physical) return VFS_ENOSPC;
        u8 block[EXT2_BLOCK];
        if (block_read_at(physical, block)) return VFS_EIO;
        u32 chunk = size - done;
        if (chunk > EXT2_BLOCK - within) chunk = EXT2_BLOCK - within;
        memcpy(block + within, in + done, chunk);
        if (block_write_at(physical, block)) return VFS_EIO;
        done += chunk;
    }
    if (offset + size > inode.size) inode.size = offset + size;
    if (size) inode.mtime = 0;
    return inode_write(ino, &inode) ? VFS_EIO : (int)size;
}

static int truncate_op(const char* path, u32 size) {
    u32 ino;
    int result = lookup(path, &ino);
    if (result) return result;
    ext2_inode inode;
    result = inode_read(ino, &inode);
    if (result) return result;
    if (inode.mode & EXT2_S_IFDIR) return VFS_EISDIR;
    u32 old_blocks = (inode.size + EXT2_BLOCK - 1) / EXT2_BLOCK;
    u32 new_blocks = (size + EXT2_BLOCK - 1) / EXT2_BLOCK;
    for (u32 b = new_blocks; b < old_blocks; ++b) {
        u32 physical;
        if (!inode_bmap(&inode, b, 0, &physical) && physical) block_free(physical);
        if (b >= 12 && b < 12 + 256 && b == 12) inode.block[12] = 0;
        if (b >= 268 && b == 268) inode.block[13] = 0;
    }
    if (new_blocks < old_blocks && size) {
        u32 physical;
        if (!inode_bmap(&inode, new_blocks - 1, 0, &physical) && physical) {
            u8 buffer[EXT2_BLOCK];
            if (!block_read_at(physical, buffer)) {
                u32 tail = size % EXT2_BLOCK;
                if (tail) memset(buffer + tail, 0, EXT2_BLOCK - tail);
                block_write_at(physical, buffer);
            }
        }
    }
    inode.size = size;
    if (!size) inode_free_blocks(&inode);
    return inode_write(ino, &inode) ? VFS_EIO : 0;
}

static int replace_op(const char* path, const void* buffer, u32 size) {
    int result = truncate_op(path, 0);
    if (result && result != VFS_ENOENT) return result;
    if (result == VFS_ENOENT) {
        result = ext2_ops.create(path, 0);
        if (result) return result;
    }
    return write_op(path, 0, buffer, size) < 0 ? VFS_EIO : 0;
}

static int create_op(const char* path, int directory) {
    char parent[VFS_PATH_MAX], name[EXT2_NAME_MAX + 1];
    int result = split_path(path, parent, name);
    if (result) return result;
    u32 parent_ino;
    result = lookup(parent, &parent_ino);
    if (result) return result;
    if (dir_find(parent_ino, name, 0, 0) == 0) return VFS_EEXIST;
    u32 ino;
    result = inode_alloc(&ino);
    if (result) return result;
    ext2_inode inode;
    memset(&inode, 0, sizeof(inode));
    inode.mode = directory ? (EXT2_S_IFDIR | 0755) : (EXT2_S_IFREG | 0644);
    inode.links_count = directory ? 2 : 1;
    inode.size = directory ? EXT2_BLOCK : 0;
    inode.blocks = 0;
    inode.generation = 1;
    if (directory) {
        u32 physical;
        result = inode_bmap(&inode, 0, 1, &physical);
        if (result) return result;
        u8 buffer[EXT2_BLOCK];
        memset(buffer, 0, sizeof(buffer));
        ext2_dirent* dot = (ext2_dirent*)buffer;
        dot->inode = ino; dot->rec_len = 12; dot->name_len = 1; dot->file_type = EXT2_FT_DIR;
        memcpy((u8*)dot + 8, ".", 1);
        ext2_dirent* dotdot = (ext2_dirent*)(buffer + 12);
        dotdot->inode = parent_ino; dotdot->rec_len = EXT2_BLOCK - 12;
        dotdot->name_len = 2; dotdot->file_type = EXT2_FT_DIR;
        memcpy((u8*)dotdot + 8, "..", 2);
        if (block_write_at(physical, buffer)) return VFS_EIO;
    }
    if (inode_write(ino, &inode)) return VFS_EIO;
    result = dir_add(parent_ino, name, ino, directory ? EXT2_FT_DIR : EXT2_FT_REG);
    if (result) { free_file(ino); return result; }
    return 0;
}

static int remove_op(const char* path, int directory) {
    char parent[VFS_PATH_MAX], name[EXT2_NAME_MAX + 1];
    int result = split_path(path, parent, name);
    if (result) return result;
    u32 parent_ino, ino;
    result = lookup(parent, &parent_ino);
    if (result) return result;
    result = dir_find(parent_ino, name, &ino, 0);
    if (result) return result;
    ext2_inode inode;
    if (inode_read(ino, &inode)) return VFS_EIO;
    if ((inode.mode & EXT2_S_IFDIR ? 1 : 0) != directory)
        return directory ? VFS_ENOTDIR : VFS_EISDIR;
    if (directory && inode.size > EXT2_BLOCK * 2) return VFS_ENOTEMPTY;
    result = dir_remove(parent_ino, name);
    if (result) return result;
    return free_file(ino);
}

static int rename_op(const char* source, const char* destination) {
    char old_parent[VFS_PATH_MAX], old_name[EXT2_NAME_MAX + 1];
    char new_parent[VFS_PATH_MAX], new_name[EXT2_NAME_MAX + 1];
    int result = split_path(source, old_parent, old_name);
    if (result) return result;
    result = split_path(destination, new_parent, new_name);
    if (result) return result;
    u32 old_parent_ino, new_parent_ino, ino;
    result = lookup(old_parent, &old_parent_ino);
    if (result) return result;
    result = lookup(new_parent, &new_parent_ino);
    if (result) return result;
    result = dir_find(old_parent_ino, old_name, &ino, 0);
    if (result) return result;
    u32 existing;
    if (!dir_find(new_parent_ino, new_name, &existing, 0)) {
        ext2_inode target;
        if (inode_read(existing, &target)) return VFS_EIO;
        if ((target.mode & EXT2_S_IFDIR) && target.size > EXT2_BLOCK * 2) return VFS_ENOTEMPTY;
        result = dir_remove(new_parent_ino, new_name);
        if (result) return result;
        free_file(existing);
    }
    result = dir_add(new_parent_ino, new_name, ino, 0);
    if (result) return result;
    return dir_remove(old_parent_ino, old_name);
}

static int readdir_long_op(const char* path, u32* cursor, vfs_long_dirent_t* entry) {
    u32 ino;
    int result = lookup(path, &ino);
    if (result) return result;
    ext2_inode inode;
    result = inode_read(ino, &inode);
    if (result) return result;
    if (!(inode.mode & EXT2_S_IFDIR)) return VFS_ENOTDIR;
    while (*cursor < inode.size) {
        u32 logical = *cursor / EXT2_BLOCK;
        u32 within = *cursor % EXT2_BLOCK;
        u32 physical;
        result = inode_bmap(&inode, logical, 0, &physical);
        if (result) return result;
        if (!physical) { *cursor += EXT2_BLOCK; continue; }
        u8 buffer[EXT2_BLOCK];
        if (block_read_at(physical, buffer)) return VFS_EIO;
        ext2_dirent* dir = (ext2_dirent*)(buffer + within);
        if (!dir->rec_len || within + dir->rec_len > EXT2_BLOCK) return VFS_ECORRUPT;
        *cursor += dir->rec_len;
        if (!dir->inode || !dir->name_len) continue;
        if (dir->name_len == 1 && ((char*)(dir + 1))[0] == '.') continue;
        if (dir->name_len == 2 && !memcmp((char*)(dir + 1), "..", 2)) continue;
        memset(entry, 0, sizeof(*entry));
        u32 n = dir->name_len < KOS_LONG_NAME_MAX - 1 ? dir->name_len : KOS_LONG_NAME_MAX - 1;
        memcpy(entry->name, (char*)(dir + 1), n);
        entry->name[n] = 0;
        ext2_inode child;
        if (!inode_read(dir->inode, &child)) {
            entry->stat.size = child.size;
            entry->stat.directory = (child.mode & EXT2_S_IFDIR) ? 1 : 0;
            entry->stat.readonly = (child.mode & 0222) ? 0 : 1;
        }
        return 1;
    }
    return 0;
}

static int readdir_op(const char* path, u32* cursor, vfs_dirent_t* entry) {
    vfs_long_dirent_t long_entry;
    int result = readdir_long_op(path, cursor, &long_entry);
    if (result <= 0) return result;
    memset(entry, 0, sizeof(*entry));
    u32 n = 0;
    while (long_entry.name[n] && n + 1 < sizeof(entry->name)) { entry->name[n] = long_entry.name[n]; ++n; }
    entry->name[n] = 0;
    entry->stat = long_entry.stat;
    return 1;
}

static const vfs_ops_t ext2_ops = {
    stat_op, read_op, write_op, replace_op, create_op, remove_op, rename_op,
    readdir_op, readdir_long_op
};

int ext2_is_mounted(void) { return mounted; }

void ext2_cleanup(void) {
    if (mounted) block_sync();
    mounted = 0;
    vfs_mount(NULL);
    block_invalidate();
}

int ext2_init(void) {
    u8 buffer[EXT2_BLOCK];
    if (block_read_at(1, buffer)) return 0;
    memcpy(&super, buffer, sizeof(super));
    if (super.magic != EXT2_MAGIC) return 0;
    if (super.log_block_size != 0 || super.inode_size != 128) return 0;
    if (super.feature_incompat & ~(0x2u | 0x80u)) return 0; /* filetype, 64bit */
    if (super.feature_ro_compat & ~(0x1u | 0x4u)) return 0; /* sparse_super, dir_index */
    if (!super.blocks_count || super.blocks_count > 32768) return 0;
    if (super.first_data_block != 1) return 0;
    u32 needed = (super.blocks_count + super.blocks_per_group - 1) / super.blocks_per_group;
    if (!needed || needed > EXT2_MAX_GROUPS) return 0;
    group_count = needed;
    desc_size = *(u16*)(buffer + 254);
    if (desc_size < 32 || desc_size > 64) desc_size = 32;
    u8 desc[EXT2_BLOCK];
    for (u32 g = 0; g < group_count; ++g) {
        u32 byte = g * desc_size;
        u32 block = super.first_data_block + 1 + byte / EXT2_BLOCK;
        if (block_read_at(block, desc)) return 0;
        memcpy(&groups[g], desc + (byte % EXT2_BLOCK), sizeof(ext2_group));
    }
    mounted = 1;
    vfs_mount(&ext2_ops);
    log_write("[INFO] ext2 filesystem mounted\n");
    return 1;
}

void ext2_usage(u32* sectors, u32* bytes_per_block, u32* blocks, u32* free_blocks) {
    if (!mounted) {
        *sectors = *bytes_per_block = *blocks = *free_blocks = 0;
        return;
    }
    *sectors = super.blocks_count * EXT2_SECTORS;
    *bytes_per_block = EXT2_BLOCK;
    *blocks = super.blocks_count;
    *free_blocks = super.free_blocks;
}

void ext2_info(void) {
    if (!mounted) { printf("ext2: not mounted\n"); return; }
    printf("ext2: %u blocks, %u inodes, %u groups, %u free blocks\n",
           super.blocks_count, super.inodes_count, group_count, super.free_blocks);
}

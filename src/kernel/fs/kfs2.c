#include "kfs2.h"
#include "block.h"
#include "vfs.h"
#include "../drivers/ata_pio.h"
#include "../kklibc/kklibc.h"
#include "../kernel/log.h"

#define KFS_MAGIC 0x3246534bu
#define KFS_INODES 256u
#define KFS_INODE_SECTORS 64u
#define KFS_BITMAP_SECTORS 8u
#define KFS_BITMAP_START (1u + KFS_INODE_SECTORS)
#define KFS_DATA_START (KFS_BITMAP_START + KFS_BITMAP_SECTORS)
#define KFS_NAME 96
typedef struct __attribute__((packed)) { u32 magic, sectors, inode_sectors, bitmap_sectors, data_start; } kfs_super;
typedef struct __attribute__((packed)) { u8 used, directory; u16 reserved; u32 parent, size, first, sectors; char name[KFS_NAME]; } kfs_inode;
static int mounted; static kfs_super super; static const vfs_ops_t ops;
extern ata_disk_info_t ata_disks[2];
static int sec_read(u32 s, void* p) { return block_read(s, p); }
static int sec_write(u32 s, const void* p) { return block_write(s, p); }
static u32 inode_sector(u32 i) { return 1 + (i * sizeof(kfs_inode)) / 512; }
static u32 inode_off(u32 i) { return (i * sizeof(kfs_inode)) % 512; }
/* Inodes are packed consecutively on disk, NOT padded to sector boundaries.
 * A 116-byte record (for example inode 4) can span two sectors. */
static int inode_get(u32 i, kfs_inode* out) {
    u8 b[512];
    if (i >= KFS_INODES || sec_read(inode_sector(i), b)) return -1;
    u32 offset = inode_off(i), first = sizeof(*out);
    if (first > 512 - offset) first = 512 - offset;
    memcpy(out, b + offset, first);
    if (first < sizeof(*out)) {
        if (sec_read(inode_sector(i) + 1, b)) return -1;
        memcpy((u8*)out + first, b, sizeof(*out) - first);
    }
    return 0;
}
static int inode_put(u32 i, const kfs_inode* in) {
    u8 b[512];
    if (i >= KFS_INODES || sec_read(inode_sector(i), b)) return -1;
    u32 offset = inode_off(i), first = sizeof(*in);
    if (first > 512 - offset) first = 512 - offset;
    memcpy(b + offset, in, first);
    if (sec_write(inode_sector(i), b)) return -1;
    if (first < sizeof(*in)) {
        if (sec_read(inode_sector(i) + 1, b)) return -1;
        memcpy(b, (const u8*)in + first, sizeof(*in) - first);
        if (sec_write(inode_sector(i) + 1, b)) return -1;
    }
    return 0;
}
static int bit_get(u32 s) { u8 b[512]; if (sec_read(KFS_BITMAP_START + s / 4096, b)) return 1; return (b[(s % 4096) / 8] >> (s & 7)) & 1; }
static int bit_set(u32 s, int value) { u8 b[512]; u32 sec = KFS_BITMAP_START + s / 4096; if (sec_read(sec, b)) return -1; if (value) b[(s % 4096) / 8] |= 1 << (s & 7); else b[(s % 4096) / 8] &= ~(1 << (s & 7)); return sec_write(sec, b); }
static int find_child(u32 parent, const char* name, u32* found) { for (u32 i=0;i<KFS_INODES;++i) { kfs_inode n; if (inode_get(i,&n)) return VFS_EIO; if(n.used&&n.parent==parent&&!strcmp(n.name,name)){*found=i;return 0;} } return VFS_ENOENT; }
static int find_path(const char* path, u32* found) {
    if (!path || path[0] != '/') return VFS_EINVAL;
    if (!strcmp(path,"/")){*found=0;return 0;}
    char part[KFS_NAME]; u32 cur=0, p=1; while(path[p]) { u32 n=0; while(path[p]&&path[p]!='/'){if(n+1>=KFS_NAME)return VFS_EINVAL;part[n++]=path[p++];} part[n]=0; while(path[p]=='/')++p; int r=find_child(cur,part,&cur); if(r)return r; }
    *found=cur; return 0;
}
static int parent_path(const char* path, u32* parent, char name[KFS_NAME]) { char copy[VFS_PATH_MAX]; strcpy(copy,path); u32 n=strlen(copy); while(n>1&&copy[n-1]=='/')copy[--n]=0; u32 slash=n; while(slash>1&&copy[slash-1]!='/')--slash; strcpy(name,copy+slash); if(slash>1)copy[slash-1]=0; else copy[1]=0; return find_path(copy,parent); }
static int alloc_inode(u32* out) { for(u32 i=1;i<KFS_INODES;++i){kfs_inode n;if(inode_get(i,&n))return VFS_EIO;if(!n.used){*out=i;return 0;}}return VFS_ENOSPC; }
static int alloc_sectors(u32 count,u32* first) { u32 max=super.sectors; for(u32 s=KFS_DATA_START;s+count<=max;++s){int ok=1;for(u32 j=0;j<count;++j)if(bit_get(s+j)){ok=0;break;}if(ok){for(u32 j=0;j<count;++j)bit_set(s+j,1);*first=s;return 0;}}return VFS_ENOSPC; }
static int stat_op(const char* p,vfs_stat_t* st){u32 i;int r=find_path(p,&i);if(r)return r;kfs_inode n;inode_get(i,&n);st->size=n.size;st->directory=n.directory;st->readonly=0;return 0;}
static int read_op(const char*p,u32 off,void*b,u32 cap){u32 i;if(find_path(p,&i))return VFS_ENOENT;kfs_inode n;inode_get(i,&n);if(n.directory)return VFS_EISDIR;if(off>=n.size)return 0;if(cap>n.size-off)cap=n.size-off;u8 sec[512];u32 done=0;while(done<cap){if(sec_read(n.first+(off+done)/512,sec))return VFS_EIO;u32 at=(off+done)%512,k=cap-done;if(k>512-at)k=512-at;memcpy((u8*)b+done,sec+at,k);done+=k;}return done;}
static int write_op(const char*p,u32 off,const void*b,u32 size){u32 i;if(find_path(p,&i))return VFS_ENOENT;kfs_inode n;inode_get(i,&n);if(n.directory)return VFS_EISDIR;if(off+size>n.size)return VFS_ENOSPC;u8 sec[512];u32 done=0;while(done<size){if(sec_read(n.first+(off+done)/512,sec))return VFS_EIO;u32 at=(off+done)%512,k=size-done;if(k>512-at)k=512-at;memcpy(sec+at,(u8*)b+done,k);if(sec_write(n.first+(off+done)/512,sec))return VFS_EIO;done+=k;}return size;}
static int replace_op(const char*p,const void*b,u32 size){u32 i;int r=find_path(p,&i);if(r==VFS_ENOENT){r=create_op(p,0);if(r)return r;r=find_path(p,&i);if(r)return r;}else if(r)return r;kfs_inode n;if(inode_get(i,&n))return VFS_EIO;if(n.directory)return VFS_EISDIR;u32 need=(size+511)/512;if(need==0)need=1;if(need>n.sectors){u32 first;if(alloc_sectors(need,&first))return VFS_ENOSPC;for(u32 j=0;j<n.sectors;++j)bit_set(n.first+j,0);n.first=first;n.sectors=need;}n.size=size;if(inode_put(i,&n))return VFS_EIO;return write_op(p,0,b,size)<0?VFS_EIO:0;}
static int valid_name(const char* name){u32 n=strlen(name);if(!n||n>=KFS_NAME)return 0;for(u32 i=0;i<n;++i)if(name[i]=='/')return 0;return 1;}
static int create_op(const char*p,int dir){u32 parent,i;char name[KFS_NAME];int r=parent_path(p,&parent,name);if(r)return r;if(!valid_name(name))return VFS_EINVAL;kfs_inode pn;if(inode_get(parent,&pn))return VFS_EIO;if(!pn.used||!pn.directory)return VFS_ENOTDIR;if(find_child(parent,name,&i)==0)return VFS_EEXIST;if(alloc_inode(&i))return VFS_ENOSPC;kfs_inode n;memset(&n,0,sizeof(n));n.used=1;n.directory=dir;n.parent=parent;strcpy(n.name,name);if(!dir){n.sectors=1;if(alloc_sectors(1,&n.first)){return VFS_ENOSPC;}}return inode_put(i,&n);}
static int remove_op(const char*p,int dir){u32 i;if(find_path(p,&i)||!i)return VFS_ENOENT;kfs_inode n;inode_get(i,&n);if(n.directory!=!!dir)return dir?VFS_ENOTDIR:VFS_EISDIR;for(u32 j=0;j<KFS_INODES;++j){kfs_inode c;if(inode_get(j,&c))return VFS_EIO;if(c.used&&c.parent==i)return VFS_ENOTEMPTY;}for(u32 j=0;j<n.sectors;++j)bit_set(n.first+j,0);memset(&n,0,sizeof(n));return inode_put(i,&n);}
static int rename_op(const char*a,const char*b){u32 i,parent;char name[KFS_NAME];int r=find_path(a,&i);if(r)return r;if(!i)return VFS_EINVAL;r=parent_path(b,&parent,name);if(r)return r;if(!valid_name(name))return VFS_EINVAL;kfs_inode pn;if(inode_get(parent,&pn))return VFS_EIO;if(!pn.used||!pn.directory)return VFS_ENOTDIR;u32 old;if(find_child(parent,name,&old)==0){if(old==i)return 0;return VFS_EEXIST;}kfs_inode n;if(inode_get(i,&n))return VFS_EIO;if(n.directory){u32 up=parent;while(up){if(up==i)return VFS_EINVAL;kfs_inode u;if(inode_get(up,&u))return VFS_EIO;up=u.parent;}}n.parent=parent;strcpy(n.name,name);return inode_put(i,&n);}
static int readdir_op(const char*p,u32*c,vfs_dirent_t*e){vfs_long_dirent_t x;int r=0;while((r=ops.readdir_long(p,c,&x))>0){memset(e,0,sizeof(*e));memcpy(e->name,x.name,sizeof(e->name)-1);e->name[sizeof(e->name)-1]=0;e->stat=x.stat;return 1;}return r;}
static int readdir_long_op(const char*p,u32*c,vfs_long_dirent_t*e){u32 parent;if(find_path(p,&parent))return VFS_ENOENT;for(u32 i=*c;i<KFS_INODES;++i){kfs_inode n;*c=i+1;if(inode_get(i,&n))return VFS_EIO;if(n.used&&n.parent==parent){memset(e,0,sizeof(*e));memcpy(e->name,n.name,sizeof(e->name)-1);e->name[sizeof(e->name)-1]=0;e->stat=(vfs_stat_t){n.size,n.directory,0};return 1;}}return 0;}
static const vfs_ops_t ops={stat_op,read_op,write_op,replace_op,create_op,remove_op,rename_op,readdir_op,readdir_long_op};
int kfs2_is_mounted(void){return mounted;} void kfs2_cleanup(void){if(mounted)block_sync();mounted=0;vfs_mount(NULL);block_invalidate();}
int kfs2_init(void){
    u8 sector[512];
    kfs_super s;
    if (sec_read(0, sector)) return 0;
    memcpy(&s, sector, sizeof(s));
    if (s.magic != KFS_MAGIC || s.inode_sectors != KFS_INODE_SECTORS
        || s.bitmap_sectors != KFS_BITMAP_SECTORS || s.data_start != KFS_DATA_START
        || s.sectors <= KFS_DATA_START || s.sectors > ata_disks[0].size) return 0;
    super=s; mounted=1; vfs_mount(&ops);
    log_write("[INFO] KFS2 native filesystem mounted\n");
    return 1;
}
void print_kfs2_info(void){printf("KFS2: native long-name filesystem, inode table=%u entries\n",KFS_INODES);}

/* Read-only consistency check: inode metadata, extent bounds, bitmap
 * allocation and overlap detection. Returns 0 when consistent. */
static u8 check_bm[4096];
static u8 check_claimed[4096];
int kfs2_check(void){
    if(!mounted)return VFS_EIO;
    for(u32 s=0;s<KFS_BITMAP_SECTORS;++s)if(sec_read(KFS_BITMAP_START+s,check_bm+s*512))return VFS_EIO;
    memset(check_claimed,0,sizeof(check_claimed));
    int errors=0;
    kfs_inode root;
    if(inode_get(0,&root)||!root.used||!root.directory)return VFS_ECORRUPT;
    for(u32 i=0;i<KFS_INODES;++i){
        kfs_inode n;
        if(inode_get(i,&n))return VFS_EIO;
        if(!n.used)continue;
        u32 nl=0;while(nl<KFS_NAME&&n.name[nl])++nl;
        if(nl==0||nl>=KFS_NAME){++errors;continue;}
        if(n.parent>=KFS_INODES){++errors;continue;}
        if(i!=0){kfs_inode p;if(inode_get(n.parent,&p)||!p.used||!p.directory){++errors;continue;}}
        if(!n.directory){
            if(n.size>n.sectors*512u){++errors;continue;}
            if(n.sectors&& (n.first<KFS_DATA_START||n.first+n.sectors>super.sectors)){++errors;continue;}
            for(u32 j=0;j<n.sectors;++j){
                u32 s=n.first+j;
                if(!((check_bm[s/8]>>(s&7))&1)){++errors;}
                u32 cb=(s-KFS_DATA_START);
                if(cb<sizeof(check_claimed)*8){
                    if((check_claimed[cb/8]>>(cb&7))&1)++errors; /* overlap */
                    check_claimed[cb/8]|=1<<(cb&7);
                }
            }
        }
    }
    return errors?VFS_ECORRUPT:0;
}
void kfs2_usage(u32*sectors,u32*cluster_bytes,u32*clusters,u32*free_clusters){
    if(!mounted){*sectors=*cluster_bytes=*clusters=*free_clusters=0;return;}
    *sectors=super.sectors;*cluster_bytes=512;
    u32 data=super.sectors>KFS_DATA_START?super.sectors-KFS_DATA_START:0;
    *clusters=data;
    u8 bm[4096];u32 free=0;
    for(u32 s=0;s<KFS_BITMAP_SECTORS;++s)if(sec_read(KFS_BITMAP_START+s,bm+s*512)){*free_clusters=0;return;}
    for(u32 i=0;i<data;++i){u32 s=KFS_DATA_START+i,bit=s%4096;if(!((bm[bit/8]>>(bit&7))&1))++free;}
    *free_clusters=free;
}

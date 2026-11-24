#ifndef FS_KFS2_H
#define FS_KFS2_H
int kfs2_init(void);
int kfs2_is_mounted(void);
int kfs2_check(void);
void kfs2_usage(unsigned* sectors, unsigned* cluster_bytes, unsigned* clusters, unsigned* free_clusters);
void kfs2_cleanup(void);
void print_kfs2_info(void);
#endif

#ifndef SYSABI_H
#define SYSABI_H

/* Fixed-width shared read-only diagnostic ABI. Kernel and apps must be rebuilt together. */
typedef struct {
    unsigned int pid, parent, state, ticks, heap_bytes;
    unsigned int group;
    unsigned int open_fds;
    char name[32];
} kos_process_info;

typedef struct {
    unsigned int mounted, sectors, cluster_bytes, clusters, free_clusters;
    unsigned int disk_sectors;
    char model[41];
} kos_disk_info;
#endif

#ifndef SYSABI_H
#define SYSABI_H

/* Fixed-width shared read-only diagnostic ABI. */
typedef struct {
    unsigned int pid, parent, state, ticks, heap_bytes;
} kos_process_info;

typedef struct {
    unsigned int mounted, sectors, cluster_bytes, clusters, free_clusters;
} kos_disk_info;
#endif

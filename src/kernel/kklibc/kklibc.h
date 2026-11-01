#ifndef KKLIBC_H
#define KKLIBC_H

#include "ctypes.h"
#include "function.h"
#include "math.h"
#include "mem.h"
#include "stdio.h"
#include "stdlib.h"
/* Compatibility umbrella for kernel clients; libc mem.h itself has no allocator. */
#include "../mm/heap.h"

#define VERSION "v0.5.0a-0-main a3c0c5f"

#endif

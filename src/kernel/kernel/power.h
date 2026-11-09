#ifndef KERNEL_POWER_H
#define KERNEL_POWER_H
int system_sync(void);
__attribute__((noreturn)) void system_shutdown(void);
#endif

#ifndef KOS_DESKTOP_ABI_H
#define KOS_DESKTOP_ABI_H
#define KOS_SYS_DISPLAY 40
#define KOS_SYS_DESKTOP 41
#define KOS_SYS_MOUSE 42
#define KOS_SYS_LAYOUT 43
typedef struct { unsigned width, height; } kos_video_mode;
typedef struct {
    unsigned width, height, graphics, mode, uptime, free_kib, ram_kib, processes, russian;
} kos_desktop_info;
typedef struct { int x, y; unsigned buttons, sequence, available; } kos_mouse;
#define KOS_VIDEO_MODES 4
static const kos_video_mode kos_video_modes[KOS_VIDEO_MODES] = {
    {1024, 768}, {1280, 800}, {1440, 900}, {1600, 900}
};
#endif

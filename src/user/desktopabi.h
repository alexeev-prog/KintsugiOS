#ifndef KOS_DESKTOP_ABI_H
#define KOS_DESKTOP_ABI_H
#define KOS_SYS_DISPLAY 40
#define KOS_SYS_DESKTOP 41
#define KOS_SYS_MOUSE 42
#define KOS_SYS_LAYOUT 43
#define KOS_SYS_FRAMEBUFFER_THEME 45
#define KOS_SYS_FRAMEBUFFER_INFO 46
#define KOS_SYS_FRAMEBUFFER_SCALE 47
#define KOS_SYS_FRAMEBUFFER_FONT 48
#define KOS_SYS_SOUND_TONE 49
#define KOS_SYS_SOUND_VOLUME 50
#define KOS_SYS_SOUND_INFO 51
#define KOS_SYS_SLEEP 52
#define KOS_SYS_GAME_PALETTE 53
#define KOS_SYS_SETPRIORITY 54
#define KOS_SYS_SERVICE 55
#define KOS_SYS_FRAMEBUFFER_FONT_SIZE 67
/* Pixel GUI ABI. Surfaces live in each process's own heap; the kernel only
 * validates the source range and clips the destination to the framebuffer. */
#define KOS_SYS_GFX_INFO 70
#define KOS_SYS_GFX_BLIT 71
#define KOS_SYS_GFX_FILL 72
#define KOS_SYS_GFX_MODE 73
#define KOS_SYS_GFX_WINDOW_CREATE 74
#define KOS_SYS_GFX_WINDOW_CLOSE 75
#define KOS_SYS_GFX_WINDOW_FILL 76
#define KOS_SYS_GFX_WINDOW_BLIT 77
#define KOS_SYS_GFX_WINDOW_TITLE 78
#define KOS_SYS_GFX_WINDOW_MOVE 79
#define KOS_SYS_GFX_WINDOW_RESIZE 80
#define KOS_SYS_GFX_WINDOW_STATE 81
#define KOS_SYS_GFX_WINDOW_LIST 82
#define KOS_SYS_GFX_WINDOW_READ 83
#define KOS_SYS_GFX_WINDOW_POST 84
#define KOS_SYS_GFX_WINDOW_RAISE 85
#define KOS_SYS_GFX_WINDOW_EVENT 86
/* Redirect a child's fullscreen TUI presentation (syscall 7) into a buffer
 * the window compositor can read and draw inside a terminal window. */
#define KOS_SYS_TUI_ATTACH 87
#define KOS_SYS_TUI_READ 88
/* Deliver a key event to a specific process (used by the GUI compositor to
 * forward console input to a window-hosted TUI child). */
#define KOS_SYS_KEY_INJECT 89

typedef struct {
    unsigned width, height;
} kos_video_mode;

typedef struct {
    unsigned width, height, graphics, mode, uptime, free_kib, ram_kib, processes, russian;
} kos_desktop_info;

typedef struct {
    int x, y;
    unsigned buttons, sequence, available;
    int wheel;
} kos_mouse;

typedef struct {
    unsigned width, height, pitch, bpp;
} kos_gfx_info;

typedef struct {
    int x, y;
    unsigned width, height, color;
} kos_gfx_rect;

/* Pixel blit request. `stride` is the source row pitch in pixels; 0 means
 * `width`. It must be passed explicitly for sub-rectangle blits of a surface
 * whose stride differs from the region being copied (dirty updates). */
typedef struct {
    unsigned source, x, y, width, height, stride;
} kos_gfx_blit;

typedef struct {
    int id, x, y, width, height, minimized, maximized, owner;
    char title[48];
} kos_gui_window_info;

typedef struct {
    int type, x, y, button, key;
} kos_gui_event;

/* Compositor -> client events delivered through KOS_SYS_GFX_WINDOW_POST. */
#define KOS_GUI_EV_PRESS 1
#define KOS_GUI_EV_RELEASE 2
#define KOS_GUI_EV_MOVE 3
#define KOS_GUI_EV_KEY 4
#define KOS_GUI_EV_CLOSE 5
#define KOS_VIDEO_MODES 7
#define KOS_FRAMEBUFFER_THEMES 6
#define KOS_FRAMEBUFFER_SCALES 3
#define KOS_FRAMEBUFFER_FONTS 3
#define KOS_FRAMEBUFFER_FONT_SIZES 4
static const kos_video_mode kos_video_modes[KOS_VIDEO_MODES] = {
    { 1024, 768  },
    { 1280, 800  },
    { 1440, 900  },
    { 1600, 900  },
    { 1680, 1050 },
    { 1920, 1080 },
    { 1920, 1200 }
};
#endif

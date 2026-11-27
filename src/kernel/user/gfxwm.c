#include "gfxwm.h"

#include "../../user/ioabi.h"
#include "../fs/vfs.h"
#include "../kernel/log.h"
#include "../kklibc/kklibc.h"
#include "process.h"

#define GFXWM_MAX 16
#define GFXWM_EVENT_QUEUE 16

typedef struct {
    int used, owner, x, y, width, height, minimized, maximized, z;
    char title[48];
    u32* pixels;
    kos_gui_event events[GFXWM_EVENT_QUEUE];
    u32 event_head, event_tail, event_count;
} gfxwm_window;

static gfxwm_window windows[GFXWM_MAX];
static int window_count;
static int focused = -1;
static int zorder[GFXWM_MAX];
static u32 window_generation;
u32 gfxwm_generation(void) { return window_generation; }

void gfxwm_init(void) {
    for (int i = 0; i < GFXWM_MAX; ++i) {
        windows[i].used = 0;
        zorder[i] = -1;
    }
    window_count = 0;
    focused = -1;
}

static int allocate_slot(void) {
    for (int i = 0; i < GFXWM_MAX; ++i) {
        if (!windows[i].used) {
            return i;
        }
    }
    return -1;
}

static void raise_slot(int slot) {
    if (slot < 0 || !windows[slot].used) {
        return;
    }
    int position = 0;
    while (position < window_count && zorder[position] != slot) {
        ++position;
    }
    if (position >= window_count) {
        zorder[window_count++] = slot;
    } else {
        for (int i = position; i < window_count - 1; ++i) {
            zorder[i] = zorder[i + 1];
        }
        zorder[window_count - 1] = slot;
    }
    focused = slot;
}

int gfxwm_create(u32 width, u32 height) {
    ++window_generation;
    if (!width || !height || width > 1920 || height > 1200) {
        return -1;
    }
    int slot = allocate_slot();
    if (slot < 0) {
        return -1;
    }
    gfxwm_window* w = &windows[slot];
    w->used = 1;
    w->owner = (int)process_pid();
    w->x = 40 + slot * 24;
    w->y = 40 + slot * 24;
    w->width = width;
    w->height = height;
    w->minimized = 0;
    w->maximized = 0;
    w->title[0] = 0;
    w->pixels = kmalloc(width * height * 4);
    if (!w->pixels) {
        w->used = 0;
        return -1;
    }
    for (u32 i = 0; i < width * height; ++i) {
        w->pixels[i] = 0xc0c0c0;
    }
    w->event_head = w->event_tail = w->event_count = 0;
    raise_slot(slot);
    return slot + 1;
}

int gfxwm_close(int id, u32 owner_pid, int privileged_uid) {
    ++window_generation;
    if (id < 1 || id > GFXWM_MAX) {
        return VFS_EINVAL;
    }
    gfxwm_window* w = &windows[id - 1];
    if (!w->used) {
        return VFS_ENOENT;
    }
    /* The creator owns the window; root may clean up abandoned windows. */
    if (w->owner != (int)owner_pid && privileged_uid != 0) {
        return KOS_EPERM;
    }
    kfree(w->pixels);
    memset(w, 0, sizeof(*w));
    for (int i = 0; i < window_count; ++i) {
        if (zorder[i] == id - 1) {
            for (int j = i; j < window_count - 1; ++j) {
                zorder[j] = zorder[j + 1];
            }
            --window_count;
            break;
        }
    }
    if (focused == id - 1) {
        focused = window_count ? zorder[window_count - 1] : -1;
    }
    return 0;
}

static int valid_window(int id) {
    return id >= 1 && id <= GFXWM_MAX && windows[id - 1].used;
}

static int clip(int id, int* x, int* y, u32* w, u32* h) {
    gfxwm_window* window = &windows[id - 1];
    /* Clamp in signed arithmetic: adding a negative offset to u32 wraps and
     * turns a fully off-screen request into a huge (crashing) copy. */
    int width = (int)*w, height = (int)*h;
    if (*x < 0) {
        width += *x;
        *x = 0;
    }
    if (*y < 0) {
        height += *y;
        *y = 0;
    }
    if (width <= 0 || height <= 0) {
        return 0;
    }
    if (*x >= window->width || *y >= window->height) {
        return 0;
    }
    if (*x + width > window->width) {
        width = window->width - *x;
    }
    if (*y + height > window->height) {
        height = window->height - *y;
    }
    if (width <= 0 || height <= 0) {
        return 0;
    }
    *w = (u32)width;
    *h = (u32)height;
    return 1;
}

int gfxwm_fill(int id, int x, int y, u32 width, u32 height, u32 color) {
    ++window_generation;
    if (!valid_window(id)) {
        return VFS_ENOENT;
    }
    gfxwm_window* w = &windows[id - 1];
    if (!clip(id, &x, &y, &width, &height)) {
        return 0;
    }
    for (u32 row = 0; row < height; ++row) {
        for (u32 col = 0; col < width; ++col) {
            w->pixels[(y + row) * w->width + (x + col)] = color;
        }
    }
    return 0;
}

int gfxwm_blit(int id, const u32* source, int x, int y, u32 width, u32 height) {
    ++window_generation;
    if (!valid_window(id) || !source || !width || !height) {
        return VFS_EINVAL;
    }
    gfxwm_window* w = &windows[id - 1];
    if (!clip(id, &x, &y, &width, &height)) {
        return 0;
    }
    for (u32 row = 0; row < height; ++row) {
        const u32* src = source + row * width;
        u32* dst = w->pixels + (y + row) * w->width + x;
        for (u32 col = 0; col < width; ++col) {
            dst[col] = src[col];
        }
    }
    return 0;
}

int gfxwm_title(int id, const char* title) {
    ++window_generation;
    if (!valid_window(id) || !title) {
        return VFS_EINVAL;
    }
    u32 n = 0;
    while (title[n] && n + 1 < sizeof(windows[id - 1].title)) {
        windows[id - 1].title[n] = title[n];
        ++n;
    }
    windows[id - 1].title[n] = 0;
    return 0;
}

int gfxwm_move(int id, int x, int y) {
    ++window_generation;
    if (!valid_window(id)) {
        return VFS_ENOENT;
    }
    windows[id - 1].x = x;
    windows[id - 1].y = y;
    return 0;
}

int gfxwm_resize(int id, u32 width, u32 height) {
    ++window_generation;
    if (!valid_window(id) || !width || !height || width > 1920 || height > 1200) {
        return VFS_EINVAL;
    }
    gfxwm_window* w = &windows[id - 1];
    u32* pixels = kmalloc(width * height * 4);
    if (!pixels) {
        return VFS_ENOMEM;
    }
    for (u32 i = 0; i < width * height; ++i) {
        pixels[i] = 0xc0c0c0;
    }
    /* copy overlapping region */
    u32 copy_width = width < (u32)w->width ? width : (u32)w->width;
    u32 copy_height = height < (u32)w->height ? height : (u32)w->height;
    for (u32 row = 0; row < copy_height; ++row) {
        memcpy(pixels + row * width, w->pixels + row * w->width, copy_width * 4);
    }
    kfree(w->pixels);
    w->pixels = pixels;
    w->width = width;
    w->height = height;
    return 0;
}

int gfxwm_state(int id, int state) {
    ++window_generation;
    if (!valid_window(id)) {
        return VFS_ENOENT;
    }
    if (state == 1) {
        windows[id - 1].minimized = 1;
    } else if (state == 2) {
        windows[id - 1].maximized = 1;
    } else {
        windows[id - 1].minimized = 0;
        windows[id - 1].maximized = 0;
    }
    return 0;
}

int gfxwm_raise(int id) {
    ++window_generation;
    if (!valid_window(id)) {
        return VFS_ENOENT;
    }
    raise_slot(id - 1);
    return 0;
}

int gfxwm_list(kos_gui_window_info* out, u32 capacity) {
    if (!out || !capacity) {
        return VFS_EINVAL;
    }
    u32 count = 0;
    for (int i = 0; i < window_count && count < capacity; ++i) {
        gfxwm_window* w = &windows[zorder[i]];
        if (!w->used) {
            continue;
        }
        memset(&out[count], 0, sizeof(out[count]));
        out[count].id = zorder[i] + 1;
        out[count].x = w->x;
        out[count].y = w->y;
        out[count].width = w->width;
        out[count].height = w->height;
        out[count].minimized = w->minimized;
        out[count].maximized = w->maximized;
        out[count].owner = w->owner;
        for (int j = 0; j < 47 && w->title[j]; ++j) {
            out[count].title[j] = w->title[j];
        }
        out[count].title[47] = 0;
        ++count;
    }
    return (int)count;
}

int gfxwm_read(int id, u32* out, int x, int y, u32 width, u32 height) {
    if (!valid_window(id) || !out) {
        return VFS_EINVAL;
    }
    gfxwm_window* w = &windows[id - 1];
    if (!clip(id, &x, &y, &width, &height)) {
        return 0;
    }
    for (u32 row = 0; row < height; ++row) {
        memcpy(out + row * width, w->pixels + (y + row) * w->width + x, width * 4);
    }
    return 0;
}

int gfxwm_post(int id, const kos_gui_event* event) {
    if (!valid_window(id) || !event) {
        return VFS_EINVAL;
    }
    gfxwm_window* w = &windows[id - 1];
    if (w->event_count >= GFXWM_EVENT_QUEUE) {
        return VFS_ENOSPC;
    }
    w->events[w->event_tail] = *event;
    w->event_tail = (w->event_tail + 1) % GFXWM_EVENT_QUEUE;
    ++w->event_count;
    return 0;
}

int gfxwm_event(int id, kos_gui_event* out) {
    if (!valid_window(id) || !out) {
        return VFS_EINVAL;
    }
    gfxwm_window* w = &windows[id - 1];
    if (!w->event_count) {
        return 0;
    }
    *out = w->events[w->event_head];
    w->event_head = (w->event_head + 1) % GFXWM_EVENT_QUEUE;
    --w->event_count;
    return 1;
}

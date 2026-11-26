#include "gfx.h"
#include "tuiabi.h"

/* External GUI client demo: it owns a kernel-managed window and draws into it
 * through the graphic ABI, while kgui acts as the compositor. */
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    int id = gfx_window_create(320, 200);
    if (id < 0) {
        return 1;
    }
    gfx_window_title(id, "GDemo - external client");
    gfx_window_move(id, 120, 120);
    int tick = 0;
    u32 color = GFX_RED;
    for (;;) {
        gfx_surface* surface = gfx_surface_create(320, 200);
        if (!surface) {
            return 1;
        }
        gfx_clear(surface, GFX_TEAL);
        gfx_bevel(surface, 0, 0, 320, 200, 1);
        gfx_text(surface, 12, 12, "External GUI client", GFX_WHITE, 0);
        gfx_text(surface, 12, 34, "Drawing into a kernel window", GFX_WHITE, 0);
        gfx_text(surface, 12, 54, "Click to toggle, ESC to close", GFX_WHITE, 0);
        int x = (tick * 7) % 260;
        gfx_fill(surface, 20 + x, 80, 40, 40, color);
        gfx_fill(surface, 20 + x, 80, 40, 2, GFX_WHITE);
        gfx_window_blit(id, surface, 0, 0, 320, 200);
        gfx_surface_destroy(surface);
        kos_gui_event event;
        while (gfx_window_event(id, &event)) {
            if (event.type == KOS_GUI_EV_KEY && event.key == 27) {
                /* ESC from the compositor closes the client. */
                goto done;
            }
            if (event.type == KOS_GUI_EV_PRESS) {
                /* Simple feedback: clicking toggles the moving block's color. */
                color = color == GFX_RED ? GFX_RGB(220, 180, 40) : GFX_RED;
            }
        }
        syscall(KOS_SYS_SLEEP, 50, 0, 0, 0);
        ++tick;
    }
done:
    gfx_window_close(id);
    return 0;
}

#include "ktuilib.h"
#define sleep_ms(ms) syscall(KOS_SYS_SLEEP, ms, 0, 0, 0)

/* KLife - Conway's Game of Life with Japanese garden palettes. */
#define W 60
#define H 20
static unsigned char grid[H][W], next[H][W];
static int gen, running, speed = 8;
static unsigned seed = 0xabc123u;

static unsigned rnd(unsigned n) {
    seed = seed * 1664525u + 1013904223u;
    return n ? (seed >> 16) % n : 0;
}

static void randomize(void) {
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            grid[y][x] = rnd(100) < 28 ? 1 : 0;
        }
    }
    gen = 0;
}

static void step(void) {
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) {
                        continue;
                    }
                    int ax = x + dx, ay = y + dy;
                    if (ax >= 0 && ay >= 0 && ax < W && ay < H) {
                        n += grid[ay][ax];
                    }
                }
            }
            next[y][x] = (grid[y][x] && (n == 2 || n == 3)) || (!grid[y][x] && n == 3);
        }
    }
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            grid[y][x] = next[y][x];
        }
    }
    ++gen;
}

static void draw(int cx, int cy, const char* status) {
    tui_clear(TUI_DESKTOP);
    tui_bar(
        0,
        "KLife - Conway's Game of Life | arrows cursor | Space toggle | Enter run/pause | R random | +/- "
        "speed | Q quit");
    tui_box(2, 2, 64, 22, "Sakura garden", TUI_BORDER);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            char c[2] = { grid[y][x] ? '*' : '.', 0 };
            unsigned color = grid[y][x] ? TUI_SUCCESS : (x == cx && y == cy ? TUI_VISUAL : TUI_COMMENT);
            tui_text(4 + x, 3 + y, c, color);
        }
    }
    tui_text(70, 4, "GEN", TUI_KEYWORD);
    tui_number(70, 6, gen, TUI_NUMBER);
    tui_text(70, 8, running ? "RUNNING" : "PAUSED", running ? TUI_SUCCESS : TUI_WARNING);
    tui_text(70, 10, "SPEED", TUI_KEYWORD);
    tui_number(70, 12, speed, TUI_NUMBER);
    tui_text(70, 15, "cursor", TUI_STATUS);
    tui_number(70, 17, cx, TUI_NUMBER);
    tui_number(70, 18, cy, TUI_NUMBER);
    tui_bar(23, status);
    tui_status(24);
    tui_present(cx, cy);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    randomize();
    int cx = W / 2, cy = H / 2, frames = 0;
    const char* status = "Space toggles a cell";
    for (;;) {
        draw(cx, cy, status);
        int key = tui_key(0);
        if (key == 'q' || key == 27) {
            return 0;
        }
        if (key == 'r') {
            randomize();
            status = "Randomized";
        }
        if (key == '+') {
            if (speed > 1) {
                --speed;
            }
            status = "Faster";
        }
        if (key == '-') {
            if (speed < 30) {
                ++speed;
            }
            status = "Slower";
        }
        if (key == TUI_LEFT && cx) {
            --cx;
        }
        if (key == TUI_RIGHT && cx + 1 < W) {
            ++cx;
        }
        if (key == TUI_UP && cy) {
            --cy;
        }
        if (key == TUI_DOWN && cy + 1 < H) {
            ++cy;
        }
        if (key == ' ') {
            grid[cy][cx] = !grid[cy][cx];
            status = "Toggled";
        }
        if (key == '\n') {
            running = !running;
            status = running ? "Running" : "Paused";
        }
        if (running && ++frames >= speed) {
            frames = 0;
            step();
        }
        sleep_ms(30);
    }
}

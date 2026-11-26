#include "ktuilib.h"

/* KMines - classic Minesweeper with mouse and keyboard control. */
#define W 16
#define H 10
#define MINES 24

static char cells[H][W]; /* '#' hidden, ' ' empty, '0'..'8' counts, '*' mine, 'F' flag */
static int revealed, flags, game_over, won;
#define tone(n, ms) syscall(KOS_SYS_SOUND_TONE, n, ms, 0, 0)
static unsigned seed = 0x1234567u;

static unsigned rnd(unsigned n) {
    seed = seed * 1664525u + 1013904223u;
    return n ? (seed >> 16) % n : 0;
}

static int count_mines(int x, int y) {
    int n = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int ax = x + dx, ay = y + dy;
            if (ax >= 0 && ay >= 0 && ax < W && ay < H && cells[ay][ax] == '*') {
                ++n;
            }
        }
    }
    return n;
}

static void reveal(int x, int y) {
    if (x < 0 || y < 0 || x >= W || y >= H || cells[y][x] != '#' || game_over) {
        return;
    }
    if (cells[y][x] == '*') {
        game_over = 1;
        tone(120, 200);
        return;
    }
    int n = count_mines(x, y);
    cells[y][x] = n ? (char)('0' + n) : ' ';
    ++revealed;
    if (!n) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                reveal(x + dx, y + dy);
            }
        }
    }
    if (revealed == W * H - MINES) {
        won = 1;
        game_over = 1;
        tone(880, 200);
    }
}

static void init_game(void) {
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            cells[y][x] = '#';
        }
    }
    revealed = flags = game_over = won = 0;
    int placed = 0;
    while (placed < MINES) {
        int x = (int)rnd(W), y = (int)rnd(H);
        if (cells[y][x] != '*') {
            cells[y][x] = '*';
            ++placed;
        }
    }
}

static void draw(int cx, int cy, int focused, const char* status) {
    tui_clear(TUI_DESKTOP);
    tui_bar(0, "KMines - minesweeper | arrows move | Space open | F flag | N new | Q quit");
    tui_box(8, 3, 64, 14, "Kage no Meikyu", TUI_BORDER);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            char c[2];
            unsigned color = 0x1f;
            if (game_over && cells[y][x] == '*') {
                c[0] = '*';
                color = TUI_ERROR;
            } else if (cells[y][x] == '#') {
                c[0] = (x == cx && y == cy && focused) ? 'X' : '#';
                color = (x == cx && y == cy) ? TUI_VISUAL : 0x18;
            } else if (cells[y][x] == 'F') {
                c[0] = 'F';
                color = TUI_WARNING;
            } else {
                c[0] = cells[y][x];
                color = (cells[y][x] >= '1' && cells[y][x] <= '9') ? TUI_NUMBER : TUI_COMMENT;
            }
            c[1] = 0;
            tui_text(10 + x * 3, 5 + y, c, color);
        }
    }
    tui_text(10, 16, status, TUI_STATUS);
    tui_text(50, 16, game_over ? (won ? "WON" : "BOOM") : "PLAY", won ? TUI_SUCCESS : TUI_KEYWORD);
    tui_status(24);
    tui_present(cx, cy);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    seed ^= (unsigned)syscall(15, 0, 0, 0, 0);
    init_game();
    int cx = 0, cy = 0;
    const char* status = "Reveal cells, avoid mines";
    for (;;) {
        draw(cx, cy, 1, status);
        int key = tui_key(1);
        if (key == 'q' || key == 27) {
            return 0;
        }
        if (key == 'n') {
            init_game();
            status = "New game";
            continue;
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
            reveal(cx, cy);
            status = "Revealed";
        }
        if (key == 'f' || key == 'F') {
            if (cells[cy][cx] == '#') {
                cells[cy][cx] = 'F';
                ++flags;
            } else if (cells[cy][cx] == 'F') {
                cells[cy][cx] = '#';
                --flags;
            }
            status = "Flag toggled";
        }
    }
}

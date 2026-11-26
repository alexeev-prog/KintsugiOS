#include "ktuilib.h"

/* K2048 - the classic sliding number puzzle.
 *
 * Movement is defined as "a line changed": compaction alone counts, not just a
 * merge. Tiles merge at most once per move, a new tile appears only after a
 * real change, and game-over locks movement input until a new game starts. */

#define N 4

static unsigned board[N][N];
static unsigned score;
static unsigned seed = 0x2048u;
static int won, over;

static unsigned rnd(unsigned n) {
    seed = seed * 1664525u + 1013904223u;
    return n ? (seed >> 16) % n : 0;
}

static void add_tile(void) {
    int empty[N * N], count = 0;
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            if (!board[y][x]) {
                empty[count++] = y * N + x;
            }
        }
    }
    if (!count) {
        return;
    }
    int at = empty[rnd((unsigned)count)];
    board[at / N][at % N] = rnd(10) ? 2 : 4;
}

static int has_value(unsigned value) {
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            if (board[y][x] == value) {
                return 1;
            }
        }
    }
    return 0;
}

/* Slide one line toward index 0. Returns 1 when any tile moved or merged. */
static int slide(unsigned* line) {
    unsigned packed[N];
    int count = 0;
    for (int i = 0; i < N; ++i) {
        if (line[i]) {
            packed[count++] = line[i];
        }
    }
    unsigned merged[N];
    int out = 0;
    for (int i = 0; i < count; ++i) {
        if (i + 1 < count && packed[i] == packed[i + 1]) {
            unsigned value = packed[i] * 2u;
            merged[out++] = value;
            score += value;
            ++i;
        } else {
            merged[out++] = packed[i];
        }
    }
    int changed = 0;
    for (int i = 0; i < N; ++i) {
        unsigned value = i < out ? merged[i] : 0;
        if (line[i] != value) {
            changed = 1;
        }
        line[i] = value;
    }
    return changed;
}

/* Direction order: the line is read from the edge the tiles travel toward. */
static int move(int dx, int dy) {
    int changed = 0;
    for (int i = 0; i < N; ++i) {
        unsigned line[N];
        for (int j = 0; j < N; ++j) {
            int x = dx > 0 ? N - 1 - j : dx < 0 ? j : i;
            int y = dy > 0 ? N - 1 - j : dy < 0 ? j : i;
            line[j] = board[y][x];
        }
        changed |= slide(line);
        for (int j = 0; j < N; ++j) {
            int x = dx > 0 ? N - 1 - j : dx < 0 ? j : i;
            int y = dy > 0 ? N - 1 - j : dy < 0 ? j : i;
            board[y][x] = line[j];
        }
    }
    if (changed) {
        add_tile();
    }
    return changed;
}

static int can_move(void) {
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            if (!board[y][x]) {
                return 1;
            }
            if (x + 1 < N && board[y][x] == board[y][x + 1]) {
                return 1;
            }
            if (y + 1 < N && board[y][x] == board[y + 1][x]) {
                return 1;
            }
        }
    }
    return 0;
}

static void reset(void) {
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            board[y][x] = 0;
        }
    }
    score = 0;
    won = over = 0;
    add_tile();
    add_tile();
}

static unsigned color_for(unsigned value) {
    if (!value) {
        return 0x08;
    }
    if (value <= 4) {
        return 0x1f;
    }
    if (value <= 16) {
        return 0x2f;
    }
    if (value <= 64) {
        return 0x6f;
    }
    if (value <= 256) {
        return 0x4f;
    }
    if (value <= 1024) {
        return 0x5f;
    }
    if (value <= 2048) {
        return 0x4e;
    }
    return 0x7e;
}

static void number_text(char* out, unsigned value) {
    char reversed[12];
    int n = 0;
    if (!value) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (value && n < 11) {
        reversed[n++] = (char)('0' + value % 10);
        value /= 10;
    }
    int i = 0;
    while (n) {
        out[i++] = reversed[--n];
    }
    out[i] = 0;
}

static void draw(const char* status) {
    tui_clear(TUI_DESKTOP);
    tui_bar(0, "K2048 - WASD / arrows slide   N new game   Q quit");
    tui_box(21, 2, 38, 19, "2048 - Kintsugi edition", TUI_BORDER);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            int px = 24 + x * 7, py = 5 + y * 3;
            unsigned value = board[y][x];
            tui_fill(px, py, 7, 3, color_for(value));
            if (value) {
                char text[12];
                number_text(text, value);
                u32 n = tui_len(text);
                int pad = n < 6 ? (int)((6 - n) / 2) : 0;
                tui_textn(px + pad, py + 1, text, 7 - (u32)pad, value > 999 ? 0x0f : 0x17);
            }
        }
    }
    tui_text(24, 18, "SCORE", TUI_KEYWORD);
    tui_number(31, 18, score, TUI_NUMBER);
    if (won && !over) {
        tui_text(41, 18, "2048 reached!", TUI_SUCCESS);
    }
    tui_bar(22, status);
    tui_bar(23, over ? "Game over - N starts a new game" : "Arrows/WASD move   N new game   Q quit");
    tui_status(24);
    tui_present(79, 24);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    reset();
    const char* status = "Reach 2048";
    for (;;) {
        draw(status);
        int key = tui_key(1);
        seed ^= (unsigned)key * 2654435761u;
        if (key == 'q' || key == 27 || key == TUI_F10) {
            return 0;
        }
        if (key == 'n' || key == 'N') {
            reset();
            status = "New game";
            continue;
        }
        if (over) {
            continue;
        }
        int dx = 0, dy = 0;
        if (key == TUI_LEFT || key == 'a' || key == 'A') {
            dx = -1;
        } else if (key == TUI_RIGHT || key == 'd' || key == 'D') {
            dx = 1;
        } else if (key == TUI_UP || key == 'w' || key == 'W') {
            dy = -1;
        } else if (key == TUI_DOWN || key == 's' || key == 'S') {
            dy = 1;
        }
        if (!dx && !dy) {
            continue;
        }
        int moved = move(dx, dy);
        status = moved ? "Moved" : "Nothing moves that way";
        if (!won && has_value(2048)) {
            won = 1;
            status = "You reached 2048! Keep playing or press N.";
            tui_sound(1);
        }
        /* A full board with no equal neighbours cannot move in any direction,
         * including the direction just pressed. Detect that even when the
         * attempted move was a no-op, otherwise the game never ends. */
        if (!can_move()) {
            over = 1;
            status = "No moves left - game over.";
            tui_sound(2);
        }
    }
}
